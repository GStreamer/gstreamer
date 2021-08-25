/* GStreamer
 * Copyright (C) <2021> Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-rtphdrextrepairedstreamid
 * @title: rtphdrextrepairedstreamid
 * @short_description: RTP SDES Header Extension for RFC8852 RepairedRtpStreamId
 *   Extension
 * @see_also: #GstRTPHeaderExtension, #GstRTPBasePayload, #GstRTPBaseDepayload, gstrtpbuffer
 *
 * Since: 1.22
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtphdrext-repairedstreamid.h"

GST_DEBUG_CATEGORY_STATIC (rtphdrext_repaired_stream_id_debug);
#define GST_CAT_DEFAULT (rtphdrext_repaired_stream_id_debug)

#define REPAIRED_RID_EXTMAP_STR GST_RTP_HDREXT_BASE "sdes:repaired-rtp-stream-id"

enum
{
  PROP_0,
  PROP_RID,
};

struct _GstRTPHeaderExtensionRepairedStreamId
{
  GstRTPHeaderExtension parent;

  char *rid;
};

#define gst_rtp_header_extension_repaired_stream_id_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRTPHeaderExtensionRepairedStreamId,
    gst_rtp_header_extension_repaired_stream_id,
    GST_TYPE_RTP_HEADER_EXTENSION, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "rtphdrextrepairedstreamid", 0,
        "RTP RFC8852 RepairedRtpStreamId Header Extensions");
    );
GST_ELEMENT_REGISTER_DEFINE (rtphdrextrepairedstreamid,
    "rtphdrextrepairedstreamid", GST_RANK_MARGINAL,
    GST_TYPE_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID);

static GstRTPHeaderExtensionFlags
    gst_rtp_header_extension_repaired_stream_id_get_supported_flags
    (GstRTPHeaderExtension * ext)
{
  GstRTPHeaderExtensionRepairedStreamId *self =
      GST_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (ext);
  GstRTPHeaderExtensionFlags flags =
      GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE;
  gssize rid_len = -1;

  GST_OBJECT_LOCK (ext);
  if (self->rid)
    rid_len = strlen (self->rid);
  GST_OBJECT_UNLOCK (ext);
  /* One byte extensions only support [1, 16] bytes */
  if (rid_len > 16)
    flags = GST_RTP_HEADER_EXTENSION_TWO_BYTE;

  return flags;
}

static gsize
    gst_rtp_header_extension_repaired_stream_id_get_max_size
    (GstRTPHeaderExtension * ext, const GstBuffer * buffer)
{
  if (gst_rtp_header_extension_repaired_stream_id_get_supported_flags
      (ext) & GST_RTP_HEADER_EXTENSION_ONE_BYTE)
    return 16;
  else
    return 255;
}

static gssize
gst_rtp_header_extension_repaired_stream_id_write (GstRTPHeaderExtension
    * ext, const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstRTPHeaderExtensionRepairedStreamId *self =
      GST_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (ext);
  gsize len = 0;

  g_return_val_if_fail (size >=
      gst_rtp_header_extension_repaired_stream_id_get_max_size (ext, NULL), -1);
  g_return_val_if_fail (write_flags &
      gst_rtp_header_extension_repaired_stream_id_get_supported_flags
      (ext), -1);

  GST_OBJECT_LOCK (ext);
  if (!self->rid) {
    GST_LOG_OBJECT (self, "no rid to write");
    goto out;
  }

  /* TODO: we don't need to always add rid, we can selectively omit it from e.g.
   * non-video-keyframes or some percentage of the produced frames, e.g. RFC8852
   * mentions possibly using packet-loss as a indication of how often to add rid
   * to packets */
  GST_LOG_OBJECT (self, "writing rid \'%s\'", self->rid);
  len = strlen (self->rid);
  if ((write_flags & GST_RTP_HEADER_EXTENSION_TWO_BYTE) == 0 && len > 16) {
    GST_DEBUG_OBJECT (self, "cannot write a rid of size %" G_GSIZE_FORMAT
        " without using the two byte extension format", len);
    len = 0;
    goto out;
  }
  if (len > 0) {
    GST_LOG_OBJECT (self, "writing rid \'%s\'", self->rid);
    memcpy (data, self->rid, len);
  }

out:
  GST_OBJECT_UNLOCK (ext);
  return len;
}

static gboolean
gst_rtp_header_extension_repaired_stream_id_read (GstRTPHeaderExtension
    * ext, GstRTPHeaderExtensionFlags read_flags, const guint8 * data,
    gsize size, GstBuffer * buffer)
{
  GstRTPHeaderExtensionRepairedStreamId *self =
      GST_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (ext);
  gboolean notify = FALSE;

  if (!data || size == 0)
    return TRUE;

  if (read_flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE && (size < 1 || size > 16)) {
    GST_ERROR_OBJECT (ext,
        "one-byte header extensions must be between 1 and 16 bytes inculusive");
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  if (!self->rid || strncmp ((const char *) data, self->rid, size) != 0) {
    g_clear_pointer (&self->rid, g_free);
    self->rid = g_strndup ((const char *) data, size);
    notify = TRUE;
  }
  GST_OBJECT_UNLOCK (self);

  if (notify)
    g_object_notify ((GObject *) self, "rid");

  return TRUE;
}

static void
gst_rtp_header_extension_repaired_stream_id_get_property (GObject *
    object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionRepairedStreamId *self =
      GST_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (object);

  switch (prop_id) {
    case PROP_RID:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->rid);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
validate_rid (const char *rid)
{
  const char *iter;

  /* For avoidance of doubt, the only allowed byte values for
   * these IDs are decimal 48 through 57, 65 through 90, and 97 through
   * 122.
   */
  for (iter = rid; iter && iter[0]; iter++) {
    if (iter[0] < 48)
      return FALSE;
    if (iter[0] > 122)
      return FALSE;
    if (iter[0] > 57 && iter[0] < 65)
      return FALSE;
    if (iter[0] > 90 && iter[0] < 97)
      return FALSE;
  }

  return TRUE;
}

static void
gst_rtp_header_extension_repaired_stream_id_set_property (GObject *
    object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionRepairedStreamId *self =
      GST_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (object);

  switch (prop_id) {
    case PROP_RID:{
      const char *rid;
      GST_OBJECT_LOCK (self);
      rid = g_value_get_string (value);
      if (!validate_rid (rid)) {
        GST_WARNING_OBJECT (self, "Could not set rid \'%s\'. Validation failed",
            rid);
      } else {
        g_clear_pointer (&self->rid, g_free);
        self->rid = g_strdup (rid);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_header_extension_repaired_stream_id_finalize (GObject * object)
{
  GstRTPHeaderExtensionRepairedStreamId *self =
      GST_RTP_HEADER_EXTENSION_REPAIRED_STREAM_ID (object);

  g_clear_pointer (&self->rid, g_free);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
    gst_rtp_header_extension_repaired_stream_id_class_init
    (GstRTPHeaderExtensionRepairedStreamIdClass * klass)
{
  GstRTPHeaderExtensionClass *rtp_hdr_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  rtp_hdr_class = (GstRTPHeaderExtensionClass *) klass;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property =
      gst_rtp_header_extension_repaired_stream_id_set_property;
  gobject_class->get_property =
      gst_rtp_header_extension_repaired_stream_id_get_property;
  gobject_class->finalize =
      gst_rtp_header_extension_repaired_stream_id_finalize;

  /**
   * rtphdrextrepairedstreamid:rid:
   *
   * The RepairedRtpStreamID (RID) value either last retrieved from the RTP
   * Header extension, or to set on outgoing RTP packets.
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_RID,
      g_param_spec_string ("rid", "rid",
          "The RepairedRtpStreamId (RID) value last read or to write from/to "
          "RTP buffers", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  rtp_hdr_class->get_supported_flags =
      gst_rtp_header_extension_repaired_stream_id_get_supported_flags;
  rtp_hdr_class->get_max_size =
      gst_rtp_header_extension_repaired_stream_id_get_max_size;
  rtp_hdr_class->write = gst_rtp_header_extension_repaired_stream_id_write;
  rtp_hdr_class->read = gst_rtp_header_extension_repaired_stream_id_read;

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP Header Extension Repaired RFC8852 Stream ID",
      GST_RTP_HDREXT_ELEMENT_CLASS,
      "Extends RTP packets to add or retrieve a RepairedStreamId (RID) "
      "value as specified in RFC8852",
      "Matthew Waters <matthew@centricular.com>");
  gst_rtp_header_extension_class_set_uri (rtp_hdr_class,
      REPAIRED_RID_EXTMAP_STR);
}

static void
    gst_rtp_header_extension_repaired_stream_id_init
    (GstRTPHeaderExtensionRepairedStreamId * self)
{
}

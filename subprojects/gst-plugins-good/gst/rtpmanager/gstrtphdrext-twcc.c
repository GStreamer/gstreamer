/* GStreamer
 * Copyright (C) <2020> Matthew Waters <matthew@centricular.com>
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
 * SECTION:rtphdrexttwcc
 * @title: GstRtphdrext-TWCC
 * @short_description: Helper methods for dealing with RTP header extensions
 * in the Audio/Video RTP Profile for transport-wide-cc
 * @see_also: #GstRTPHeaderExtension, #GstRTPBasePayload, #GstRTPBaseDepayload, gstrtpbuffer
 *
 * Since: 1.20
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtphdrext-twcc.h"

GST_DEBUG_CATEGORY_STATIC (rtphdrext_twcc_debug);
#define GST_CAT_DEFAULT (rtphdrext_twcc_debug)

#define gst_gl_base_filter_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRTPHeaderExtensionTWCC,
    gst_rtp_header_extension_twcc, GST_TYPE_RTP_HEADER_EXTENSION,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "rtphdrexttwcc", 0,
        "RTP TWCC Header Extensions");
    );
GST_ELEMENT_REGISTER_DEFINE (rtphdrexttwcc, "rtphdrexttwcc", GST_RANK_MARGINAL,
    GST_TYPE_RTP_HEADER_EXTENSION_TWCC);

#define TWCC_EXTMAP_STR "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"

static void gst_rtp_header_extension_twcc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtp_header_extension_twcc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstRTPHeaderExtensionFlags
gst_rtp_header_extension_twcc_get_supported_flags (GstRTPHeaderExtension * ext);
static gsize gst_rtp_header_extension_twcc_get_max_size (GstRTPHeaderExtension *
    ext, const GstBuffer * buffer);
static gssize gst_rtp_header_extension_twcc_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size);
static gboolean gst_rtp_header_extension_twcc_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data, gsize size,
    GstBuffer * buffer);

enum
{
  PROP_0,
  PROP_N_STREAMS,
};

static void
gst_rtp_header_extension_twcc_class_init (GstRTPHeaderExtensionTWCCClass *
    klass)
{
  GstRTPHeaderExtensionClass *rtp_hdr_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  rtp_hdr_class = (GstRTPHeaderExtensionClass *) klass;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_rtp_header_extension_twcc_set_property;
  gobject_class->get_property = gst_rtp_header_extension_twcc_get_property;

  /**
   * rtphdrexttwcc:n-streams:
   *
   * The number of independant RTP streams that are being used for the transport
   * wide counter for TWCC.  If set to 1 (the default), then any existing
   * transport wide counter is kept.
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_N_STREAMS,
      g_param_spec_uint ("n-streams", "N Streams",
          "The number of separate RTP streams this header applies to",
          1, G_MAXUINT32, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  rtp_hdr_class->get_supported_flags =
      gst_rtp_header_extension_twcc_get_supported_flags;
  rtp_hdr_class->get_max_size = gst_rtp_header_extension_twcc_get_max_size;
  rtp_hdr_class->write = gst_rtp_header_extension_twcc_write;
  rtp_hdr_class->read = gst_rtp_header_extension_twcc_read;

  gst_element_class_set_static_metadata (gstelement_class,
      "Transport Wide Congestion Control", GST_RTP_HDREXT_ELEMENT_CLASS,
      "Extends RTP packets to add sequence number transport wide.",
      "Matthew Waters <matthew@centricular.com>");
  gst_rtp_header_extension_class_set_uri (rtp_hdr_class, TWCC_EXTMAP_STR);
}

static void
gst_rtp_header_extension_twcc_init (GstRTPHeaderExtensionTWCC * twcc)
{
  twcc->n_streams = 1;
}

static void
gst_rtp_header_extension_twcc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionTWCC *twcc = GST_RTP_HEADER_EXTENSION_TWCC (object);

  switch (prop_id) {
    case PROP_N_STREAMS:
      twcc->n_streams = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_header_extension_twcc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionTWCC *twcc = GST_RTP_HEADER_EXTENSION_TWCC (object);

  switch (prop_id) {
    case PROP_N_STREAMS:
      g_value_set_uint (value, twcc->n_streams);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstRTPHeaderExtensionFlags
gst_rtp_header_extension_twcc_get_supported_flags (GstRTPHeaderExtension * ext)
{
  return GST_RTP_HEADER_EXTENSION_ONE_BYTE;
}

static gsize
gst_rtp_header_extension_twcc_get_max_size (GstRTPHeaderExtension * ext,
    const GstBuffer * buffer)
{
  return 2;
}

static gssize
gst_rtp_header_extension_twcc_write (GstRTPHeaderExtension * ext,
    const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstRTPHeaderExtensionTWCC *twcc = GST_RTP_HEADER_EXTENSION_TWCC (ext);
  GstRTPBuffer rtp = { NULL, };
  gpointer ext_data;
  guint ext_size;
  gsize written = 0;

  g_return_val_if_fail (size >= gst_rtp_header_extension_twcc_get_max_size (ext,
          NULL), -1);
  g_return_val_if_fail (write_flags &
      gst_rtp_header_extension_twcc_get_supported_flags (ext), -1);

  if (!gst_rtp_buffer_map (output, GST_MAP_READWRITE, &rtp))
    goto map_failed;

  /* if there already is a twcc-seqnum inside the packet */
  if (gst_rtp_buffer_get_extension_onebyte_header (&rtp,
          gst_rtp_header_extension_get_id (ext), 0, &ext_data, &ext_size)) {
    if (ext_size < gst_rtp_header_extension_twcc_get_max_size (ext, NULL))
      goto existing_too_small;

    /* with only one stream, we read the twcc-seqnum */
    if (twcc->n_streams == 1)
      twcc->seqnum = GST_READ_UINT16_BE (ext_data);
  } else {
    /* with only one stream, we read the existing seqnum */
    if (twcc->n_streams == 1)
      twcc->seqnum = gst_rtp_buffer_get_seq (&rtp);

    written = 2;
  }
  GST_WRITE_UINT16_BE (data, twcc->seqnum);

  gst_rtp_buffer_unmap (&rtp);

  twcc->seqnum++;

  return written;

  /* ERRORS */
map_failed:
  {
    GST_ERROR ("failed to map buffer %p", output);
    return -1;
  }

existing_too_small:
  {
    GST_ERROR ("Cannot rewrite twcc data of smaller size (%u)", ext_size);
    return 0;
  }
}

static gboolean
gst_rtp_header_extension_twcc_read (GstRTPHeaderExtension * ext,
    GstRTPHeaderExtensionFlags read_flags, const guint8 * data, gsize size,
    GstBuffer * buffer)
{
  /* TODO: does this need an extra GstMeta? */
  return TRUE;
}

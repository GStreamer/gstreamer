/* GStreamer
 * Copyright (C) <2022> Sebastian Dröge <sebastian@centricular.com>
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
 * SECTION:element-rtphdrextntp64
 * @title: rtphdrextntp64
 * @short_description: RTP Header Extension for RFC6051 64-bit NTP timestamps for rapid synchronization.
 * @see_also: #GstRTPHeaderExtension, #GstRTPBasePayload, #GstRTPBaseDepayload, gstrtpbuffer
 *
 * Since: 1.22
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtphdrext-ntp.h"

GST_DEBUG_CATEGORY_STATIC (rtphdrext_ntp_debug);
#define GST_CAT_DEFAULT (rtphdrext_ntp_debug)

enum
{
  PROP_0,
  PROP_INTERVAL,
  PROP_EVERY_PACKET,
};

#define DEFAULT_INTERVAL 0
#define DEFAULT_EVERY_PACKET FALSE

static GstStaticCaps ntp_reference_timestamp_caps =
GST_STATIC_CAPS ("timestamp/x-ntp");

struct _GstRTPHeaderExtensionNtp64
{
  GstRTPHeaderExtension parent;

  GstClockTime last_pts;

  GstClockTime interval;
  gboolean every_packet;
};

G_DEFINE_TYPE_WITH_CODE (GstRTPHeaderExtensionNtp64,
    gst_rtp_header_extension_ntp_64,
    GST_TYPE_RTP_HEADER_EXTENSION, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "rtphdrextntp", 0, "RTP RFC6051 NTP Timestamps Header Extension");
    );

GST_ELEMENT_REGISTER_DEFINE (rtphdrextntp64,
    "rtphdrextntp64", GST_RANK_MARGINAL, GST_TYPE_RTP_HEADER_EXTENSION_NTP_64);

static GstRTPHeaderExtensionFlags
    gst_rtp_header_extension_ntp_get_supported_flags
    (GstRTPHeaderExtension * ext)
{
  return GST_RTP_HEADER_EXTENSION_ONE_BYTE | GST_RTP_HEADER_EXTENSION_TWO_BYTE;
}

static gsize
    gst_rtp_header_extension_ntp_64_get_max_size
    (GstRTPHeaderExtension * ext, const GstBuffer * buffer)
{
  return 8;
}

static gssize
gst_rtp_header_extension_ntp_64_write (GstRTPHeaderExtension
    * ext, const GstBuffer * input_meta, GstRTPHeaderExtensionFlags write_flags,
    GstBuffer * output, guint8 * data, gsize size)
{
  GstRTPHeaderExtensionNtp64 *self = GST_RTP_HEADER_EXTENSION_NTP_64 (ext);

  g_return_val_if_fail (size >=
      gst_rtp_header_extension_ntp_64_get_max_size (ext, NULL), -1);
  g_return_val_if_fail (write_flags &
      gst_rtp_header_extension_ntp_get_supported_flags (ext), -1);

  if (self->every_packet
      || self->last_pts == GST_CLOCK_TIME_NONE
      || !GST_BUFFER_PTS_IS_VALID (input_meta)
      || (self->last_pts != GST_BUFFER_PTS (input_meta)
          && (GST_BUFFER_IS_DISCONT (input_meta)
              || (GST_BUFFER_PTS (input_meta) >= self->last_pts
                  && GST_BUFFER_PTS (input_meta) - self->last_pts >=
                  self->interval)))) {
    GstCaps *caps;
    GstReferenceTimestampMeta *meta;

    caps = gst_static_caps_get (&ntp_reference_timestamp_caps);
    meta =
        gst_buffer_get_reference_timestamp_meta ((GstBuffer *) input_meta,
        caps);
    if (meta) {
      guint64 ntptime =
          gst_util_uint64_scale (meta->timestamp, G_GUINT64_CONSTANT (1) << 32,
          GST_SECOND);

      GST_WRITE_UINT64_BE (data, ntptime);
    } else {
      memset (data, 0, 8);
    }
    gst_caps_unref (caps);
    self->last_pts = GST_BUFFER_PTS (input_meta);
    return 8;
  } else {
    return 0;
  }
}

static gboolean
gst_rtp_header_extension_ntp_64_read (GstRTPHeaderExtension
    * ext, GstRTPHeaderExtensionFlags read_flags, const guint8 * data,
    gsize size, GstBuffer * buffer)
{
  GstCaps *caps;
  guint64 ntptime;
  GstClockTime timestamp;

  if (size < 8)
    return FALSE;

  caps = gst_static_caps_get (&ntp_reference_timestamp_caps);

  ntptime = GST_READ_UINT64_BE (data);
  timestamp =
      gst_util_uint64_scale (ntptime, GST_SECOND, G_GUINT64_CONSTANT (1) << 32);

  gst_buffer_add_reference_timestamp_meta (buffer, caps, timestamp,
      GST_CLOCK_TIME_NONE);

  gst_caps_unref (caps);

  return TRUE;
}


static void
gst_rtp_header_extension_ntp_64_get_property (GObject *
    object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionNtp64 *self = GST_RTP_HEADER_EXTENSION_NTP_64 (object);

  switch (prop_id) {
    case PROP_INTERVAL:
      GST_OBJECT_LOCK (self);
      g_value_set_uint64 (value, self->interval);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_EVERY_PACKET:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->every_packet);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_header_extension_ntp_64_set_property (GObject *
    object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPHeaderExtensionNtp64 *self = GST_RTP_HEADER_EXTENSION_NTP_64 (object);

  switch (prop_id) {
    case PROP_INTERVAL:
      GST_OBJECT_LOCK (self);
      self->interval = g_value_get_uint64 (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_EVERY_PACKET:
      GST_OBJECT_LOCK (self);
      self->every_packet = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
    gst_rtp_header_extension_ntp_64_class_init
    (GstRTPHeaderExtensionNtp64Class * klass)
{
  GstRTPHeaderExtensionClass *rtp_hdr_class;
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  rtp_hdr_class = (GstRTPHeaderExtensionClass *) klass;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_rtp_header_extension_ntp_64_set_property;
  gobject_class->get_property = gst_rtp_header_extension_ntp_64_get_property;

  /**
   * rtphdrextntp64:interval:
   *
   * The minimum interval between packets that get the header extension added.
   *
   * On discontinuities the interval will be reset and the next packet gets
   * the header extension added.
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_INTERVAL,
      g_param_spec_uint64 ("interval", "Interval",
          "Interval between consecutive packets that get the header extension added",
          0, G_MAXUINT64, DEFAULT_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * rtphdrextntp64:every-packet:
   *
   * If set to %TRUE the header extension will be added to every packet,
   * independent of its timestamp. By default only the first packet with a
   * given timestamp will get the header extension added.
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_EVERY_PACKET,
      g_param_spec_boolean ("every-packet", "Every Packet",
          "Add the header extension to every packet", DEFAULT_EVERY_PACKET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  rtp_hdr_class->get_supported_flags =
      gst_rtp_header_extension_ntp_get_supported_flags;
  rtp_hdr_class->get_max_size = gst_rtp_header_extension_ntp_64_get_max_size;
  rtp_hdr_class->write = gst_rtp_header_extension_ntp_64_write;
  rtp_hdr_class->read = gst_rtp_header_extension_ntp_64_read;

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP Header Extension RFC6051 64-bit NTP timestamp",
      GST_RTP_HDREXT_ELEMENT_CLASS,
      "Extends RTP packets to add or retrieve a 64-bit NTP "
      "timestamp as specified in RFC6051",
      "Sebastian Dröge <sebastian@centricular.com>");
  gst_rtp_header_extension_class_set_uri (rtp_hdr_class,
      GST_RTP_HDREXT_BASE GST_RTP_HDREXT_NTP_64);
}

static void
gst_rtp_header_extension_ntp_64_init (GstRTPHeaderExtensionNtp64 * self)
{
  self->last_pts = GST_CLOCK_TIME_NONE;
  self->interval = DEFAULT_INTERVAL;
  self->every_packet = DEFAULT_EVERY_PACKET;
}

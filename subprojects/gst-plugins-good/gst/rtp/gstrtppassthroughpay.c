/*
 * GStreamer
 *
 * Copyright (C) 2023 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2023 Jonas Danielsson <jonas.danielsson@spiideo.com>
 *
 * gstrtppassthroughpay.c:
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
 */

/**
 * SECTION:element-rtppassthroughpay
 * @title: rtppassthroughpay
 *
 * This elements pass RTP packets along unchanged and appear as a RTP
 * payloader element to the outside world.
 * 
 * This is useful, for example, in the case where you are receiving RTP
 * packets from a different source and want to serve them over RTSP. Since the
 * gst-rtsp-server library expect the element marked as `payX` to be a RTP
 * payloader element and assumes certain properties are available.
 * 
 * ## Example pipelines
 *
 * |[
 * gst-launch-1.0 rtpbin name=rtpbin \
 *     videotestsrc ! videoconvert ! x264enc ! rtph264pay ! rtpbin.send_rtp_sink_0 \
 *     rtpbin.send_rtp_src_0 ! udpsink port=5000
 * ]| Encode and payload H264 video from videotestsrc. The H264 RTP packets are
 * sent on UDP port 5000.
 * |[
 * test-launch "( udpsrc port=5000 caps=application/x-rtp, ..." ! .recv_rtp_sink_0 \
 *      rtpbin ! rtppassthroughpay name=pay0 )"
 * ]| Setup a gstreamer-rtsp-server using the example tool, it will listen for
 * H264 RTP packets on port 5000 and present them using the rtppassthroughpay
 * element as the well-known payloader pay0.
 *
 * Since: 1.24
 */

#include <gst/rtp/rtp.h>

#include "gstrtpelements.h"
#include "gstrtppassthroughpay.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_passthrough_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_passthrough_pay_debug

#define PAYLOAD_TYPE_INVALID 128        /* valid range is 0 - 127 (seven bits) */

G_DEFINE_TYPE (GstRtpPassthroughPay, gst_rtp_passthrough_pay, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtppassthroughpay,
    "rtppassthroughpay", GST_RANK_NONE, GST_TYPE_RTP_PASSTHROUGH_PAY,
    rtp_element_init (plugin));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, " "payload = (int) [ 0, 127 ],"
        "clock-rate = (int) [ 1, 2147483647 ]"));

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, " "payload = (int) [ 0, 127 ],"
        "clock-rate = (int) [ 1, 2147483647 ]"));

enum
{
  PROP_0,
  PROP_PT,
  PROP_MTU,
  PROP_STATS,
  PROP_SEQNUM,
  PROP_SEQNUM_OFFSET,
  PROP_TIMESTAMP,
  PROP_TIMESTAMP_OFFSET,
};

static void gst_rtp_passthrough_pay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_rtp_passthrough_pay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rtp_passthrough_pay_finalize (GObject * object);

static GstStateChangeReturn
gst_rtp_passthrough_pay_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_rtp_passthrough_pay_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_rtp_passthrough_pay_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

static void gst_rtp_passthrough_set_payload_type (GstRtpPassthroughPay * self,
    guint pt);

static GstStructure
    * gst_rtp_passthrough_pay_create_stats (GstRtpPassthroughPay * self);

static void
gst_rtp_passthrough_pay_init (GstRtpPassthroughPay * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (self->sinkpad, gst_rtp_passthrough_pay_chain);
  gst_pad_set_event_function (self->sinkpad,
      gst_rtp_passthrough_pay_sink_event);
  GST_OBJECT_FLAG_SET (self->sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);
  GST_OBJECT_FLAG_SET (self->sinkpad, GST_PAD_FLAG_PROXY_CAPS);
  GST_OBJECT_FLAG_SET (self->sinkpad, GST_PAD_FLAG_PROXY_SCHEDULING);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  GST_OBJECT_FLAG_SET (self->srcpad, GST_PAD_FLAG_PROXY_CAPS);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->pt = PAYLOAD_TYPE_INVALID;
}

static void
gst_rtp_passthrough_pay_class_init (GstRtpPassthroughPayClass
    * gst_rtp_passthrough_pay_class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (gst_rtp_passthrough_pay_class);
  GstElementClass *element_class =
      GST_ELEMENT_CLASS (gst_rtp_passthrough_pay_class);

  gobject_class->set_property = gst_rtp_passthrough_pay_set_property;
  gobject_class->get_property = gst_rtp_passthrough_pay_get_property;
  gobject_class->finalize = gst_rtp_passthrough_pay_finalize;

  /**
   * GstRtpPassthroughPay:pt:
   *
   * If set this will override the payload of the incoming RTP packets.
   * If not set the payload type will be same as incoming RTP packets.
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class), PROP_PT,
      g_param_spec_uint ("pt", "payload type",
          "The payload type of the packets", 0, 0x80, PAYLOAD_TYPE_INVALID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpPassthroughPay:mtu:
   *
   * Setting this property has no effect on this element, it is here and it
   * is writable only to emulate a proper RTP payloader.
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class), PROP_MTU,
      g_param_spec_uint ("mtu", "MTU", "Maximum size of one packet", 28,
          G_MAXUINT, 1492, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpPassthroughPay:timestamp:
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class),
      PROP_TIMESTAMP, g_param_spec_uint ("timestamp", "Timestamp",
          "The RTP timestamp of the last processed packet", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpPassthroughPay:seqnum:
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet", 0,
          G_MAXUINT16, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpPassthroughPay:timestamp-offset:
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class),
      PROP_TIMESTAMP_OFFSET, g_param_spec_uint ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (default = random)", 0,
          G_MAXUINT32, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpPassthroughPay:seqnum-offset:
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class),
      PROP_SEQNUM_OFFSET, g_param_spec_int ("seqnum-offset",
          "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXUINT16,
          -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRtpPassthroughPay:stats:
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (gobject_class), PROP_STATS,
      g_param_spec_boxed ("stats", "Statistics", "Various statistics",
          GST_TYPE_STRUCTURE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = gst_rtp_passthrough_pay_change_state;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "RTP Passthrough payloader", "Codec/Payloader/Network/RTP",
      "Passes through RTP packets",
      "Sebastian Dröge <sebastian@centricular.com>, "
      "Jonas Danielsson <jonas.danielsson@spiideo.com>");

  GST_DEBUG_CATEGORY_INIT (gst_rtp_passthrough_pay_debug, "rtppassthroughpay",
      0, "RTP Passthrough Payloader");
}

static void
gst_rtp_passthrough_pay_finalize (GObject * object)
{
  GstRtpPassthroughPay *self = GST_RTP_PASSTHROUGH_PAY (object);

  gst_clear_caps (&self->caps);
  G_OBJECT_CLASS (gst_rtp_passthrough_pay_parent_class)->finalize (object);
}

static void
gst_rtp_passthrough_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpPassthroughPay *self = GST_RTP_PASSTHROUGH_PAY (object);

  switch (prop_id) {
    case PROP_PT:
      g_value_set_uint (value, self->pt);
      break;
    case PROP_MTU:
      g_value_set_uint (value, 0U);
      break;
    case PROP_TIMESTAMP:
      g_value_set_uint (value, self->timestamp);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_uint (value, self->timestamp_offset);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, self->seqnum);
      break;
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, (guint16) self->seqnum_offset);
      break;
    case PROP_STATS:
      g_value_take_boxed (value, gst_rtp_passthrough_pay_create_stats (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_passthrough_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpPassthroughPay *self = GST_RTP_PASSTHROUGH_PAY (object);

  switch (prop_id) {
    case PROP_PT:
      gst_rtp_passthrough_set_payload_type (self, g_value_get_uint (value));
      break;
    case PROP_MTU:
      GST_WARNING_OBJECT (self, "Setting the mtu property has no effect");
      break;
    case PROP_TIMESTAMP_OFFSET:
      GST_FIXME_OBJECT (self,
          "Setting the timestamp-offset property has no effect");
      break;
    case PROP_SEQNUM_OFFSET:
      GST_FIXME_OBJECT (self,
          "Setting the seqnum-offset property has no effect");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_passthrough_pay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpPassthroughPay *self = GST_RTP_PASSTHROUGH_PAY (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (gst_rtp_passthrough_pay_parent_class)
      ->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_clear_caps (&self->caps);
      gst_segment_init (&self->segment, GST_FORMAT_TIME);
      self->clock_rate = -1;
      self->pt = PAYLOAD_TYPE_INVALID;
      self->pt_override = FALSE;
      self->ssrc = -1;
      self->ssrc_set = FALSE;
      self->timestamp = -1;
      self->timestamp_offset = -1;
      self->timestamp_offset_set = FALSE;
      self->seqnum = -1;
      self->pts_or_dts = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_rtp_passthrough_pay_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer)
{
  GstRtpPassthroughPay *self = GST_RTP_PASSTHROUGH_PAY (parent);
  GstRTPBuffer rtp_buffer = GST_RTP_BUFFER_INIT;
  guint pt, ssrc, seqnum, timestamp;

  if (!gst_rtp_buffer_map (buffer, GST_MAP_READWRITE, &rtp_buffer)) {
    GST_ERROR_OBJECT (self, "Invalid RTP buffer");
    return gst_pad_push (self->srcpad, buffer);
  }

  /* If the PROP_PT property is set we override the incoming packets payload
   * type. If it is not, we will mirror the payload type.
   *
   */
  pt = gst_rtp_buffer_get_payload_type (&rtp_buffer);
  if (self->pt_override && self->pt != PAYLOAD_TYPE_INVALID) {
    gst_rtp_buffer_set_payload_type (&rtp_buffer, self->pt);
  } else {
    if (pt != self->pt) {
      if (self->pt != PAYLOAD_TYPE_INVALID) {
        GST_WARNING_OBJECT (self, "Payload type changed from %u to %u",
            self->pt, pt);
      }
      self->pt = pt;
      g_object_notify (G_OBJECT (self), "pt");
    }
  }

  ssrc = gst_rtp_buffer_get_ssrc (&rtp_buffer);
  if (self->ssrc_set && self->ssrc != ssrc) {
    GST_WARNING_OBJECT (self, "SSRC changed from %u to %u", self->ssrc, ssrc);
  }
  self->ssrc = ssrc;
  self->ssrc_set = TRUE;

  seqnum = gst_rtp_buffer_get_seq (&rtp_buffer);
  self->seqnum = seqnum;
  if (self->seqnum_offset == (guint) (-1)) {
    self->seqnum_offset = seqnum;
    g_object_notify (G_OBJECT (self), "seqnum-offset");
  }

  timestamp = gst_rtp_buffer_get_timestamp (&rtp_buffer);
  self->timestamp = timestamp;
  if (!self->timestamp_offset_set) {
    self->timestamp_offset = timestamp;
    self->timestamp_offset_set = TRUE;
    g_object_notify (G_OBJECT (self), "timestamp-offset");
  }

  gst_rtp_buffer_unmap (&rtp_buffer);

  if (GST_BUFFER_PTS_IS_VALID (buffer))
    self->pts_or_dts = GST_BUFFER_PTS (buffer);
  else if (GST_BUFFER_DTS_IS_VALID (buffer))
    self->pts_or_dts = GST_BUFFER_DTS (buffer);

  return gst_pad_push (self->srcpad, buffer);
}

static gboolean
gst_rtp_passthrough_pay_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event)
{
  GstRtpPassthroughPay *self = GST_RTP_PASSTHROUGH_PAY (parent);
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:{
      gst_event_copy_segment (event, &self->segment);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      const GstStructure *s;

      gst_event_parse_caps (event, &caps);
      gst_caps_replace (&self->caps, caps);

      s = gst_caps_get_structure (caps, 0);

      gst_structure_get_uint (s, "payload", &self->pt);
      gst_structure_get_uint (s, "clock-rate", &self->clock_rate);
      if (gst_structure_get_uint (s, "ssrc", &self->ssrc))
        self->ssrc_set = TRUE;
      if (gst_structure_get_uint (s, "clock-base", &self->timestamp_offset))
        self->timestamp_offset_set = TRUE;
      gst_structure_get_uint (s, "seqnum-base", &self->seqnum_offset);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static void
gst_rtp_passthrough_set_payload_type (GstRtpPassthroughPay * self, guint pt)
{
  if (self->pt == pt) {
    return;
  }

  if (pt != PAYLOAD_TYPE_INVALID) {
    GST_INFO_OBJECT (self, "Overriding payload type (%u)", pt);
    self->pt_override = TRUE;
  } else {
    self->pt_override = FALSE;
  }

  self->pt = pt;
}

static GstStructure *
gst_rtp_passthrough_pay_create_stats (GstRtpPassthroughPay * self)
{
  GstClockTime running_time;

  running_time = gst_segment_to_running_time (&self->segment, GST_FORMAT_TIME,
      self->pts_or_dts);

  return gst_structure_new ("application/x-rtp-payload-stats", "clock-rate",
      G_TYPE_UINT, (guint) self->clock_rate, "running-time", G_TYPE_UINT64,
      running_time, "seqnum", G_TYPE_UINT, (guint) self->seqnum, "timestamp",
      G_TYPE_UINT, (guint) self->timestamp, "ssrc", G_TYPE_UINT, self->ssrc,
      "pt", G_TYPE_UINT, self->pt, "seqnum-offset", G_TYPE_UINT,
      (guint) self->seqnum_offset, "timestamp-offset", G_TYPE_UINT,
      (guint) self->timestamp_offset, NULL);

  return NULL;
}

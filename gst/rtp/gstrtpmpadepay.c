/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpmpadepay.h"

/* elementfactory information */
static GstElementDetails gst_rtp_mpadepay_details =
GST_ELEMENT_DETAILS ("RTP packet parser",
    "Codec/Depayr/Network",
    "Extracts MPEG audio from RTP packets (RFC 2038)",
    "Wim Taymans <wim@fluendo.com>");

/* RtpMPADepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FREQUENCY
};

static GstStaticPadTemplate gst_rtp_mpa_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
    );

static GstStaticPadTemplate gst_rtp_mpa_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPA\"")
    );


static void gst_rtp_mpa_depay_class_init (GstRtpMPADepayClass * klass);
static void gst_rtp_mpa_depay_base_init (GstRtpMPADepayClass * klass);
static void gst_rtp_mpa_depay_init (GstRtpMPADepay * rtpmpadepay);

static GstFlowReturn gst_rtp_mpa_depay_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtp_mpa_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mpa_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtp_mpa_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtp_mpa_depay_get_type (void)
{
  static GType rtpmpadepay_type = 0;

  if (!rtpmpadepay_type) {
    static const GTypeInfo rtpmpadepay_info = {
      sizeof (GstRtpMPADepayClass),
      (GBaseInitFunc) gst_rtp_mpa_depay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_mpa_depay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMPADepay),
      0,
      (GInstanceInitFunc) gst_rtp_mpa_depay_init,
    };

    rtpmpadepay_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpMPADepay",
        &rtpmpadepay_info, 0);
  }
  return rtpmpadepay_type;
}

static void
gst_rtp_mpa_depay_base_init (GstRtpMPADepayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpa_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpa_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpadepay_details);
}

static void
gst_rtp_mpa_depay_class_init (GstRtpMPADepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtp_mpa_depay_set_property;
  gobject_class->get_property = gst_rtp_mpa_depay_get_property;

  gstelement_class->change_state = gst_rtp_mpa_depay_change_state;
}

static void
gst_rtp_mpa_depay_init (GstRtpMPADepay * rtpmpadepay)
{
  rtpmpadepay->srcpad =
      gst_pad_new_from_static_template (&gst_rtp_mpa_depay_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (rtpmpadepay), rtpmpadepay->srcpad);

  rtpmpadepay->sinkpad =
      gst_pad_new_from_static_template (&gst_rtp_mpa_depay_sink_template,
      "sink");
  gst_pad_set_chain_function (rtpmpadepay->sinkpad, gst_rtp_mpa_depay_chain);
  gst_element_add_pad (GST_ELEMENT (rtpmpadepay), rtpmpadepay->sinkpad);
}

static GstFlowReturn
gst_rtp_mpa_depay_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpMPADepay *rtpmpadepay;
  GstBuffer *outbuf;
  guint8 pt;
  GstFlowReturn ret;

  rtpmpadepay = GST_RTP_MPA_DEPAY (GST_OBJECT_PARENT (pad));

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  if ((pt = gst_rtp_buffer_get_payload_type (buf)) != GST_RTP_PAYLOAD_MPA)
    goto bad_payload;


  {
    gint payload_len;
    guint8 *payload;
    guint16 frag_offset;
    guint32 timestamp;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

    frag_offset = (payload[2] << 8) | payload[3];

    /* strip off header
     *
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |             MBZ               |          Frag_offset          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    payload_len -= 4;
    payload += 4;

    timestamp = gst_rtp_buffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);

    //GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / 90000;

    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    GST_DEBUG ("gst_rtp_mpa_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    gst_buffer_unref (buf);

    /* FIXME, we can push half mpeg frames when they are split over multiple
     * RTP packets */
    ret = gst_pad_push (rtpmpadepay->srcpad, outbuf);
  }

  return ret;

bad_packet:
  {
    GST_DEBUG ("Packet did not validate");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
bad_payload:
  {
    GST_DEBUG ("Unexpected payload type %u", pt);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_rtp_mpa_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMPADepay *rtpmpadepay;

  rtpmpadepay = GST_RTP_MPA_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mpa_depay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpMPADepay *rtpmpadepay;

  rtpmpadepay = GST_RTP_MPA_DEPAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtp_mpa_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpMPADepay *rtpmpadepay;
  GstStateChangeReturn ret;

  rtpmpadepay = GST_RTP_MPA_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_mpa_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpadepay",
      GST_RANK_NONE, GST_TYPE_RTP_MPA_DEPAY);
}

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
#include "gstrtpmpadec.h"

/* elementfactory information */
static GstElementDetails gst_rtp_mpadec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts MPEG audio from RTP packets (RFC 2038)",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpMPADec signals and args */
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

static GstStaticPadTemplate gst_rtpmpadec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
    );

static GstStaticPadTemplate gst_rtpmpadec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 255 ], "
        "clock_rate = (int) 90000, " "encoding_name = (string) \"MPA\"")
    );


static void gst_rtpmpadec_class_init (GstRtpMPADecClass * klass);
static void gst_rtpmpadec_base_init (GstRtpMPADecClass * klass);
static void gst_rtpmpadec_init (GstRtpMPADec * rtpmpadec);

static GstFlowReturn gst_rtpmpadec_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpmpadec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpmpadec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpmpadec_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpmpadec_get_type (void)
{
  static GType rtpmpadec_type = 0;

  if (!rtpmpadec_type) {
    static const GTypeInfo rtpmpadec_info = {
      sizeof (GstRtpMPADecClass),
      (GBaseInitFunc) gst_rtpmpadec_base_init,
      NULL,
      (GClassInitFunc) gst_rtpmpadec_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMPADec),
      0,
      (GInstanceInitFunc) gst_rtpmpadec_init,
    };

    rtpmpadec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpMPADec",
        &rtpmpadec_info, 0);
  }
  return rtpmpadec_type;
}

static void
gst_rtpmpadec_base_init (GstRtpMPADecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmpadec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmpadec_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpadec_details);
}

static void
gst_rtpmpadec_class_init (GstRtpMPADecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpmpadec_set_property;
  gobject_class->get_property = gst_rtpmpadec_get_property;

  gstelement_class->change_state = gst_rtpmpadec_change_state;
}

static void
gst_rtpmpadec_init (GstRtpMPADec * rtpmpadec)
{
  rtpmpadec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpmpadec_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpmpadec), rtpmpadec->srcpad);

  rtpmpadec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpmpadec_sink_template), "sink");
  gst_pad_set_chain_function (rtpmpadec->sinkpad, gst_rtpmpadec_chain);
  gst_element_add_pad (GST_ELEMENT (rtpmpadec), rtpmpadec->sinkpad);
}

static GstFlowReturn
gst_rtpmpadec_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpMPADec *rtpmpadec;
  GstBuffer *outbuf;
  guint8 pt;
  GstFlowReturn ret;

  rtpmpadec = GST_RTP_MPA_DEC (GST_OBJECT_PARENT (pad));

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  if ((pt = gst_rtpbuffer_get_payload_type (buf)) != GST_RTP_PAYLOAD_MPA)
    goto bad_payload;


  {
    gint payload_len;
    guint8 *payload;
    guint16 frag_offset;
    guint32 timestamp;

    payload_len = gst_rtpbuffer_get_payload_len (buf);
    payload = gst_rtpbuffer_get_payload (buf);

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

    timestamp = gst_rtpbuffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);

    //GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / 90000;

    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    GST_DEBUG ("gst_rtpmpadec_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    gst_buffer_unref (buf);

    /* FIXME, we can push half mpeg frames when they are split over multiple
     * RTP packets */
    ret = gst_pad_push (rtpmpadec->srcpad, outbuf);
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
gst_rtpmpadec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMPADec *rtpmpadec;

  rtpmpadec = GST_RTP_MPA_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpmpadec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpMPADec *rtpmpadec;

  rtpmpadec = GST_RTP_MPA_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtpmpadec_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpMPADec *rtpmpadec;
  GstStateChangeReturn ret;

  rtpmpadec = GST_RTP_MPA_DEC (element);

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
gst_rtpmpadec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpadec",
      GST_RANK_NONE, GST_TYPE_RTP_MPA_DEC);
}

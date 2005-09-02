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

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpmpaenc.h"

/* elementfactory information */
static GstElementDetails gst_rtp_mpaenc_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Encode MPEG audio as RTP packets",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpMPAEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_MTU	1024

enum
{
  PROP_0,
  PROP_MTU
};

static GstStaticPadTemplate gst_rtpmpaenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
    );

static GstStaticPadTemplate gst_rtpmpaenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static void gst_rtpmpaenc_class_init (GstRtpMPAEncClass * klass);
static void gst_rtpmpaenc_base_init (GstRtpMPAEncClass * klass);
static void gst_rtpmpaenc_init (GstRtpMPAEnc * rtpmpaenc);

static GstFlowReturn gst_rtpmpaenc_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpmpaenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpmpaenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpmpaenc_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpmpaenc_get_type (void)
{
  static GType rtpmpaenc_type = 0;

  if (!rtpmpaenc_type) {
    static const GTypeInfo rtpmpaenc_info = {
      sizeof (GstRtpMPAEncClass),
      (GBaseInitFunc) gst_rtpmpaenc_base_init,
      NULL,
      (GClassInitFunc) gst_rtpmpaenc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMPAEnc),
      0,
      (GInstanceInitFunc) gst_rtpmpaenc_init,
    };

    rtpmpaenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpMPAEnc",
        &rtpmpaenc_info, 0);
  }
  return rtpmpaenc_type;
}

static void
gst_rtpmpaenc_base_init (GstRtpMPAEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmpaenc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmpaenc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpaenc_details);
}

static void
gst_rtpmpaenc_class_init (GstRtpMPAEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpmpaenc_set_property;
  gobject_class->get_property = gst_rtpmpaenc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MTU,
      g_param_spec_uint ("mtu", "MTU",
          "Maximum size of one packet",
          28, G_MAXUINT, DEFAULT_MTU, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_rtpmpaenc_change_state;
}

static void
gst_rtpmpaenc_init (GstRtpMPAEnc * rtpmpaenc)
{
  rtpmpaenc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpmpaenc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpmpaenc), rtpmpaenc->srcpad);

  rtpmpaenc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpmpaenc_sink_template), "sink");
  gst_pad_set_chain_function (rtpmpaenc->sinkpad, gst_rtpmpaenc_chain);
  gst_element_add_pad (GST_ELEMENT (rtpmpaenc), rtpmpaenc->sinkpad);

  rtpmpaenc->adapter = gst_adapter_new ();
  rtpmpaenc->mtu = DEFAULT_MTU;
}

static GstFlowReturn
gst_rtpmpaenc_flush (GstRtpMPAEnc * rtpmpaenc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint16 frag_offset;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to split the MPA data
   * over multiple packets. The frag_offset in each packet header
   * needs to be updated with the position in the MPA frame. */
  avail = gst_adapter_available (rtpmpaenc->adapter);

  ret = GST_FLOW_OK;

  frag_offset = 0;
  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtpbuffer_calc_packet_len (4 + avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, rtpmpaenc->mtu);

    /* this is the payload length */
    payload_len = gst_rtpbuffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);

    payload_len -= 4;

    /* set timestamp */
    gst_rtpbuffer_set_timestamp (outbuf,
        rtpmpaenc->first_ts * 90000 / GST_SECOND);
    gst_rtpbuffer_set_payload_type (outbuf, GST_RTP_PAYLOAD_MPA);
    gst_rtpbuffer_set_seq (outbuf, rtpmpaenc->seqnum++);

    payload = gst_rtpbuffer_get_payload (outbuf);
    payload[0] = 0;
    payload[1] = 0;
    payload[2] = frag_offset >> 8;
    payload[3] = frag_offset & 0xff;

    data = (guint8 *) gst_adapter_peek (rtpmpaenc->adapter, payload_len);
    memcpy (&payload[4], data, payload_len);
    gst_adapter_flush (rtpmpaenc->adapter, payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmpaenc->first_ts;

    ret = gst_pad_push (rtpmpaenc->srcpad, outbuf);

    avail -= payload_len;
    frag_offset += payload_len;
  }

  return ret;
}

static GstFlowReturn
gst_rtpmpaenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRtpMPAEnc *rtpmpaenc;
  GstFlowReturn ret;
  guint size, avail;
  guint packet_len;

  rtpmpaenc = GST_RTP_MPA_ENC (gst_pad_get_parent (pad));

  size = GST_BUFFER_SIZE (buffer);
  avail = gst_adapter_available (rtpmpaenc->adapter);

  /* get packet length of previous data and this new data, 
   * payload length includes a 4 byte header */
  packet_len = gst_rtpbuffer_calc_packet_len (4 + avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (packet_len > rtpmpaenc->mtu) {
    ret = gst_rtpmpaenc_flush (rtpmpaenc);
    avail = 0;
  }

  gst_adapter_push (rtpmpaenc->adapter, buffer);

  if (avail == 0) {
    rtpmpaenc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
  }
  gst_object_unref (rtpmpaenc);

  ret = GST_FLOW_OK;

  return ret;
}

static void
gst_rtpmpaenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMPAEnc *rtpmpaenc;

  rtpmpaenc = GST_RTP_MPA_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      rtpmpaenc->mtu = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpmpaenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpMPAEnc *rtpmpaenc;

  rtpmpaenc = GST_RTP_MPA_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, rtpmpaenc->mtu);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtpmpaenc_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpMPAEnc *rtpmpaenc;
  GstStateChangeReturn ret;

  rtpmpaenc = GST_RTP_MPA_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rtpmpaenc->seqnum = 0;
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
gst_rtpmpaenc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpaenc",
      GST_RANK_NONE, GST_TYPE_RTP_MPA_ENC);
}

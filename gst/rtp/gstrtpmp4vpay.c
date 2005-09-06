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

#include "gstrtpmp4venc.h"

/* elementfactory information */
static GstElementDetails gst_rtp_mp4venc_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Encode MPEG4 video as RTP packets (RFC 3016)",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpMP4VEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_MTU	1024
#define DEFAULT_PT      96
#define DEFAULT_SSRC    0

enum
{
  PROP_0,
  PROP_MTU,
  PROP_PT,
  PROP_SSRC
};

static GstStaticPadTemplate gst_rtpmp4venc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg,"
        "mpegversion=(int) 4," "systemstream=(boolean)false")
    );

static GstStaticPadTemplate gst_rtpmp4venc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp,"
        "rate=(int) [1, MAX]," "profile-level-id=[1,MAX]"
        /* "config=" */
    )
    );


static void gst_rtpmp4venc_class_init (GstRtpMP4VEncClass * klass);
static void gst_rtpmp4venc_base_init (GstRtpMP4VEncClass * klass);
static void gst_rtpmp4venc_init (GstRtpMP4VEnc * rtpmp4venc);

static gboolean gst_rtpmp4venc_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_rtpmp4venc_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpmp4venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpmp4venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpmp4venc_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpmp4venc_get_type (void)
{
  static GType rtpmp4venc_type = 0;

  if (!rtpmp4venc_type) {
    static const GTypeInfo rtpmp4venc_info = {
      sizeof (GstRtpMP4VEncClass),
      (GBaseInitFunc) gst_rtpmp4venc_base_init,
      NULL,
      (GClassInitFunc) gst_rtpmp4venc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMP4VEnc),
      0,
      (GInstanceInitFunc) gst_rtpmp4venc_init,
    };

    rtpmp4venc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpMP4VEnc",
        &rtpmp4venc_info, 0);
  }
  return rtpmp4venc_type;
}

static void
gst_rtpmp4venc_base_init (GstRtpMP4VEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmp4venc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpmp4venc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mp4venc_details);
}

static void
gst_rtpmp4venc_class_init (GstRtpMP4VEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpmp4venc_set_property;
  gobject_class->get_property = gst_rtpmp4venc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MTU,
      g_param_spec_uint ("mtu", "MTU",
          "Maximum size of one packet",
          28, G_MAXUINT, DEFAULT_MTU, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PT,
      g_param_spec_uint ("pt", "payload type",
          "The payload type of the packets",
          0, 0x80, DEFAULT_PT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets",
          0, G_MAXUINT, DEFAULT_SSRC, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_rtpmp4venc_change_state;
}

static void
gst_rtpmp4venc_init (GstRtpMP4VEnc * rtpmp4venc)
{
  rtpmp4venc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpmp4venc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpmp4venc), rtpmp4venc->srcpad);

  rtpmp4venc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpmp4venc_sink_template), "sink");
  gst_pad_set_setcaps_function (rtpmp4venc->sinkpad, gst_rtpmp4venc_setcaps);
  gst_pad_set_chain_function (rtpmp4venc->sinkpad, gst_rtpmp4venc_chain);
  gst_element_add_pad (GST_ELEMENT (rtpmp4venc), rtpmp4venc->sinkpad);

  rtpmp4venc->adapter = gst_adapter_new ();
  rtpmp4venc->mtu = DEFAULT_MTU;
  rtpmp4venc->pt = DEFAULT_PT;
  rtpmp4venc->ssrc = DEFAULT_SSRC;
  rtpmp4venc->rate = 90000;
  rtpmp4venc->profile = 1;
}

static gboolean
gst_rtpmp4venc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRtpMP4VEnc *rtpmp4venc;
  GstCaps *srccaps;

  rtpmp4venc = GST_RTP_MP4V_ENC (gst_pad_get_parent (pad));

  srccaps = gst_caps_new_simple ("application/x-rtp",
      "rate", G_TYPE_INT, rtpmp4venc->rate,
      "profile-level-id", G_TYPE_INT, rtpmp4venc->profile, NULL);
  gst_pad_set_caps (rtpmp4venc->srcpad, srccaps);
  gst_caps_unref (srccaps);

  gst_object_unref (rtpmp4venc);

  return TRUE;
}

static GstFlowReturn
gst_rtpmp4venc_flush (GstRtpMP4VEnc * rtpmp4venc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint16 frag_offset;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to split the MP4V data
   * over multiple packets. */
  avail = gst_adapter_available (rtpmp4venc->adapter);

  ret = GST_FLOW_OK;

  frag_offset = 0;
  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtpbuffer_calc_packet_len (avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, rtpmp4venc->mtu);

    /* this is the payload length */
    payload_len = gst_rtpbuffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (rtpmp4venc->srcpad));

    /* set timestamp */
    if (GST_CLOCK_TIME_IS_VALID (rtpmp4venc->first_ts)) {
      gst_rtpbuffer_set_timestamp (outbuf,
          rtpmp4venc->first_ts * 90000 / GST_SECOND);
    }
    gst_rtpbuffer_set_payload_type (outbuf, rtpmp4venc->pt);
    gst_rtpbuffer_set_seq (outbuf, rtpmp4venc->seqnum++);

    payload = gst_rtpbuffer_get_payload (outbuf);

    data = (guint8 *) gst_adapter_peek (rtpmp4venc->adapter, payload_len);
    memcpy (payload, data, payload_len);
    gst_adapter_flush (rtpmp4venc->adapter, payload_len);

    avail -= payload_len;
    frag_offset += payload_len;

    gst_rtpbuffer_set_marker (outbuf, avail == 0);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmp4venc->first_ts;

    ret = gst_pad_push (rtpmp4venc->srcpad, outbuf);
  }

  return ret;
}

/* we expect buffers starting on startcodes. 
 *
 * FIXME, need to flush the adapter if we receive non VOP
 * packets. 
 */
static GstFlowReturn
gst_rtpmp4venc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRtpMP4VEnc *rtpmp4venc;
  GstFlowReturn ret;
  guint size, avail;
  guint packet_len;

  rtpmp4venc = GST_RTP_MP4V_ENC (gst_pad_get_parent (pad));

  size = GST_BUFFER_SIZE (buffer);
  avail = gst_adapter_available (rtpmp4venc->adapter);

  /* get packet length of previous data and this new data */
  packet_len = gst_rtpbuffer_calc_packet_len (avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (packet_len > rtpmp4venc->mtu) {
    ret = gst_rtpmp4venc_flush (rtpmp4venc);
    avail = 0;
  }

  gst_adapter_push (rtpmp4venc->adapter, buffer);

  if (avail == 0) {
    rtpmp4venc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
  }
  gst_object_unref (rtpmp4venc);

  ret = GST_FLOW_OK;

  return ret;
}

static void
gst_rtpmp4venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP4VEnc *rtpmp4venc;

  rtpmp4venc = GST_RTP_MP4V_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      rtpmp4venc->mtu = g_value_get_uint (value);
      break;
    case PROP_PT:
      rtpmp4venc->pt = g_value_get_uint (value);
      break;
    case PROP_SSRC:
      rtpmp4venc->ssrc = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpmp4venc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpMP4VEnc *rtpmp4venc;

  rtpmp4venc = GST_RTP_MP4V_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, rtpmp4venc->mtu);
      break;
    case PROP_PT:
      g_value_set_uint (value, rtpmp4venc->pt);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, rtpmp4venc->ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtpmp4venc_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpMP4VEnc *rtpmp4venc;
  GstStateChangeReturn ret;

  rtpmp4venc = GST_RTP_MP4V_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rtpmp4venc->seqnum = 0;
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
gst_rtpmp4venc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp4venc",
      GST_RANK_NONE, GST_TYPE_RTP_MP4V_ENC);
}

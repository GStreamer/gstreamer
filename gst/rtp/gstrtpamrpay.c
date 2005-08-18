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

#include "gstrtpamrenc.h"

/* elementfactory information */
static GstElementDetails gst_rtp_amrenc_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Encode AMR audio into RTP packets",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpAMREnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_MTU	1024
#define DEFAULT_PT	96
#define DEFAULT_SSRC	0

enum
{
  PROP_0,
  PROP_MTU,
  PROP_PT,
  PROP_SSRC
};

static GstStaticPadTemplate gst_rtpamrenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-amr-nb, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtpamrenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static void gst_rtpamrenc_class_init (GstRtpAMREncClass * klass);
static void gst_rtpamrenc_base_init (GstRtpAMREncClass * klass);
static void gst_rtpamrenc_init (GstRtpAMREnc * rtpamrenc);

static GstFlowReturn gst_rtpamrenc_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpamrenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpamrenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_rtpamrenc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpamrenc_get_type (void)
{
  static GType rtpamrenc_type = 0;

  if (!rtpamrenc_type) {
    static const GTypeInfo rtpamrenc_info = {
      sizeof (GstRtpAMREncClass),
      (GBaseInitFunc) gst_rtpamrenc_base_init,
      NULL,
      (GClassInitFunc) gst_rtpamrenc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpAMREnc),
      0,
      (GInstanceInitFunc) gst_rtpamrenc_init,
    };

    rtpamrenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpAMREnc",
        &rtpamrenc_info, 0);
  }
  return rtpamrenc_type;
}

static void
gst_rtpamrenc_base_init (GstRtpAMREncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpamrenc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpamrenc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_amrenc_details);
}

static void
gst_rtpamrenc_class_init (GstRtpAMREncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpamrenc_set_property;
  gobject_class->get_property = gst_rtpamrenc_get_property;

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

  gstelement_class->change_state = gst_rtpamrenc_change_state;
}

static void
gst_rtpamrenc_init (GstRtpAMREnc * rtpamrenc)
{
  rtpamrenc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpamrenc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtpamrenc), rtpamrenc->srcpad);

  rtpamrenc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpamrenc_sink_template), "sink");
  gst_pad_set_chain_function (rtpamrenc->sinkpad, gst_rtpamrenc_chain);
  gst_element_add_pad (GST_ELEMENT (rtpamrenc), rtpamrenc->sinkpad);

  rtpamrenc->mtu = DEFAULT_MTU;
  rtpamrenc->pt = DEFAULT_PT;
  rtpamrenc->ssrc = DEFAULT_SSRC;
}

static GstFlowReturn
gst_rtpamrenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRtpAMREnc *rtpamrenc;
  GstFlowReturn ret;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload;
  GstClockTime timestamp;

  rtpamrenc = GST_RTP_AMR_ENC (gst_pad_get_parent (pad));

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* FIXME, only one AMR frame per RTP packet for now, 
   * octet aligned, no interleaving, single channel, no CRC,
   * no robust-sorting. */
  payload_len = size + 2;

  outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
  /* FIXME, assert for now */
  g_assert (GST_BUFFER_SIZE (outbuf) < rtpamrenc->mtu);

  gst_rtpbuffer_set_payload_type (outbuf, rtpamrenc->pt);
  gst_rtpbuffer_set_ssrc (outbuf, rtpamrenc->ssrc);
  gst_rtpbuffer_set_seq (outbuf, rtpamrenc->seqnum++);
  gst_rtpbuffer_set_timestamp (outbuf, timestamp * 8000 / GST_SECOND);

  /* copy timestamp */
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  /* get payload */
  payload = gst_rtpbuffer_get_payload (outbuf);

  /*   0 1 2 3 4 5 6 7 
   *  +-+-+-+-+-+-+-+-+
   *  |  CMR  |R|R|R|R|
   *  +-+-+-+-+-+-+-+-+
   */
  payload[0] = 0xF0;            /* CMR, no specific mode requested */
  /*   0 1 2 3 4 5 6 7
   *  +-+-+-+-+-+-+-+-+
   *  |F|  FT   |Q|P|P|
   *  +-+-+-+-+-+-+-+-+
   */
  payload[1] = 0x04;            /* ToC, no damage (Q=1) */

  /* copy data in payload */
  memcpy (&payload[2], GST_BUFFER_DATA (buffer), size);

  gst_buffer_unref (buffer);

  ret = gst_pad_push (rtpamrenc->srcpad, outbuf);

  gst_object_unref (rtpamrenc);

  return ret;
}

static void
gst_rtpamrenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpAMREnc *rtpamrenc;

  rtpamrenc = GST_RTP_AMR_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      rtpamrenc->mtu = g_value_get_uint (value);
      break;
    case PROP_PT:
      rtpamrenc->pt = g_value_get_uint (value);
      break;
    case PROP_SSRC:
      rtpamrenc->ssrc = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpamrenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpAMREnc *rtpamrenc;

  rtpamrenc = GST_RTP_AMR_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, rtpamrenc->mtu);
      break;
    case PROP_PT:
      g_value_set_uint (value, rtpamrenc->pt);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, rtpamrenc->ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_rtpamrenc_change_state (GstElement * element)
{
  GstRtpAMREnc *rtpamrenc;
  gint transition;
  GstElementStateReturn ret;

  rtpamrenc = GST_RTP_AMR_ENC (element);
  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      rtpamrenc->seqnum = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element);

  switch (transition) {
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtpamrenc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrenc",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_ENC);
}

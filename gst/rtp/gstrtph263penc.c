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

#include "gstrtph263penc.h"

/* elementfactory information */
static GstElementDetails gst_rtp_h263penc_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts H263+ video from RTP packets",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpH263PEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_MTU     1024
#define DEFAULT_PT      96
#define DEFAULT_SSRC    0

enum
{
  PROP_0,
  PROP_MTU,
  PROP_PT,
  PROP_SSRC
};

static GstStaticPadTemplate gst_rtph263penc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263")
    );

static GstStaticPadTemplate gst_rtph263penc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static void gst_rtph263penc_class_init (GstRtpH263PEncClass * klass);
static void gst_rtph263penc_base_init (GstRtpH263PEncClass * klass);
static void gst_rtph263penc_init (GstRtpH263PEnc * rtph263penc);

static GstFlowReturn gst_rtph263penc_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtph263penc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtph263penc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtph263penc_change_state (GstElement *
    element, GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtph263penc_get_type (void)
{
  static GType rtph263penc_type = 0;

  if (!rtph263penc_type) {
    static const GTypeInfo rtph263penc_info = {
      sizeof (GstRtpH263PEncClass),
      (GBaseInitFunc) gst_rtph263penc_base_init,
      NULL,
      (GClassInitFunc) gst_rtph263penc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpH263PEnc),
      0,
      (GInstanceInitFunc) gst_rtph263penc_init,
    };

    rtph263penc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpH263PEnc",
        &rtph263penc_info, 0);
  }
  return rtph263penc_type;
}

static void
gst_rtph263penc_base_init (GstRtpH263PEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263penc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtph263penc_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_h263penc_details);
}

static void
gst_rtph263penc_class_init (GstRtpH263PEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtph263penc_set_property;
  gobject_class->get_property = gst_rtph263penc_get_property;

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

  gstelement_class->change_state = gst_rtph263penc_change_state;
}

static void
gst_rtph263penc_init (GstRtpH263PEnc * rtph263penc)
{
  rtph263penc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtph263penc_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (rtph263penc), rtph263penc->srcpad);

  rtph263penc->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtph263penc_sink_template), "sink");
  gst_pad_set_chain_function (rtph263penc->sinkpad, gst_rtph263penc_chain);
  gst_element_add_pad (GST_ELEMENT (rtph263penc), rtph263penc->sinkpad);

  rtph263penc->adapter = gst_adapter_new ();
  rtph263penc->mtu = DEFAULT_MTU;
}

static GstFlowReturn
gst_rtph263penc_flush (GstRtpH263PEnc * rtph263penc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gboolean fragmented;

  avail = gst_adapter_available (rtph263penc->adapter);
  if (avail == 0)
    return GST_FLOW_OK;

  fragmented = FALSE;

  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    gint header_len;

    /* FIXME, do better mtu packing, header len etc should be
     * included in this calculation. */
    towrite = MIN (avail, rtph263penc->mtu);
    /* for fragmented frames we need 2 bytes header, for other
     * frames we must reuse the first 2 bytes of the data as the
     * header */
    header_len = (fragmented ? 2 : 0);
    payload_len = header_len + towrite;

    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
    gst_rtpbuffer_set_timestamp (outbuf,
        rtph263penc->first_ts * 90000 / GST_SECOND);
    gst_rtpbuffer_set_payload_type (outbuf, rtph263penc->pt);
    gst_rtpbuffer_set_ssrc (outbuf, rtph263penc->ssrc);
    gst_rtpbuffer_set_seq (outbuf, rtph263penc->seqnum++);
    /* last fragment gets the marker bit set */
    gst_rtpbuffer_set_marker (outbuf, avail > towrite ? 0 : 1);

    payload = gst_rtpbuffer_get_payload (outbuf);

    data = (guint8 *) gst_adapter_peek (rtph263penc->adapter, towrite);
    memcpy (&payload[header_len], data, towrite);

    /*  0                   1
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |   RR    |P|V|   PLEN    |PEBIT|
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    payload[0] = fragmented ? 0x00 : 0x04;
    payload[1] = 0;

    GST_BUFFER_TIMESTAMP (outbuf) = rtph263penc->first_ts;
    gst_adapter_flush (rtph263penc->adapter, towrite);

    ret = gst_pad_push (rtph263penc->srcpad, outbuf);

    avail -= towrite;
    fragmented = TRUE;
  }

  return ret;
}

static GstFlowReturn
gst_rtph263penc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRtpH263PEnc *rtph263penc;
  GstFlowReturn ret;
  guint size;

  rtph263penc = GST_RTP_H263P_ENC (GST_OBJECT_PARENT (pad));

  size = GST_BUFFER_SIZE (buffer);
  rtph263penc->first_ts = GST_BUFFER_TIMESTAMP (buffer);

  /* we always encode and flush a full picture */
  gst_adapter_push (rtph263penc->adapter, buffer);
  ret = gst_rtph263penc_flush (rtph263penc);

  return ret;
}

static void
gst_rtph263penc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH263PEnc *rtph263penc;

  rtph263penc = GST_RTP_H263P_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      rtph263penc->mtu = g_value_get_uint (value);
      break;
    case PROP_PT:
      rtph263penc->pt = g_value_get_uint (value);
      break;
    case PROP_SSRC:
      rtph263penc->ssrc = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtph263penc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpH263PEnc *rtph263penc;

  rtph263penc = GST_RTP_H263P_ENC (object);

  switch (prop_id) {
    case PROP_MTU:
      g_value_set_uint (value, rtph263penc->mtu);
      break;
    case PROP_PT:
      g_value_set_uint (value, rtph263penc->pt);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, rtph263penc->ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtph263penc_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpH263PEnc *rtph263penc;
  GstStateChangeReturn ret;

  rtph263penc = GST_RTP_H263P_ENC (element);

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
gst_rtph263penc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263penc",
      GST_RANK_NONE, GST_TYPE_RTP_H263P_ENC);
}

/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#include "gstrtpmp4gpay.h"

GST_DEBUG_CATEGORY (rtpmp4gpay_debug);
#define GST_CAT_DEFAULT (rtpmp4gpay_debug)

/* elementfactory information */
static GstElementDetails gst_rtp_mp4gpay_details = {
  "RTP packet parser",
  "Codec/Payloader/Network",
  "Payload MPEG4 elementary streams as RTP packets (RFC 3640)",
  "Wim Taymans <wim@fluendo.com>"
};

static GstStaticPadTemplate gst_rtp_mp4g_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg,"
        "mpegversion=(int) 4,"
        "systemstream=(boolean)false;" "audio/mpeg," "mpegversion=(int) 4")
    );

static GstStaticPadTemplate gst_rtp_mp4g_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [1, MAX ], "
        "encoding-name = (string) \"MP4G-ES\", "
        /* required string params */
        "streamtype = (string) { \"4\", \"5\" }, "      /* 4 = video, 5 = audio */
        "profile-level-id = (int) [1,MAX], "
        /* "config = (string) [1,MAX]" */
        "mode = (string) { \"generic\", \"CELP-cbr\", \"CELP-vbr\", \"AAC-lbr\", \"AAC-hbr\" }, "
        /* Optional general parameters */
        "objecttype = (int) [1,MAX], " "constantsize = (int) [1,MAX], " /* constant size of each AU */
        "constantduration = (int) [1,MAX], "    /* constant duration of each AU */
        "maxdisplacement = (int) [1,MAX], "
        "de-interleavebuffersize: = (int) [1,MAX], "
        /* Optional configuration parameters */
        "sizelength = (int) [1, 16], "  /* max 16 bits, should be enough... */
        "indexlength = (int) [1, 8], "
        "indexdeltalength = (int) [1, 8], "
        "ctsdeltalength = (int) [1, 64], "
        "dtsdeltalength = (int) [1, 64], "
        "randomaccessindication = (int) {0, 1}, "
        "streamstateindication = (int) [0, 64], "
        "auxiliarydatasizelength = (int) [0, 64]")
    );

enum
{
  PROP_0,
};


static void gst_rtp_mp4g_pay_class_init (GstRtpMP4GPayClass * klass);
static void gst_rtp_mp4g_pay_base_init (GstRtpMP4GPayClass * klass);
static void gst_rtp_mp4g_pay_init (GstRtpMP4GPay * rtpmp4gpay);
static void gst_rtp_mp4g_pay_finalize (GObject * object);

static void gst_rtp_mp4g_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mp4g_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtp_mp4g_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_mp4g_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtp_mp4g_pay_get_type (void)
{
  static GType rtpmp4gpay_type = 0;

  if (!rtpmp4gpay_type) {
    static const GTypeInfo rtpmp4gpay_info = {
      sizeof (GstRtpMP4GPayClass),
      (GBaseInitFunc) gst_rtp_mp4g_pay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_mp4g_pay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpMP4GPay),
      0,
      (GInstanceInitFunc) gst_rtp_mp4g_pay_init,
    };

    rtpmp4gpay_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpMP4GPay",
        &rtpmp4gpay_info, 0);
  }
  return rtpmp4gpay_type;
}

static void
gst_rtp_mp4g_pay_base_init (GstRtpMP4GPayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp4g_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mp4g_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mp4gpay_details);
}

static void
gst_rtp_mp4g_pay_class_init (GstRtpMP4GPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gobject_class->set_property = gst_rtp_mp4g_pay_set_property;
  gobject_class->get_property = gst_rtp_mp4g_pay_get_property;

  gobject_class->finalize = gst_rtp_mp4g_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_mp4g_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_mp4g_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpmp4gpay_debug, "rtpmp4gpay", 0,
      "MP4-generic RTP Payloader");

}

static void
gst_rtp_mp4g_pay_init (GstRtpMP4GPay * rtpmp4gpay)
{
  rtpmp4gpay->adapter = gst_adapter_new ();
  rtpmp4gpay->rate = 90000;
  rtpmp4gpay->profile = 1;
}

static void
gst_rtp_mp4g_pay_finalize (GObject * object)
{
  GstRtpMP4GPay *rtpmp4gpay;

  rtpmp4gpay = GST_RTP_MP4G_PAY (object);

  g_object_unref (rtpmp4gpay->adapter);
  rtpmp4gpay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#if 0
static void
gst_rtp_mp4g_pay_new_caps (GstRtpMP4GPay * rtpmp4gpay)
{
  gchar *profile, *config;
  GValue v = { 0 };

  profile = g_strdup_printf ("%d", rtpmp4gpay->profile);
  g_value_init (&v, GST_TYPE_BUFFER);
  gst_value_set_buffer (&v, rtpmp4gpay->config);
  config = gst_value_serialize (&v);

  gst_basertppayload_set_outcaps (GST_BASE_RTP_PAYLOAD (rtpmp4gpay),
      "profile-level-id", G_TYPE_STRING, profile,
      "config", G_TYPE_STRING, config, NULL);

  g_value_unset (&v);

  g_free (profile);
  g_free (config);
}
#endif

static gboolean
gst_rtp_mp4g_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  GstRtpMP4GPay *rtpmp4gpay;

  rtpmp4gpay = GST_RTP_MP4G_PAY (payload);

  gst_basertppayload_set_options (payload, "video", TRUE, "mpeg4-generic",
      rtpmp4gpay->rate);

  return TRUE;
}

static GstFlowReturn
gst_rtp_mp4g_pay_flush (GstRtpMP4GPay * rtpmp4gpay)
{
  guint avail, total;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  gboolean fragmented;
  guint mtu;

  fragmented = FALSE;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to fragment the MPEG data
   * over multiple packets. */
  total = avail = gst_adapter_available (rtpmp4gpay->adapter);

  ret = GST_FLOW_OK;
  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtpmp4gpay);

  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (avail, 0, 0);

    /* fill one MTU or all available bytes, we need 4 spare bytes for
     * the AU header. */
    towrite = MIN (packet_len, mtu - 4);

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    GST_DEBUG_OBJECT (rtpmp4gpay,
        "avail %d, towrite %d, packet_len %d, payload_len %d", avail, towrite,
        packet_len, payload_len);

    /* create buffer to hold the payload, also make room for the 4 header bytes. */
    outbuf = gst_rtp_buffer_new_allocate (payload_len + 4, 0, 0);

    /* copy payload */
    payload = gst_rtp_buffer_get_payload (outbuf);

    /* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
     * |AU-headers-length|AU-header|AU-header|      |AU-header|padding|
     * |                 |   (1)   |   (2)   |      |   (n)   | bits  |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- .. -+-+-+-+-+-+-+-+-+-+
     */
    /* AU-headers-length, we only have 1 AU-header */
    payload[0] = 0x00;
    payload[1] = 0x10;          /* we use 16 bits for the header */

    /* +---------------------------------------+
     * |     AU-size                           |
     * +---------------------------------------+
     * |     AU-Index / AU-Index-delta         |
     * +---------------------------------------+
     * |     CTS-flag                          |
     * +---------------------------------------+
     * |     CTS-delta                         |
     * +---------------------------------------+
     * |     DTS-flag                          |
     * +---------------------------------------+
     * |     DTS-delta                         |
     * +---------------------------------------+
     * |     RAP-flag                          |
     * +---------------------------------------+
     * |     Stream-state                      |
     * +---------------------------------------+
     */
    /* The AU-header, no CTS, DTS, RAP, Stream-state 
     *
     * AU-size is always the total size of the AU, not the fragmented size 
     */
    payload[2] = (total & 0x1fe0) >> 5;
    payload[3] = (total & 0x1f) << 3;   /* we use 13 bits for the size, 3 bits index */

    data = (guint8 *) gst_adapter_peek (rtpmp4gpay->adapter, payload_len);
    memcpy (&payload[4], data, payload_len);

    gst_adapter_flush (rtpmp4gpay->adapter, payload_len);

    gst_rtp_buffer_set_marker (outbuf, avail > payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmp4gpay->first_ts;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpmp4gpay), outbuf);

    avail -= payload_len;
    fragmented = TRUE;
  }

  return ret;
}

/* we expect buffers as exactly one complete AU
 */
static GstFlowReturn
gst_rtp_mp4g_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpMP4GPay *rtpmp4gpay;
  GstFlowReturn ret;

  ret = GST_FLOW_OK;

  rtpmp4gpay = GST_RTP_MP4G_PAY (basepayload);

  rtpmp4gpay->first_ts = GST_BUFFER_TIMESTAMP (buffer);

  /* we always encode and flush a full AU */
  gst_adapter_push (rtpmp4gpay->adapter, buffer);
  ret = gst_rtp_mp4g_pay_flush (rtpmp4gpay);

  return ret;
}

static void
gst_rtp_mp4g_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP4GPay *rtpmp4gpay;

  rtpmp4gpay = GST_RTP_MP4G_PAY (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_rtp_mp4g_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP4GPay *rtpmp4gpay;

  rtpmp4gpay = GST_RTP_MP4G_PAY (object);

  switch (prop_id) {
    default:
      break;
  }
}

gboolean
gst_rtp_mp4g_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp4gpay",
      GST_RANK_NONE, GST_TYPE_RTP_MP4G_PAY);
}

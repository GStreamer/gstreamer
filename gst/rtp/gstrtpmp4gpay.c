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
        "encoding-name = (string) \"mpeg4-generic\", "
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
  rtpmp4gpay->mode = "";
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

static unsigned sampling_table[16] = {
  96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
  16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

static gboolean
gst_rtp_mp4g_pay_parse_audio_config (GstRtpMP4GPay * rtpmp4gpay,
    GstBuffer * buffer)
{
  guint8 *data;
  guint size;
  guint8 objectType;
  guint8 samplingIdx;
  guint8 channelCfg;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  if (size < 2)
    goto too_short;

  /* only AAC LC for now */
  objectType = (data[0] & 0xf8) >> 3;
  if (objectType != 2)
    goto unsupported_type;

  samplingIdx = ((data[0] & 0x07) << 1) | ((data[1] & 0x80) >> 7);
  /* only fixed values for now */
  if (samplingIdx > 12 && samplingIdx != 15)
    goto wrong_freq;

  channelCfg = ((data[1] & 0x78) >> 3);
  if (channelCfg > 7)
    goto wrong_channels;

  /* rtp rate depends on sampling rate of the audio */
  if (samplingIdx == 15) {
    if (size < 5)
      goto too_short;

    /* index of 15 means we get the rate in the next 24 bits */
    rtpmp4gpay->rate = ((data[1] & 0x7f) << 17) |
        ((data[2]) << 9) | ((data[3]) << 1) | ((data[4] & 0x80) >> 7);
  } else {
    /* else use the rate from the table */
    rtpmp4gpay->rate = sampling_table[samplingIdx];
  }
  /* extra rtp params contain the number of channels */
  rtpmp4gpay->params = channelCfg;
  /* audio stream type */
  rtpmp4gpay->streamtype = 5;
  /* mode */
  rtpmp4gpay->mode = "ACC-hbr";
  /* profile (should be 1) */
  rtpmp4gpay->profile = objectType - 1;

  GST_DEBUG_OBJECT (rtpmp4gpay,
      "objectType: %d, samplingIdx: %d (%d), channelCfg: %d", objectType,
      samplingIdx, rtpmp4gpay->rate, channelCfg);

  return TRUE;

  /* ERROR */
too_short:
  {
    GST_ELEMENT_ERROR (rtpmp4gpay, STREAM, FORMAT,
        (NULL), ("config string too short"));
    return FALSE;
  }
unsupported_type:
  {
    GST_ELEMENT_ERROR (rtpmp4gpay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("unsupported object type %d", objectType));
    return FALSE;
  }
wrong_freq:
  {
    GST_ELEMENT_ERROR (rtpmp4gpay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("unsupported frequency index %d", samplingIdx));
    return FALSE;
  }
wrong_channels:
  {
    GST_ELEMENT_ERROR (rtpmp4gpay, STREAM, NOT_IMPLEMENTED,
        (NULL), ("unsupported number of channels %d", channelCfg));
    return FALSE;
  }
}

#define VOS_STARTCODE                   0x000001B0

static gboolean
gst_rtp_mp4g_pay_parse_video_config (GstRtpMP4GPay * rtpmp4gpay,
    GstBuffer * buffer)
{
  guint8 *data;
  guint size;
  guint32 code;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  if (size < 5)
    goto too_short;

  code = GST_READ_UINT32_BE (data);
  if (code == VOS_STARTCODE) {
    /* get profile */
    rtpmp4gpay->profile = data[4];
  } else {
    GST_ELEMENT_WARNING (rtpmp4gpay, STREAM, FORMAT,
        (NULL), ("profile not found in config string"));
    rtpmp4gpay->profile = 1;
  }

  /* fixed rate */
  rtpmp4gpay->rate = 90000;
  /* video stream type */
  rtpmp4gpay->streamtype = 4;
  /* no params for video */
  rtpmp4gpay->params = 0;
  /* mode */
  rtpmp4gpay->mode = "generic";

  GST_LOG_OBJECT (rtpmp4gpay, "profile %d", rtpmp4gpay->profile);

  return TRUE;

  /* ERROR */
too_short:
  {
    GST_ELEMENT_ERROR (rtpmp4gpay, STREAM, FORMAT,
        (NULL), ("config string too short"));
    return FALSE;
  }
}

static void
gst_rtp_mp4g_pay_new_caps (GstRtpMP4GPay * rtpmp4gpay)
{
  gchar *config;
  GValue v = { 0 };

#define MP4GCAPS					\
  "streamtype", G_TYPE_INT, rtpmp4gpay->streamtype, 	\
  "profile-level-id", G_TYPE_INT, rtpmp4gpay->profile,	\
  "mode", G_TYPE_STRING, rtpmp4gpay->mode,		\
  "config", G_TYPE_STRING, config,			\
  "sizelength", G_TYPE_INT, 13,				\
  "indexlength", G_TYPE_INT, 3,				\
  "indexdeltalength", G_TYPE_INT, 3,			\
  NULL

  g_value_init (&v, GST_TYPE_BUFFER);
  gst_value_set_buffer (&v, rtpmp4gpay->config);
  config = gst_value_serialize (&v);

  /* hmm, silly */
  if (rtpmp4gpay->params) {
    gst_basertppayload_set_outcaps (GST_BASE_RTP_PAYLOAD (rtpmp4gpay),
        "encoding-params", G_TYPE_INT, rtpmp4gpay->params, MP4GCAPS);
  } else {
    gst_basertppayload_set_outcaps (GST_BASE_RTP_PAYLOAD (rtpmp4gpay),
        MP4GCAPS);
  }

  g_value_unset (&v);
  g_free (config);

#undef MP4GCAPS
}

static gboolean
gst_rtp_mp4g_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  GstRtpMP4GPay *rtpmp4gpay;
  GstStructure *structure;
  const GValue *codec_info;
  gboolean res = TRUE;

  rtpmp4gpay = GST_RTP_MP4G_PAY (payload);

  structure = gst_caps_get_structure (caps, 0);

  codec_info = gst_structure_get_value (structure, "codec_info");
  if (codec_info) {
    GST_LOG_OBJECT (rtpmp4gpay, "got codec_info");
    if (G_VALUE_TYPE (codec_info) == GST_TYPE_BUFFER) {
      GstBuffer *buffer;
      const gchar *name;

      buffer = gst_value_get_buffer (codec_info);
      GST_LOG_OBJECT (rtpmp4gpay, "configuring codec_info");

      name = gst_structure_get_name (structure);

      /* parse buffer */
      if (!strcmp (name, "audio/mpeg")) {
        res = gst_rtp_mp4g_pay_parse_audio_config (rtpmp4gpay, buffer);
      } else if (!strcmp (name, "video/mpeg")) {
        res = gst_rtp_mp4g_pay_parse_video_config (rtpmp4gpay, buffer);
      } else {
        res = FALSE;
      }
      if (!res)
        goto config_failed;

      /* now we can configure the buffer */
      if (rtpmp4gpay->config)
        gst_buffer_unref (rtpmp4gpay->config);

      rtpmp4gpay->config = gst_buffer_copy (buffer);
    }
  }

  gst_basertppayload_set_options (payload, "video", TRUE, "mpeg4-generic",
      rtpmp4gpay->rate);

  gst_rtp_mp4g_pay_new_caps (rtpmp4gpay);

  return res;

  /* ERRORS */
config_failed:
  {
    GST_DEBUG_OBJECT (rtpmp4gpay, "failed to parse config");
    return FALSE;
  }
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

    /* marker only if the packet is complete */
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

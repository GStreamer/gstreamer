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
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP4V-ES\""
        /* two string params
         *
         "profile-level-id = (string) [1,MAX]"
         "config = (string) [1,MAX]"
         */
    )
    );

#define DEFAULT_SEND_CONFIG	FALSE

enum
{
  ARG_0,
  ARG_SEND_CONFIG
};


static void gst_rtpmp4venc_class_init (GstRtpMP4VEncClass * klass);
static void gst_rtpmp4venc_base_init (GstRtpMP4VEncClass * klass);
static void gst_rtpmp4venc_init (GstRtpMP4VEnc * rtpmp4venc);
static void gst_rtpmp4venc_finalize (GObject * object);

static void gst_rtpmp4venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpmp4venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtpmp4venc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtpmp4venc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

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
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpMP4VEnc",
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
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gobject_class->set_property = gst_rtpmp4venc_set_property;
  gobject_class->get_property = gst_rtpmp4venc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SEND_CONFIG,
      g_param_spec_boolean ("send-config", "Send Config",
          "Send the config parameters in RTP packets as well",
          DEFAULT_SEND_CONFIG, G_PARAM_READWRITE));

  gobject_class->finalize = gst_rtpmp4venc_finalize;

  gstbasertppayload_class->set_caps = gst_rtpmp4venc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtpmp4venc_handle_buffer;
}

static void
gst_rtpmp4venc_init (GstRtpMP4VEnc * rtpmp4venc)
{
  rtpmp4venc->adapter = gst_adapter_new ();
  rtpmp4venc->rate = 90000;
  rtpmp4venc->profile = 1;
  rtpmp4venc->send_config = DEFAULT_SEND_CONFIG;
}

static void
gst_rtpmp4venc_finalize (GObject * object)
{
  GstRtpMP4VEnc *rtpmp4venc;

  rtpmp4venc = GST_RTP_MP4V_ENC (object);

  g_object_unref (rtpmp4venc->adapter);
  rtpmp4venc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtpmp4venc_new_caps (GstRtpMP4VEnc * rtpmp4venc)
{
  gchar *profile, *config;
  GValue v = { 0 };

  profile = g_strdup_printf ("%d", rtpmp4venc->profile);
  g_value_init (&v, GST_TYPE_BUFFER);
  gst_value_set_buffer (&v, rtpmp4venc->config);
  config = gst_value_serialize (&v);

  gst_basertppayload_set_outcaps (GST_BASE_RTP_PAYLOAD (rtpmp4venc),
      "profile-level-id", G_TYPE_STRING, profile,
      "config", G_TYPE_STRING, config, NULL);

  g_value_unset (&v);

  g_free (profile);
  g_free (config);
}

static gboolean
gst_rtpmp4venc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  GstRtpMP4VEnc *rtpmp4venc;

  rtpmp4venc = GST_RTP_MP4V_ENC (payload);

  gst_basertppayload_set_options (payload, "video", TRUE, "MP4V-ES",
      rtpmp4venc->rate);

  return TRUE;
}

static GstFlowReturn
gst_rtpmp4venc_flush (GstRtpMP4VEnc * rtpmp4venc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to split the MP4V data
   * over multiple packets. */
  avail = gst_adapter_available (rtpmp4venc->adapter);

  ret = GST_FLOW_OK;

  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtpbuffer_calc_packet_len (avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpmp4venc));

    /* this is the payload length */
    payload_len = gst_rtpbuffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);

    /* copy payload */
    payload = gst_rtpbuffer_get_payload (outbuf);
    data = (guint8 *) gst_adapter_peek (rtpmp4venc->adapter, payload_len);
    memcpy (payload, data, payload_len);

    gst_adapter_flush (rtpmp4venc->adapter, payload_len);

    avail -= payload_len;

    gst_rtpbuffer_set_marker (outbuf, avail == 0);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmp4venc->first_ts;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpmp4venc), outbuf);
  }

  return ret;
}

#define VOS_STARTCODE  			0x000001B0
#define VOS_ENDCODE    			0x000001B1
#define USER_DATA_STARTCODE   	 	0x000001B2
#define GOP_STARTCODE            	0x000001B3
#define VISUAL_OBJECT_STARTCODE        	0x000001B5
#define VOP_STARTCODE                  	0x000001B6

static gboolean
gst_rtpmp4venc_parse_data (GstRtpMP4VEnc * enc, guint8 * data, guint size,
    gint * strip)
{
  guint32 code;
  gboolean result;

  *strip = 0;

  if (size < 5)
    return FALSE;

  code = GST_READ_UINT32_BE (data);

  switch (code) {
    case VOS_STARTCODE:
    {
      gint i;
      guint8 profile;
      gboolean newprofile = FALSE;
      gboolean equal;

      /* profile_and_level_indication */
      profile = data[4];

      if (profile != enc->profile) {
        newprofile = TRUE;
        enc->profile = profile;
      }

      /* up to the next GOP_STARTCODE or VOP_STARTCODE is
       * the config information */
      code = 0xffffffff;
      for (i = 5; i < size - 4; i++) {
        code = (code << 8) | data[i];
        if (code == GOP_STARTCODE || code == VOP_STARTCODE)
          break;
      }
      i -= 3;
      /* see if config changed */
      equal = FALSE;
      if (enc->config) {
        if (GST_BUFFER_SIZE (enc->config) == i) {
          equal = memcmp (GST_BUFFER_DATA (enc->config), data, i) == 0;
        }
      }
      /* if config string changed or new profile, make new caps */
      if (!equal || newprofile) {
        if (enc->config)
          gst_buffer_unref (enc->config);
        enc->config = gst_buffer_new_and_alloc (i);
        memcpy (GST_BUFFER_DATA (enc->config), data, i);
        gst_rtpmp4venc_new_caps (enc);
      }
      *strip = i;
      /* we need to flush out the current packet. */
      result = TRUE;
      break;
    }
    case VOP_STARTCODE:
      /* VOP startcode, we don't have to flush the packet */
      result = FALSE;
      break;
    default:
      /* all other startcodes need a flush */
      result = TRUE;
      break;
  }
  return result;
}

/* we expect buffers starting on startcodes. 
 */
static GstFlowReturn
gst_rtpmp4venc_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpMP4VEnc *rtpmp4venc;
  GstFlowReturn ret;
  guint size, avail;
  guint packet_len;
  guint8 *data;
  gboolean flush;
  gint strip;

  ret = GST_FLOW_OK;

  rtpmp4venc = GST_RTP_MP4V_ENC (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  avail = gst_adapter_available (rtpmp4venc->adapter);

  /* empty buffer, take timestamp */
  if (avail == 0) {
    rtpmp4venc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
  }

  /* parse incomming data and see if we need to start a new RTP
   * packet */
  flush = gst_rtpmp4venc_parse_data (rtpmp4venc, data, size, &strip);
  if (strip) {
    /* strip off config if requested */
    if (!rtpmp4venc->send_config) {
      GstBuffer *subbuf;

      /* strip off header */
      subbuf = gst_buffer_create_sub (buffer, strip, size - strip);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buffer);
      gst_buffer_unref (buffer);
      buffer = subbuf;

      size = GST_BUFFER_SIZE (buffer);
      data = GST_BUFFER_DATA (buffer);
    }
  }

  /* if we need to flush, do so now */
  if (flush) {
    ret = gst_rtpmp4venc_flush (rtpmp4venc);
    avail = 0;
    rtpmp4venc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
  }

  avail = gst_adapter_available (rtpmp4venc->adapter);

  /* get packet length of data and see if we exceeded MTU. */
  packet_len = gst_rtpbuffer_calc_packet_len (avail + size, 0, 0);

  if (packet_len > GST_BASE_RTP_PAYLOAD_MTU (rtpmp4venc)) {
    ret = gst_rtpmp4venc_flush (rtpmp4venc);
    avail = 0;
    rtpmp4venc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
  }

  /* push new data */
  gst_adapter_push (rtpmp4venc->adapter, buffer);

  return ret;
}

static void
gst_rtpmp4venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP4VEnc *rtpmp4venc;

  rtpmp4venc = GST_RTP_MP4V_ENC (object);

  switch (prop_id) {
    case ARG_SEND_CONFIG:
      rtpmp4venc->send_config = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_rtpmp4venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP4VEnc *rtpmp4venc;

  rtpmp4venc = GST_RTP_MP4V_ENC (object);

  switch (prop_id) {
    case ARG_SEND_CONFIG:
      g_value_set_boolean (value, rtpmp4venc->send_config);
      break;
    default:
      break;
  }
}

gboolean
gst_rtpmp4venc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp4venc",
      GST_RANK_NONE, GST_TYPE_RTP_MP4V_ENC);
}

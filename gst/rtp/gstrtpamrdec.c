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
#include "gstrtpamrdec.h"

/* references:
 *
 * RFC 3267 - Real-Time Transport Protocol (RTP) Payload Format and File Storage Format 
 *   for the Adaptive Multi-Rate (AMR) and Adaptive Multi-Rate Wideband (AMR-WB) Audio 
 *   Codecs.
 */

/* elementfactory information */
static GstElementDetails gst_rtp_amrdec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts AMR audio from RTP packets (RFC 3267)",
  "Wim Taymans <wim@fluendo.com>"
};

/* RtpAMRDec signals and args */
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

/* input is an RTP packet 
 *
 * params see RFC 3267, section 8.1
 */
static GstStaticPadTemplate gst_rtpamrdec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 255 ], "
        "clock_rate = (int) 8000, "
        "encoding_name = (string) \"AMR\", "
        "encoding_params = (string) \"1\", "
        "octet-align = (boolean) TRUE, "
        "crc = (boolean) FALSE, "
        "robust-sorting = (boolean) FALSE, " "interleaving = (boolean) FALSE"
        /* following options are not needed for a decoder 
         *
         "mode-set = (int) [ 0, 7 ], "
         "mode-change-period = (int) [ 1, MAX ], "
         "mode-change-neighbor = (boolean) { TRUE, FALSE }, "
         "maxptime = (int) [ 20, MAX ], "
         "ptime = (int) [ 20, MAX ]"
         */
    )
    );

static GstStaticPadTemplate gst_rtpamrdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, " "channels = (int) 1," "rate = (int) 8000")
    );

static void gst_rtpamrdec_class_init (GstRtpAMRDecClass * klass);
static void gst_rtpamrdec_base_init (GstRtpAMRDecClass * klass);
static void gst_rtpamrdec_init (GstRtpAMRDec * rtpamrdec);

static gboolean gst_rtpamrdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_rtpamrdec_chain (GstPad * pad, GstBuffer * buffer);

static void gst_rtpamrdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpamrdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_rtpamrdec_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_rtpamrdec_get_type (void)
{
  static GType rtpamrdec_type = 0;

  if (!rtpamrdec_type) {
    static const GTypeInfo rtpamrdec_info = {
      sizeof (GstRtpAMRDecClass),
      (GBaseInitFunc) gst_rtpamrdec_base_init,
      NULL,
      (GClassInitFunc) gst_rtpamrdec_class_init,
      NULL,
      NULL,
      sizeof (GstRtpAMRDec),
      0,
      (GInstanceInitFunc) gst_rtpamrdec_init,
    };

    rtpamrdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRtpAMRDec",
        &rtpamrdec_info, 0);
  }
  return rtpamrdec_type;
}

static void
gst_rtpamrdec_base_init (GstRtpAMRDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpamrdec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpamrdec_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_amrdec_details);
}

static void
gst_rtpamrdec_class_init (GstRtpAMRDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_rtpamrdec_set_property;
  gobject_class->get_property = gst_rtpamrdec_get_property;

  gstelement_class->change_state = gst_rtpamrdec_change_state;
}

static void
gst_rtpamrdec_init (GstRtpAMRDec * rtpamrdec)
{
  rtpamrdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpamrdec_src_template), "src");

  gst_element_add_pad (GST_ELEMENT (rtpamrdec), rtpamrdec->srcpad);

  rtpamrdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rtpamrdec_sink_template), "sink");
  gst_pad_set_setcaps_function (rtpamrdec->sinkpad, gst_rtpamrdec_sink_setcaps);
  gst_pad_set_chain_function (rtpamrdec->sinkpad, gst_rtpamrdec_chain);
  gst_element_add_pad (GST_ELEMENT (rtpamrdec), rtpamrdec->sinkpad);
}

static gboolean
gst_rtpamrdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *srccaps;
  GstRtpAMRDec *rtpamrdec;
  const gchar *params;

  rtpamrdec = GST_RTP_AMR_DEC (GST_OBJECT_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_boolean (structure, "octet-align",
          &rtpamrdec->octet_align))
    rtpamrdec->octet_align = FALSE;

  if (!gst_structure_get_boolean (structure, "crc", &rtpamrdec->crc))
    rtpamrdec->crc = FALSE;

  if (rtpamrdec->crc) {
    /* crc mode implies octet aligned mode */
    rtpamrdec->octet_align = TRUE;
  }

  if (!gst_structure_get_boolean (structure, "robust-sorting",
          &rtpamrdec->robust_sorting))
    rtpamrdec->robust_sorting = FALSE;

  if (rtpamrdec->robust_sorting) {
    /* robust_sorting mode implies octet aligned mode */
    rtpamrdec->octet_align = TRUE;
  }

  if (!gst_structure_get_boolean (structure, "interleaving",
          &rtpamrdec->interleaving))
    rtpamrdec->interleaving = FALSE;

  if (rtpamrdec->interleaving) {
    /* interleaving mode implies octet aligned mode */
    rtpamrdec->octet_align = TRUE;
  }

  if (!(params = gst_structure_get_string (structure, "encoding_params")))
    rtpamrdec->channels = 1;
  else {
    rtpamrdec->channels = atoi (params);
  }

  if (!gst_structure_get_int (structure, "clock_rate", &rtpamrdec->rate))
    rtpamrdec->rate = 8000;

  /* we require 1 channel, 8000 Hz, octet aligned, no CRC, 
   * no robust sorting, no interleaving for now */
  if (rtpamrdec->channels != 1)
    return FALSE;
  if (rtpamrdec->rate != 8000)
    return FALSE;
  if (rtpamrdec->octet_align != TRUE)
    return FALSE;
  if (rtpamrdec->crc != FALSE)
    return FALSE;
  if (rtpamrdec->robust_sorting != FALSE)
    return FALSE;
  if (rtpamrdec->interleaving != FALSE)
    return FALSE;

  srccaps = gst_caps_new_simple ("audio/AMR",
      "channels", G_TYPE_INT, rtpamrdec->channels,
      "rate", G_TYPE_INT, rtpamrdec->rate, NULL);
  gst_pad_set_caps (rtpamrdec->srcpad, srccaps);
  gst_caps_unref (srccaps);

  rtpamrdec->negotiated = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_rtpamrdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstRtpAMRDec *rtpamrdec;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  rtpamrdec = GST_RTP_AMR_DEC (GST_OBJECT_PARENT (pad));

  if (!rtpamrdec->negotiated)
    goto not_negotiated;

  if (!gst_rtpbuffer_validate (buf))
    goto bad_packet;

  /* when we get here, 1 channel, 8000 Hz, octet aligned, no CRC, 
   * no robust sorting, no interleaving data is to be parsed */
  {
    gint payload_len;
    guint8 *payload;
    guint32 timestamp;
    guint8 CMR, F, FT, Q;

    payload_len = gst_rtpbuffer_get_payload_len (buf);

    /* need at least 2 bytes for the header */
    if (payload_len < 2)
      goto bad_packet;

    payload = gst_rtpbuffer_get_payload (buf);

    /* parse header 
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+..
     * | CMR   |R|R|R|R|F|  FT   |Q|P|P|
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+..
     */
    CMR = (payload[0] & 0xf0) >> 4;
    F = (payload[1] & 0x80) >> 7;
    /* we only support 1 packet per RTP packet for now */
    if (F != 0)
      goto one_packet_only;

    FT = (payload[1] & 0x78) >> 3;
    Q = (payload[1] & 0x04) >> 2;

    /* skip packet */
    if (FT > 9 && FT < 15) {
      ret = GST_FLOW_OK;
      goto skip;
    }

    /* strip header now, leave FT in the data for the decoder */
    payload_len -= 1;
    payload += 1;

    timestamp = gst_rtpbuffer_get_timestamp (buf);

    outbuf = gst_buffer_new_and_alloc (payload_len);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp * GST_SECOND / rtpamrdec->rate;

    memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (rtpamrdec->srcpad));

    GST_DEBUG ("gst_rtpamrdec_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));
    ret = gst_pad_push (rtpamrdec->srcpad, outbuf);

  skip:
    gst_buffer_unref (buf);
  }

  return ret;

not_negotiated:
  {
    GST_DEBUG ("not_negotiated");
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
bad_packet:
  {
    GST_DEBUG ("Packet did not validate");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
one_packet_only:
  {
    GST_DEBUG ("One packet per RTP packet only");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static void
gst_rtpamrdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpAMRDec *rtpamrdec;

  rtpamrdec = GST_RTP_AMR_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtpamrdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRtpAMRDec *rtpamrdec;

  rtpamrdec = GST_RTP_AMR_DEC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_rtpamrdec_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpAMRDec *rtpamrdec;
  GstStateChangeReturn ret;

  rtpamrdec = GST_RTP_AMR_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
gst_rtpamrdec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrdec",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_DEC);
}

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

/* references:
 *
 * RFC 3267 - Real-Time Transport Protocol (RTP) Payload Format and File 
 *    Storage Format for the Adaptive Multi-Rate (AMR) and Adaptive 
 *    Multi-Rate Wideband (AMR-WB) Audio Codecs.
 */

/* elementfactory information */
static GstElementDetails gst_rtp_amrenc_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Encode AMR audio into RTP packets (RFC 3267)",
  "Wim Taymans <wim@fluendo.com>"
};

static GstStaticPadTemplate gst_rtpamrenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtpamrenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 255 ], "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"AMR\", "
        "encoding-params = (string) \"1\", "
        "octet-align = (string) \"1\", "
        "crc = (string) \"0\", "
        "robust-sorting = (string) \"0\", "
        "interleaving = (string) \"0\", "
        "mode-set = (int) [ 0, 7 ], "
        "mode-change-period = (int) [ 1, MAX ], "
        "mode-change-neighbor = (string) { \"0\", \"1\" }, "
        "maxptime = (int) [ 20, MAX ], " "ptime = (int) [ 20, MAX ]")
    );

static void gst_rtpamrenc_class_init (GstRtpAMREncClass * klass);
static void gst_rtpamrenc_base_init (GstRtpAMREncClass * klass);
static void gst_rtpamrenc_init (GstRtpAMREnc * rtpamrenc);

static gboolean gst_rtpamrenc_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtpamrenc_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

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
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpAMREnc",
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
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->set_caps = gst_rtpamrenc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtpamrenc_handle_buffer;
}

static void
gst_rtpamrenc_init (GstRtpAMREnc * rtpamrenc)
{
}

static gboolean
gst_rtpamrenc_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpAMREnc *rtpamrenc;

  rtpamrenc = GST_RTP_AMR_ENC (basepayload);

  gst_basertppayload_set_options (basepayload, "audio", TRUE, "AMR", 8000);
  gst_basertppayload_set_outcaps (basepayload,
      "encoding-params", G_TYPE_STRING, "1", "octet-align", G_TYPE_STRING, "1",
      /* don't set the defaults 
       * 
       * "crc", G_TYPE_STRING, "0",
       * "robust-sorting", G_TYPE_STRING, "0",
       * "interleaving", G_TYPE_STRING, "0", 
       */
      NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtpamrenc_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpAMREnc *rtpamrenc;
  GstFlowReturn ret;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload, *data;
  GstClockTime timestamp;

  rtpamrenc = GST_RTP_AMR_ENC (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* FIXME, only one AMR frame per RTP packet for now, 
   * octet aligned, no interleaving, single channel, no CRC,
   * no robust-sorting. */

  /* we need one extra byte for the CMR, the ToC is in the input
   * data */
  payload_len = size + 1;

  outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
  /* FIXME, assert for now */
  g_assert (GST_BUFFER_SIZE (outbuf) < GST_BASE_RTP_PAYLOAD_MTU (rtpamrenc));

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

  data = GST_BUFFER_DATA (buffer);

  /* copy data in payload */
  memcpy (&payload[1], data, size);

  /*   0 1 2 3 4 5 6 7
   *  +-+-+-+-+-+-+-+-+
   *  |F|  FT   |Q|P|P|
   *  +-+-+-+-+-+-+-+-+
   */
  /* clear F flag */
  payload[1] = payload[1] & 0x7f;

  gst_buffer_unref (buffer);

  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;
}

gboolean
gst_rtpamrenc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrenc",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_ENC);
}

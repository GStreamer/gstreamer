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

#include "gstrtpamrpay.h"

GST_DEBUG_CATEGORY (rtpamrpay_debug);
#define GST_CAT_DEFAULT (rtpamrpay_debug)

/* references:
 *
 * RFC 3267 - Real-Time Transport Protocol (RTP) Payload Format and File 
 *    Storage Format for the Adaptive Multi-Rate (AMR) and Adaptive 
 *    Multi-Rate Wideband (AMR-WB) Audio Codecs.
 */

/* elementfactory information */
static GstElementDetails gst_rtp_amrpay_details = {
  "RTP packet parser",
  "Codec/Payloader/Network",
  "Payode AMR audio into RTP packets (RFC 3267)",
  "Wim Taymans <wim@fluendo.com>"
};

static GstStaticPadTemplate gst_rtp_amr_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/AMR, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtp_amr_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
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

static void gst_rtp_amr_pay_class_init (GstRtpAMRPayClass * klass);
static void gst_rtp_amr_pay_base_init (GstRtpAMRPayClass * klass);
static void gst_rtp_amr_pay_init (GstRtpAMRPay * rtpamrpay);

static gboolean gst_rtp_amr_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_amr_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtp_amr_pay_get_type (void)
{
  static GType rtpamrpay_type = 0;

  if (!rtpamrpay_type) {
    static const GTypeInfo rtpamrpay_info = {
      sizeof (GstRtpAMRPayClass),
      (GBaseInitFunc) gst_rtp_amr_pay_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_amr_pay_class_init,
      NULL,
      NULL,
      sizeof (GstRtpAMRPay),
      0,
      (GInstanceInitFunc) gst_rtp_amr_pay_init,
    };

    rtpamrpay_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpAMRPay",
        &rtpamrpay_info, 0);
  }
  return rtpamrpay_type;
}

static void
gst_rtp_amr_pay_base_init (GstRtpAMRPayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_amr_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_amr_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_amrpay_details);
}

static void
gst_rtp_amr_pay_class_init (GstRtpAMRPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);

  gstbasertppayload_class->set_caps = gst_rtp_amr_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_amr_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpamrpay_debug, "rtpamrpay", 0,
      "AMR RTP Depayloader");

}

static void
gst_rtp_amr_pay_init (GstRtpAMRPay * rtpamrpay)
{
}

static gboolean
gst_rtp_amr_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpAMRPay *rtpamrpay;

  rtpamrpay = GST_RTP_AMR_PAY (basepayload);

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

/* -1 is invalid */
static gint frame_size[16] = {
  12, 13, 15, 17, 19, 20, 26, 31,
  5, -1, -1, -1, -1, -1, -1, 0
};

static GstFlowReturn
gst_rtp_amr_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpAMRPay *rtpamrpay;
  GstFlowReturn ret;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload, *data, *payload_amr;
  GstClockTime timestamp;
  guint packet_len, mtu;
  gint i, num_packets, num_nonempty_packets;
  gint amr_len;

  rtpamrpay = GST_RTP_AMR_PAY (basepayload);
  mtu = GST_BASE_RTP_PAYLOAD_MTU (rtpamrpay);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* FIXME, only 
   * octet aligned, no interleaving, single channel, no CRC,
   * no robust-sorting. */

  GST_DEBUG_OBJECT (basepayload, "got %d bytes", size);

  /* first count number of packets and total amr frame size */
  amr_len = num_packets = num_nonempty_packets = 0;
  for (i = 0; i < size; i++) {
    guint8 FT;
    gint fr_size;

    FT = (data[i] & 0x78) >> 3;

    fr_size = frame_size[FT];
    GST_DEBUG_OBJECT (basepayload, "frame size %d", fr_size);
    /* FIXME, we don't handle this yet.. */
    if (fr_size <= 0)
      goto wrong_size;

    amr_len += fr_size;
    num_nonempty_packets++;
    num_packets++;
    i += fr_size;
  }
  if (amr_len > size)
    goto incomplete_frame;

  /* we need one extra byte for the CMR, the ToC is in the input
   * data */
  payload_len = size + 1;

  /* get packet len to check against MTU */
  packet_len = gst_rtp_buffer_calc_packet_len (payload_len, 0, 0);
  if (packet_len > mtu)
    goto too_big;

  /* now alloc output buffer */
  outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

  /* copy timestamp */
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  /* get payload, this is now writable */
  payload = gst_rtp_buffer_get_payload (outbuf);

  /*   0 1 2 3 4 5 6 7 
   *  +-+-+-+-+-+-+-+-+
   *  |  CMR  |R|R|R|R|
   *  +-+-+-+-+-+-+-+-+
   */
  payload[0] = 0xF0;            /* CMR, no specific mode requested */

  /* this is where we copy the AMR data, after num_packets FTs and the
   * CMR. */
  payload_amr = payload + num_packets + 1;

  /* copy data in payload, first we copy all the FTs then all
   * the AMR data. The last FT has to have the F flag cleared. */
  for (i = 1; i <= num_packets; i++) {
    guint8 FT;
    gint fr_size;

    /*   0 1 2 3 4 5 6 7
     *  +-+-+-+-+-+-+-+-+
     *  |F|  FT   |Q|P|P| more FT...
     *  +-+-+-+-+-+-+-+-+
     */
    FT = (*data & 0x78) >> 3;

    fr_size = frame_size[FT];

    if (i == num_packets)
      /* last packet, clear F flag */
      payload[i] = *data & 0x7f;
    else
      /* set F flag */
      payload[i] = *data | 0x80;

    memcpy (payload_amr, &data[1], fr_size);

    /* all sizes are > 0 since we checked for that above */
    data += fr_size + 1;
    payload_amr += fr_size;
  }

  gst_buffer_unref (buffer);

  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, FORMAT,
        (NULL), ("received AMR frame with size <= 0"));
    gst_buffer_unref (buffer);

    return GST_FLOW_ERROR;
  }
incomplete_frame:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, FORMAT,
        (NULL), ("received incomplete AMR frames"));
    gst_buffer_unref (buffer);

    return GST_FLOW_ERROR;
  }
too_big:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, FORMAT,
        (NULL), ("received too many AMR frames for MTU"));
    gst_buffer_unref (buffer);

    return GST_FLOW_ERROR;
  }
}

gboolean
gst_rtp_amr_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpamrpay",
      GST_RANK_NONE, GST_TYPE_RTP_AMR_PAY);
}

/* GStreamer
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
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

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpspeexpay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_speex_pay_details =
GST_ELEMENT_DETAILS ("RTP packet parser",
    "Codec/Payloader/Network",
    "Payload-encodes Speex audio into a RTP packet",
    "Edgard Lima <edgard.lima@indt.org.br>");

static GstStaticPadTemplate gst_rtp_speex_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-speex")
    );

static GstStaticPadTemplate gst_rtp_speex_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate =  (int) 8000, "
        "encoding-name = (string) \"speex\", "
        "encoding-params = (string) \"1\"")
    );

static gboolean gst_rtp_speex_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_speex_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);

GST_BOILERPLATE (GstRtpSPEEXPay, gst_rtp_speex_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_speex_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_speex_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_speex_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_speex_pay_details);
}

static void
gst_rtp_speex_pay_class_init (GstRtpSPEEXPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertppayload_class->set_caps = gst_rtp_speex_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_speex_pay_handle_buffer;
}

static void
gst_rtp_speex_pay_init (GstRtpSPEEXPay * rtpspeexpay,
    GstRtpSPEEXPayClass * klass)
{
  GST_BASE_RTP_PAYLOAD (rtpspeexpay)->clock_rate = 8000;
  GST_BASE_RTP_PAYLOAD_PT (rtpspeexpay) = 110;  /* Create String */
}

static gboolean
gst_rtp_speex_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "audio", FALSE, "speex", 8000);
  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtp_speex_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpSPEEXPay *rtpspeexpay;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload, *data;
  GstClockTime timestamp;
  GstFlowReturn ret;

  rtpspeexpay = GST_RTP_SPEEX_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* FIXME, only one SPEEX frame per RTP packet for now */
  payload_len = size;

  outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);
  /* FIXME, assert for now */
  g_assert (payload_len <= GST_BASE_RTP_PAYLOAD_MTU (rtpspeexpay));

  /* copy timestamp */
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  /* get payload */
  payload = gst_rtp_buffer_get_payload (outbuf);

  data = GST_BUFFER_DATA (buffer);

  /* copy data in payload */
  memcpy (&payload[0], data, size);

  gst_buffer_unref (buffer);

  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;
}

gboolean
gst_rtp_speex_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpspeexpay",
      GST_RANK_NONE, GST_TYPE_RTP_SPEEX_PAY);
}

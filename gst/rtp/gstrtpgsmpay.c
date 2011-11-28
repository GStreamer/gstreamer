/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Zeeshan Ali <zeenix@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpgsmpay.h"

GST_DEBUG_CATEGORY_STATIC (rtpgsmpay_debug);
#define GST_CAT_DEFAULT (rtpgsmpay_debug)

static GstStaticPadTemplate gst_rtp_gsm_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) 8000, " "channels = (int) 1")
    );

static GstStaticPadTemplate gst_rtp_gsm_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_GSM_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"GSM\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"GSM\"")
    );

static gboolean gst_rtp_gsm_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_gsm_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRTPGSMPay, gst_rtp_gsm_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_gsm_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_gsm_pay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_gsm_pay_src_template);
  gst_element_class_set_details_simple (element_class, "RTP GSM payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encodes GSM audio into a RTP packet",
      "Zeeshan Ali <zeenix@gmail.com>");
}

static void
gst_rtp_gsm_pay_class_init (GstRTPGSMPayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_gsm_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_gsm_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpgsmpay_debug, "rtpgsmpay", 0,
      "GSM Audio RTP Payloader");
}

static void
gst_rtp_gsm_pay_init (GstRTPGSMPay * rtpgsmpay, GstRTPGSMPayClass * klass)
{
  GST_BASE_RTP_PAYLOAD (rtpgsmpay)->clock_rate = 8000;
  GST_BASE_RTP_PAYLOAD_PT (rtpgsmpay) = GST_RTP_PAYLOAD_GSM;
}

static gboolean
gst_rtp_gsm_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  const char *stname;
  GstStructure *structure;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  stname = gst_structure_get_name (structure);

  if (strcmp ("audio/x-gsm", stname))
    goto invalid_type;

  gst_basertppayload_set_options (payload, "audio", FALSE, "GSM", 8000);
  res = gst_basertppayload_set_outcaps (payload, NULL);

  return res;

  /* ERRORS */
invalid_type:
  {
    GST_WARNING_OBJECT (payload, "invalid media type received");
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_gsm_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRTPGSMPay *rtpgsmpay;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload, *data;
  GstClockTime timestamp, duration;
  GstFlowReturn ret;

  rtpgsmpay = GST_RTP_GSM_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  /* FIXME, only one GSM frame per RTP packet for now */
  payload_len = size;

  /* FIXME, just error out for now */
  if (payload_len > GST_BASE_RTP_PAYLOAD_MTU (rtpgsmpay)) {
    GST_ELEMENT_ERROR (rtpgsmpay, STREAM, ENCODE, (NULL),
        ("payload_len %u > mtu %u", payload_len,
            GST_BASE_RTP_PAYLOAD_MTU (rtpgsmpay)));
    return GST_FLOW_ERROR;
  }

  outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

  /* copy timestamp and duration */
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;

  /* get payload */
  payload = gst_rtp_buffer_get_payload (outbuf);

  data = GST_BUFFER_DATA (buffer);

  /* copy data in payload */
  memcpy (&payload[0], data, size);

  gst_buffer_unref (buffer);

  GST_DEBUG ("gst_rtp_gsm_pay_chain: pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;
}

gboolean
gst_rtp_gsm_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgsmpay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_GSM_PAY);
}

/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) <2005> Nokia Corporation <kai.vehmanen@nokia.com>
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

#include "gstrtppcmapay.h"

/* elementfactory information */
static GstElementDetails gst_rtp_pcma_pay_details = {
  "RTP packet parser",
  "Codec/Payloader/Network",
  "Payload-encodes PCMA audio into a RTP packet",
  "Edgard Lima <edgard.lima@indt.org.br>"
};

static GstStaticPadTemplate gst_rtp_pcma_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-alaw, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtp_pcma_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMA_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMA\" ")
    );

static gboolean gst_rtp_pcma_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_pcma_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);
static void gst_rtp_pcma_pay_finalize (GObject * object);

GST_BOILERPLATE (GstRtpPmcaPay, gst_rtp_pcma_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

/* The lower limit for number of octet to put in one packet
 * (clock-rate=8000, octet-per-sample=1). The default 80 is equal
 * to to 10msec (see RFC3551) */
#define GST_RTP_PCMA_MIN_PTIME_OCTETS   80

static void
gst_rtp_pcma_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_pcma_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_pcma_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_pcma_pay_details);
}

static void
gst_rtp_pcma_pay_class_init (GstRtpPmcaPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);
  gobject_class->finalize = gst_rtp_pcma_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_pcma_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_pcma_pay_handle_buffer;
}

static void
gst_rtp_pcma_pay_init (GstRtpPmcaPay * rtppcmapay, GstRtpPmcaPayClass * klass)
{
  rtppcmapay->adapter = gst_adapter_new ();
  GST_BASE_RTP_PAYLOAD (rtppcmapay)->clock_rate = 8000;
}

static void
gst_rtp_pcma_pay_finalize (GObject * object)
{
  GstRtpPmcaPay *rtppcmapay;

  rtppcmapay = GST_RTP_PCMA_PAY (object);

  g_object_unref (rtppcmapay->adapter);
  rtppcmapay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_pcma_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  payload->pt = GST_RTP_PAYLOAD_PCMA;
  gst_basertppayload_set_options (payload, "audio", FALSE, "PCMA", 8000);

  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtp_pcma_pay_flush (GstRtpPmcaPay * rtppcmapay)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint maxptime_octets = G_MAXUINT;
  guint minptime_octets = GST_RTP_PCMA_MIN_PTIME_OCTETS;

  if (GST_BASE_RTP_PAYLOAD (rtppcmapay)->max_ptime > 0) {
    /* calculate octet count with:
       maxptime-nsec * samples-per-sec / nsecs-per-sec * octets-per-sample */
    maxptime_octets =
        GST_BASE_RTP_PAYLOAD (rtppcmapay)->max_ptime *
        GST_BASE_RTP_PAYLOAD (rtppcmapay)->clock_rate / GST_SECOND;
  }

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet.  */
  avail = gst_adapter_available (rtppcmapay->adapter);

  ret = GST_FLOW_OK;

  while (avail >= minptime_octets) {
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* fill one MTU or all available bytes */
    payload_len =
        MIN (MIN (GST_BASE_RTP_PAYLOAD_MTU (rtppcmapay), maxptime_octets),
        avail);

    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (payload_len, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

    /* copy payload */
    gst_rtp_buffer_set_payload_type (outbuf,
        GST_BASE_RTP_PAYLOAD_PT (rtppcmapay));
    payload = gst_rtp_buffer_get_payload (outbuf);
    data = (guint8 *) gst_adapter_peek (rtppcmapay->adapter, payload_len);
    memcpy (payload, data, payload_len);
    gst_adapter_flush (rtppcmapay->adapter, payload_len);

    avail -= payload_len;

    GST_BUFFER_TIMESTAMP (outbuf) = rtppcmapay->first_ts;
    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtppcmapay), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtp_pcma_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpPmcaPay *rtppcmapay;
  guint size, packet_len, avail;
  GstFlowReturn ret;
  GstClockTime duration;

  rtppcmapay = GST_RTP_PCMA_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  duration = GST_BUFFER_TIMESTAMP (buffer);

  avail = gst_adapter_available (rtppcmapay->adapter);
  if (avail == 0) {
    rtppcmapay->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtppcmapay->duration = 0;
  }

  /* get packet length of data and see if we exceeded MTU. */
  packet_len = gst_rtp_buffer_calc_packet_len (avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtppcmapay->duration + duration)) {
    ret = gst_rtp_pcma_pay_flush (rtppcmapay);
    rtppcmapay->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtppcmapay->duration = 0;
  } else {
    ret = GST_FLOW_OK;
  }

  gst_adapter_push (rtppcmapay->adapter, buffer);
  rtppcmapay->duration += duration;

  return ret;
}

gboolean
gst_rtp_pcma_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtppcmapay",
      GST_RANK_NONE, GST_TYPE_RTP_PCMA_PAY);
}

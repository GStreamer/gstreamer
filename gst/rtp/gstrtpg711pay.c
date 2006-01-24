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

#include "gstrtpg711pay.h"

/* elementfactory information */
static GstElementDetails gst_rtp_g711_pay_details = {
  "RTP packet parser",
  "Codec/Payloader/Network",
  "Payload-encodes PCMU/PCMA audio into a RTP packet",
  "Edgard Lima <edgard.lima@indt.org.br>"
};

static GstStaticPadTemplate gst_rtp_g711_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, channels=(int)1, rate=(int)8000 ;"
        "audio/x-alaw, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtp_g711_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMU_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"PCMU\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMA_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMA\"")
    );

static gboolean gst_rtp_g711_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_g711_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);
static void gst_rtp_g711_pay_finalize (GObject * object);

GST_BOILERPLATE (GstRtpG711Pay, gst_rtp_g711_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

/* The lower limit for number of octet to put in one packet
 * (clock-rate=8000, octet-per-sample=1). The default 80 is equal
 * to to 10msec (see RFC3551) */
#define GST_RTP_G711_MIN_PTIME_OCTETS   80

static void
gst_rtp_g711_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g711_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g711_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_g711_pay_details);
}

static void
gst_rtp_g711_pay_class_init (GstRtpG711PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);
  gobject_class->finalize = gst_rtp_g711_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_g711_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_g711_pay_handle_buffer;
}

static void
gst_rtp_g711_pay_init (GstRtpG711Pay * rtpg711pay, GstRtpG711PayClass * klass)
{
  rtpg711pay->adapter = gst_adapter_new ();
  GST_BASE_RTP_PAYLOAD (rtpg711pay)->clock_rate = 8000;
}

static void
gst_rtp_g711_pay_finalize (GObject * object)
{
  GstRtpG711Pay *rtpg711pay;

  rtpg711pay = GST_RTP_G711_PAY (object);

  g_object_unref (rtpg711pay->adapter);
  rtpg711pay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_g711_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{

  const char *stname;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  stname = gst_structure_get_name (structure);

  if (0 == strcmp ("audio/x-mulaw", stname)) {
    payload->pt = GST_RTP_PAYLOAD_PCMU;
    gst_basertppayload_set_options (payload, "audio", FALSE, "PCMU", 8000);
  } else if (0 == strcmp ("audio/x-alaw", stname)) {
    payload->pt = GST_RTP_PAYLOAD_PCMA;
    gst_basertppayload_set_options (payload, "audio", FALSE, "PCMA", 8000);
  } else {
    return FALSE;
  }

  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtp_g711_pay_flush (GstRtpG711Pay * rtpg711pay)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint maxptime_octets = G_MAXUINT;
  guint minptime_octets = GST_RTP_G711_MIN_PTIME_OCTETS;

  if (GST_BASE_RTP_PAYLOAD (rtpg711pay)->max_ptime > 0) {
    /* calculate octet count with:
       maxptime-nsec * samples-per-sec / nsecs-per-sec * octets-per-sample */
    maxptime_octets =
        GST_BASE_RTP_PAYLOAD (rtpg711pay)->max_ptime *
        GST_BASE_RTP_PAYLOAD (rtpg711pay)->clock_rate / GST_SECOND;
  }

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet.  */
  avail = gst_adapter_available (rtpg711pay->adapter);

  ret = GST_FLOW_OK;

  while (avail >= minptime_octets) {
    guint8 *payload;
    guint8 *data;
    guint payload_len;
    guint packet_len;

    /* fill one MTU or all available bytes */
    payload_len =
        MIN (MIN (GST_BASE_RTP_PAYLOAD_MTU (rtpg711pay), maxptime_octets),
        avail);

    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (payload_len, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

    /* copy payload */
    gst_rtp_buffer_set_payload_type (outbuf,
        GST_BASE_RTP_PAYLOAD_PT (rtpg711pay));
    payload = gst_rtp_buffer_get_payload (outbuf);
    data = (guint8 *) gst_adapter_peek (rtpg711pay->adapter, payload_len);
    memcpy (payload, data, payload_len);
    gst_adapter_flush (rtpg711pay->adapter, payload_len);

    avail -= payload_len;

    GST_BUFFER_TIMESTAMP (outbuf) = rtpg711pay->first_ts;
    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpg711pay), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtp_g711_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpG711Pay *rtpg711pay;
  guint size, packet_len, avail;
  GstFlowReturn ret;
  GstClockTime duration;

  rtpg711pay = GST_RTP_G711_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  duration = GST_BUFFER_TIMESTAMP (buffer);

  avail = gst_adapter_available (rtpg711pay->adapter);
  if (avail == 0) {
    rtpg711pay->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpg711pay->duration = 0;
  }

  /* get packet length of data and see if we exceeded MTU. */
  packet_len = gst_rtp_buffer_calc_packet_len (avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpg711pay->duration + duration)) {
    ret = gst_rtp_g711_pay_flush (rtpg711pay);
    rtpg711pay->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpg711pay->duration = 0;
  } else {
    ret = GST_FLOW_OK;
  }

  gst_adapter_push (rtpg711pay->adapter, buffer);
  rtpg711pay->duration += duration;

  return ret;
}

gboolean
gst_rtp_g711_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg711pay",
      GST_RANK_NONE, GST_TYPE_RTP_G711_PAY);
}

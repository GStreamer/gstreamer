/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gstrtpg711enc.h"

/* elementfactory information */
static GstElementDetails gst_rtpg711enc_details = {
  "RTP packet parser",
  "Codec/Encoder/Network",
  "Encodes PCMU/PCMA audio into a RTP packet",
  "Edgard Lima <edgard.lima@indt.org.br>"
};

static GstStaticPadTemplate gst_rtpg711enc_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, channels=(int)1, rate=(int)8000 ;"
        "audio/x-alaw, channels=(int)1, rate=(int)8000")
    );

static GstStaticPadTemplate gst_rtpg711enc_src_template =
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

static gboolean gst_rtpg711enc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtpg711enc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);
static void gst_rtpg711enc_finalize (GObject * object);

GST_BOILERPLATE (GstRtpG711Enc, gst_rtpg711enc, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtpg711enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg711enc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg711enc_src_template));
  gst_element_class_set_details (element_class, &gst_rtpg711enc_details);
}

static void
gst_rtpg711enc_class_init (GstRtpG711EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_PAYLOAD);
  gobject_class->finalize = gst_rtpg711enc_finalize;

  gstbasertppayload_class->set_caps = gst_rtpg711enc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtpg711enc_handle_buffer;
}

static void
gst_rtpg711enc_init (GstRtpG711Enc * rtpg711enc, GstRtpG711EncClass * klass)
{
  rtpg711enc->adapter = gst_adapter_new ();
  GST_BASE_RTP_PAYLOAD (rtpg711enc)->clock_rate = 8000;
}

static void
gst_rtpg711enc_finalize (GObject * object)
{
  GstRtpG711Enc *rtpg711enc;

  rtpg711enc = GST_RTP_G711_ENC (object);

  g_object_unref (rtpg711enc->adapter);
  rtpg711enc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtpg711enc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
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
gst_rtpg711enc_flush (GstRtpG711Enc * rtpg711enc)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet.  */
  avail = gst_adapter_available (rtpg711enc->adapter);

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
    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpg711enc));
    /* this is the payload length */
    payload_len = gst_rtpbuffer_calc_payload_len (towrite, 0, 0);
    /* create buffer to hold the payload */
    outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);

    /* copy payload */
    gst_rtpbuffer_set_payload_type (outbuf,
        GST_BASE_RTP_PAYLOAD_PT (rtpg711enc));
    payload = gst_rtpbuffer_get_payload (outbuf);
    data = (guint8 *) gst_adapter_peek (rtpg711enc->adapter, payload_len);
    memcpy (payload, data, payload_len);
    gst_adapter_flush (rtpg711enc->adapter, payload_len);

    avail -= payload_len;

    GST_BUFFER_TIMESTAMP (outbuf) = rtpg711enc->first_ts;
    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpg711enc), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtpg711enc_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpG711Enc *rtpg711enc;
  guint size, packet_len, avail;
  GstFlowReturn ret;
  GstClockTime duration;

  rtpg711enc = GST_RTP_G711_ENC (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  duration = GST_BUFFER_TIMESTAMP (buffer);

  avail = gst_adapter_available (rtpg711enc->adapter);
  if (avail == 0) {
    rtpg711enc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpg711enc->duration = 0;
  }

  /* get packet length of data and see if we exceeded MTU. */
  packet_len = gst_rtpbuffer_calc_packet_len (avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpg711enc->duration + duration)) {
    ret = gst_rtpg711enc_flush (rtpg711enc);
    rtpg711enc->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpg711enc->duration = 0;
  } else {
    ret = GST_FLOW_OK;
  }

  gst_adapter_push (rtpg711enc->adapter, buffer);
  rtpg711enc->duration += duration;

  return ret;
}

gboolean
gst_rtpg711enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg711enc",
      GST_RANK_NONE, GST_TYPE_RTP_G711_ENC);
}

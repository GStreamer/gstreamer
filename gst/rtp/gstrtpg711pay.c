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
        "payload = (int) 0, "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"PCMU\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) 8, "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMA\"")
    );


static void gst_rtpg711enc_class_init (GstRtpG711EncClass * klass);
static void gst_rtpg711enc_base_init (GstRtpG711EncClass * klass);
static void gst_rtpg711enc_init (GstRtpG711Enc * rtpg711enc);

static gboolean gst_rtpg711enc_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtpg711enc_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

static GstBaseRTPPayloadClass *parent_class = NULL;

static GType
gst_rtpg711enc_get_type (void)
{
  static GType rtpg711enc_type = 0;

  if (!rtpg711enc_type) {
    static const GTypeInfo rtpg711enc_info = {
      sizeof (GstRtpG711EncClass),
      (GBaseInitFunc) gst_rtpg711enc_base_init,
      NULL,
      (GClassInitFunc) gst_rtpg711enc_class_init,
      NULL,
      NULL,
      sizeof (GstRtpG711Enc),
      0,
      (GInstanceInitFunc) gst_rtpg711enc_init,
    };

    rtpg711enc_type =
        g_type_register_static (GST_TYPE_BASE_RTP_PAYLOAD, "GstRtpG711Enc",
        &rtpg711enc_info, 0);
  }
  return rtpg711enc_type;
}

static void
gst_rtpg711enc_base_init (GstRtpG711EncClass * klass)
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

  gstbasertppayload_class->set_caps = gst_rtpg711enc_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtpg711enc_handle_buffer;
}

static void
gst_rtpg711enc_init (GstRtpG711Enc * rtpg711enc)
{
}

static gboolean
gst_rtpg711enc_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{

  const char *stname;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  stname = gst_structure_get_name (structure);

  if (0 == strcmp ("audio/x-mulaw", stname)) {
    gst_basertppayload_set_options (payload, "audio", TRUE, "PCMU", 8000);
  } else if (0 == strcmp ("audio/x-alaw", stname)) {
    gst_basertppayload_set_options (payload, "audio", TRUE, "PCMA", 8000);
  } else {
    return FALSE;
  }

  gst_basertppayload_set_outcaps (payload, NULL);

  return TRUE;
}

static GstFlowReturn
gst_rtpg711enc_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpG711Enc *rtpg711enc;
  guint size, payload_len;
  GstBuffer *outbuf;
  guint8 *payload, *data;
  GstClockTime timestamp;
  GstFlowReturn ret;

  rtpg711enc = GST_RTP_G711_ENC (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* FIXME, only one G711 frame per RTP packet for now */
  payload_len = size;

  outbuf = gst_rtpbuffer_new_allocate (payload_len, 0, 0);
  /* FIXME, assert for now */
  g_assert (GST_BUFFER_SIZE (outbuf) < GST_BASE_RTP_PAYLOAD_MTU (rtpg711enc));

  gst_rtpbuffer_set_timestamp (outbuf, timestamp * 8000 / GST_SECOND);

  /* copy timestamp */
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  /* get payload */
  payload = gst_rtpbuffer_get_payload (outbuf);

  data = GST_BUFFER_DATA (buffer);

  /* copy data in payload */
  memcpy (&payload[0], data, size);

  gst_buffer_unref (buffer);

  ret = gst_basertppayload_push (basepayload, outbuf);

  return ret;
}

gboolean
gst_rtpg711enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg711enc",
      GST_RANK_NONE, GST_TYPE_RTP_G711_ENC);
}

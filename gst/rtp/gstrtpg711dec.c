/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
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
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpg711dec.h"

/* elementfactory information */
static GstElementDetails gst_rtp_g711dec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts PCMU/PCMA audio from RTP packets",
  "Edgard Lima <edgard.lima@indt.org.br>, Zeeshan Ali <zeenix@gmail.com>"
};

/* RtpG711Dec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rtpg711dec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 0, 255 ], "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"PCMU\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 0, 255 ], "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"PCMA\"")

    );

static GstStaticPadTemplate gst_rtpg711dec_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, "
        "channels = (int) 1; " "audio/x-alaw, " "channels = (int) 1")
    );

static GstBuffer *gst_rtpg711dec_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtpg711dec_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpG711Dec, gst_rtpg711dec, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtpg711dec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg711dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpg711dec_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_g711dec_details);
}

static void
gst_rtpg711dec_class_init (GstRtpG711DecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_DEPAYLOAD);

  gstbasertpdepayload_class->process = gst_rtpg711dec_process;
  gstbasertpdepayload_class->set_caps = gst_rtpg711dec_setcaps;
}

static void
gst_rtpg711dec_init (GstRtpG711Dec * rtpg711dec, GstRtpG711DecClass * klass)
{
  GstBaseRTPDepayload *depayload;

  depayload = GST_BASE_RTP_DEPAYLOAD (rtpg711dec);

  depayload->clock_rate = 8000;
  gst_pad_use_fixed_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload));
}

static gboolean
gst_rtpg711dec_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  const gchar *enc_name;
  GstStructure *structure;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);
  enc_name = gst_structure_get_string (structure, "encoding-name");

  if (NULL == enc_name) {
    return FALSE;
  }

  if (0 == strcmp ("PCMU", enc_name)) {
    srccaps = gst_caps_new_simple ("audio/x-mulaw",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
  } else if (0 == strcmp ("PCMA", enc_name)) {
    srccaps = gst_caps_new_simple ("audio/x-alaw",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
  } else {
    return FALSE;
  }

  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return ret;
}

static GstBuffer *
gst_rtpg711dec_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstCaps *srccaps;
  GstBuffer *outbuf = NULL;
  gint payload_len;
  guint8 *payload;

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf),
      gst_rtpbuffer_get_marker (buf),
      gst_rtpbuffer_get_timestamp (buf), gst_rtpbuffer_get_seq (buf));

  srccaps = GST_PAD_CAPS (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload));
  if (!srccaps) {
    /* Set the default caps */
    srccaps = gst_caps_new_simple ("audio/x-mulaw",
        "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL);
    gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
    gst_caps_unref (srccaps);
  }

  payload_len = gst_rtpbuffer_get_payload_len (buf);
  payload = gst_rtpbuffer_get_payload (buf);

  outbuf = gst_buffer_new_and_alloc (payload_len);
  memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);
  return outbuf;
}

gboolean
gst_rtpg711dec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg711dec",
      GST_RANK_NONE, GST_TYPE_RTP_G711_DEC);
}

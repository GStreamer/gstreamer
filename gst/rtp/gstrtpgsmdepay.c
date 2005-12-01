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
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpgsmdepay.h"

/* elementfactory information */
static GstElementDetails gst_rtp_gsmdepay_details = {
  "RTP packet parser",
  "Codec/Depayr/Network",
  "Extracts GSM audio from RTP packets",
  "Zeeshan Ali <zeenix@gmail.com>"
};

/* RTPGSMDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate gst_rtp_gsm_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-gsm, " "rate = (int) 8000, " "channels = 1")
    );

static GstStaticPadTemplate gst_rtp_gsm_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 0, 255 ], "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"GSM\"")
    );

static GstBuffer *gst_rtp_gsm_depay_process (GstBaseRTPDepayload * _depayload,
    GstBuffer * buf);
static gboolean gst_rtp_gsm_depay_setcaps (GstBaseRTPDepayload * _depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPGSMDepay, gst_rtp_gsm_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtp_gsm_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_gsm_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_gsm_depay_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_gsmdepay_details);
}

static void
gst_rtp_gsm_depay_class_init (GstRTPGSMDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertp_depayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertp_depayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_DEPAYLOAD);

  gstbasertp_depayload_class->process = gst_rtp_gsm_depay_process;
  gstbasertp_depayload_class->set_caps = gst_rtp_gsm_depay_setcaps;
}

static void
gst_rtp_gsm_depay_init (GstRTPGSMDepay * rtpgsmdepay,
    GstRTPGSMDepayClass * klass)
{
  GST_BASE_RTP_DEPAYLOAD (rtpgsmdepay)->clock_rate = 8000;
}

static gboolean
gst_rtp_gsm_depay_setcaps (GstBaseRTPDepayload * _depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  gboolean ret;

  srccaps = gst_static_pad_template_get_caps (&gst_rtp_gsm_depay_src_template);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (_depayload), srccaps);

  gst_caps_unref (srccaps);
  return ret;
}

static GstBuffer *
gst_rtp_gsm_depay_process (GstBaseRTPDepayload * _depayload, GstBuffer * buf)
{
  GstBuffer *outbuf = NULL;
  gint payload_len;
  guint8 *payload;

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf),
      gst_rtp_buffer_get_marker (buf),
      gst_rtp_buffer_get_timestamp (buf), gst_rtp_buffer_get_seq (buf));

  payload_len = gst_rtp_buffer_get_payload_len (buf);
  payload = gst_rtp_buffer_get_payload (buf);

  outbuf = gst_buffer_new_and_alloc (payload_len);
  memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);
  return outbuf;
}

gboolean
gst_rtp_gsm_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgsmdepay",
      GST_RANK_NONE, GST_TYPE_RTP_GSM_DEPAY);
}

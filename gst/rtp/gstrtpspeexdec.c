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

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpspeexdec.h"

/* elementfactory information */
static GstElementDetails gst_rtp_speexdec_details = {
  "RTP packet parser",
  "Codec/Parser/Network",
  "Extracts Speex audio from RTP packets",
  "Edgard Lima <edgard.lima@indt.org.br>"
};

/* RtpSPEEXDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rtpspeexdec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate =  (int) [6000, 48000], "
        "encoding-name = (string) \"speex\", "
        "encoding-params = (string) \"1\"")
    );

static GstStaticPadTemplate gst_rtpspeexdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-speex")
    );

static GstBuffer *gst_rtpspeexdec_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtpspeexdec_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpSPEEXDec, gst_rtpspeexdec, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtpspeexdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpspeexdec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpspeexdec_sink_template));
  gst_element_class_set_details (element_class, &gst_rtp_speexdec_details);
}

static void
gst_rtpspeexdec_class_init (GstRtpSPEEXDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_RTP_DEPAYLOAD);

  gstbasertpdepayload_class->process = gst_rtpspeexdec_process;
  gstbasertpdepayload_class->set_caps = gst_rtpspeexdec_setcaps;
}

static void
gst_rtpspeexdec_init (GstRtpSPEEXDec * rtpspeexdec, GstRtpSPEEXDecClass * klass)
{
  GST_BASE_RTP_DEPAYLOAD (rtpspeexdec)->clock_rate = 8000;
}

static gboolean
gst_rtpspeexdec_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  gboolean ret;

  srccaps = gst_static_pad_template_get_caps (&gst_rtpspeexdec_src_template);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);

  gst_caps_unref (srccaps);
  return ret;
}

static GstBuffer *
gst_rtpspeexdec_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstBuffer *outbuf = NULL;
  gint payload_len;
  guint8 *payload;

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf),
      gst_rtpbuffer_get_marker (buf),
      gst_rtpbuffer_get_timestamp (buf), gst_rtpbuffer_get_seq (buf));

  payload_len = gst_rtpbuffer_get_payload_len (buf);
  payload = gst_rtpbuffer_get_payload (buf);

  outbuf = gst_buffer_new_and_alloc (payload_len);
  memcpy (GST_BUFFER_DATA (outbuf), payload, payload_len);
  return outbuf;
}

gboolean
gst_rtpspeexdec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpspeexdec",
      GST_RANK_NONE, GST_TYPE_RTP_SPEEX_DEC);
}

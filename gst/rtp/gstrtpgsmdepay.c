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

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpgsmdepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpgsmdepay_debug);
#define GST_CAT_DEFAULT (rtpgsmdepay_debug)

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
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"GSM\";"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_GSM_STRING ", "
        "clock-rate = (int) 8000")
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
  gst_element_class_set_details_simple (element_class, "RTP GSM depayloader",
      "Codec/Depayloader/Network/RTP",
      "Extracts GSM audio from RTP packets", "Zeeshan Ali <zeenix@gmail.com>");
}

static void
gst_rtp_gsm_depay_class_init (GstRTPGSMDepayClass * klass)
{
  GstBaseRTPDepayloadClass *gstbasertp_depayload_class;

  gstbasertp_depayload_class = (GstBaseRTPDepayloadClass *) klass;

  gstbasertp_depayload_class->process = gst_rtp_gsm_depay_process;
  gstbasertp_depayload_class->set_caps = gst_rtp_gsm_depay_setcaps;

  GST_DEBUG_CATEGORY_INIT (rtpgsmdepay_debug, "rtpgsmdepay", 0,
      "GSM Audio RTP Depayloader");
}

static void
gst_rtp_gsm_depay_init (GstRTPGSMDepay * rtpgsmdepay,
    GstRTPGSMDepayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static gboolean
gst_rtp_gsm_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  gboolean ret;
  GstStructure *structure;
  gint clock_rate;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 8000;          /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("audio/x-gsm",
      "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, clock_rate, NULL);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return ret;
}

static GstBuffer *
gst_rtp_gsm_depay_process (GstBaseRTPDepayload * _depayload, GstBuffer * buf)
{
  GstBuffer *outbuf = NULL;
  gboolean marker;

  marker = gst_rtp_buffer_get_marker (buf);

  GST_DEBUG ("process : got %d bytes, mark %d ts %u seqn %d",
      GST_BUFFER_SIZE (buf), marker,
      gst_rtp_buffer_get_timestamp (buf), gst_rtp_buffer_get_seq (buf));

  outbuf = gst_rtp_buffer_get_payload_buffer (buf);

  if (marker && outbuf) {
    /* mark start of talkspurt with DISCONT */
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
  }

  return outbuf;
}

gboolean
gst_rtp_gsm_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgsmdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_GSM_DEPAY);
}

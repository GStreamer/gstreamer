/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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
#include <stdlib.h>

#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpbvdepay.h"

static GstStaticPadTemplate gst_rtp_bv_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"BV16\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 16000, " "encoding-name = (string) \"BV32\"")
    );

static GstStaticPadTemplate gst_rtp_bv_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-bv, " "mode = (int) { 16, 32 }")
    );

static GstBuffer *gst_rtp_bv_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_bv_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPBVDepay, gst_rtp_bv_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void
gst_rtp_bv_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_bv_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_bv_depay_sink_template);
  gst_element_class_set_details_simple (element_class,
      "RTP BroadcomVoice depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts BroadcomVoice audio from RTP packets (RFC 4298)",
      "Wim Taymans <wim.taymans@collabora.co.uk>");
}

static void
gst_rtp_bv_depay_class_init (GstRTPBVDepayClass * klass)
{
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gstbasertpdepayload_class->process = gst_rtp_bv_depay_process;
  gstbasertpdepayload_class->set_caps = gst_rtp_bv_depay_setcaps;
}

static void
gst_rtp_bv_depay_init (GstRTPBVDepay * rtpbvdepay, GstRTPBVDepayClass * klass)
{
  rtpbvdepay->mode = -1;
}

static gboolean
gst_rtp_bv_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstRTPBVDepay *rtpbvdepay = GST_RTP_BV_DEPAY (depayload);
  GstCaps *srccaps;
  GstStructure *structure;
  const gchar *mode_str = NULL;
  gint mode, clock_rate, expected_rate;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);

  mode_str = gst_structure_get_string (structure, "encoding-name");
  if (!mode_str)
    goto no_mode;

  if (!strcmp (mode_str, "BV16")) {
    mode = 16;
    expected_rate = 8000;
  } else if (!strcmp (mode_str, "BV32")) {
    mode = 32;
    expected_rate = 16000;
  } else
    goto invalid_mode;

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = expected_rate;
  else if (clock_rate != expected_rate)
    goto wrong_rate;

  depayload->clock_rate = clock_rate;
  rtpbvdepay->mode = mode;

  srccaps = gst_caps_new_simple ("audio/x-bv",
      "mode", G_TYPE_INT, rtpbvdepay->mode, NULL);
  ret = gst_pad_set_caps (GST_BASE_RTP_DEPAYLOAD_SRCPAD (depayload), srccaps);

  GST_DEBUG ("set caps on source: %" GST_PTR_FORMAT " (ret=%d)", srccaps, ret);
  gst_caps_unref (srccaps);

  return ret;

  /* ERRORS */
no_mode:
  {
    GST_ERROR_OBJECT (rtpbvdepay, "did not receive an encoding-name");
    return FALSE;
  }
invalid_mode:
  {
    GST_ERROR_OBJECT (rtpbvdepay,
        "invalid encoding-name, expected BV16 or BV32, got %s", mode_str);
    return FALSE;
  }
wrong_rate:
  {
    GST_ERROR_OBJECT (rtpbvdepay, "invalid clock-rate, expected %d, got %d",
        expected_rate, clock_rate);
    return FALSE;
  }
}

static GstBuffer *
gst_rtp_bv_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstBuffer *outbuf;
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
gst_rtp_bv_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpbvdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_BV_DEPAY);
}

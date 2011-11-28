/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim.taymans@gmail.com>
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

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpmpadepay.h"

GST_DEBUG_CATEGORY_STATIC (rtpmpadepay_debug);
#define GST_CAT_DEFAULT (rtpmpadepay_debug)

static GstStaticPadTemplate gst_rtp_mpa_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1")
    );

static GstStaticPadTemplate gst_rtp_mpa_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPA\";"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_MPA_STRING ", "
        "clock-rate = (int) 90000")
    );

GST_BOILERPLATE (GstRtpMPADepay, gst_rtp_mpa_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static gboolean gst_rtp_mpa_depay_setcaps (GstBaseRTPDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mpa_depay_process (GstBaseRTPDepayload * depayload,
    GstBuffer * buf);

static void
gst_rtp_mpa_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_mpa_depay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_mpa_depay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP MPEG audio depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts MPEG audio from RTP packets (RFC 2038)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_mpa_depay_class_init (GstRtpMPADepayClass * klass)
{
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gstbasertpdepayload_class->set_caps = gst_rtp_mpa_depay_setcaps;
  gstbasertpdepayload_class->process = gst_rtp_mpa_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpmpadepay_debug, "rtpmpadepay", 0,
      "MPEG Audio RTP Depayloader");
}

static void
gst_rtp_mpa_depay_init (GstRtpMPADepay * rtpmpadepay,
    GstRtpMPADepayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static gboolean
gst_rtp_mpa_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  GstCaps *outcaps;
  gint clock_rate;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;
  depayload->clock_rate = clock_rate;

  outcaps =
      gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1, NULL);
  res = gst_pad_set_caps (depayload->srcpad, outcaps);
  gst_caps_unref (outcaps);

  return res;
}

static GstBuffer *
gst_rtp_mpa_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMPADepay *rtpmpadepay;
  GstBuffer *outbuf;

  rtpmpadepay = GST_RTP_MPA_DEPAY (depayload);

  {
    gint payload_len;
    gboolean marker;

    payload_len = gst_rtp_buffer_get_payload_len (buf);

    if (payload_len <= 4)
      goto empty_packet;

    /* strip off header
     *
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |             MBZ               |          Frag_offset          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    /* frag_offset = (payload[2] << 8) | payload[3]; */

    /* subbuffer skipping the 4 header bytes */
    outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 4, -1);
    marker = gst_rtp_buffer_get_marker (buf);

    if (marker) {
      /* mark start of talkspurt with discont */
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    }
    GST_DEBUG_OBJECT (rtpmpadepay,
        "gst_rtp_mpa_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    /* FIXME, we can push half mpeg frames when they are split over multiple
     * RTP packets */
    return outbuf;
  }

  return NULL;

  /* ERRORS */
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpmpadepay, STREAM, DECODE,
        ("Empty Payload."), (NULL));
    return NULL;
  }
}

gboolean
gst_rtp_mpa_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpadepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_MPA_DEPAY);
}

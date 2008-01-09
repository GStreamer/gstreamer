/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

/* elementfactory information */
static const GstElementDetails gst_rtp_mpadepay_details =
GST_ELEMENT_DETAILS ("RTP packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts MPEG audio from RTP packets (RFC 2038)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_mpa_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
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

static GstStateChangeReturn gst_rtp_mpa_depay_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_rtp_mpa_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpa_depay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpa_depay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpadepay_details);
}

static void
gst_rtp_mpa_depay_class_init (GstRtpMPADepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_rtp_mpa_depay_change_state;

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
  GstRtpMPADepay *rtpmpadepay;
  gint clock_rate = 90000;      /* default */

  rtpmpadepay = GST_RTP_MPA_DEPAY (depayload);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "clock-rate", &clock_rate);
  depayload->clock_rate = clock_rate;

  return TRUE;
}

static GstBuffer *
gst_rtp_mpa_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpMPADepay *rtpmpadepay;
  GstBuffer *outbuf;

  rtpmpadepay = GST_RTP_MPA_DEPAY (depayload);

  if (!gst_rtp_buffer_validate (buf))
    goto bad_packet;

  {
    gint payload_len;
    guint8 *payload;
    guint16 frag_offset;

    payload_len = gst_rtp_buffer_get_payload_len (buf);
    payload = gst_rtp_buffer_get_payload (buf);

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
    frag_offset = (payload[2] << 8) | payload[3];

    /* subbuffer skipping the 4 header bytes */
    outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, 4, -1);

    GST_DEBUG_OBJECT (rtpmpadepay,
        "gst_rtp_mpa_depay_chain: pushing buffer of size %d",
        GST_BUFFER_SIZE (outbuf));

    /* FIXME, we can push half mpeg frames when they are split over multiple
     * RTP packets */
    return outbuf;
  }

  return NULL;

bad_packet:
  {
    GST_ELEMENT_WARNING (rtpmpadepay, STREAM, DECODE,
        ("Packet did not validate."), (NULL));
    return NULL;
  }
#if 0
bad_payload:
  {
    GST_ELEMENT_WARNING (rtpmpadepay, STREAM, DECODE,
        ("Unexpected payload type."), (NULL));
    return NULL;
  }
#endif
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpmpadepay, STREAM, DECODE,
        ("Empty Payload."), (NULL));
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_mpa_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstRtpMPADepay *rtpmpadepay;
  GstStateChangeReturn ret;

  rtpmpadepay = GST_RTP_MPA_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_mpa_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpadepay",
      GST_RANK_MARGINAL, GST_TYPE_RTP_MPA_DEPAY);
}

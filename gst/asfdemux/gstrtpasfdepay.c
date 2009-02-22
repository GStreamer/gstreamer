/* GStreamer RTP ASF depayloader
 * Copyright (C) 2006 Tim-Philipp Müller  <tim centricular net>
 *               2009 Wim Taymans  <wim.taymans@gmail.com>
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
# include "config.h"
#endif

#include "gstrtpasfdepay.h"
#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (rtpasfdepayload_debug);
#define GST_CAT_DEFAULT rtpasfdepayload_debug

static const GstElementDetails rtp_asf_depay_details =
GST_ELEMENT_DETAILS ("RTP ASF packet depayloader",
    "Codec/Depayloader/Network",
    "Extracts ASF streams from RTP",
    "Tim-Philipp Müller <tim centricular net>, "
    "Wim Taymans <wim.taymans@gmail.com>");

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

/* Other parameters: config, maxps */
#define SINK_CAPS \
  "application/x-rtp, "                                          \
  "media = (string) { \"application\", \"video\", \"audio\" }, " \
  "clock-rate = (int) [1, MAX ], "                               \
  "encoding-name = (string) \"X-ASF-PF\""

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

GST_BOILERPLATE (GstRtpAsfDepay, gst_rtp_asf_depay, GstBaseRTPDepayload,
    GST_TYPE_BASE_RTP_DEPAYLOAD);

static void gst_rtp_asf_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_asf_depay_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_rtp_asf_depay_setcaps (GstBaseRTPDepayload * depay,
    GstCaps * caps);
static GstBuffer *gst_rtp_asf_depay_process (GstBaseRTPDepayload * basedepay,
    GstBuffer * buf);

static void
gst_rtp_asf_depay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &rtp_asf_depay_details);
}

static void
gst_rtp_asf_depay_class_init (GstRtpAsfDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPDepayloadClass *gstbasertpdepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertpdepayload_class = (GstBaseRTPDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_asf_depay_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_asf_depay_change_state);

  gstbasertpdepayload_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_rtp_asf_depay_setcaps);
  gstbasertpdepayload_class->process =
      GST_DEBUG_FUNCPTR (gst_rtp_asf_depay_process);

  GST_DEBUG_CATEGORY_INIT (rtpasfdepayload_debug, "rtpasfdepayload", 0,
      "RTP asf depayloader element");
}

static void
gst_rtp_asf_depay_init (GstRtpAsfDepay * depay, GstRtpAsfDepayClass * klass)
{
  depay->adapter = gst_adapter_new ();
}

static void
gst_rtp_asf_depay_finalize (GObject * object)
{
  GstRtpAsfDepay *depay;

  depay = GST_RTP_ASF_DEPAY (object);

  g_object_unref (depay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const guint8 asf_marker[16] = { 0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66,
  0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
};

static gboolean
gst_rtp_asf_depay_setcaps (GstBaseRTPDepayload * depayload, GstCaps * caps)
{
  GstRtpAsfDepay *depay;
  GstStructure *s;
  const gchar *config_str, *ps_string;
  GstBuffer *buf;
  GstCaps *src_caps;
  guint8 *headers;
  gsize headers_len;
  gint clock_rate;

  depay = GST_RTP_ASF_DEPAY (depayload);

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "clock-rate", &clock_rate) || clock_rate < 0)
    clock_rate = 1000;
  depayload->clock_rate = clock_rate;

  /* config contains the asf headers in base64 coding */
  config_str = gst_structure_get_string (s, "config");
  if (config_str == NULL || *config_str == '\0')
    goto no_config;

  ps_string = gst_structure_get_string (s, "maxps");
  if (ps_string == NULL || *ps_string == '\0')
    goto no_packetsize;

  depay->packet_size = atoi (ps_string);
  if (depay->packet_size <= 16)
    goto invalid_packetsize;

  headers = (guint8 *) g_base64_decode (config_str, &headers_len);

  if (headers == NULL || headers_len < 16
      || memcmp (headers, asf_marker, 16) != 0)
    goto invalid_headers;

  src_caps = gst_caps_new_simple ("video/x-ms-asf", NULL);
  gst_pad_set_caps (depayload->srcpad, src_caps);

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = headers;
  GST_BUFFER_MALLOCDATA (buf) = headers;
  GST_BUFFER_SIZE (buf) = headers_len;
  gst_buffer_set_caps (buf, src_caps);
  gst_caps_unref (src_caps);

  gst_base_rtp_depayload_push (depayload, buf);

  return TRUE;

  /* ERRORS */
no_config:
  {
    GST_WARNING_OBJECT (depay, "caps without 'config' field with asf headers");
    return FALSE;
  }
no_packetsize:
  {
    GST_WARNING_OBJECT (depay, "caps without 'maxps' (packet size) field");
    return FALSE;
  }
invalid_packetsize:
  {
    GST_WARNING_OBJECT (depay, "packet size %u invalid", depay->packet_size);
    return FALSE;
  }
invalid_headers:
  {
    GST_WARNING_OBJECT (depay, "headers don't look like valid ASF headers");
    g_free (headers);
    return FALSE;
  }
}

/* Docs: 'RTSP Protocol PDF' document from http://sdp.ppona.com/ (page 8) */

static GstBuffer *
gst_rtp_asf_depay_process (GstBaseRTPDepayload * depayload, GstBuffer * buf)
{
  GstRtpAsfDepay *depay;
  const guint8 *payload;
  GstBuffer *outbuf;
  gboolean S, L, R, D, I;
  guint payload_len, hdr_len, offset;
  guint len_offs;
  GstClockTime timestamp;

  depay = GST_RTP_ASF_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    gst_adapter_clear (depay->adapter);
    depay->wait_start = TRUE;
    depay->discont = TRUE;
  }

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  payload_len = gst_rtp_buffer_get_payload_len (buf);
  payload = gst_rtp_buffer_get_payload (buf);
  offset = 0;

  GST_LOG_OBJECT (depay, "got payload len of %u", payload_len);

  do {
    guint packet_len;

    /* packet header is at least 4 bytes */
    if (payload_len < 4)
      goto too_small;

    /*                      1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |S|L|R|D|I|RES  | Length/Offset                                 |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * | Relative Timestamp (optional)                                 |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * | Duration (optional)                                           |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * | LocationId (optional)                                         |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *
     * S: packet contains a keyframe.
     * L: Length or offset present.
     * R: relative timestamp present
     * R: duration present
     * I: locationid present
     */

    S = ((payload[0] & 0x80) != 0);
    L = ((payload[0] & 0x40) != 0);
    R = ((payload[0] & 0x20) != 0);
    D = ((payload[0] & 0x10) != 0);
    I = ((payload[0] & 0x08) != 0);

    hdr_len = 4;

    len_offs = (payload[1] << 16) | (payload[2] << 8) | payload[3];

    if (R)
      hdr_len += 4;
    if (D)
      hdr_len += 4;
    if (I)
      hdr_len += 4;

    GST_LOG_OBJECT (depay, "S %d, L %d, R %d, D %d, I %d", S, L, R, D, I);

    if (payload_len < hdr_len)
      goto too_small;

    /* skip headers */
    payload_len -= hdr_len;
    payload += hdr_len;
    offset += hdr_len;

    if (L) {
      /* L bit set, len contains the length of the packet */
      packet_len = len_offs;
    } else {
      packet_len = 0;
      /* else it contains an offset which we don't handle yet */
      g_assert_not_reached ();
    }

    if (packet_len > payload_len)
      packet_len = payload_len;

    GST_LOG_OBJECT (depay, "packet len %u, payload len %u", packet_len,
        payload_len);

    if (packet_len >= depay->packet_size) {
      GST_LOG_OBJECT (depay, "creating subbuffer");
      outbuf = gst_rtp_buffer_get_payload_subbuffer (buf, offset, packet_len);
    } else {
      GST_LOG_OBJECT (depay, "padding buffer");
      /* we need to pad with zeroes to packet_size if it's smaller */
      outbuf = gst_buffer_new_and_alloc (depay->packet_size);
      memcpy (GST_BUFFER_DATA (outbuf), payload, packet_len);
      memset (GST_BUFFER_DATA (outbuf) + packet_len, 0,
          depay->packet_size - packet_len);
    }

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (depayload->srcpad));

    if (S)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

    if (depay->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      depay->discont = FALSE;
    }

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    gst_base_rtp_depayload_push (depayload, outbuf);

    /* only apply the timestamp to the first buffer of this packet */
    timestamp = -1;

    /* skip packet data */
    payload += packet_len;
    offset += packet_len;
    payload_len -= packet_len;
  } while (payload_len > 0);

  return NULL;

/* ERRORS */
too_small:
  {
    GST_WARNING_OBJECT (depayload, "Payload too small, expected at least 4 "
        "bytes for header, but got only %d bytes", payload_len);
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_asf_depay_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret;
  GstRtpAsfDepay *depay;

  depay = GST_RTP_ASF_DEPAY (element);

  switch (trans) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (depay->adapter);
      depay->wait_start = TRUE;
      depay->discont = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  switch (trans) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_adapter_clear (depay->adapter);
      break;
    default:
      break;
  }

  return ret;
}

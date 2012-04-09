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
#include <stdlib.h>

GST_DEBUG_CATEGORY_STATIC (rtpasfdepayload_debug);
#define GST_CAT_DEFAULT rtpasfdepayload_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

/* Other parameters: config, maxps */
#define SINK_CAPS \
  "application/x-rtp, "                                          \
  "media = (string) { \"application\", \"video\", \"audio\" }, " \
  "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "         \
  "clock-rate = (int) [1, MAX ], "                               \
  "encoding-name = (string) \"X-ASF-PF\""

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

#define gst_rtp_asf_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpAsfDepay, gst_rtp_asf_depay, GST_TYPE_RTP_BASE_DEPAYLOAD);

static void gst_rtp_asf_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_asf_depay_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_rtp_asf_depay_setcaps (GstRTPBaseDepayload * depay,
    GstCaps * caps);
static GstBuffer *gst_rtp_asf_depay_process (GstRTPBaseDepayload * basedepay,
    GstBuffer * buf);

static void
gst_rtp_asf_depay_class_init (GstRtpAsfDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP ASF packet depayloader", "Codec/Depayloader/Network",
      "Extracts ASF streams from RTP",
      "Tim-Philipp Müller <tim centricular net>, "
      "Wim Taymans <wim.taymans@gmail.com>");

  gobject_class->finalize = gst_rtp_asf_depay_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_asf_depay_change_state);

  gstrtpbasedepayload_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_rtp_asf_depay_setcaps);
  gstrtpbasedepayload_class->process =
      GST_DEBUG_FUNCPTR (gst_rtp_asf_depay_process);

  GST_DEBUG_CATEGORY_INIT (rtpasfdepayload_debug, "rtpasfdepayload", 0,
      "RTP asf depayloader element");
}

static void
gst_rtp_asf_depay_init (GstRtpAsfDepay * depay)
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
gst_rtp_asf_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
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

  if (depay->packet_size) {
    /* header sent again following seek;
     * discard to avoid confusing upstream */
    if (depay->packet_size == atoi (ps_string)) {
      goto duplicate_header;
    } else {
      /* since we should fiddle with downstream state to handle this */
      goto refuse_renegotiation;
    }
  } else
    depay->packet_size = atoi (ps_string);
  if (depay->packet_size <= 16)
    goto invalid_packetsize;

  headers = (guint8 *) g_base64_decode (config_str, &headers_len);

  if (headers == NULL || headers_len < 16
      || memcmp (headers, asf_marker, 16) != 0)
    goto invalid_headers;

  src_caps = gst_caps_new_empty_simple ("video/x-ms-asf");
  gst_pad_set_caps (depayload->srcpad, src_caps);
  gst_caps_unref (src_caps);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, headers, headers_len, 0, headers_len, headers,
          g_free));

  gst_rtp_base_depayload_push (depayload, buf);

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
duplicate_header:
  {
    GST_DEBUG_OBJECT (depayload, "discarding duplicate header");
    return TRUE;
  }
refuse_renegotiation:
  {
    GST_WARNING_OBJECT (depayload, "cannot renegotiate to different header");
    return FALSE;
  }
}

static gint
field_size (guint8 field)
{
  switch (field) {
      /* DWORD - 32 bits */
    case 3:
      return 4;

      /* WORD - 16 bits */
    case 2:
      return 2;

      /* BYTE - 8 bits */
    case 1:
      return 1;

      /* non-exitent */
    case 0:
    default:
      return 0;
  }
}

/* 
 * Set the padding field to te correct value as the spec
 * says it should be se to 0 in the rtp packets
 */
static void
gst_rtp_asf_depay_set_padding (GstRtpAsfDepay * depayload,
    GstBuffer * buf, guint32 padding)
{
  GstMapInfo map;
  guint8 *data;
  gint offset = 0;
  guint8 aux;
  guint8 seq_type;
  guint8 pad_type;
  guint8 pkt_type;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = map.data;

  aux = data[offset++];
  if (aux & 0x80) {
    guint8 err_len = 0;
    if (aux & 0x60) {
      GST_WARNING_OBJECT (depayload, "Error correction length type should be "
          "set to 0");
      /* this packet doesn't follow the spec */
      gst_buffer_unmap (buf, &map);
      return;
    }
    err_len = aux & 0x0F;
    offset += err_len;

    aux = data[offset++];
  }
  seq_type = (aux >> 1) & 0x3;
  pad_type = (aux >> 3) & 0x3;
  pkt_type = (aux >> 5) & 0x3;

  offset += 1;                  /* skip property flags */
  offset += field_size (pkt_type);      /* skip packet length */
  offset += field_size (seq_type);      /* skip sequence field */

  /* write padding */
  switch (pad_type) {
      /* DWORD */
    case 3:
      GST_WRITE_UINT32_LE (&(data[offset]), padding);
      break;

      /* WORD */
    case 2:
      GST_WRITE_UINT16_LE (&(data[offset]), padding);
      break;

      /* BYTE */
    case 1:
      data[offset] = (guint8) padding;
      break;

      /* non-existent */
    case 0:
    default:
      break;
  }
  gst_buffer_unmap (buf, &map);
}

/* Docs: 'RTSP Protocol PDF' document from http://sdp.ppona.com/ (page 8) */

static GstBuffer *
gst_rtp_asf_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRtpAsfDepay *depay;
  const guint8 *payload;
  GstBuffer *outbuf;
  gboolean S, L, R, D, I;
  guint payload_len, hdr_len, offset;
  guint len_offs;
  GstClockTime timestamp;
  GstRTPBuffer rtpbuf = { NULL };

  depay = GST_RTP_ASF_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (depay, "got DISCONT");
    gst_adapter_clear (depay->adapter);
    depay->discont = TRUE;
  }

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtpbuf);
  timestamp = GST_BUFFER_TIMESTAMP (buf);

  payload_len = gst_rtp_buffer_get_payload_len (&rtpbuf);
  payload = gst_rtp_buffer_get_payload (&rtpbuf);
  offset = 0;

  GST_LOG_OBJECT (depay, "got payload len of %u", payload_len);

  do {
    guint packet_len;
    gsize plen;

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
     * L: If 1, Length/Offset contains length, else contains the byte offset
     *    of the fragment's first byte counted from the beginning of the
     *    complete ASF data packet.
     * R: relative timestamp present
     * D: duration present
     * I: locationid present
     */

    S = ((payload[0] & 0x80) != 0);
    L = ((payload[0] & 0x40) != 0);
    R = ((payload[0] & 0x20) != 0);
    D = ((payload[0] & 0x10) != 0);
    I = ((payload[0] & 0x08) != 0);

    hdr_len = 4;

    len_offs = (payload[1] << 16) | (payload[2] << 8) | payload[3];

    if (R) {
      GST_DEBUG ("Relative timestamp field present : %u",
          GST_READ_UINT32_BE (payload + hdr_len));
      hdr_len += 4;
    }
    if (D) {
      GST_DEBUG ("Duration field present : %u",
          GST_READ_UINT32_BE (payload + hdr_len));
      hdr_len += 4;
    }
    if (I) {
      GST_DEBUG ("LocationId field present : %u",
          GST_READ_UINT32_BE (payload + hdr_len));
      hdr_len += 4;
    }

    GST_LOG_OBJECT (depay, "S %d, L %d, R %d, D %d, I %d", S, L, R, D, I);
    GST_LOG_OBJECT (depay, "payload_len:%d, hdr_len:%d, len_offs:%d",
        payload_len, hdr_len, len_offs);

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
      /* else it contains an offset which we don't handle yet */
      GST_LOG_OBJECT (depay, "We have a fragmented packet");
      packet_len = payload_len;
    }

    if (packet_len > payload_len)
      packet_len = payload_len;

    GST_LOG_OBJECT (depay, "packet len %u, payload len %u, packet_size:%u",
        packet_len, payload_len, depay->packet_size);

    if (!L) {
      guint available;
      GstBuffer *sub;

      /* Fragmented packet handling */
      outbuf = NULL;

      if (len_offs == (available = gst_adapter_available (depay->adapter))) {
        /* fragment aligns with what we have, add it */
        GST_LOG_OBJECT (depay, "collecting fragment");
        sub =
            gst_rtp_buffer_get_payload_subbuffer (&rtpbuf, offset, packet_len);
        gst_adapter_push (depay->adapter, sub);
        /* RTP marker bit M is set if this is last fragment */
        if (gst_rtp_buffer_get_marker (&rtpbuf)) {
          GST_LOG_OBJECT (depay, "last fragment, assembling packet");
          outbuf =
              gst_adapter_take_buffer (depay->adapter, available + packet_len);
        }
      } else {
        if (available) {
          GST_WARNING_OBJECT (depay, "Offset doesn't match previous data?!");
          GST_DEBUG_OBJECT (depay, "clearing for re-sync");
          gst_adapter_clear (depay->adapter);
        } else
          GST_DEBUG_OBJECT (depay, "waiting for start of packet");
      }
    } else {
      GST_LOG_OBJECT (depay, "collecting packet");
      outbuf =
          gst_rtp_buffer_get_payload_subbuffer (&rtpbuf, offset, packet_len);
    }

    /* If we haven't completed a full ASF packet, return */
    if (!outbuf)
      return NULL;

    /* we need to pad with zeroes to packet_size if it's smaller */
    plen = gst_buffer_get_size (outbuf);
    if (plen < depay->packet_size) {
      GstBuffer *tmp;

      GST_LOG_OBJECT (depay,
          "padding buffer size %" G_GSIZE_FORMAT " to packet size %d", plen,
          depay->packet_size);

      tmp = gst_buffer_new_and_alloc (depay->packet_size);
      gst_buffer_copy_into (tmp, outbuf, GST_BUFFER_COPY_ALL, 0, plen);
      gst_buffer_unref (outbuf);
      outbuf = tmp;

      gst_buffer_memset (outbuf, plen, 0, depay->packet_size - plen);
      gst_rtp_asf_depay_set_padding (depay, outbuf, depay->packet_size - plen);
    }

    if (!S)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

    if (depay->discont) {
      GST_LOG_OBJECT (depay, "setting DISCONT");
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      depay->discont = FALSE;
    }

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    gst_rtp_base_depayload_push (depayload, outbuf);

    /* only apply the timestamp to the first buffer of this packet */
    timestamp = -1;

    /* skip packet data */
    payload += packet_len;
    offset += packet_len;
    payload_len -= packet_len;
  } while (payload_len > 0);

  gst_rtp_buffer_unmap (&rtpbuf);

  return NULL;

/* ERRORS */
too_small:
  {
    gst_rtp_buffer_unmap (&rtpbuf);
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

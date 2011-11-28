/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim.taymans@gmail.com>
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

#include "fnv1hash.h"
#include "gstrtpvorbispay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvorbispay_debug);
#define GST_CAT_DEFAULT (rtpvorbispay_debug)

/* references:
 * http://www.rfc-editor.org/rfc/rfc5215.txt
 */

static GstStaticPadTemplate gst_rtp_vorbis_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"VORBIS\""
        /* All required parameters
         *
         * "encoding-params = (string) <num channels>"
         * "configuration = (string) ANY"
         */
    )
    );

static GstStaticPadTemplate gst_rtp_vorbis_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstRtpVorbisPay, gst_rtp_vorbis_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static gboolean gst_rtp_vorbis_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstStateChangeReturn gst_rtp_vorbis_pay_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_rtp_vorbis_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);
static gboolean gst_rtp_vorbis_pay_handle_event (GstPad * pad,
    GstEvent * event);

static void
gst_rtp_vorbis_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vorbis_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_vorbis_pay_sink_template);

  gst_element_class_set_details_simple (element_class, "RTP Vorbis depayloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode Vorbis audio into RTP packets (RFC 5215)",
      "Wim Taymans <wimi.taymans@gmail.com>");
}

static void
gst_rtp_vorbis_pay_class_init (GstRtpVorbisPayClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstelement_class->change_state = gst_rtp_vorbis_pay_change_state;

  gstbasertppayload_class->set_caps = gst_rtp_vorbis_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_vorbis_pay_handle_buffer;
  gstbasertppayload_class->handle_event = gst_rtp_vorbis_pay_handle_event;

  GST_DEBUG_CATEGORY_INIT (rtpvorbispay_debug, "rtpvorbispay", 0,
      "Vorbis RTP Payloader");
}

static void
gst_rtp_vorbis_pay_init (GstRtpVorbisPay * rtpvorbispay,
    GstRtpVorbisPayClass * klass)
{
  /* needed because of GST_BOILERPLATE */
}

static void
gst_rtp_vorbis_pay_clear_packet (GstRtpVorbisPay * rtpvorbispay)
{
  if (rtpvorbispay->packet)
    gst_buffer_unref (rtpvorbispay->packet);
  rtpvorbispay->packet = NULL;
}

static void
gst_rtp_vorbis_pay_cleanup (GstRtpVorbisPay * rtpvorbispay)
{
  g_list_foreach (rtpvorbispay->headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (rtpvorbispay->headers);
  rtpvorbispay->headers = NULL;

  gst_rtp_vorbis_pay_clear_packet (rtpvorbispay);
}

static gboolean
gst_rtp_vorbis_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpVorbisPay *rtpvorbispay;

  rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);

  rtpvorbispay->need_headers = TRUE;

  return TRUE;
}

static void
gst_rtp_vorbis_pay_reset_packet (GstRtpVorbisPay * rtpvorbispay, guint8 VDT)
{
  guint payload_len;

  GST_LOG_OBJECT (rtpvorbispay, "reset packet");

  rtpvorbispay->payload_pos = 4;
  payload_len = gst_rtp_buffer_get_payload_len (rtpvorbispay->packet);
  rtpvorbispay->payload_left = payload_len - 4;
  rtpvorbispay->payload_duration = 0;
  rtpvorbispay->payload_F = 0;
  rtpvorbispay->payload_VDT = VDT;
  rtpvorbispay->payload_pkts = 0;
}

static void
gst_rtp_vorbis_pay_init_packet (GstRtpVorbisPay * rtpvorbispay, guint8 VDT,
    GstClockTime timestamp)
{
  GST_LOG_OBJECT (rtpvorbispay, "starting new packet, VDT: %d", VDT);

  if (rtpvorbispay->packet)
    gst_buffer_unref (rtpvorbispay->packet);

  /* new packet allocate max packet size */
  rtpvorbispay->packet =
      gst_rtp_buffer_new_allocate_len (GST_BASE_RTP_PAYLOAD_MTU
      (rtpvorbispay), 0, 0);
  gst_rtp_vorbis_pay_reset_packet (rtpvorbispay, VDT);
  GST_BUFFER_TIMESTAMP (rtpvorbispay->packet) = timestamp;
}

static GstFlowReturn
gst_rtp_vorbis_pay_flush_packet (GstRtpVorbisPay * rtpvorbispay)
{
  GstFlowReturn ret;
  guint8 *payload;
  guint hlen;

  /* check for empty packet */
  if (!rtpvorbispay->packet || rtpvorbispay->payload_pos <= 4)
    return GST_FLOW_OK;

  GST_LOG_OBJECT (rtpvorbispay, "flushing packet");

  /* fix header */
  payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Ident                     | F |VDT|# pkts.|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * F: Fragment type (0=none, 1=start, 2=cont, 3=end)
   * VDT: Vorbis data type (0=vorbis, 1=config, 2=comment, 3=reserved)
   * pkts: number of packets.
   */
  payload[0] = (rtpvorbispay->payload_ident >> 16) & 0xff;
  payload[1] = (rtpvorbispay->payload_ident >> 8) & 0xff;
  payload[2] = (rtpvorbispay->payload_ident) & 0xff;
  payload[3] = (rtpvorbispay->payload_F & 0x3) << 6 |
      (rtpvorbispay->payload_VDT & 0x3) << 4 |
      (rtpvorbispay->payload_pkts & 0xf);

  /* shrink the buffer size to the last written byte */
  hlen = gst_rtp_buffer_calc_header_len (0);
  GST_BUFFER_SIZE (rtpvorbispay->packet) = hlen + rtpvorbispay->payload_pos;

  GST_BUFFER_DURATION (rtpvorbispay->packet) = rtpvorbispay->payload_duration;

  /* push, this gives away our ref to the packet, so clear it. */
  ret =
      gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpvorbispay),
      rtpvorbispay->packet);
  rtpvorbispay->packet = NULL;

  return ret;
}

static gboolean
gst_rtp_vorbis_pay_finish_headers (GstBaseRTPPayload * basepayload)
{
  GstRtpVorbisPay *rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);
  GList *walk;
  guint length, size, n_headers, configlen;
  gchar *cstr, *configuration;
  guint8 *data, *config;
  guint32 ident;
  gboolean res;

  GST_DEBUG_OBJECT (rtpvorbispay, "finish headers");

  if (!rtpvorbispay->headers)
    goto no_headers;

  /* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                     Number of packed headers                  |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Packed header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          ....                                 |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * We only construct a config containing 1 packed header like this:
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                   Ident                       | length       ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              | n. of headers |    length1    |    length2   ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              |             Identification Header            ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..              |         Comment Header                       ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                        Comment Header                        |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Setup Header                        ..
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * .................................................................
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * ..                         Setup Header                         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */

  /* we need 4 bytes for the number of headers (which is always 1), 3 bytes for
   * the ident, 2 bytes for length, 1 byte for n. of headers. */
  size = 4 + 3 + 2 + 1;

  /* count the size of the headers first and update the hash */
  length = 0;
  n_headers = 0;
  ident = fnv1_hash_32_new ();
  for (walk = rtpvorbispay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);
    guint bsize;

    bsize = GST_BUFFER_SIZE (buf);
    length += bsize;
    n_headers++;

    /* count number of bytes needed for length fields, we don't need this for
     * the last header. */
    if (g_list_next (walk)) {
      do {
        size++;
        bsize >>= 7;
      } while (bsize);
    }
    /* update hash */
    ident = fnv1_hash_32_update (ident, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
  }

  /* packet length is header size + packet length */
  configlen = size + length;
  config = data = g_malloc (configlen);

  /* number of packed headers, we only pack 1 header */
  data[0] = 0;
  data[1] = 0;
  data[2] = 0;
  data[3] = 1;

  ident = fnv1_hash_32_to_24 (ident);
  rtpvorbispay->payload_ident = ident;
  GST_DEBUG_OBJECT (rtpvorbispay, "ident 0x%08x", ident);

  /* take lower 3 bytes */
  data[4] = (ident >> 16) & 0xff;
  data[5] = (ident >> 8) & 0xff;
  data[6] = ident & 0xff;

  /* store length of all vorbis headers */
  data[7] = ((length) >> 8) & 0xff;
  data[8] = (length) & 0xff;

  /* store number of headers minus one. */
  data[9] = n_headers - 1;
  data += 10;

  /* store length for each header */
  for (walk = rtpvorbispay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);
    guint bsize, size, temp;
    guint flag;

    /* only need to store the length when it's not the last header */
    if (!g_list_next (walk))
      break;

    bsize = GST_BUFFER_SIZE (buf);

    /* calc size */
    size = 0;
    do {
      size++;
      bsize >>= 7;
    } while (bsize);
    temp = size;

    bsize = GST_BUFFER_SIZE (buf);
    /* write the size backwards */
    flag = 0;
    while (size) {
      size--;
      data[size] = (bsize & 0x7f) | flag;
      bsize >>= 7;
      flag = 0x80;              /* Flag bit on all bytes of the length except the last */
    }
    data += temp;
  }

  /* copy header data */
  for (walk = rtpvorbispay->headers; walk; walk = g_list_next (walk)) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    memcpy (data, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    data += GST_BUFFER_SIZE (buf);
  }

  /* serialize to base64 */
  configuration = g_base64_encode (config, configlen);
  g_free (config);

  /* configure payloader settings */
  cstr = g_strdup_printf ("%d", rtpvorbispay->channels);
  gst_basertppayload_set_options (basepayload, "audio", TRUE, "VORBIS",
      rtpvorbispay->rate);
  res =
      gst_basertppayload_set_outcaps (basepayload, "encoding-params",
      G_TYPE_STRING, cstr, "configuration", G_TYPE_STRING, configuration, NULL);
  g_free (cstr);
  g_free (configuration);

  return res;

  /* ERRORS */
no_headers:
  {
    GST_DEBUG_OBJECT (rtpvorbispay, "finish headers");
    return FALSE;
  }
}

static gboolean
gst_rtp_vorbis_pay_parse_id (GstBaseRTPPayload * basepayload, guint8 * data,
    guint size)
{
  GstRtpVorbisPay *rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);
  guint8 channels;
  gint32 rate, version;

  if (G_UNLIKELY (size < 16))
    goto too_short;

  if (G_UNLIKELY (memcmp (data, "\001vorbis", 7)))
    goto invalid_start;
  data += 7;

  if (G_UNLIKELY ((version = GST_READ_UINT32_LE (data)) != 0))
    goto invalid_version;
  data += 4;

  if (G_UNLIKELY ((channels = *data++) < 1))
    goto invalid_channels;

  if (G_UNLIKELY ((rate = GST_READ_UINT32_LE (data)) < 1))
    goto invalid_rate;

  /* all fine, store the values */
  rtpvorbispay->channels = channels;
  rtpvorbispay->rate = rate;

  return TRUE;

  /* ERRORS */
too_short:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Identification packet is too short, need at least 16, got %d", size),
        (NULL));
    return FALSE;
  }
invalid_start:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid header start in identification packet"), (NULL));
    return FALSE;
  }
invalid_version:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid version, expected 0, got %d", version), (NULL));
    return FALSE;
  }
invalid_rate:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid rate %d", rate), (NULL));
    return FALSE;
  }
invalid_channels:
  {
    GST_ELEMENT_ERROR (basepayload, STREAM, DECODE,
        ("Invalid channels %d", channels), (NULL));
    return FALSE;
  }
}

static GstFlowReturn
gst_rtp_vorbis_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpVorbisPay *rtpvorbispay;
  GstFlowReturn ret;
  guint size, newsize;
  guint8 *data;
  guint packet_len;
  GstClockTime duration, newduration, timestamp;
  gboolean flush;
  guint8 VDT;
  guint plen;
  guint8 *ppos, *payload;
  gboolean fragmented;

  rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  GST_LOG_OBJECT (rtpvorbispay, "size %u, duration %" GST_TIME_FORMAT,
      size, GST_TIME_ARGS (duration));

  if (G_UNLIKELY (size < 1 || size > 0xffff))
    goto wrong_size;

  /* find packet type */
  if (data[0] & 1) {
    /* header */
    if (data[0] == 1) {
      /* identification, we need to parse this in order to get the clock rate. */
      if (G_UNLIKELY (!gst_rtp_vorbis_pay_parse_id (basepayload, data, size)))
        goto parse_id_failed;
      VDT = 1;
    } else if (data[0] == 3) {
      /* comment */
      VDT = 2;
    } else if (data[0] == 5) {
      /* setup */
      VDT = 1;
    } else
      goto unknown_header;
  } else
    /* data */
    VDT = 0;

  if (rtpvorbispay->need_headers) {
    /* we need to collect the headers and construct a config string from them */
    if (VDT != 0) {
      GST_DEBUG_OBJECT (rtpvorbispay, "collecting header");
      /* append header to the list of headers */
      rtpvorbispay->headers = g_list_append (rtpvorbispay->headers, buffer);
      ret = GST_FLOW_OK;
      goto done;
    } else {
      if (!gst_rtp_vorbis_pay_finish_headers (basepayload))
        goto header_error;
      rtpvorbispay->need_headers = FALSE;
    }
  }

  /* size increases with packet length and 2 bytes size eader. */
  newduration = rtpvorbispay->payload_duration;
  if (duration != GST_CLOCK_TIME_NONE)
    newduration += duration;

  newsize = rtpvorbispay->payload_pos + 2 + size;
  packet_len = gst_rtp_buffer_calc_packet_len (newsize, 0, 0);

  /* check buffer filled against length and max latency */
  flush = gst_basertppayload_is_filled (basepayload, packet_len, newduration);
  /* we can store up to 15 vorbis packets in one RTP packet. */
  flush |= (rtpvorbispay->payload_pkts == 15);
  /* flush if we have a new VDT */
  if (rtpvorbispay->packet)
    flush |= (rtpvorbispay->payload_VDT != VDT);
  if (flush)
    gst_rtp_vorbis_pay_flush_packet (rtpvorbispay);

  /* create new packet if we must */
  if (!rtpvorbispay->packet) {
    gst_rtp_vorbis_pay_init_packet (rtpvorbispay, VDT, timestamp);
  }

  payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
  ppos = payload + rtpvorbispay->payload_pos;
  fragmented = FALSE;

  ret = GST_FLOW_OK;

  /* put buffer in packet, it either fits completely or needs to be fragmented
   * over multiple RTP packets. */
  while (size) {
    plen = MIN (rtpvorbispay->payload_left - 2, size);

    GST_LOG_OBJECT (rtpvorbispay, "append %u bytes", plen);

    /* data is copied in the payload with a 2 byte length header */
    ppos[0] = (plen >> 8) & 0xff;
    ppos[1] = (plen & 0xff);
    memcpy (&ppos[2], data, plen);

    size -= plen;
    data += plen;

    rtpvorbispay->payload_pos += plen + 2;
    rtpvorbispay->payload_left -= plen + 2;

    if (fragmented) {
      if (size == 0)
        /* last fragment, set F to 0x3. */
        rtpvorbispay->payload_F = 0x3;
      else
        /* fragment continues, set F to 0x2. */
        rtpvorbispay->payload_F = 0x2;
    } else {
      if (size > 0) {
        /* fragmented packet starts, set F to 0x1, mark ourselves as
         * fragmented. */
        rtpvorbispay->payload_F = 0x1;
        fragmented = TRUE;
      }
    }
    if (fragmented) {
      /* fragmented packets are always flushed and have ptks of 0 */
      rtpvorbispay->payload_pkts = 0;
      ret = gst_rtp_vorbis_pay_flush_packet (rtpvorbispay);

      if (size > 0) {
        /* start new packet and get pointers. VDT stays the same. */
        gst_rtp_vorbis_pay_init_packet (rtpvorbispay,
            rtpvorbispay->payload_VDT, timestamp);
        payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
        ppos = payload + rtpvorbispay->payload_pos;
      }
    } else {
      /* unfragmented packet, update stats for next packet, size == 0 and we
       * exit the while loop */
      rtpvorbispay->payload_pkts++;
      if (duration != GST_CLOCK_TIME_NONE)
        rtpvorbispay->payload_duration += duration;
    }
  }
  gst_buffer_unref (buffer);

done:
  return ret;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_WARNING (rtpvorbispay, STREAM, DECODE,
        ("Invalid packet size (1 < %d <= 0xffff)", size), (NULL));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
parse_id_failed:
  {
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
unknown_header:
  {
    GST_ELEMENT_WARNING (rtpvorbispay, STREAM, DECODE,
        (NULL), ("Ignoring unknown header received"));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
header_error:
  {
    GST_ELEMENT_WARNING (rtpvorbispay, STREAM, DECODE,
        (NULL), ("Error initializing header config"));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static gboolean
gst_rtp_vorbis_pay_handle_event (GstPad * pad, GstEvent * event)
{
  GstRtpVorbisPay *rtpvorbispay = GST_RTP_VORBIS_PAY (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_rtp_vorbis_pay_clear_packet (rtpvorbispay);
      break;
    default:
      break;
  }
  /* false to let parent handle event as well */
  return FALSE;
}

static GstStateChangeReturn
gst_rtp_vorbis_pay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpVorbisPay *rtpvorbispay;
  GstStateChangeReturn ret;

  rtpvorbispay = GST_RTP_VORBIS_PAY (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtp_vorbis_pay_cleanup (rtpvorbispay);
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_vorbis_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvorbispay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_VORBIS_PAY);
}

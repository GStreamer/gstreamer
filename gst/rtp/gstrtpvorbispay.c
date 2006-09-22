/* GStreamer
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#include "gstrtpvorbispay.h"

GST_DEBUG_CATEGORY_STATIC (rtpvorbispay_debug);
#define GST_CAT_DEFAULT (rtpvorbispay_debug)

/* references:
 * http://svn.xiph.org/trunk/vorbis/doc/draft-ietf-avt-rtp-vorbis-01.txt
 */

/* elementfactory information */
static const GstElementDetails gst_rtp_vorbispay_details =
GST_ELEMENT_DETAILS ("RTP packet parser",
    "Codec/Payloader/Network",
    "Payload-encode Vorbis audio into RTP packets (draft-01 RFC XXXX)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate gst_rtp_vorbis_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"vorbis\""
        /* All required parameters
         *
         * "encoding-params = (string) <num channels>"
         * "delivery-method = (string) { inline, in_band, out_band/<specific_name> } "
         * "configuration = (string) ANY"
         */
        /* All optional parameters
         *
         * "configuration-uri ="
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
static GstFlowReturn gst_rtp_vorbis_pay_handle_buffer (GstBaseRTPPayload * pad,
    GstBuffer * buffer);

static void
gst_rtp_vorbis_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vorbis_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_vorbis_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_vorbispay_details);
}

static void
gst_rtp_vorbis_pay_class_init (GstRtpVorbisPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertppayload_class->set_caps = gst_rtp_vorbis_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_vorbis_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpvorbispay_debug, "rtpvorbispay", 0,
      "Vorbis RTP Payloader");
}

static void
gst_rtp_vorbis_pay_init (GstRtpVorbisPay * rtpvorbispay,
    GstRtpVorbisPayClass * klass)
{
}

static gboolean
gst_rtp_vorbis_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpVorbisPay *rtpvorbispay;

  rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);

  gst_basertppayload_set_options (basepayload, "audio", TRUE, "vorbis", 8000);
  gst_basertppayload_set_outcaps (basepayload,
      "encoding-params", G_TYPE_STRING, "1",
      /* don't set the defaults 
       */
      NULL);

  return TRUE;
}

static void
gst_rtp_vorbis_pay_init_packet (GstRtpVorbisPay * rtpvorbispay)
{
  guint payload_len;

  if (rtpvorbispay->packet)
    gst_buffer_unref (rtpvorbispay->packet);

  GST_DEBUG_OBJECT (rtpvorbispay, "starting new packet");

  /* new packet allocate max packet size */
  rtpvorbispay->packet =
      gst_rtp_buffer_new_allocate_len (GST_BASE_RTP_PAYLOAD_MTU
      (rtpvorbispay), 0, 0);
  rtpvorbispay->payload_pos = 4;
  payload_len = gst_rtp_buffer_get_payload_len (rtpvorbispay->packet);
  rtpvorbispay->payload_left = payload_len - 4;
  rtpvorbispay->payload_duration = 0;
  rtpvorbispay->payload_ident = 0;
  rtpvorbispay->payload_F = 0;
  rtpvorbispay->payload_VDT = 0;
  rtpvorbispay->payload_pkts = 0;
}

static GstFlowReturn
gst_rtp_vorbis_pay_flush_packet (GstRtpVorbisPay * rtpvorbispay)
{
  GstFlowReturn ret;
  guint8 *payload;
  guint hlen;

  /* check for empty packet */
  if (!rtpvorbispay || rtpvorbispay->payload_pos <= 4)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (rtpvorbispay, "flushing packet");

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

  /* push, this gives away our ref to the packet, so clear it. */
  ret =
      gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpvorbispay),
      rtpvorbispay->packet);
  rtpvorbispay->packet = NULL;

  /* prepare new packet */
  gst_rtp_vorbis_pay_init_packet (rtpvorbispay);

  return ret;
}

static GstFlowReturn
gst_rtp_vorbis_pay_append_buffer (GstRtpVorbisPay * rtpvorbispay,
    GstBuffer * buffer)
{
  GstFlowReturn res;
  guint size;
  GstClockTime duration;
  guint plen;
  guint8 *ppos, *payload, *data;
  gboolean fragmented;

  res = GST_FLOW_OK;

  if (rtpvorbispay->payload_left < 2)
    return res;

  size = GST_BUFFER_SIZE (buffer);
  /* skip packets that are too big */
  if (size > 0xffff)
    return res;

  data = GST_BUFFER_DATA (buffer);
  duration = GST_BUFFER_DURATION (buffer);
  payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
  ppos = payload + rtpvorbispay->payload_pos;
  fragmented = FALSE;

  while (size) {
    plen = MIN (rtpvorbispay->payload_left - 2, size);

    GST_DEBUG_OBJECT (rtpvorbispay, "append %u bytes", plen);

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
      if (size == 0) {
        /* unfragmented packet, update stats for next packet */
        rtpvorbispay->payload_pkts++;
        if (duration != GST_CLOCK_TIME_NONE)
          rtpvorbispay->payload_duration += duration;
      } else {
        /* fragmented packet starts, set F to 0x1, mark ourselves as
         * fragmented. */
        rtpvorbispay->payload_F = 0x1;
        fragmented = TRUE;
      }
    }
    if (fragmented) {
      /* fragmented packets are always flushed and have ptks of 0 */
      rtpvorbispay->payload_pkts = 0;
      res = gst_rtp_vorbis_pay_flush_packet (rtpvorbispay);
      /* get new pointers */
      payload = gst_rtp_buffer_get_payload (rtpvorbispay->packet);
      ppos = payload + rtpvorbispay->payload_pos;
    }
  }

  return res;
}

static GstFlowReturn
gst_rtp_vorbis_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpVorbisPay *rtpvorbispay;
  GstFlowReturn ret;
  guint size, newsize;
  guint packet_len;
  GstClockTime duration, newduration;
  gboolean flush;

  rtpvorbispay = GST_RTP_VORBIS_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  GST_DEBUG_OBJECT (rtpvorbispay, "size %u, duration %" GST_TIME_FORMAT,
      size, GST_TIME_ARGS (duration));

  if (!rtpvorbispay->packet)
    gst_rtp_vorbis_pay_init_packet (rtpvorbispay);

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

  if (flush)
    ret = gst_rtp_vorbis_pay_flush_packet (rtpvorbispay);

  /* put buffer in packet */
  ret = gst_rtp_vorbis_pay_append_buffer (rtpvorbispay, buffer);

  return ret;
}

gboolean
gst_rtp_vorbis_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpvorbispay",
      GST_RANK_NONE, GST_TYPE_RTP_VORBIS_PAY);
}

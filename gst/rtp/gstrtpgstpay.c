/* GStreamer
 * Copyright (C) <2010> Wim Taymans <wim.taymans@gmail.com>
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

#include "gstrtpgstpay.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_pay_debug

/*
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |C| CV  |D|0|0|0|                  MBZ                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Frag_offset                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * C: caps inlined flag 
 *   When C set, first part of payload contains caps definition. Caps definition
 *   starts with variable-length length prefix and then a string of that length.
 *   the length is encoded in big endian 7 bit chunks, the top 1 bit of a byte
 *   is the continuation marker and the 7 next bits the data. A continuation
 *   marker of 1 means that the next byte contains more data. 
 *
 * CV: caps version, 0 = caps from SDP, 1 - 7 inlined caps
 * D: delta unit buffer
 *
 *
 */

static GstStaticPadTemplate gst_rtp_gst_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtp_gst_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"application\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"X-GST\"")
    );

static void gst_rtp_gst_pay_finalize (GObject * obj);

static gboolean gst_rtp_gst_pay_setcaps (GstRTPBasePayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_gst_pay_handle_buffer (GstRTPBasePayload * payload,
    GstBuffer * buffer);

#define gst_rtp_gst_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpGSTPay, gst_rtp_gst_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void
gst_rtp_gst_pay_class_init (GstRtpGSTPayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->finalize = gst_rtp_gst_pay_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_gst_pay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_gst_pay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP GStreamer payloader", "Codec/Payloader/Network/RTP",
      "Payload GStreamer buffers as RTP packets",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstrtpbasepayload_class->set_caps = gst_rtp_gst_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_gst_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_pay_debug, "rtpgstpay", 0,
      "rtpgstpay element");
}

static void
gst_rtp_gst_pay_init (GstRtpGSTPay * rtpgstpay)
{
}

static void
gst_rtp_gst_pay_finalize (GObject * obj)
{
  GstRtpGSTPay *rtpgstpay;

  rtpgstpay = GST_RTP_GST_PAY (obj);

  g_free (rtpgstpay->capsstr);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static gboolean
gst_rtp_gst_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  GstRtpGSTPay *rtpgstpay;
  gboolean res;
  gchar *capsenc, *capsver;

  rtpgstpay = GST_RTP_GST_PAY (payload);

  g_free (rtpgstpay->capsstr);
  rtpgstpay->capsstr = gst_caps_to_string (caps);
  rtpgstpay->capslen = strlen (rtpgstpay->capsstr);
  rtpgstpay->current_CV = rtpgstpay->next_CV;

  /* encode without 0 byte */
  capsenc = g_base64_encode ((guchar *) rtpgstpay->capsstr, rtpgstpay->capslen);
  GST_DEBUG_OBJECT (payload, "caps=%s, caps(base64)=%s",
      rtpgstpay->capsstr, capsenc);
  /* for 0 byte */
  rtpgstpay->capslen++;

  capsver = g_strdup_printf ("%d", rtpgstpay->current_CV);

  gst_rtp_base_payload_set_options (payload, "application", TRUE, "X-GST",
      90000);
  res =
      gst_rtp_base_payload_set_outcaps (payload, "caps", G_TYPE_STRING, capsenc,
      "capsversion", G_TYPE_STRING, capsver, NULL);
  g_free (capsenc);
  g_free (capsver);

  return res;
}

static GstFlowReturn
gst_rtp_gst_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpGSTPay *rtpgstpay;
  GstMapInfo map;
  guint8 *ptr;
  gsize left;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  GstClockTime timestamp;
  guint32 frag_offset;
  guint flags;
  gchar *capsstr;
  guint capslen;
  guint capslen_prefix_len;

  rtpgstpay = GST_RTP_GST_PAY (basepayload);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  ret = GST_FLOW_OK;

  /* caps always from SDP for now */
  flags = 0;
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    flags |= (1 << 3);

  capsstr = rtpgstpay->capsstr;
  capslen = rtpgstpay->capslen;
  if (capslen) {
    /* start of buffer, calculate length */
    capslen_prefix_len = 1;
    while (capslen >> (7 * capslen_prefix_len))
      capslen_prefix_len++;

    GST_DEBUG_OBJECT (rtpgstpay, "sending inline caps");
    rtpgstpay->next_CV++;

    flags |= (1 << 7);
  } else {
    capslen_prefix_len = 0;
  }

  flags |= (rtpgstpay->current_CV << 4);

  /*
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |C| CV  |D|X|Y|Z|                  MBZ                          |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |                          Frag_offset                          |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  frag_offset = 0;
  ptr = map.data;
  left = map.size;

  GST_DEBUG_OBJECT (basepayload, "buffer size=%u", left);

  while (left > 0) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    guint packet_len;
    GstRTPBuffer rtp = { NULL };

    /* this will be the total lenght of the packet */
    packet_len =
        gst_rtp_buffer_calc_packet_len (8 + capslen + capslen_prefix_len + left,
        0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_RTP_BASE_PAYLOAD_MTU (rtpgstpay));

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

    gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);
    payload = gst_rtp_buffer_get_payload (&rtp);

    GST_DEBUG_OBJECT (basepayload, "new packet len %u, frag %u", packet_len,
        frag_offset);

    payload[0] = flags;
    payload[1] = payload[2] = payload[3] = 0;
    payload[4] = frag_offset >> 24;
    payload[5] = frag_offset >> 16;
    payload[6] = frag_offset >> 8;
    payload[7] = frag_offset & 0xff;

    payload += 8;
    payload_len -= 8;

    if (capslen) {
      guint tocopy;

      /* we need to write caps */
      if (frag_offset == 0) {
        /* write caps length */
        while (capslen_prefix_len) {
          capslen_prefix_len--;
          *payload++ = ((capslen_prefix_len > 0) ? 0x80 : 0) |
              ((capslen >> (7 * capslen_prefix_len)) & 0x7f);
          payload_len--;
          frag_offset++;
        }
      }

      tocopy = MIN (payload_len, capslen);
      GST_DEBUG_OBJECT (basepayload, "copy %u bytes from caps to payload",
          tocopy);
      memcpy (payload, capsstr, tocopy);

      capsstr += tocopy;
      capslen -= tocopy;
      payload += tocopy;
      payload_len -= tocopy;
      frag_offset += tocopy;

      if (capslen == 0) {
        rtpgstpay->capslen = 0;
        g_free (rtpgstpay->capsstr);
        rtpgstpay->capsstr = NULL;
      }
    }

    if (capslen == 0) {
      /* no more caps, continue with data */
      GST_DEBUG_OBJECT (basepayload, "copy %u bytes from buffer to payload",
          payload_len);
      memcpy (payload, ptr, payload_len);

      ptr += payload_len;
      left -= payload_len;
      frag_offset += payload_len;
    }

    if (left == 0)
      gst_rtp_buffer_set_marker (&rtp, TRUE);

    gst_rtp_buffer_unmap (&rtp);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    ret = gst_rtp_base_payload_push (basepayload, outbuf);
  }
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  return ret;
}

gboolean
gst_rtp_gst_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgstpay",
      GST_RANK_NONE, GST_TYPE_RTP_GST_PAY);
}

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

/*
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |C| CV  |D|X|Y|Z|                  MBZ                          |
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
 * X: media 1 flag
 * Y: media 2 flag
 * Z: media 3 flag
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

static gboolean gst_rtp_gst_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_gst_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRtpGSTPay, gst_rtp_gst_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD)

     static void gst_rtp_gst_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_gst_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_gst_pay_sink_template);

  gst_element_class_set_details_simple (element_class,
      "RTP GStreamer payloader", "Codec/Payloader/Network/RTP",
      "Payload GStreamer buffers as RTP packets",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_gst_pay_class_init (GstRtpGSTPayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_gst_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_gst_pay_handle_buffer;
}

static void
gst_rtp_gst_pay_init (GstRtpGSTPay * rtpgstpay, GstRtpGSTPayClass * klass)
{
}

static gboolean
gst_rtp_gst_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;
  gchar *capsstr, *capsenc;

  capsstr = gst_caps_to_string (caps);
  capsenc = g_base64_encode ((guchar *) capsstr, strlen (capsstr));
  g_free (capsstr);

  gst_basertppayload_set_options (payload, "application", TRUE, "X-GST", 90000);
  res =
      gst_basertppayload_set_outcaps (payload, "caps", G_TYPE_STRING, capsenc,
      NULL);
  g_free (capsenc);

  return res;
}

static GstFlowReturn
gst_rtp_gst_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpGSTPay *rtpgstpay;
  guint8 *data;
  guint size;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  GstClockTime timestamp;
  guint32 frag_offset;
  guint flags;

  rtpgstpay = GST_RTP_GST_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  ret = GST_FLOW_OK;

  /* caps always from SDP for now */
  flags = 0;
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    flags |= (1 << 3);
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MEDIA1))
    flags |= (1 << 2);
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MEDIA2))
    flags |= (1 << 1);
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_MEDIA3))
    flags |= (1 << 0);

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

  while (size > 0) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (8 + size, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpgstpay));

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);
    payload = gst_rtp_buffer_get_payload (outbuf);

    payload[0] = flags;
    payload[1] = payload[2] = payload[3] = 0;
    payload[4] = frag_offset >> 24;
    payload[5] = frag_offset >> 16;
    payload[6] = frag_offset >> 8;
    payload[7] = frag_offset & 0xff;

    payload += 8;
    payload_len -= 8;

    memcpy (payload, data, payload_len);

    data += payload_len;
    size -= payload_len;
    frag_offset += payload_len;

    if (size == 0)
      gst_rtp_buffer_set_marker (outbuf, TRUE);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    ret = gst_basertppayload_push (basepayload, outbuf);
  }
  gst_buffer_unref (buffer);

  return ret;
}

gboolean
gst_rtp_gst_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgstpay",
      GST_RANK_NONE, GST_TYPE_RTP_GST_PAY);
}

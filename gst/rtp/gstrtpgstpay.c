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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
 * |C| CV  |D|0|0|0|     ETYPE     |  MBZ                          |
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
 * ETYPE: type of event. Payload contains the event, prefixed with a
 *        variable length field.
 *   0 = NO event
 *   1 = GST_EVENT_TAG
 *   2 = GST_EVENT_CUSTOM_DOWNSTREAM
 *   3 = GST_EVENT_CUSTOM_BOTH
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
static gboolean gst_rtp_gst_pay_sink_event (GstRTPBasePayload * payload,
    GstEvent * event);

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
  gstrtpbasepayload_class->sink_event = gst_rtp_gst_pay_sink_event;

  GST_DEBUG_CATEGORY_INIT (gst_rtp_pay_debug, "rtpgstpay", 0,
      "rtpgstpay element");
}

static void
gst_rtp_gst_pay_init (GstRtpGSTPay * rtpgstpay)
{
  rtpgstpay->adapter = gst_adapter_new ();
  gst_rtp_base_payload_set_options (GST_RTP_BASE_PAYLOAD (rtpgstpay),
      "application", TRUE, "X-GST", 90000);
}

static void
gst_rtp_gst_pay_finalize (GObject * obj)
{
  GstRtpGSTPay *rtpgstpay;

  rtpgstpay = GST_RTP_GST_PAY (obj);

  g_object_unref (rtpgstpay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstFlowReturn
gst_rtp_gst_pay_flush (GstRtpGSTPay * rtpgstpay, GstClockTime timestamp)
{
  GstFlowReturn ret;
  guint avail;
  guint frag_offset;
  GstBufferList *list;

  frag_offset = 0;
  avail = gst_adapter_available (rtpgstpay->adapter);
  if (avail == 0)
    return GST_FLOW_OK;

  list = gst_buffer_list_new ();

  while (avail) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    guint packet_len;
    GstBuffer *outbuf;
    GstRTPBuffer rtp = { NULL };
    GstBuffer *paybuf;


    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (8 + avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_RTP_BASE_PAYLOAD_MTU (rtpgstpay));

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the header */
    outbuf = gst_rtp_buffer_new_allocate (8, 0, 0);

    gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);
    payload = gst_rtp_buffer_get_payload (&rtp);

    GST_DEBUG_OBJECT (rtpgstpay, "new packet len %u, frag %u", packet_len,
        frag_offset);

    /*
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |C| CV  |D|0|0|0|     ETYPE     |  MBZ                          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |                          Frag_offset                          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    payload[0] = rtpgstpay->flags;
    payload[1] = rtpgstpay->etype;
    payload[2] = payload[3] = 0;
    payload[4] = frag_offset >> 24;
    payload[5] = frag_offset >> 16;
    payload[6] = frag_offset >> 8;
    payload[7] = frag_offset & 0xff;

    payload += 8;
    payload_len -= 8;

    frag_offset += payload_len;
    avail -= payload_len;

    if (avail == 0)
      gst_rtp_buffer_set_marker (&rtp, TRUE);

    gst_rtp_buffer_unmap (&rtp);

    /* create a new buf to hold the payload */
    GST_DEBUG_OBJECT (rtpgstpay, "take %u bytes from adapter", payload_len);
    paybuf = gst_adapter_take_buffer (rtpgstpay->adapter, payload_len);

    /* create a new group to hold the rtp header and the payload */
    gst_buffer_append (outbuf, paybuf);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

    /* and add to list */
    gst_buffer_list_insert (list, -1, outbuf);
  }
  /* push the whole buffer list at once */
  ret = gst_rtp_base_payload_push_list (GST_RTP_BASE_PAYLOAD (rtpgstpay), list);

  rtpgstpay->flags &= 0x70;
  rtpgstpay->etype = 0;

  return ret;
}

static GstBuffer *
make_data_buffer (GstRtpGSTPay * rtpgstpay, gchar * data, guint size)
{
  guint plen;
  guint8 *ptr;
  GstBuffer *outbuf;
  GstMapInfo map;

  /* calculate length */
  plen = 1;
  while (size >> (7 * plen))
    plen++;

  outbuf = gst_buffer_new_allocate (NULL, plen + size, NULL);

  gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
  ptr = map.data;

  /* write length */
  while (plen) {
    plen--;
    *ptr++ = ((plen > 0) ? 0x80 : 0) | ((size >> (7 * plen)) & 0x7f);
  }
  /* copy data */
  memcpy (ptr, data, size);
  gst_buffer_unmap (outbuf, &map);

  return outbuf;
}

static gboolean
gst_rtp_gst_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  GstRtpGSTPay *rtpgstpay;
  gboolean res;
  gchar *capsstr, *capsenc, *capsver;
  guint capslen;
  GstBuffer *outbuf;

  rtpgstpay = GST_RTP_GST_PAY (payload);

  capsstr = gst_caps_to_string (caps);
  capslen = strlen (capsstr);

  rtpgstpay->current_CV = rtpgstpay->next_CV;

  /* encode without 0 byte */
  capsenc = g_base64_encode ((guchar *) capsstr, capslen);
  GST_DEBUG_OBJECT (payload, "caps=%s, caps(base64)=%s", capsstr, capsenc);
  /* for 0 byte */
  capslen++;

  /* make a data buffer of it */
  outbuf = make_data_buffer (rtpgstpay, capsstr, capslen);
  g_free (capsstr);

  /* store in adapter, we don't flush yet, buffer might follow */
  rtpgstpay->flags = (1 << 7) | (rtpgstpay->current_CV << 4);
  rtpgstpay->next_CV = (rtpgstpay->next_CV + 1) & 0x7;
  gst_adapter_push (rtpgstpay->adapter, outbuf);

  /* make caps for SDP */
  capsver = g_strdup_printf ("%d", rtpgstpay->current_CV);
  res =
      gst_rtp_base_payload_set_outcaps (payload, "caps", G_TYPE_STRING, capsenc,
      "capsversion", G_TYPE_STRING, capsver, NULL);
  g_free (capsenc);
  g_free (capsver);

  return res;
}

static gboolean
gst_rtp_gst_pay_sink_event (GstRTPBasePayload * payload, GstEvent * event)
{
  gboolean ret;
  GstRtpGSTPay *rtpgstpay;
  guint etype;

  rtpgstpay = GST_RTP_GST_PAY (payload);

  ret =
      GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->sink_event (payload,
      gst_event_ref (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      etype = 1;
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      etype = 2;
      break;
    case GST_EVENT_CUSTOM_BOTH:
      etype = 2;
      break;
    default:
      etype = 0;
      GST_LOG_OBJECT (rtpgstpay, "no event for %s",
          GST_EVENT_TYPE_NAME (event));
      break;
  }
  if (etype) {
    const GstStructure *s;
    gchar *estr;
    guint elen;
    GstBuffer *outbuf;

    /* make sure the adapter is flushed */
    gst_rtp_gst_pay_flush (rtpgstpay, GST_CLOCK_TIME_NONE);

    GST_DEBUG_OBJECT (rtpgstpay, "make event type %d for %s",
        etype, GST_EVENT_TYPE_NAME (event));
    s = gst_event_get_structure (event);

    estr = gst_structure_to_string (s);
    elen = strlen (estr);
    outbuf = make_data_buffer (rtpgstpay, estr, elen);
    g_free (estr);

    rtpgstpay->etype = etype;
    gst_adapter_push (rtpgstpay->adapter, outbuf);
    /* flush the adapter immediately */
    gst_rtp_gst_pay_flush (rtpgstpay, GST_CLOCK_TIME_NONE);
  }

  gst_event_unref (event);

  return ret;
}

static GstFlowReturn
gst_rtp_gst_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstRtpGSTPay *rtpgstpay;
  GstClockTime timestamp;

  rtpgstpay = GST_RTP_GST_PAY (basepayload);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  /* caps always from SDP for now */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT))
    rtpgstpay->flags |= (1 << 3);

  gst_adapter_push (rtpgstpay->adapter, buffer);
  ret = gst_rtp_gst_pay_flush (rtpgstpay, timestamp);

  return ret;
}

gboolean
gst_rtp_gst_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpgstpay",
      GST_RANK_NONE, GST_TYPE_RTP_GST_PAY);
}

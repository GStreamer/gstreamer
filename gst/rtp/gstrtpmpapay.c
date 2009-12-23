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

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpmpapay.h"

/* elementfactory information */
static const GstElementDetails gst_rtp_mpapay_details =
GST_ELEMENT_DETAILS ("RTP MPEG audio payloader",
    "Codec/Payloader/Network",
    "Payload MPEG audio as RTP packets (RFC 2038)",
    "Wim Taymans <wim.taymans@gmail.com>");

static GstStaticPadTemplate gst_rtp_mpa_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1")
    );

static GstStaticPadTemplate gst_rtp_mpa_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_MPA_STRING ", "
        "clock-rate = (int) 90000; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPA\"")
    );

static void gst_rtp_mpa_pay_finalize (GObject * object);

static gboolean gst_rtp_mpa_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_mpa_pay_handle_buffer (GstBaseRTPPayload * payload,
    GstBuffer * buffer);

GST_BOILERPLATE (GstRtpMPAPay, gst_rtp_mpa_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD)

     static void gst_rtp_mpa_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpa_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpa_pay_sink_template));

  gst_element_class_set_details (element_class, &gst_rtp_mpapay_details);
}

static void
gst_rtp_mpa_pay_class_init (GstRtpMPAPayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_mpa_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_mpa_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_mpa_pay_handle_buffer;
}

static void
gst_rtp_mpa_pay_init (GstRtpMPAPay * rtpmpapay, GstRtpMPAPayClass * klass)
{
  rtpmpapay->adapter = gst_adapter_new ();
}

static void
gst_rtp_mpa_pay_finalize (GObject * object)
{
  GstRtpMPAPay *rtpmpapay;

  rtpmpapay = GST_RTP_MPA_PAY (object);

  g_object_unref (rtpmpapay->adapter);
  rtpmpapay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_mpa_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;

  gst_basertppayload_set_options (payload, "audio", TRUE, "MPA", 90000);
  res = gst_basertppayload_set_outcaps (payload, NULL);

  return res;
}

static GstFlowReturn
gst_rtp_mpa_pay_flush (GstRtpMPAPay * rtpmpapay)
{
  guint avail;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint16 frag_offset;

  /* the data available in the adapter is either smaller
   * than the MTU or bigger. In the case it is smaller, the complete
   * adapter contents can be put in one packet. In the case the
   * adapter has more than one MTU, we need to split the MPA data
   * over multiple packets. The frag_offset in each packet header
   * needs to be updated with the position in the MPA frame. */
  avail = gst_adapter_available (rtpmpapay->adapter);

  ret = GST_FLOW_OK;

  frag_offset = 0;
  while (avail > 0) {
    guint towrite;
    guint8 *payload;
    guint payload_len;
    guint packet_len;

    /* this will be the total lenght of the packet */
    packet_len = gst_rtp_buffer_calc_packet_len (4 + avail, 0, 0);

    /* fill one MTU or all available bytes */
    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpmpapay));

    /* this is the payload length */
    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 0, 0);

    /* create buffer to hold the payload */
    outbuf = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

    payload_len -= 4;

    gst_rtp_buffer_set_payload_type (outbuf, GST_RTP_PAYLOAD_MPA);

    /*
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |             MBZ               |          Frag_offset          |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     */
    payload = gst_rtp_buffer_get_payload (outbuf);
    payload[0] = 0;
    payload[1] = 0;
    payload[2] = frag_offset >> 8;
    payload[3] = frag_offset & 0xff;

    gst_adapter_copy (rtpmpapay->adapter, &payload[4], 0, payload_len);
    gst_adapter_flush (rtpmpapay->adapter, payload_len);

    avail -= payload_len;
    frag_offset += payload_len;

    if (avail == 0)
      gst_rtp_buffer_set_marker (outbuf, TRUE);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmpapay->first_ts;
    GST_BUFFER_DURATION (outbuf) = rtpmpapay->duration;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpmpapay), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtp_mpa_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpMPAPay *rtpmpapay;
  GstFlowReturn ret;
  guint size, avail;
  guint packet_len;
  GstClockTime duration;

  rtpmpapay = GST_RTP_MPA_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  avail = gst_adapter_available (rtpmpapay->adapter);
  if (avail == 0) {
    rtpmpapay->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpmpapay->duration = 0;
  }

  /* get packet length of previous data and this new data,
   * payload length includes a 4 byte header */
  packet_len = gst_rtp_buffer_calc_packet_len (4 + avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpmpapay->duration + duration)) {
    ret = gst_rtp_mpa_pay_flush (rtpmpapay);
    rtpmpapay->first_ts = GST_BUFFER_TIMESTAMP (buffer);
    rtpmpapay->duration = 0;
  } else {
    ret = GST_FLOW_OK;
  }

  gst_adapter_push (rtpmpapay->adapter, buffer);
  rtpmpapay->duration += duration;

  return ret;
}

gboolean
gst_rtp_mpa_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpapay",
      GST_RANK_NONE, GST_TYPE_RTP_MPA_PAY);
}

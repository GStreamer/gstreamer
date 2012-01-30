/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpmp2tpay.h"

static GstStaticPadTemplate gst_rtp_mp2t_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts,"
        "packetsize=(int)188," "systemstream=(boolean)true")
    );

static GstStaticPadTemplate gst_rtp_mp2t_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MP2T-ES\"")
    );

static gboolean gst_rtp_mp2t_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_mp2t_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);
static GstFlowReturn gst_rtp_mp2t_pay_flush (GstRTPMP2TPay * rtpmp2tpay);
static void gst_rtp_mp2t_pay_finalize (GObject * object);

GST_BOILERPLATE (GstRTPMP2TPay, gst_rtp_mp2t_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_mp2t_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_mp2t_pay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_mp2t_pay_src_template);
  gst_element_class_set_details_simple (element_class,
      "RTP MPEG2 Transport Stream payloader", "Codec/Payloader/Network/RTP",
      "Payload-encodes MPEG2 TS into RTP packets (RFC 2250)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_mp2t_pay_class_init (GstRTPMP2TPayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_mp2t_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_mp2t_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_mp2t_pay_handle_buffer;
}

static void
gst_rtp_mp2t_pay_init (GstRTPMP2TPay * rtpmp2tpay, GstRTPMP2TPayClass * klass)
{
  GST_BASE_RTP_PAYLOAD (rtpmp2tpay)->clock_rate = 90000;
  GST_BASE_RTP_PAYLOAD_PT (rtpmp2tpay) = GST_RTP_PAYLOAD_MP2T;

  rtpmp2tpay->adapter = gst_adapter_new ();
}

static void
gst_rtp_mp2t_pay_finalize (GObject * object)
{
  GstRTPMP2TPay *rtpmp2tpay;

  rtpmp2tpay = GST_RTP_MP2T_PAY (object);

  g_object_unref (rtpmp2tpay->adapter);
  rtpmp2tpay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_mp2t_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;

  gst_basertppayload_set_options (payload, "video", TRUE, "MP2T-ES", 90000);
  res = gst_basertppayload_set_outcaps (payload, NULL);

  return res;
}

static GstFlowReturn
gst_rtp_mp2t_pay_flush (GstRTPMP2TPay * rtpmp2tpay)
{
  guint avail;
  guint8 *payload;
  GstFlowReturn ret;
  GstBuffer *outbuf;

  avail = gst_adapter_available (rtpmp2tpay->adapter);
  if (avail == 0)
    return GST_FLOW_OK;
  outbuf = gst_rtp_buffer_new_allocate (avail, 0, 0);

  /* get payload */
  payload = gst_rtp_buffer_get_payload (outbuf);

  /* copy stuff from adapter to payload */
  gst_adapter_copy (rtpmp2tpay->adapter, payload, 0, avail);

  GST_BUFFER_TIMESTAMP (outbuf) = rtpmp2tpay->first_ts;
  GST_BUFFER_DURATION (outbuf) = rtpmp2tpay->duration;

  GST_DEBUG_OBJECT (rtpmp2tpay, "pushing buffer of size %d",
      GST_BUFFER_SIZE (outbuf));

  ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpmp2tpay), outbuf);

  /* flush the adapter content */
  gst_adapter_flush (rtpmp2tpay->adapter, avail);

  return ret;
}

static GstFlowReturn
gst_rtp_mp2t_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRTPMP2TPay *rtpmp2tpay;
  guint size, avail, packet_len;
  GstClockTime timestamp, duration;
  GstFlowReturn ret;

  rtpmp2tpay = GST_RTP_MP2T_PAY (basepayload);

  size = GST_BUFFER_SIZE (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  ret = GST_FLOW_OK;
  avail = gst_adapter_available (rtpmp2tpay->adapter);

  /* Initialize new RTP payload */
  if (avail == 0) {
    rtpmp2tpay->first_ts = timestamp;
    rtpmp2tpay->duration = duration;
  }

  /* get packet length of previous data and this new data, 
   * payload length includes a 4 byte header */
  packet_len = gst_rtp_buffer_calc_packet_len (4 + avail + size, 0, 0);

  /* if this buffer is going to overflow the packet, flush what we
   * have. */
  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpmp2tpay->duration + duration)) {
    ret = gst_rtp_mp2t_pay_flush (rtpmp2tpay);
    rtpmp2tpay->first_ts = timestamp;
    rtpmp2tpay->duration = duration;

    /* keep filling the payload */
  } else {
    if (GST_CLOCK_TIME_IS_VALID (duration))
      rtpmp2tpay->duration += duration;
  }

  /* copy buffer to adapter */
  gst_adapter_push (rtpmp2tpay->adapter, buffer);

  return ret;

}

gboolean
gst_rtp_mp2t_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp2tpay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_MP2T_PAY);
}

/* GStreamer
 * Copyright (C) <2007> Thijs Vermeir <thijsvermeir@gmail.com>
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
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpmpvpay.h"

static GstStaticPadTemplate gst_rtp_mpv_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 2, systemstream = (boolean) FALSE")
    );

static GstStaticPadTemplate gst_rtp_mpv_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_MPV_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"MPV\"")
    );

static gboolean gst_rtp_mpv_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_mpv_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);
static GstFlowReturn gst_rtp_mpv_pay_flush (GstRTPMPVPay * rtpmpvpay);
static void gst_rtp_mpv_pay_finalize (GObject * object);

GST_BOILERPLATE (GstRTPMPVPay, gst_rtp_mpv_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_mpv_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpv_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_mpv_pay_src_template));
  gst_element_class_set_details_simple (element_class,
      "RTP MPEG2 ES video payloader", "Codec/Payloader/Network",
      "Payload-encodes MPEG2 ES into RTP packets (RFC 2250)",
      "Thijs Vermeir <thijsvermeir@gmail.com>");
}

static void
gst_rtp_mpv_pay_class_init (GstRTPMPVPayClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gobject_class->finalize = gst_rtp_mpv_pay_finalize;

  gstbasertppayload_class->set_caps = gst_rtp_mpv_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_mpv_pay_handle_buffer;
}

static void
gst_rtp_mpv_pay_init (GstRTPMPVPay * rtpmpvpay, GstRTPMPVPayClass * klass)
{
  GST_BASE_RTP_PAYLOAD (rtpmpvpay)->clock_rate = 90000;
  GST_BASE_RTP_PAYLOAD_PT (rtpmpvpay) = GST_RTP_PAYLOAD_MPV;

  rtpmpvpay->adapter = gst_adapter_new ();
}

static void
gst_rtp_mpv_pay_finalize (GObject * object)
{
  GstRTPMPVPay *rtpmpvpay;

  rtpmpvpay = GST_RTP_MPV_PAY (object);

  g_object_unref (rtpmpvpay->adapter);
  rtpmpvpay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_mpv_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gst_basertppayload_set_options (payload, "video", FALSE, "MPV", 90000);
  return gst_basertppayload_set_outcaps (payload, NULL);
}

static GstFlowReturn
gst_rtp_mpv_pay_flush (GstRTPMPVPay * rtpmpvpay)
{
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint avail;

  guint8 *payload;

  avail = gst_adapter_available (rtpmpvpay->adapter);

  ret = GST_FLOW_OK;

  while (avail > 0) {
    guint towrite;
    guint packet_len;
    guint payload_len;

    packet_len = gst_rtp_buffer_calc_packet_len (avail, 4, 0);

    towrite = MIN (packet_len, GST_BASE_RTP_PAYLOAD_MTU (rtpmpvpay));

    payload_len = gst_rtp_buffer_calc_payload_len (towrite, 4, 0);

    outbuf = gst_rtp_buffer_new_allocate (payload_len, 4, 0);

    payload = gst_rtp_buffer_get_payload (outbuf);
    /* enable MPEG Video-specific header
     *
     *  0                   1                   2                   3
     *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     * |    MBZ  |T|         TR        | |N|S|B|E|  P  | | BFC | | FFC |
     * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     *                                  AN              FBV     FFV
     */

    /* fill in the MPEG Video-specific header 
     * data is set to 0x0 here
     */
    memset (payload, 0x0, 4);

    gst_adapter_copy (rtpmpvpay->adapter, payload + 4, 0, payload_len);
    gst_adapter_flush (rtpmpvpay->adapter, payload_len);

    avail -= payload_len;

    gst_rtp_buffer_set_marker (outbuf, avail == 0);

    GST_BUFFER_TIMESTAMP (outbuf) = rtpmpvpay->first_ts;

    ret = gst_basertppayload_push (GST_BASE_RTP_PAYLOAD (rtpmpvpay), outbuf);
  }

  return ret;
}

static GstFlowReturn
gst_rtp_mpv_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstRTPMPVPay *rtpmpvpay;
  guint avail, packet_len;
  GstClockTime timestamp, duration;
  GstFlowReturn ret = GST_FLOW_OK;

  rtpmpvpay = GST_RTP_MPV_PAY (basepayload);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  avail = gst_adapter_available (rtpmpvpay->adapter);

  if (duration == -1)
    duration = 0;

  /* Initialize new RTP payload */
  if (avail == 0) {
    rtpmpvpay->first_ts = timestamp;
    rtpmpvpay->duration = duration;
  }

  /* get packet length of previous data and this new data,
   * payload length includes a 4 byte MPEG video-specific header */
  packet_len = gst_rtp_buffer_calc_packet_len (avail, 4, 0);

  if (gst_basertppayload_is_filled (basepayload,
          packet_len, rtpmpvpay->duration + duration)) {
    ret = gst_rtp_mpv_pay_flush (rtpmpvpay);
    rtpmpvpay->first_ts = timestamp;
    rtpmpvpay->duration = 0;
  }

  gst_adapter_push (rtpmpvpay->adapter, buffer);

  rtpmpvpay->duration += duration;

  return ret;
}

gboolean
gst_rtp_mpv_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmpvpay",
      GST_RANK_NONE, GST_TYPE_RTP_MPV_PAY);
}

/*
 * Opus Payloader Gst Element
 *
 *   @author: Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>
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

#include "gstrtpopuspay.h"

GST_DEBUG_CATEGORY_STATIC (rtpopuspay_debug);
#define GST_CAT_DEFAULT (rtpopuspay_debug)


static GstStaticPadTemplate gst_rtp_opus_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus, multistream = (boolean) FALSE")
    );

static GstStaticPadTemplate gst_rtp_opus_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 48000, "
        "encoding-name = (string) \"X-GST-OPUS-DRAFT-SPITTKA-00\"")
    );

static gboolean gst_rtp_opus_pay_setcaps (GstRTPBasePayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_opus_pay_handle_buffer (GstRTPBasePayload *
    payload, GstBuffer * buffer);

G_DEFINE_TYPE (GstRtpOPUSPay, gst_rtp_opus_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void
gst_rtp_opus_pay_class_init (GstRtpOPUSPayClass * klass)
{
  GstRTPBasePayloadClass *gstbasertppayload_class;
  GstElementClass *element_class;

  gstbasertppayload_class = (GstRTPBasePayloadClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gstbasertppayload_class->set_caps = gst_rtp_opus_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_opus_pay_handle_buffer;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_opus_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_opus_pay_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "RTP Opus payloader",
      "Codec/Payloader/Network/RTP",
      "Puts Opus audio in RTP packets",
      "Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>");

  GST_DEBUG_CATEGORY_INIT (rtpopuspay_debug, "rtpopuspay", 0,
      "Opus RTP Payloader");
}

static void
gst_rtp_opus_pay_init (GstRtpOPUSPay * rtpopuspay)
{
}

static gboolean
gst_rtp_opus_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  gboolean res;

  gst_rtp_base_payload_set_options (payload, "audio", FALSE,
      "X-GST-OPUS-DRAFT-SPITTKA-00", 48000);
  res = gst_rtp_base_payload_set_outcaps (payload, NULL);

  return res;
}

static GstFlowReturn
gst_rtp_opus_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstBuffer *outbuf;
  GstClockTime pts, dts, duration;

  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  outbuf = gst_rtp_buffer_new_allocate (0, 0, 0);
  outbuf = gst_buffer_append (outbuf, buffer);

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;
  GST_BUFFER_DURATION (outbuf) = duration;

  /* Push out */
  return gst_rtp_base_payload_push (basepayload, outbuf);
}

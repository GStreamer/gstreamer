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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

static gboolean gst_rtp_opus_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_opus_pay_handle_buffer (GstBaseRTPPayload *
    payload, GstBuffer * buffer);

GST_BOILERPLATE (GstRtpOPUSPay, gst_rtp_opus_pay, GstBaseRTPPayload,
    GST_TYPE_BASE_RTP_PAYLOAD);

static void
gst_rtp_opus_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_opus_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_opus_pay_sink_template));

  gst_element_class_set_details_simple (element_class,
      "RTP Opus payloader",
      "Codec/Payloader/Network/RTP",
      "Puts Opus audio in RTP packets",
      "Danilo Cesar Lemes de Paula <danilo.cesar@collabora.co.uk>");
}

static void
gst_rtp_opus_pay_class_init (GstRtpOPUSPayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_opus_pay_setcaps;
  gstbasertppayload_class->handle_buffer = gst_rtp_opus_pay_handle_buffer;

  GST_DEBUG_CATEGORY_INIT (rtpopuspay_debug, "rtpopuspay", 0,
      "Opus RTP Payloader");
}

static void
gst_rtp_opus_pay_init (GstRtpOPUSPay * rtpopuspay, GstRtpOPUSPayClass * klass)
{
}

static gboolean
gst_rtp_opus_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;
  gchar *capsstr;

  capsstr = gst_caps_to_string (caps);

  gst_basertppayload_set_options (payload, "audio", FALSE,
      "X-GST-OPUS-DRAFT-SPITTKA-00", 48000);
  res =
      gst_basertppayload_set_outcaps (payload, "caps", G_TYPE_STRING, capsstr,
      NULL);
  g_free (capsstr);

  return res;
}

static GstFlowReturn
gst_rtp_opus_pay_handle_buffer (GstBaseRTPPayload * basepayload,
    GstBuffer * buffer)
{
  GstBuffer *outbuf;
  GstClockTime timestamp;

  guint size;
  guint8 *data;
  guint8 *payload;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);
  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  outbuf = gst_rtp_buffer_new_allocate (size, 0, 0);
  payload = gst_rtp_buffer_get_payload (outbuf);

  memcpy (payload, data, size);

  gst_rtp_buffer_set_marker (outbuf, FALSE);
  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  return gst_basertppayload_push (basepayload, outbuf);
}

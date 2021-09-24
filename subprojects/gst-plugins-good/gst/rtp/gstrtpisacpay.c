/* GStreamer
 * Copyright (C) 2020 Collabora Ltd.
 *  Author: Guillaume Desmottes <guillaume.desmottes@collabora.com>, Collabora Ltd.
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

/**
 * SECTION:element-rtpisacpay
 * @title: rtpisacpay
 * @short_description: iSAC RTP Payloader
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpelements.h"
#include "gstrtpisacpay.h"
#include "gstrtputils.h"

GST_DEBUG_CATEGORY_STATIC (rtpisacpay_debug);
#define GST_CAT_DEFAULT (rtpisacpay_debug)

static GstStaticPadTemplate gst_rtp_isac_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/isac, "
        "rate = (int) { 16000, 32000 }, " "channels = (int) 1")
    );

static GstStaticPadTemplate gst_rtp_isac_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate =  (int) { 16000, 32000 }, "
        "encoding-name = (string) \"ISAC\", "
        "encoding-params = (string) \"1\"")
    );

struct _GstRtpIsacPay
{
  /*< private > */
  GstRTPBasePayload parent;
};

#define gst_rtp_isac_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpIsacPay, gst_rtp_isac_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpisacpay, "rtpisacpay",
    GST_RANK_SECONDARY, GST_TYPE_RTP_ISAC_PAY, rtp_element_init (plugin));

static GstCaps *
gst_rtp_isac_pay_getcaps (GstRTPBasePayload * payload, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *otherpadcaps;
  GstCaps *caps;

  otherpadcaps = gst_pad_get_allowed_caps (payload->srcpad);
  caps = gst_pad_get_pad_template_caps (pad);

  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      GstStructure *ps;
      GstStructure *s;
      const GValue *v;

      ps = gst_caps_get_structure (otherpadcaps, 0);
      caps = gst_caps_make_writable (caps);
      s = gst_caps_get_structure (caps, 0);

      v = gst_structure_get_value (ps, "clock-rate");
      if (v)
        gst_structure_set_value (s, "rate", v);
    }
    gst_caps_unref (otherpadcaps);
  }

  if (filter) {
    GstCaps *tcaps = caps;

    caps = gst_caps_intersect_full (filter, tcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tcaps);
  }

  GST_DEBUG_OBJECT (payload, "%" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_rtp_isac_pay_setcaps (GstRTPBasePayload * payload, GstCaps * caps)
{
  GstStructure *s;
  gint rate;

  GST_DEBUG_OBJECT (payload, "%" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "rate", &rate)) {
    GST_ERROR_OBJECT (payload, "Missing 'rate' in caps");
    return FALSE;
  }

  gst_rtp_base_payload_set_options (payload, "audio", TRUE, "ISAC", rate);

  return gst_rtp_base_payload_set_outcaps (payload, NULL);
}

static GstFlowReturn
gst_rtp_isac_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstBuffer *outbuf;
  GstClockTime pts, dts, duration;

  pts = GST_BUFFER_PTS (buffer);
  dts = GST_BUFFER_DTS (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  outbuf = gst_rtp_base_payload_allocate_output_buffer (basepayload, 0, 0, 0);

  gst_rtp_copy_audio_meta (basepayload, outbuf, buffer);

  outbuf = gst_buffer_append (outbuf, buffer);

  GST_BUFFER_PTS (outbuf) = pts;
  GST_BUFFER_DTS (outbuf) = dts;
  GST_BUFFER_DURATION (outbuf) = duration;

  return gst_rtp_base_payload_push (basepayload, outbuf);
}

static void
gst_rtp_isac_pay_class_init (GstRtpIsacPayClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstRTPBasePayloadClass *payload_class = (GstRTPBasePayloadClass *) klass;

  payload_class->get_caps = gst_rtp_isac_pay_getcaps;
  payload_class->set_caps = gst_rtp_isac_pay_setcaps;
  payload_class->handle_buffer = gst_rtp_isac_pay_handle_buffer;

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_isac_pay_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_rtp_isac_pay_src_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP iSAC payloader", "Codec/Payloader/Network/RTP",
      "Payload-encodes iSAC audio into a RTP packet",
      "Guillaume Desmottes <guillaume.desmottes@collabora.com>");

  GST_DEBUG_CATEGORY_INIT (rtpisacpay_debug, "rtpisacpay", 0,
      "iSAC RTP Payloader");
}

static void
gst_rtp_isac_pay_init (GstRtpIsacPay * rtpisacpay)
{
}

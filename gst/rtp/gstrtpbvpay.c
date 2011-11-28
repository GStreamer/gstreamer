/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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
#include "gstrtpbvpay.h"

GST_DEBUG_CATEGORY_STATIC (rtpbvpay_debug);
#define GST_CAT_DEFAULT (rtpbvpay_debug)

static GstStaticPadTemplate gst_rtp_bv_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-bv, " "mode = (int) {16, 32}")
    );

static GstStaticPadTemplate gst_rtp_bv_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"BV16\";"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 16000, " "encoding-name = (string) \"BV32\"")
    );


static GstCaps *gst_rtp_bv_pay_sink_getcaps (GstBaseRTPPayload * payload,
    GstPad * pad);
static gboolean gst_rtp_bv_pay_sink_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);

GST_BOILERPLATE (GstRTPBVPay, gst_rtp_bv_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_bv_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_bv_pay_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_bv_pay_src_template);
  gst_element_class_set_details_simple (element_class, "RTP BV Payloader",
      "Codec/Payloader/Network/RTP",
      "Packetize BroadcomVoice audio streams into RTP packets (RFC 4298)",
      "Wim Taymans <wim.taymans@collabora.co.uk>");
}

static void
gst_rtp_bv_pay_class_init (GstRTPBVPayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_bv_pay_sink_setcaps;
  gstbasertppayload_class->get_caps = gst_rtp_bv_pay_sink_getcaps;

  GST_DEBUG_CATEGORY_INIT (rtpbvpay_debug, "rtpbvpay", 0,
      "BroadcomVoice audio RTP payloader");
}

static void
gst_rtp_bv_pay_init (GstRTPBVPay * rtpbvpay, GstRTPBVPayClass * klass)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtpbvpay);

  rtpbvpay->mode = -1;

  /* tell basertpaudiopayload that this is a frame based codec */
  gst_base_rtp_audio_payload_set_frame_based (basertpaudiopayload);
}

static gboolean
gst_rtp_bv_pay_sink_setcaps (GstBaseRTPPayload * basertppayload, GstCaps * caps)
{
  GstRTPBVPay *rtpbvpay;
  GstBaseRTPAudioPayload *basertpaudiopayload;
  gint mode;
  GstStructure *structure;
  const char *payload_name;

  rtpbvpay = GST_RTP_BV_PAY (basertppayload);
  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basertppayload);

  structure = gst_caps_get_structure (caps, 0);

  payload_name = gst_structure_get_name (structure);
  if (g_ascii_strcasecmp ("audio/x-bv", payload_name))
    goto wrong_caps;

  if (!gst_structure_get_int (structure, "mode", &mode))
    goto no_mode;

  if (mode != 16 && mode != 32)
    goto wrong_mode;

  if (mode == 16) {
    gst_basertppayload_set_options (basertppayload, "audio", TRUE, "BV16",
        8000);
    basertppayload->clock_rate = 8000;
  } else {
    gst_basertppayload_set_options (basertppayload, "audio", TRUE, "BV32",
        16000);
    basertppayload->clock_rate = 16000;
  }

  /* set options for this frame based audio codec */
  gst_base_rtp_audio_payload_set_frame_options (basertpaudiopayload,
      mode, mode == 16 ? 10 : 20);

  if (mode != rtpbvpay->mode && rtpbvpay->mode != -1)
    goto mode_changed;

  rtpbvpay->mode = mode;

  return TRUE;

  /* ERRORS */
wrong_caps:
  {
    GST_ERROR_OBJECT (rtpbvpay, "expected audio/x-bv, received %s",
        payload_name);
    return FALSE;
  }
no_mode:
  {
    GST_ERROR_OBJECT (rtpbvpay, "did not receive a mode");
    return FALSE;
  }
wrong_mode:
  {
    GST_ERROR_OBJECT (rtpbvpay, "mode must be 16 or 32, received %d", mode);
    return FALSE;
  }
mode_changed:
  {
    GST_ERROR_OBJECT (rtpbvpay, "Mode has changed from %d to %d! "
        "Mode cannot change while streaming", rtpbvpay->mode, mode);
    return FALSE;
  }
}

/* we return the padtemplate caps with the mode field fixated to a value if we
 * can */
static GstCaps *
gst_rtp_bv_pay_sink_getcaps (GstBaseRTPPayload * rtppayload, GstPad * pad)
{
  GstCaps *otherpadcaps;
  GstCaps *caps;

  otherpadcaps = gst_pad_get_allowed_caps (rtppayload->srcpad);
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      GstStructure *structure;
      const gchar *mode_str;
      gint mode;

      structure = gst_caps_get_structure (otherpadcaps, 0);

      /* construct mode, if we can */
      mode_str = gst_structure_get_string (structure, "encoding-name");
      if (mode_str) {
        if (!strcmp (mode_str, "BV16"))
          mode = 16;
        else if (!strcmp (mode_str, "BV32"))
          mode = 32;
        else
          mode = -1;

        if (mode == 16 || mode == 32) {
          structure = gst_caps_get_structure (caps, 0);
          gst_structure_set (structure, "mode", G_TYPE_INT, mode, NULL);
        }
      }
    }
    gst_caps_unref (otherpadcaps);
  }
  return caps;
}

gboolean
gst_rtp_bv_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpbvpay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_BV_PAY);
}

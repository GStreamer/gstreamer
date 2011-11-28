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

#include <string.h>

#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpL16pay.h"
#include "gstrtpchannels.h"

GST_DEBUG_CATEGORY_STATIC (rtpL16pay_debug);
#define GST_CAT_DEFAULT (rtpL16pay_debug)

static GstStaticPadTemplate gst_rtp_L16_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BIG_ENDIAN, "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate gst_rtp_L16_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [ 1, MAX ], "
        "encoding-name = (string) \"L16\", "
        "channels = (int) [ 1, MAX ];"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "encoding-name = (string) \"L16\", "
        "payload = (int) " GST_RTP_PAYLOAD_L16_STEREO_STRING ", "
        "clock-rate = (int) 44100;"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "encoding-name = (string) \"L16\", "
        "payload = (int) " GST_RTP_PAYLOAD_L16_MONO_STRING ", "
        "clock-rate = (int) 44100")
    );

static gboolean gst_rtp_L16_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstCaps *gst_rtp_L16_pay_getcaps (GstBaseRTPPayload * rtppayload,
    GstPad * pad);

GST_BOILERPLATE (GstRtpL16Pay, gst_rtp_L16_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_L16_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_L16_pay_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_L16_pay_sink_template);

  gst_element_class_set_details_simple (element_class, "RTP audio payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode Raw audio into RTP packets (RFC 3551)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_L16_pay_class_init (GstRtpL16PayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_L16_pay_setcaps;
  gstbasertppayload_class->get_caps = gst_rtp_L16_pay_getcaps;

  GST_DEBUG_CATEGORY_INIT (rtpL16pay_debug, "rtpL16pay", 0,
      "L16 RTP Payloader");
}

static void
gst_rtp_L16_pay_init (GstRtpL16Pay * rtpL16pay, GstRtpL16PayClass * klass)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtpL16pay);

  /* tell basertpaudiopayload that this is a sample based codec */
  gst_base_rtp_audio_payload_set_sample_based (basertpaudiopayload);
}

static gboolean
gst_rtp_L16_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpL16Pay *rtpL16pay;
  GstStructure *structure;
  gint channels, rate;
  gboolean res;
  gchar *params;
  GstAudioChannelPosition *pos;
  const GstRTPChannelOrder *order;
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);
  rtpL16pay = GST_RTP_L16_PAY (basepayload);

  structure = gst_caps_get_structure (caps, 0);

  /* first parse input caps */
  if (!gst_structure_get_int (structure, "rate", &rate))
    goto no_rate;

  if (!gst_structure_get_int (structure, "channels", &channels))
    goto no_channels;

  /* get the channel order */
  pos = gst_audio_get_channel_positions (structure);
  if (pos)
    order = gst_rtp_channels_get_by_pos (channels, pos);
  else
    order = NULL;

  gst_basertppayload_set_options (basepayload, "audio", TRUE, "L16", rate);
  params = g_strdup_printf ("%d", channels);

  if (!order && channels > 2) {
    GST_ELEMENT_WARNING (rtpL16pay, STREAM, DECODE,
        (NULL), ("Unknown channel order for %d channels", channels));
  }

  if (order && order->name) {
    res = gst_basertppayload_set_outcaps (basepayload,
        "encoding-params", G_TYPE_STRING, params, "channels", G_TYPE_INT,
        channels, "channel-order", G_TYPE_STRING, order->name, NULL);
  } else {
    res = gst_basertppayload_set_outcaps (basepayload,
        "encoding-params", G_TYPE_STRING, params, "channels", G_TYPE_INT,
        channels, NULL);
  }

  g_free (params);
  g_free (pos);

  rtpL16pay->rate = rate;
  rtpL16pay->channels = channels;

  /* octet-per-sample is 2 * channels for L16 */
  gst_base_rtp_audio_payload_set_sample_options (basertpaudiopayload,
      2 * rtpL16pay->channels);

  return res;

  /* ERRORS */
no_rate:
  {
    GST_DEBUG_OBJECT (rtpL16pay, "no rate given");
    return FALSE;
  }
no_channels:
  {
    GST_DEBUG_OBJECT (rtpL16pay, "no channels given");
    return FALSE;
  }
}

static GstCaps *
gst_rtp_L16_pay_getcaps (GstBaseRTPPayload * rtppayload, GstPad * pad)
{
  GstCaps *otherpadcaps;
  GstCaps *caps;

  otherpadcaps = gst_pad_get_allowed_caps (rtppayload->srcpad);
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      GstStructure *structure;
      gint channels;
      gint pt;
      gint rate;

      structure = gst_caps_get_structure (otherpadcaps, 0);

      if (gst_structure_get_int (structure, "channels", &channels)) {
        gst_caps_set_simple (caps, "channels", G_TYPE_INT, channels, NULL);
      } else if (gst_structure_get_int (structure, "payload", &pt)) {
        if (pt == 10)
          gst_caps_set_simple (caps, "channels", G_TYPE_INT, 2, NULL);
        else if (pt == 11)
          gst_caps_set_simple (caps, "channels", G_TYPE_INT, 1, NULL);
      }

      if (gst_structure_get_int (structure, "clock-rate", &rate)) {
        gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
      } else if (gst_structure_get_int (structure, "payload", &pt)) {
        if (pt == 10 || pt == 11)
          gst_caps_set_simple (caps, "rate", G_TYPE_INT, 44100, NULL);
      }

    }
    gst_caps_unref (otherpadcaps);
  }
  return caps;
}

gboolean
gst_rtp_L16_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16pay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_L16_PAY);
}

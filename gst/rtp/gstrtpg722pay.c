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

#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpg722pay.h"
#include "gstrtpchannels.h"

GST_DEBUG_CATEGORY_STATIC (rtpg722pay_debug);
#define GST_CAT_DEFAULT (rtpg722pay_debug)

static GstStaticPadTemplate gst_rtp_g722_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/G722, " "rate = (int) 16000, " "channels = (int) 1")
    );

static GstStaticPadTemplate gst_rtp_g722_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "encoding-name = (string) \"G722\", "
        "payload = (int) " GST_RTP_PAYLOAD_G722_STRING ", "
        "clock-rate = (int) 8000")
    );

static gboolean gst_rtp_g722_pay_setcaps (GstBaseRTPPayload * basepayload,
    GstCaps * caps);
static GstCaps *gst_rtp_g722_pay_getcaps (GstBaseRTPPayload * rtppayload,
    GstPad * pad);

GST_BOILERPLATE (GstRtpG722Pay, gst_rtp_g722_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_g722_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g722_pay_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g722_pay_sink_template));

  gst_element_class_set_details_simple (element_class, "RTP audio payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode Raw audio into RTP packets (RFC 3551)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_rtp_g722_pay_class_init (GstRtpG722PayClass * klass)
{
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  gstbasertppayload_class->set_caps = gst_rtp_g722_pay_setcaps;
  gstbasertppayload_class->get_caps = gst_rtp_g722_pay_getcaps;

  GST_DEBUG_CATEGORY_INIT (rtpg722pay_debug, "rtpg722pay", 0,
      "G722 RTP Payloader");
}

static void
gst_rtp_g722_pay_init (GstRtpG722Pay * rtpg722pay, GstRtpG722PayClass * klass)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtpg722pay);

  /* tell basertpaudiopayload that this is a sample based codec */
  gst_base_rtp_audio_payload_set_sample_based (basertpaudiopayload);
}

static gboolean
gst_rtp_g722_pay_setcaps (GstBaseRTPPayload * basepayload, GstCaps * caps)
{
  GstRtpG722Pay *rtpg722pay;
  GstStructure *structure;
  gint rate, channels, clock_rate;
  gboolean res;
  gchar *params;
  GstAudioChannelPosition *pos;
  const GstRTPChannelOrder *order;
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (basepayload);
  rtpg722pay = GST_RTP_G722_PAY (basepayload);

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

  /* Clock rate is always 8000 Hz for G722 according to
   * RFC 3551 although the sampling rate is 16000 Hz */
  clock_rate = 8000;

  gst_basertppayload_set_options (basepayload, "audio", TRUE, "G722",
      clock_rate);
  params = g_strdup_printf ("%d", channels);

  if (!order && channels > 2) {
    GST_ELEMENT_WARNING (rtpg722pay, STREAM, DECODE,
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

  rtpg722pay->rate = rate;
  rtpg722pay->channels = channels;

  /* bits-per-sample is 4 * channels for G722, but as the RTP clock runs at
   * half speed (8 instead of 16 khz), pretend it's 8 bits per sample
   * channels. */
  gst_base_rtp_audio_payload_set_samplebits_options (basertpaudiopayload,
      8 * rtpg722pay->channels);

  return res;

  /* ERRORS */
no_rate:
  {
    GST_DEBUG_OBJECT (rtpg722pay, "no rate given");
    return FALSE;
  }
no_channels:
  {
    GST_DEBUG_OBJECT (rtpg722pay, "no channels given");
    return FALSE;
  }
}

static GstCaps *
gst_rtp_g722_pay_getcaps (GstBaseRTPPayload * rtppayload, GstPad * pad)
{
  GstCaps *otherpadcaps;
  GstCaps *caps;

  otherpadcaps = gst_pad_get_allowed_caps (rtppayload->srcpad);
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      gst_caps_set_simple (caps, "channels", G_TYPE_INT, 1, NULL);
      gst_caps_set_simple (caps, "rate", G_TYPE_INT, 16000, NULL);
    }
    gst_caps_unref (otherpadcaps);
  }
  return caps;
}

gboolean
gst_rtp_g722_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg722pay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_G722_PAY);
}

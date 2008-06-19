/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) 2005 Nokia Corporation <kai.vehmanen@nokia.com>
 * Copyright (C) 2007,2008 Axis Communications <dev-gstreamer@axis.com>
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

#include "gstrtpg726pay.h"

static const GstElementDetails gst_rtp_g726_pay_details =
GST_ELEMENT_DETAILS ("RTP packet payloader",
    "Codec/Payloader/Network",
    "Payload-encodes G.726 audio into a RTP packet",
    "Axis Communications <dev-gstreamer@axis.com>");

static GstStaticPadTemplate gst_rtp_g726_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-adpcm, "
        "channels = (int) 1, "
        "rate = (int) 8000, "
        "bitrate = (int) { 16000, 24000, 32000, 40000 }, "
        "layout = (string) \"g726\"")
    );

static GstStaticPadTemplate gst_rtp_g726_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) { \"G726-16\", \"G726-24\", \"G726-32\", \"G726-40\" } ")
    );

static gboolean gst_rtp_g726_pay_setcaps (GstBaseRTPPayload * payload,
    GstCaps * caps);

GST_BOILERPLATE (GstRtpG726Pay, gst_rtp_g726_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_g726_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g726_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g726_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_g726_pay_details);
}

static void
gst_rtp_g726_pay_class_init (GstRtpG726PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseRTPPayloadClass *gstbasertppayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasertppayload_class = (GstBaseRTPPayloadClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstbasertppayload_class->set_caps = gst_rtp_g726_pay_setcaps;
}

static void
gst_rtp_g726_pay_init (GstRtpG726Pay * rtpg726pay, GstRtpG726PayClass * klass)
{
  GstBaseRTPAudioPayload *basertpaudiopayload;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (rtpg726pay);

  GST_BASE_RTP_PAYLOAD (rtpg726pay)->clock_rate = 8000;

  /* sample based codec */
  gst_base_rtp_audio_payload_set_sample_based (basertpaudiopayload);
}

static gboolean
gst_rtp_g726_pay_setcaps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gchar *encoding_name;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstBaseRTPAudioPayload *basertpaudiopayload;
  gint bitrate;

  basertpaudiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (payload);

  if (!gst_structure_get_int (structure, "bitrate", &bitrate))
    bitrate = 32000;

  switch (bitrate) {
    case 16000:
      encoding_name = g_strdup ("G726-16");
      gst_base_rtp_audio_payload_set_samplebits_options (basertpaudiopayload,
          2);
      break;
    case 24000:
      encoding_name = g_strdup ("G726-24");
      gst_base_rtp_audio_payload_set_samplebits_options (basertpaudiopayload,
          3);
      break;
    case 32000:
      encoding_name = g_strdup ("G726-32");
      gst_base_rtp_audio_payload_set_samplebits_options (basertpaudiopayload,
          4);
      break;
    case 40000:
      encoding_name = g_strdup ("G726-40");
      gst_base_rtp_audio_payload_set_samplebits_options (basertpaudiopayload,
          5);
      break;
    default:
      goto invalid_bitrate;
  }

  gst_basertppayload_set_options (payload, "audio", TRUE, encoding_name, 8000);
  gst_basertppayload_set_outcaps (payload, NULL);

  g_free (encoding_name);

  return TRUE;

  /* ERRORS */
invalid_bitrate:
  {
    GST_ERROR_OBJECT (payload, "invalid bitrate %d specified", bitrate);
    return FALSE;
  }
}

gboolean
gst_rtp_g726_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg726pay",
      GST_RANK_NONE, GST_TYPE_RTP_G726_PAY);
}

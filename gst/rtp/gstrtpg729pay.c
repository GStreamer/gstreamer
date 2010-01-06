/* GStreamer
 * Copyright (C) <2007> Nokia Corporation
 * Copyright (C) <2007> Collabora Ltd
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
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

/*
 * This payloader assumes that the data will ALWAYS come as zero or more
 * 10 bytes frame of audio followed by 0 or 1 2 byte frame of silence.
 * Any other buffer format won't work
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/base/gstadapter.h>

#include "gstrtpg729pay.h"

GST_DEBUG_CATEGORY_STATIC (rtpg729pay_debug);
#define GST_CAT_DEFAULT (rtpg729pay_debug)

#define G729_FRAME_SIZE 10
#define G729B_CN_FRAME_SIZE 2
#define G729_FRAME_DURATION (10 * GST_MSECOND)
#define G729_FRAME_DURATION_MS (10)

static gboolean
gst_rtp_g729_pay_set_caps (GstBaseRTPPayload * payload, GstCaps * caps);
static GstFlowReturn
gst_rtp_g729_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buf);


static const GstElementDetails gst_rtp_g729_pay_details =
GST_ELEMENT_DETAILS ("RTP G.729 payloader",
    "Codec/Payloader/Network",
    "Packetize G.729 audio into RTP packets",
    "Olivier Crete <olivier.crete@collabora.co.uk>");

static GstStaticPadTemplate gst_rtp_g729_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/G729, "     /* according to RFC 3555 */
        "channels = (int) 1, " "rate = (int) 8000")
    );

static GstStaticPadTemplate gst_rtp_g729_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_G729_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"G729\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"G729\"")
    );

static void
gst_rtp_g729_pay_init (GstRTPG729Pay * pay, GstRTPG729PayClass * klass);

GST_BOILERPLATE (GstRTPG729Pay, gst_rtp_g729_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_g729_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g729_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g729_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_g729_pay_details);

  GST_DEBUG_CATEGORY_INIT (rtpg729pay_debug, "rtpg729pay", 0,
      "G.729 RTP Payloader");
}

static void
gst_rtp_g729_pay_class_init (GstRTPG729PayClass * klass)
{
  GstBaseRTPPayloadClass *payload_class = GST_BASE_RTP_PAYLOAD_CLASS (klass);

  payload_class->set_caps = gst_rtp_g729_pay_set_caps;
  payload_class->handle_buffer = gst_rtp_g729_pay_handle_buffer;
}

static void
gst_rtp_g729_pay_init (GstRTPG729Pay * pay, GstRTPG729PayClass * klass)
{
  GstBaseRTPPayload *payload = GST_BASE_RTP_PAYLOAD (pay);
  GstBaseRTPAudioPayload *audiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (pay);

  payload->pt = GST_RTP_PAYLOAD_G729;
  gst_basertppayload_set_options (payload, "audio", FALSE, "G729", 8000);

  gst_base_rtp_audio_payload_set_frame_based (audiopayload);
  gst_base_rtp_audio_payload_set_frame_options (audiopayload,
      G729_FRAME_DURATION_MS, G729_FRAME_SIZE);

}

static gboolean
gst_rtp_g729_pay_set_caps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;
  GstStructure *structure;
  gint pt;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "payload", &pt))
    pt = GST_RTP_PAYLOAD_G729;

  payload->pt = pt;
  payload->dynamic = pt != GST_RTP_PAYLOAD_G729;

  res = gst_basertppayload_set_outcaps (payload, NULL);

  return res;
}

static GstFlowReturn
gst_rtp_g729_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseRTPAudioPayload *basertpaudiopayload =
      GST_BASE_RTP_AUDIO_PAYLOAD (payload);
  GstAdapter *adapter = NULL;
  guint payload_len;
  guint available;
  guint maxptime_octets = G_MAXUINT;
  guint minptime_octets = 0;
  guint min_payload_len;
  guint max_payload_len;

  available = GST_BUFFER_SIZE (buf);

  if (available % G729_FRAME_SIZE != 0 &&
      available % G729_FRAME_SIZE != G729B_CN_FRAME_SIZE)
    goto invalid_size;

  /* max number of bytes based on given ptime, has to be multiple of
   * frame_duration */
  if (payload->max_ptime != -1) {
    guint ptime_ms = payload->max_ptime / 1000000;

    maxptime_octets = G729_FRAME_SIZE *
        (int) (ptime_ms / G729_FRAME_DURATION_MS);

    if (maxptime_octets < G729_FRAME_SIZE) {
      GST_WARNING_OBJECT (basertpaudiopayload, "Given ptime %" G_GINT64_FORMAT
          " is smaller than minimum %d ns, overwriting to minimum",
          payload->max_ptime, G729_FRAME_DURATION_MS);
      maxptime_octets = G729_FRAME_SIZE;
    }
  }

  max_payload_len = MIN (
      /* MTU max */
      (int) (gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
              (basertpaudiopayload), 0, 0) / G729_FRAME_SIZE) * G729_FRAME_SIZE,
      /* ptime max */
      maxptime_octets);

  /* min number of bytes based on a given ptime, has to be a multiple
     of frame duration */
  {
    guint64 min_ptime = payload->min_ptime;

    min_ptime = min_ptime / 1000000;
    minptime_octets = G729_FRAME_SIZE *
        (int) (min_ptime / G729_FRAME_DURATION_MS);
  }

  min_payload_len = MAX (minptime_octets, G729_FRAME_SIZE);

  if (min_payload_len > max_payload_len) {
    min_payload_len = max_payload_len;
  }

  /* If the ptime is specified in the caps, tried to adhere to it exactly */
  if (payload->abidata.ABI.ptime) {
    guint ptime_in_bytes = G729_FRAME_SIZE *
        (guint) (payload->abidata.ABI.ptime / G729_FRAME_DURATION_MS);

    /* clip to computed min and max lengths */
    ptime_in_bytes = MAX (min_payload_len, ptime_in_bytes);
    ptime_in_bytes = MIN (max_payload_len, ptime_in_bytes);

    min_payload_len = max_payload_len = ptime_in_bytes;
  }

  GST_LOG_OBJECT (basertpaudiopayload,
      "Calculated min_payload_len %u and max_payload_len %u",
      min_payload_len, max_payload_len);

  adapter = gst_base_rtp_audio_payload_get_adapter (basertpaudiopayload);


  /* let's reset the base timestamp when the adapter is empty */
  if (gst_adapter_available (adapter) == 0)
    basertpaudiopayload->base_ts = GST_BUFFER_TIMESTAMP (buf);

  if (gst_adapter_available (adapter) == 0 &&
      GST_BUFFER_SIZE (buf) >= min_payload_len &&
      GST_BUFFER_SIZE (buf) <= max_payload_len) {
    ret = gst_base_rtp_audio_payload_push (basertpaudiopayload,
        GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
        GST_BUFFER_TIMESTAMP (buf));
    gst_buffer_unref (buf);
    g_object_unref (adapter);
    return ret;
  }

  gst_adapter_push (adapter, buf);

  available = gst_adapter_available (adapter);
  /* as long as we have full frames */
  /* this loop will push all available buffers till the last frame */
  while (available >= min_payload_len ||
      available % G729_FRAME_SIZE == G729B_CN_FRAME_SIZE) {
    guint num;

    /* We send as much as we can */
    if (available <= max_payload_len) {
      payload_len = available;
    } else {
      payload_len = MIN (max_payload_len,
          (available / G729_FRAME_SIZE) * G729_FRAME_SIZE);
    }

    ret = gst_base_rtp_audio_payload_flush (basertpaudiopayload, payload_len,
        basertpaudiopayload->base_ts);

    num = payload_len / G729_FRAME_SIZE;
    basertpaudiopayload->base_ts += G729_FRAME_DURATION * num;

    available = gst_adapter_available (adapter);
  }

  g_object_unref (adapter);

  return ret;

  /* ERRORS */
invalid_size:
  {
    GST_ELEMENT_ERROR (payload, STREAM, WRONG_TYPE,
        ("Invalid input buffer size"),
        ("Invalid buffer size, should be a multiple of"
            " G729_FRAME_SIZE(10) with an optional G729B_CN_FRAME_SIZE(2)"
            " added to it, but it is %u", available));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

gboolean
gst_rtp_g729_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg729pay",
      GST_RANK_NONE, GST_TYPE_RTP_G729_PAY);
}

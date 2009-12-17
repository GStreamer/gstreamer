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

#include "gstrtpg723pay.h"

#define GST_RTP_PAYLOAD_G723 4
#define GST_RTP_PAYLOAD_G723_STRING "4"

/* According to RFC 3551, works only with G723 encoded with 6.3 kb/s high-rate */
#define G723_FRAME_SIZE 24
#define G723B_SID_FRAME_SIZE 4
#define G723_FRAME_DURATION (30 * GST_MSECOND)
#define G723_FRAME_DURATION_MS (30)

static gboolean
gst_rtp_g723_pay_set_caps (GstBaseRTPPayload * payload, GstCaps * caps);
static GstFlowReturn
gst_rtp_g723_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buf);


static const GstElementDetails gst_rtp_g723_pay_details =
GST_ELEMENT_DETAILS ("RTP G.723 payloader",
    "Codec/Payloader/Network",
    "Packetize 6.3kb/s G.723 audio into RTP packets",
    "Tiago Katcipis <tiago.katcipis@digitro.com.br>");

static GstStaticPadTemplate gst_rtp_g723_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/G723, "     /* according to RFC 3551 */
        "channels = (int) 1, " "rate = (int) 8000")
    );

static GstStaticPadTemplate gst_rtp_g723_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_G723_STRING ", "
        "clock-rate = (int) 8000, "
        "encoding-name = (string) \"G723\"; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 8000, " "encoding-name = (string) \"G723\"")
    );

static void
gst_rtp_g723_pay_init (GstRTPG723Pay * pay, GstRTPG723PayClass * klass);

GST_BOILERPLATE (GstRTPG723Pay, gst_rtp_g723_pay, GstBaseRTPAudioPayload,
    GST_TYPE_BASE_RTP_AUDIO_PAYLOAD);

static void
gst_rtp_g723_pay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g723_pay_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtp_g723_pay_src_template));
  gst_element_class_set_details (element_class, &gst_rtp_g723_pay_details);
}

static void
gst_rtp_g723_pay_class_init (GstRTPG723PayClass * klass)
{
  GstBaseRTPPayloadClass *payload_class = GST_BASE_RTP_PAYLOAD_CLASS (klass);

  payload_class->set_caps = gst_rtp_g723_pay_set_caps;
  payload_class->handle_buffer = gst_rtp_g723_pay_handle_buffer;
}

static void
gst_rtp_g723_pay_init (GstRTPG723Pay * pay, GstRTPG723PayClass * klass)
{
  GstBaseRTPPayload *payload = GST_BASE_RTP_PAYLOAD (pay);
  GstBaseRTPAudioPayload *audiopayload = GST_BASE_RTP_AUDIO_PAYLOAD (pay);

  payload->pt = GST_RTP_PAYLOAD_G723;
  gst_basertppayload_set_options (payload, "audio", FALSE, "G723", 8000);

  gst_base_rtp_audio_payload_set_frame_based (audiopayload);
  gst_base_rtp_audio_payload_set_frame_options (audiopayload,
      G723_FRAME_DURATION_MS, G723_FRAME_SIZE);

}

static gboolean
gst_rtp_g723_pay_set_caps (GstBaseRTPPayload * payload, GstCaps * caps)
{
  gboolean res;
  GstStructure *structure;
  gint pt;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "payload", &pt))
    pt = GST_RTP_PAYLOAD_G723;

  payload->pt = pt;
  payload->dynamic = pt != GST_RTP_PAYLOAD_G723;

  res = gst_basertppayload_set_outcaps (payload, NULL);

  return res;
}

static GstFlowReturn
gst_rtp_g723_pay_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseRTPAudioPayload *basertpaudiopayload =
      GST_BASE_RTP_AUDIO_PAYLOAD (payload);
  GstAdapter *adapter = NULL;
  guint payload_len;
  const guint8 *data = NULL;
  guint available;
  guint maxptime_octets = G_MAXUINT;
  guint minptime_octets = 0;
  guint min_payload_len;
  guint max_payload_len;
  gboolean use_adapter = FALSE;

  available = GST_BUFFER_SIZE (buf);

  if (available % G723_FRAME_SIZE != 0 &&
      available % G723_FRAME_SIZE != G723B_SID_FRAME_SIZE)
    goto invalid_size;

  /* max number of bytes based on given ptime, has to be multiple of
   * frame_duration */
  if (payload->max_ptime != -1) {
    guint ptime_ms = payload->max_ptime / 1000000;

    maxptime_octets = G723_FRAME_SIZE *
        (int) (ptime_ms / G723_FRAME_DURATION_MS);

    if (maxptime_octets < G723_FRAME_SIZE) {
      GST_WARNING_OBJECT (basertpaudiopayload, "Given ptime %" G_GINT64_FORMAT
          " is smaller than minimum %d ns, overwriting to minimum",
          payload->max_ptime, G723_FRAME_DURATION_MS);
      maxptime_octets = G723_FRAME_SIZE;
    }
  }

  max_payload_len = MIN (
      /* MTU max */
      (int) (gst_rtp_buffer_calc_payload_len (GST_BASE_RTP_PAYLOAD_MTU
              (basertpaudiopayload), 0, 0) / G723_FRAME_SIZE) * G723_FRAME_SIZE,
      /* ptime max */
      maxptime_octets);

  /* min number of bytes based on a given ptime, has to be a multiple
     of frame duration */
  {
    guint64 min_ptime;

    g_object_get (G_OBJECT (payload), "min-ptime", &min_ptime, NULL);

    min_ptime = min_ptime / 1000000;
    minptime_octets = G723_FRAME_SIZE *
        (int) (min_ptime / G723_FRAME_DURATION_MS);
  }

  min_payload_len = MAX (minptime_octets, G723_FRAME_SIZE);

  if (min_payload_len > max_payload_len) {
    min_payload_len = max_payload_len;
  }

  GST_DEBUG_OBJECT (basertpaudiopayload,
      "Calculated min_payload_len %u and max_payload_len %u",
      min_payload_len, max_payload_len);

  adapter = gst_base_rtp_audio_payload_get_adapter (basertpaudiopayload);

  if (adapter && gst_adapter_available (adapter)) {
    /* If there is always data in the adapter, we have to use it */
    gst_adapter_push (adapter, buf);
    available = gst_adapter_available (adapter);
    use_adapter = TRUE;
  } else {
    /* let's set the base timestamp */
    basertpaudiopayload->base_ts = GST_BUFFER_TIMESTAMP (buf);

    /* If buffer fits on an RTP packet, let's just push it through */
    /* this will check against max_ptime and max_mtu */
    if (GST_BUFFER_SIZE (buf) >= min_payload_len &&
        GST_BUFFER_SIZE (buf) <= max_payload_len) {
      ret = gst_base_rtp_audio_payload_push (basertpaudiopayload,
          GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
          GST_BUFFER_TIMESTAMP (buf));
      gst_buffer_unref (buf);
      return ret;
    }

    available = GST_BUFFER_SIZE (buf);
    data = (guint8 *) GST_BUFFER_DATA (buf);
  }

  /* as long as we have full frames */
  /* this loop will push all available buffers till the last frame */
  while (available >= min_payload_len ||
      available % G723_FRAME_SIZE == G723B_SID_FRAME_SIZE) {
    guint num;

    /* We send as much as we can */
    if (available <= max_payload_len) {
      payload_len = available;
    } else {
      payload_len = MIN (max_payload_len,
          (available / G723_FRAME_SIZE) * G723_FRAME_SIZE);
    }

    if (use_adapter) {
      data = gst_adapter_peek (adapter, payload_len);
    }

    ret = gst_base_rtp_audio_payload_push (basertpaudiopayload, data,
        payload_len, basertpaudiopayload->base_ts);

    num = payload_len / G723_FRAME_SIZE;
    basertpaudiopayload->base_ts += G723_FRAME_DURATION * num;

    if (use_adapter) {
      gst_adapter_flush (adapter, payload_len);
      available = gst_adapter_available (adapter);
    } else {
      available -= payload_len;
      data += payload_len;
    }
  }

  if (!use_adapter) {
    if (available != 0 && adapter) {
      GstBuffer *buf2;
      buf2 = gst_buffer_create_sub (buf,
          GST_BUFFER_SIZE (buf) - available, available);
      gst_adapter_push (adapter, buf2);
    } else {
      gst_buffer_unref (buf);
    }
  }

  if (adapter) {
    g_object_unref (adapter);
  }

  return ret;

  /* ERRORS */
invalid_size:
  {
    GST_ELEMENT_ERROR (payload, STREAM, WRONG_TYPE,
        ("Invalid input buffer size"),
        ("Invalid buffer size, should be a multiple of"
            " G723_FRAME_SIZE(24) with an optional G723B_SID_FRAME_SIZE(4)"
            " added to it, but it is %u", available));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

/*Plugin init functions*/
gboolean
gst_rtp_g723_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpg723pay", GST_RANK_NONE,
      gst_rtp_g723_pay_get_type ());
}

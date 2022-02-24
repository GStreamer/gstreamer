/* GStreamer RTP LDAC payloader
 * Copyright (C) 2020 Asymptotic <sanchayan@asymptotic.io>
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
 * SECTION:element-rtpldacpay
 * @title: rtpldacpay
 *
 * Payload LDAC encoded audio into RTP packets.
 *
 * LDAC does not have a public specification and concerns itself only with
 * bluetooth transmission. Due to the unavailability of a specification, we
 * consider the encoding-name as X-GST-LDAC.
 *
 * The best reference is [libldac](https://android.googlesource.com/platform/external/libldac/)
 * and the A2DP LDAC implementation in Android's bluetooth stack [Flouride]
 * (https://android.googlesource.com/platform/system/bt/+/refs/heads/master/stack/a2dp/a2dp_vendor_ldac_encoder.cc).
 *
 * ## Example pipeline
 * |[
 * gst-launch-1.0 -v audiotestsrc ! ldacenc ! rtpldacpay mtu=679 ! avdtpsink
 * ]| This example pipeline will payload LDAC encoded audio.
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/audio/audio.h>
#include "gstrtpelements.h"
#include "gstrtpldacpay.h"
#include "gstrtputils.h"

#define GST_RTP_LDAC_PAYLOAD_HEADER_SIZE 1
/* MTU size required for LDAC A2DP streaming */
#define GST_LDAC_MTU_REQUIRED    679

GST_DEBUG_CATEGORY_STATIC (gst_rtp_ldac_pay_debug);
#define GST_CAT_DEFAULT gst_rtp_ldac_pay_debug

#define parent_class gst_rtp_ldac_pay_parent_class
G_DEFINE_TYPE (GstRtpLdacPay, gst_rtp_ldac_pay, GST_TYPE_RTP_BASE_PAYLOAD);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (rtpldacpay, "rtpldacpay", GST_RANK_NONE,
    GST_TYPE_RTP_LDAC_PAY, rtp_element_init (plugin));

static GstStaticPadTemplate gst_rtp_ldac_pay_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ldac, "
        "channels = (int) [ 1, 2 ], "
        "eqmid = (int) { 0, 1, 2 }, "
        "rate = (int) { 44100, 48000, 88200, 96000 }")
    );

static GstStaticPadTemplate gst_rtp_ldac_pay_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) audio,"
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) { 44100, 48000, 88200, 96000 },"
        "encoding-name = (string) \"X-GST-LDAC\"")
    );

static gboolean gst_rtp_ldac_pay_set_caps (GstRTPBasePayload * payload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_ldac_pay_handle_buffer (GstRTPBasePayload *
    payload, GstBuffer * buffer);

/**
 * gst_rtp_ldac_pay_get_num_frames
 * @eqmid: Encode Quality Mode Index
 * @channels: Number of channels
 *
 * Returns: Number of LDAC frames per packet.
 */
static guint8
gst_rtp_ldac_pay_get_num_frames (gint eqmid, gint channels)
{
  g_assert (channels == 1 || channels == 2);

  switch (eqmid) {
      /* Encode setting for High Quality */
    case 0:
      return 4 / channels;
      /* Encode setting for Standard Quality */
    case 1:
      return 6 / channels;
      /* Encode setting for Mobile use Quality */
    case 2:
      return 12 / channels;
    default:
      break;
  }

  g_assert_not_reached ();

  /* If assertion gets compiled out */
  return 6 / channels;
}

static void
gst_rtp_ldac_pay_class_init (GstRtpLdacPayClass * klass)
{
  GstRTPBasePayloadClass *payload_class = GST_RTP_BASE_PAYLOAD_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  payload_class->set_caps = GST_DEBUG_FUNCPTR (gst_rtp_ldac_pay_set_caps);
  payload_class->handle_buffer =
      GST_DEBUG_FUNCPTR (gst_rtp_ldac_pay_handle_buffer);

  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_ldac_pay_sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &gst_rtp_ldac_pay_src_factory);

  gst_element_class_set_static_metadata (element_class, "RTP packet payloader",
      "Codec/Payloader/Network", "Payload LDAC audio as RTP packets",
      "Sanchayan Maity <sanchayan@asymptotic.io>");

  GST_DEBUG_CATEGORY_INIT (gst_rtp_ldac_pay_debug, "rtpldacpay", 0,
      "RTP LDAC payloader");
}

static void
gst_rtp_ldac_pay_init (GstRtpLdacPay * self)
{

}

static gboolean
gst_rtp_ldac_pay_set_caps (GstRTPBasePayload * payload, GstCaps * caps)
{
  GstRtpLdacPay *ldacpay = GST_RTP_LDAC_PAY (payload);
  GstStructure *structure;
  gint channels, eqmid, rate;

  if (GST_RTP_BASE_PAYLOAD_MTU (ldacpay) < GST_LDAC_MTU_REQUIRED) {
    GST_ERROR_OBJECT (ldacpay, "Invalid MTU %d, should be >= %d",
        GST_RTP_BASE_PAYLOAD_MTU (ldacpay), GST_LDAC_MTU_REQUIRED);
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "rate", &rate)) {
    GST_ERROR_OBJECT (ldacpay, "Failed to get audio rate from caps");
    return FALSE;
  }

  if (!gst_structure_get_int (structure, "channels", &channels)) {
    GST_ERROR_OBJECT (ldacpay, "Failed to get audio rate from caps");
    return FALSE;
  }

  if (!gst_structure_get_int (structure, "eqmid", &eqmid)) {
    GST_ERROR_OBJECT (ldacpay, "Failed to get eqmid from caps");
    return FALSE;
  }

  ldacpay->frame_count = gst_rtp_ldac_pay_get_num_frames (eqmid, channels);

  gst_rtp_base_payload_set_options (payload, "audio", TRUE, "X-GST-LDAC", rate);

  return gst_rtp_base_payload_set_outcaps (payload, NULL);
}

/*
 * LDAC encoder does not handle split frames. Currently, the encoder will
 * always emit 660 bytes worth of payload encapsulating multiple LDAC frames.
 * This is as per eqmid and GST_LDAC_MTU_REQUIRED passed for configuring the
 * encoder upstream. Since the encoder always emit full frames and we do not
 * need to handle frame splitting, we do not use an adapter and also push out
 * the buffer as it is received.
 */
static GstFlowReturn
gst_rtp_ldac_pay_handle_buffer (GstRTPBasePayload * payload, GstBuffer * buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstRtpLdacPay *ldacpay = GST_RTP_LDAC_PAY (payload);
  GstBuffer *outbuf;
  GstClockTime outbuf_frame_duration, outbuf_pts;
  guint8 *payload_data;
  gsize buf_sz;

  outbuf =
      gst_rtp_base_payload_allocate_output_buffer (GST_RTP_BASE_PAYLOAD
      (ldacpay), GST_RTP_LDAC_PAYLOAD_HEADER_SIZE, 0, 0);

  /* Get payload */
  gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

  /* Write header and copy data into payload */
  payload_data = gst_rtp_buffer_get_payload (&rtp);
  /* Upper 3 fragment bits not used, ref A2DP v13, 4.3.4 */
  payload_data[0] = ldacpay->frame_count & 0x0f;

  gst_rtp_buffer_unmap (&rtp);

  outbuf_pts = GST_BUFFER_PTS (buffer);
  outbuf_frame_duration = GST_BUFFER_DURATION (buffer);
  buf_sz = gst_buffer_get_size (buffer);

  gst_rtp_copy_audio_meta (ldacpay, outbuf, buffer);
  outbuf = gst_buffer_append (outbuf, buffer);

  GST_BUFFER_PTS (outbuf) = outbuf_pts;
  GST_BUFFER_DURATION (outbuf) = outbuf_frame_duration;
  GST_DEBUG_OBJECT (ldacpay,
      "Pushing %" G_GSIZE_FORMAT " bytes: %" GST_TIME_FORMAT, buf_sz,
      GST_TIME_ARGS (GST_BUFFER_PTS (outbuf)));

  return gst_rtp_base_payload_push (GST_RTP_BASE_PAYLOAD (ldacpay), outbuf);
}

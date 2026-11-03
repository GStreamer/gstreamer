/* GStreamer
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Stéphane Cerveau <scerveau@igalia.com>
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

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

#include "../../ext/vulkan/base/gsth264encoder.h"

#define GST_TYPE_TEST_H264_ENCODER (gst_test_h264_encoder_get_type())
G_DECLARE_FINAL_TYPE (GstTestH264Encoder, gst_test_h264_encoder, GST,
    TEST_H264_ENCODER, GstH264Encoder);

struct _GstTestH264Encoder
{
  GstH264Encoder parent;
};

G_DEFINE_TYPE (GstTestH264Encoder, gst_test_h264_encoder,
    GST_TYPE_H264_ENCODER);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format = (string) NV12"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, profile = (string) high"));

/* slice types in encoding order */
static GstH264SliceType slice_types[64];
static guint num_encoded;

static GstFlowReturn
gst_test_h264_encoder_new_sequence (GstH264Encoder * encoder,
    GstVideoCodecState * in_state, GstH264Profile profile,
    GstH264EncoderProfileVariant variant, GstH264Level * level)
{
  GstVideoCodecState *out_state;

  gst_h264_encoder_set_max_num_references (encoder, 2, 1);

  out_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder),
      gst_caps_from_string ("video/x-h264, profile = (string) high"), in_state);
  gst_video_codec_state_unref (out_state);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_test_h264_encoder_new_parameters (GstH264Encoder * encoder,
    GstH264SPS * sps, GstH264PPS * pps)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_test_h264_encoder_encode_frame (GstH264Encoder * encoder,
    GstVideoCodecFrame * frame, GstH264EncoderFrame * h264_frame,
    GstH264SliceHdr * slice_hdr, GArray * list0, GArray * list1)
{
  fail_unless (num_encoded < G_N_ELEMENTS (slice_types));
  slice_types[num_encoded++] = slice_hdr->type;

  frame->output_buffer = gst_buffer_new ();

  return GST_FLOW_OK;
}

static void
gst_test_h264_encoder_class_init (GstTestH264EncoderClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstH264EncoderClass *h264_class = GST_H264_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class, "Test H.264 Encoder",
      "Codec/Encoder/Video", "Test wrapper for GstH264Encoder",
      "Stéphane Cerveau <scerveau@igalia.com>");

  h264_class->new_sequence = gst_test_h264_encoder_new_sequence;
  h264_class->new_parameters = gst_test_h264_encoder_new_parameters;
  h264_class->encode_frame = gst_test_h264_encoder_encode_frame;
}

static void
gst_test_h264_encoder_init (GstTestH264Encoder * self)
{
}

/* At EOS, the B frames of an incomplete mini-GOP are still waiting for their
 * backward reference, so the drain turns the last pending one into a P frame
 * and encodes the remaining ones as B frames referencing it. Only that last
 * frame shall be turned into P, whatever the number of drain iterations. */
GST_START_TEST (test_drain_incomplete_mini_gop)
{
  /* With 3 B frames per mini-GOP, B9 and B10 are pending at EOS: B10 becomes
   * P10 and B9 stays a B frame. Encoding order:
   * I0 P4 B1 B2 B3 P8 B5 B6 B7 P10 B9 */
  const GstH264SliceType expected[] = { GST_H264_I_SLICE, GST_H264_P_SLICE,
    GST_H264_B_SLICE, GST_H264_B_SLICE, GST_H264_B_SLICE, GST_H264_P_SLICE,
    GST_H264_B_SLICE, GST_H264_B_SLICE, GST_H264_B_SLICE, GST_H264_P_SLICE,
    GST_H264_B_SLICE
  };
  GstHarness *h;
  guint i;

  fail_unless (gst_element_register (NULL, "testh264enc", GST_RANK_NONE,
          GST_TYPE_TEST_H264_ENCODER));

  h = gst_harness_new ("testh264enc");
  g_object_set (h->element, "idr-period", 64, "b-frames", 3, "b-pyramid",
      FALSE, NULL);
  gst_harness_set_src_caps_str (h, "video/x-raw, format = (string) NV12, "
      "width = (int) 320, height = (int) 240, framerate = (fraction) 30/1");

  num_encoded = 0;
  for (i = 0; i < G_N_ELEMENTS (expected); i++) {
    GstBuffer *buf = gst_harness_create_buffer (h, 320 * 240 * 3 / 2);

    GST_BUFFER_PTS (buf) = i * GST_SECOND / 30;
    fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  }
  /* EOS drains the encoder: B9 and B10 are popped one per iteration */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));

  fail_unless_equals_int (num_encoded, G_N_ELEMENTS (expected));
  for (i = 0; i < num_encoded; i++) {
    fail_unless (slice_types[i] == expected[i],
        "Frame %u: slice type %d, expected %d", i, slice_types[i], expected[i]);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
gsth264encoder_suite (void)
{
  Suite *s = suite_create ("gsth264encoder");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_drain_incomplete_mini_gop);

  return s;
}

GST_CHECK_MAIN (gsth264encoder);

/* GStreamer
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "../../ext/vulkan/base/gsth265encoder.h"

/* Mock subclass for testing GstH265Encoder */
#define GST_TYPE_TEST_H265_ENCODER (gst_test_h265_encoder_get_type())
G_DECLARE_FINAL_TYPE (GstTestH265Encoder, gst_test_h265_encoder, GST,
    TEST_H265_ENCODER, GstH265Encoder);

struct _GstTestH265Encoder
{
  GstH265Encoder parent;

  GstH265SPS sps;
};

struct _GstTestH265EncoderClass
{
  GstH265EncoderClass parent_class;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { NV12, P010_10LE, Y444, I422_10LE }, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ], "
        "pixel-aspect-ratio = (fraction) [ 1, 255 ]"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "profile = (string) { main, main-10, main-444, main-444-10 }, "
        "stream-format = (string) byte-stream, "
        "alignment = (string) au, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [ 0, MAX ]"));

G_DEFINE_TYPE (GstTestH265Encoder, gst_test_h265_encoder,
    GST_TYPE_H265_ENCODER);

/* Test constants for playback testing */
#define TEST_VIDEO_WIDTH 640
#define TEST_VIDEO_HEIGHT 480
#define TEST_VIDEO_FPS_N 30
#define TEST_VIDEO_FPS_D 1
#define NUM_BUFFERS 10

/* Per-frame state captured by the mock encode_frame */
#define MAX_CAPTURED_FRAMES 64
struct frame_capture
{
  gboolean is_idr;
  gboolean is_ref;
  guint8 slice_type;
  guint num_delta_pocs;
  guint num_negative_pics;
  guint num_positive_pics;
  guint num_ref_idx_active_override_flag;
};
static struct frame_capture captures[MAX_CAPTURED_FRAMES];
static guint num_captures;

static GstFlowReturn
gst_test_h265_encoder_new_sequence (GstH265Encoder * encoder,
    GstVideoCodecState * in_state, GstH265ProfileTierLevel * ptl)
{
  GstCaps *caps;
  GstVideoCodecState *output_state;
  GstH265Profile profile = gst_h265_profile_tier_level_get_profile (ptl);

  gst_h265_encoder_set_max_num_references (encoder, 2, 1);

  /* Accept the negotiated level */
  fail_unless (ptl->level_idc == GST_H265_LEVEL_L3_1, "Level should be 3.1");
  fail_unless (ptl->tier_flag == 0, "tier should be 0/main");
  fail_unless (profile == GST_H265_PROFILE_MAIN, "profile should be main");

  caps = gst_caps_new_simple ("video/x-h265",
      "profile", G_TYPE_STRING, gst_h265_profile_to_string (profile),
      "level", G_TYPE_STRING, "3.1", "tier", G_TYPE_STRING, "main",
      "width", G_TYPE_INT, GST_VIDEO_INFO_WIDTH (&in_state->info),
      "height", G_TYPE_INT, GST_VIDEO_INFO_HEIGHT (&in_state->info),
      "tier", G_TYPE_STRING, "main", NULL);

  output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (encoder), caps,
      in_state);
  gst_video_codec_state_unref (output_state);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_test_h265_encoder_new_parameters (GstH265Encoder * encoder,
    GstH265VPS * vps, GstH265SPS * sps, GstH265PPS * pps)
{
  /* Accept parameters */
  GstTestH265Encoder *tester = GST_TEST_H265_ENCODER (encoder);

  tester->sps = *sps;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_test_h265_encoder_encode_frame (GstH265Encoder * encoder,
    GstVideoCodecFrame * frame, GstH265EncoderFrame * h265_frame,
    GstH265SliceHdr * slice_hdr, GArray * list0, GArray * list1)
{
  GstTestH265Encoder *tester = GST_TEST_H265_ENCODER (encoder);
  GstBuffer *out_buffer;
  guint8 *data;
  gsize coded_size;

  fail_unless (num_captures < MAX_CAPTURED_FRAMES, "Too many captured frames");

  captures[num_captures].is_idr = GST_H26X_GOP_IS_IDR (&h265_frame->gop);
  captures[num_captures].is_ref = h265_frame->gop.is_ref;
  captures[num_captures].slice_type = slice_hdr->type;
  captures[num_captures].num_delta_pocs =
      slice_hdr->short_term_ref_pic_sets.NumDeltaPocs;
  captures[num_captures].num_negative_pics =
      slice_hdr->short_term_ref_pic_sets.NumNegativePics;
  captures[num_captures].num_positive_pics =
      slice_hdr->short_term_ref_pic_sets.NumPositivePics;
  captures[num_captures].num_ref_idx_active_override_flag =
      slice_hdr->num_ref_idx_active_override_flag;
  num_captures++;

  /* Simple test: create a small output buffer with frame count */
  coded_size = gst_h265_calculate_coded_size (&tester->sps, 1);

  data = g_malloc (coded_size);

  out_buffer = gst_buffer_new_wrapped (data, coded_size);

  /* Set output buffer on frame */
  frame->output_buffer = out_buffer;

  /* No real encoding */
  return GST_FLOW_OK;
}

static void
gst_test_h265_encoder_class_init (GstTestH265EncoderClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstH265EncoderClass *h265_encoder_class = GST_H265_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class, "Test H.265 Encoder",
      "Codec/Encoder/Video/H.265", "Test wrapper for GstH265Encoder",
      "Víctor Jáquez");

  h265_encoder_class->new_sequence = gst_test_h265_encoder_new_sequence;
  h265_encoder_class->new_parameters = gst_test_h265_encoder_new_parameters;
  h265_encoder_class->encode_frame = gst_test_h265_encoder_encode_frame;
}

static void
gst_test_h265_encoder_init (GstTestH265Encoder * encoder)
{
}

/* Test fixture */
GstElement *test_encoder = NULL;

static void
setup (void)
{
  test_encoder = g_object_new (GST_TYPE_TEST_H265_ENCODER, NULL);
}

static void
teardown (void)
{
  gst_clear_object (&test_encoder);
}

GST_START_TEST (test_level_descriptor)
{
  const GstH265LevelDescriptor *desc;
  /* Test with Main profile */
  GstH265ProfileTierLevel ptl = {
    .profile_idc = GST_H265_PROFILE_MAIN,
    .profile_compatibility_flag = {1, 0,},
  };
  GstVideoInfo info;
  guint64 bitrate = 10000000;   /* 10 Mbps */

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_NV12, 1920, 1080);

  /* The function finds appropriate level based on parameters */
  desc = gst_h265_get_level_descriptor (&ptl, bitrate, &info, 1, 1, 1, 6);
  fail_unless (desc != NULL,
      "Should get level descriptor for valid parameters");
  fail_unless (desc->level_idc == GST_H265_LEVEL_L4,
      "Level descriptor should be 4");
}

GST_END_TEST;

GST_START_TEST (test_calculate_coded_size)
{
  GstH265SPS sps = {
    .pic_width_in_luma_samples = GST_ROUND_UP_16 (1920),
    .pic_height_in_luma_samples = GST_ROUND_UP_16 (1080),
    .chroma_format_idc = 1,     /* 4:2:0 */
    .bit_depth_luma_minus8 = 0,
    .bit_depth_chroma_minus8 = 0,
    .profile_tier_level.level_idc = GST_H265_LEVEL_L3_1,
  };
  gsize coded_size;

  coded_size = gst_h265_calculate_coded_size (&sps, 1);
  fail_unless (coded_size == 3142200, "Coded size should be 3142200, got %zu",
      coded_size);
}

GST_END_TEST;

GST_START_TEST (test_encode)
{
  GstHarness *h;
  GstCaps *downstream_caps, *upstream_caps;
  GstBuffer *outbuf, *inbuf;
  GstFlowReturn ret;
  static GstStaticPadTemplate sink_templ =
      GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h265"));
  static GstStaticPadTemplate src_templ =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));

  h = gst_harness_new_full (test_encoder, &src_templ, "sink", &sink_templ,
      "src");

  downstream_caps = gst_caps_new_simple ("video/x-h265",
      "profile", G_TYPE_STRING, "main", "level", G_TYPE_STRING, "3.1",
      "tier", G_TYPE_STRING, "main",
      "width", G_TYPE_INT, TEST_VIDEO_WIDTH,
      "height", G_TYPE_INT, TEST_VIDEO_HEIGHT,
      "framerate", GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  upstream_caps = gst_caps_new_simple ("video/x-raw",
      "height", G_TYPE_INT, TEST_VIDEO_HEIGHT,
      "width", G_TYPE_INT, TEST_VIDEO_WIDTH,
      "framerate", GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "format", G_TYPE_STRING, "NV12", NULL);

  gst_harness_set_caps (h, upstream_caps, downstream_caps);

  inbuf = gst_harness_create_buffer (h, TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT * 3 / 2);  /* NV12 size */
  GST_BUFFER_PTS (inbuf) =
      gst_util_uint64_scale_round (1, GST_SECOND * TEST_VIDEO_FPS_D,
      TEST_VIDEO_FPS_N);
  GST_BUFFER_DURATION (inbuf) =
      gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
      TEST_VIDEO_FPS_N);

  ret = gst_harness_push (h, inbuf);
  fail_unless (ret == GST_FLOW_OK, "Return value of push was %s",
      gst_flow_get_name (ret));

  outbuf = gst_harness_pull (h);
  fail_unless (outbuf, "Not buffer returned");

  fail_unless (GST_BUFFER_IS_DISCONT (outbuf),
      "Buffer should be discontinuity");

  gst_buffer_unref (outbuf);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* Reordering with B frames and b pyramid: the popped frames shall follow the
 * rendering order (I, P, then the B pyramid levels) and the pop shall not
 * stall when two B frames of the same pyramid level are in the reorder list. */
GST_START_TEST (test_reorder_b_pyramid)
{
  GstHarness *h;
  GstCaps *downstream_caps, *upstream_caps;
  GstBuffer *inbuf, *outbuf;
  GstFlowReturn ret;
  static GstStaticPadTemplate sink_templ =
      GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h265"));
  static GstStaticPadTemplate src_templ =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));
  /* Rendering order for b-frames=3 with b-pyramid: the I, then each P, then
   * the level-0 B, then the level-1 Bs. At EOS, the still-cached frames of
   * the last mini-GOP are flushed with the same pyramid ordering. */
  const guint poc_order[] = { 0, 4, 2, 1, 3, 8, 6, 5, 7, 12, 10, 9, 11 };
  const guint num_frames = G_N_ELEMENTS (poc_order);
  GstClockTime first_pts = GST_CLOCK_TIME_NONE;
  guint i = 0;

  g_object_set (test_encoder, "idr-period", 64, "b-frames", 3,
      "b-pyramid", TRUE, NULL);

  h = gst_harness_new_full (test_encoder, &src_templ, "sink", &sink_templ,
      "src");

  downstream_caps = gst_caps_new_simple ("video/x-h265",
      "profile", G_TYPE_STRING, "main", "level", G_TYPE_STRING, "3.1",
      "tier", G_TYPE_STRING, "main",
      "width", G_TYPE_INT, TEST_VIDEO_WIDTH,
      "height", G_TYPE_INT, TEST_VIDEO_HEIGHT,
      "framerate", GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  upstream_caps = gst_caps_new_simple ("video/x-raw",
      "height", G_TYPE_INT, TEST_VIDEO_HEIGHT,
      "width", G_TYPE_INT, TEST_VIDEO_WIDTH,
      "framerate", GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "format", G_TYPE_STRING, "NV12", NULL);

  gst_harness_set_caps (h, upstream_caps, downstream_caps);

  for (i = 0; i < num_frames; i++) {
    inbuf = gst_harness_create_buffer (h, TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT * 3 / 2);        /* NV12 size */
    GST_BUFFER_PTS (inbuf) =
        gst_util_uint64_scale_round (i, GST_SECOND * TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);
    GST_BUFFER_DURATION (inbuf) =
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);

    ret = gst_harness_push (h, inbuf);
    fail_unless (ret == GST_FLOW_OK, "Return value of push was %s",
        gst_flow_get_name (ret));
  }

  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()),
      "Failed to push EOS");

  i = 0;
  while (gst_harness_pull_until_eos (h, &outbuf) && outbuf) {
    GstClockTime pts;

    fail_unless (i < num_frames, "Got more than %u buffers", num_frames);

    /* The base encoder shifts all timestamps forward (min_pts sentinel) to
     * keep the first DTS positive, so check the relative PTS only. */
    if (first_pts == GST_CLOCK_TIME_NONE)
      first_pts = GST_BUFFER_PTS (outbuf);

    pts = gst_util_uint64_scale_round (poc_order[i],
        GST_SECOND * TEST_VIDEO_FPS_D, TEST_VIDEO_FPS_N);
    fail_unless (GST_BUFFER_PTS (outbuf) == first_pts + pts,
        "Buffer %u: PTS %" GST_TIME_FORMAT ", expected %" GST_TIME_FORMAT,
        i, GST_TIME_ARGS (GST_BUFFER_PTS (outbuf) - first_pts),
        GST_TIME_ARGS (pts));

    gst_buffer_unref (outbuf);
    i++;
  }
  fail_unless (i == num_frames, "Expected %u buffers, got %u", num_frames, i);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* Creates a harness for the mock encoder with matching caps, pushes
 * num_frames NV12 input buffers with a proper timeline, sends EOS and
 * drains the output, checking the buffer and capture counts. */
static GstHarness *
push_frames_and_drain (guint num_frames)
{
  static GstStaticPadTemplate sink_templ =
      GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h265"));
  static GstStaticPadTemplate src_templ =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));
  GstHarness *h;
  GstCaps *downstream_caps, *upstream_caps;
  GstBuffer *outbuf;
  guint i, n_out = 0;

  h = gst_harness_new_full (test_encoder, &src_templ, "sink", &sink_templ,
      "src");

  downstream_caps = gst_caps_new_simple ("video/x-h265",
      "profile", G_TYPE_STRING, "main", "level", G_TYPE_STRING, "3.1",
      "tier", G_TYPE_STRING, "main",
      "width", G_TYPE_INT, TEST_VIDEO_WIDTH,
      "height", G_TYPE_INT, TEST_VIDEO_HEIGHT,
      "framerate", GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
  upstream_caps = gst_caps_new_simple ("video/x-raw",
      "height", G_TYPE_INT, TEST_VIDEO_HEIGHT,
      "width", G_TYPE_INT, TEST_VIDEO_WIDTH,
      "framerate", GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "format", G_TYPE_STRING, "NV12", NULL);

  gst_harness_set_caps (h, upstream_caps, downstream_caps);

  for (i = 0; i < num_frames; i++) {
    GstBuffer *inbuf = gst_harness_create_buffer (h,
        TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT * 3 / 2);  /* NV12 size */
    GST_BUFFER_PTS (inbuf) =
        gst_util_uint64_scale_round (i, GST_SECOND * TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);
    GST_BUFFER_DURATION (inbuf) =
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);

    fail_unless (gst_harness_push (h, inbuf) == GST_FLOW_OK,
        "Failed to push frame %u", i);
  }

  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()),
      "Failed to push EOS");

  while (gst_harness_pull_until_eos (h, &outbuf) && outbuf) {
    gst_buffer_unref (outbuf);
    n_out++;
  }
  fail_unless (n_out == num_frames, "Expected %u buffers, got %u",
      num_frames, n_out);
  fail_unless (num_captures == num_frames, "Captured %u frames, expected %u",
      num_captures, num_frames);

  return h;
}

/* Regression test: reference I frames inserted in the middle of a GOP
 * (i-frames > 0) must not be handled as IDR pictures. They shall get
 * their reference lists built and signal a non-empty short-term RPS,
 * so the decoder keeps the previous pictures marked as references for
 * the following inter frames (H.265 8.3.2). */
GST_START_TEST (test_i_frames_ref_lists)
{
  GstHarness *h;
  const guint num_frames = 20;  /* 2 GOPs of 10 */
  guint n_idr = 0;
  guint n_i = 0;
  guint i;

  num_captures = 0;
  g_object_set (test_encoder, "idr-period", 10, "i-frames", 1, NULL);

  h = push_frames_and_drain (num_frames);

  for (i = 0; i < num_captures; i++) {
    if (captures[i].is_idr) {
      n_idr++;
      fail_unless (captures[i].slice_type == GST_H265_I_SLICE,
          "Frame %u: IDR shall be an I slice", i);
      fail_unless (captures[i].num_delta_pocs == 0,
          "Frame %u: IDR shall have no RPS", i);
      fail_unless (captures[i].num_ref_idx_active_override_flag == 0,
          "Frame %u: IDR shall not override the reference lists", i);
    } else {
      /* non-IDR pictures must signal a short-term RPS, otherwise the
       * decoder marks the previous pictures as unused for reference
       * (H.265 8.3.2) */
      fail_unless (captures[i].num_delta_pocs > 0,
          "Frame %u: empty short-term RPS", i);

      if (captures[i].slice_type == GST_H265_I_SLICE) {
        n_i++;
        /* num_ref_idx_active_override_flag is not part of the I slice
         * header syntax (H.265 7.3.6.1) */
        fail_unless (captures[i].num_ref_idx_active_override_flag == 0,
            "Frame %u: I slice shall not override the reference lists", i);
      } else if (captures[i].slice_type == GST_H265_P_SLICE) {
        fail_unless (captures[i].num_ref_idx_active_override_flag == 1,
            "Frame %u: P slice shall override the reference lists", i);
      }
    }
  }

  fail_unless (n_idr == 2, "Expected 2 IDR pictures, got %u", n_idr);
  fail_unless (n_i == 2, "Expected 2 mid-GOP I pictures, got %u", n_i);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* Regression test: in an intra only stream (num-ref-frames=0), the non-IDR
 * I pictures are not references, but they shall still signal a short-term
 * RPS referring to the IDR picture of the current GOP, keeping it marked as
 * reference at the decoder (H.265 8.3.2). And, since num_ref_idx_active_
 * override_flag is not part of the I slice header syntax, it shall not be
 * set (H.265 7.3.6.1). */
GST_START_TEST (test_intra_only_ref_lists)
{
  GstHarness *h;
  const guint num_frames = 10;  /* 1 GOP */
  guint i;

  num_captures = 0;
  g_object_set (test_encoder, "idr-period", 10, "num-ref-frames", 0, NULL);

  h = push_frames_and_drain (num_frames);

  for (i = 0; i < num_captures; i++) {
    fail_unless (captures[i].slice_type == GST_H265_I_SLICE,
        "Frame %u: intra only stream shall have I slices", i);
    fail_unless (captures[i].num_ref_idx_active_override_flag == 0,
        "Frame %u: I slice shall not override the reference lists", i);

    if (i == 0) {
      fail_unless (captures[i].is_idr, "Frame 0 shall be an IDR picture");
      fail_unless (captures[i].num_delta_pocs == 0, "IDR shall have no RPS");
    } else {
      fail_unless (!captures[i].is_idr,
          "Frame %u: only frame 0 shall be IDR", i);
      fail_unless (!captures[i].is_ref,
          "Frame %u: intra only stream shall have no reference frames", i);
      /* the IDR picture of the current GOP is the only entry in the RPS,
       * as a negative picture, but it is not used by these pictures */
      fail_unless (captures[i].num_delta_pocs == 1,
          "Frame %u: expected 1 delta poc, got %u", i,
          captures[i].num_delta_pocs);
      fail_unless (captures[i].num_negative_pics == 1,
          "Frame %u: expected 1 negative pic, got %u", i,
          captures[i].num_negative_pics);
      fail_unless (captures[i].num_positive_pics == 0,
          "Frame %u: expected no positive pics, got %u", i,
          captures[i].num_positive_pics);
    }
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static gboolean
caps_contain_format (GstCaps * caps, const gchar * format_str)
{
  GstVideoFormat fmt = gst_video_format_from_string (format_str);
  for (guint i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    const GValue *val = gst_structure_get_value (s, "format");
    if (!val)
      continue;
    if (G_VALUE_HOLDS_STRING (val)) {
      if (gst_video_format_from_string (g_value_get_string (val)) == fmt)
        return TRUE;
    } else if (GST_VALUE_HOLDS_LIST (val)) {
      for (guint j = 0; j < gst_value_list_get_size (val); j++) {
        const GValue *lval = gst_value_list_get_value (val, j);
        if (G_VALUE_HOLDS_STRING (lval) &&
            gst_video_format_from_string (g_value_get_string (lval)) == fmt)
          return TRUE;
      }
    }
  }
  return FALSE;
}

GST_START_TEST (test_getcaps_no_downstream)
{
  GstCaps *caps;

  /* With no downstream peer the getcaps falls back to the full sink template. */
  caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SINK_PAD (test_encoder), NULL);
  fail_unless (caps != NULL);

  fail_unless (caps_contain_format (caps, "NV12"));
  fail_unless (caps_contain_format (caps, "P010_10LE"));
  fail_unless (caps_contain_format (caps, "Y444"));
  fail_unless (caps_contain_format (caps, "I422_10LE"));

  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_getcaps_main_profile)
{
  static GstStaticPadTemplate hsrc_templ =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));
  static GstStaticPadTemplate hsink_templ =
      GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h265, profile=(string)main"));
  GstHarness *h;
  GstCaps *caps;

  h = gst_harness_new_full (test_encoder, &hsrc_templ, "sink", &hsink_templ,
      "src");

  /* "main" profile: 4:2:0, 8-bit → only NV12 qualifies */
  caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SINK_PAD (test_encoder), NULL);
  fail_unless (caps != NULL);

  fail_unless (caps_contain_format (caps, "NV12"));
  fail_unless (!caps_contain_format (caps, "P010_10LE"));
  fail_unless (!caps_contain_format (caps, "Y444"));
  fail_unless (!caps_contain_format (caps, "I422_10LE"));

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_getcaps_main_10_profile)
{
  static GstStaticPadTemplate hsrc_templ =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));
  static GstStaticPadTemplate hsink_templ =
      GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h265, profile=(string)main-10"));
  GstHarness *h;
  GstCaps *caps;

  h = gst_harness_new_full (test_encoder, &hsrc_templ, "sink", &hsink_templ,
      "src");

  /* "main-10" profile: 4:2:0, up to 10-bit → NV12 and P010_10LE qualify */
  caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SINK_PAD (test_encoder), NULL);
  fail_unless (caps != NULL);

  fail_unless (caps_contain_format (caps, "NV12"));
  fail_unless (caps_contain_format (caps, "P010_10LE"));
  fail_unless (!caps_contain_format (caps, "Y444"));
  fail_unless (!caps_contain_format (caps, "I422_10LE"));

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_getcaps_main_444_profile)
{
  static GstStaticPadTemplate hsrc_templ =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));
  static GstStaticPadTemplate hsink_templ =
      GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-h265, profile=(string)main-444"));
  GstHarness *h;
  GstCaps *caps;

  h = gst_harness_new_full (test_encoder, &hsrc_templ, "sink", &hsink_templ,
      "src");

  /* "main-444" profile: 4:4:4, 8-bit → NV12 (4:2:0≤4:4:4) and Y444 qualify */
  caps = gst_pad_query_caps (GST_VIDEO_ENCODER_SINK_PAD (test_encoder), NULL);
  fail_unless (caps != NULL);

  fail_unless (caps_contain_format (caps, "NV12"));
  fail_unless (!caps_contain_format (caps, "P010_10LE"));
  fail_unless (caps_contain_format (caps, "Y444"));
  fail_unless (!caps_contain_format (caps, "I422_10LE"));

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

/* Test suite */
static Suite *
gsth265encoder_suite (void)
{
  Suite *s = suite_create ("gsth265encoder");
  TCase *tc_chain = tcase_create ("general");

  tcase_add_checked_fixture (tc_chain, setup, teardown);

  tcase_add_test (tc_chain, test_level_descriptor);
  tcase_add_test (tc_chain, test_calculate_coded_size);
  tcase_add_test (tc_chain, test_encode);
  tcase_add_test (tc_chain, test_reorder_b_pyramid);
  tcase_add_test (tc_chain, test_i_frames_ref_lists);
  tcase_add_test (tc_chain, test_intra_only_ref_lists);
  tcase_add_test (tc_chain, test_getcaps_no_downstream);
  tcase_add_test (tc_chain, test_getcaps_main_profile);
  tcase_add_test (tc_chain, test_getcaps_main_10_profile);
  tcase_add_test (tc_chain, test_getcaps_main_444_profile);

  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (gsth265encoder);

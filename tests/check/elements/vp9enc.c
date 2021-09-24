/* GStreamer
 *
 * Copyright (c) 2016 Stian Selnes <stian@pexip.com>
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
#include <gst/check/gstharness.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>

#define gst_caps_new_i420(w, h) gst_caps_new_i420_full (w, h, 30, 1, 1, 1)
static GstCaps *
gst_caps_new_i420_full (gint width, gint height, gint fps_n, gint fps_d,
    gint par_n, gint par_d)
{
  GstVideoInfo info;
  gst_video_info_init (&info);
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, width, height);
  GST_VIDEO_INFO_FPS_N (&info) = fps_n;
  GST_VIDEO_INFO_FPS_D (&info) = fps_d;
  GST_VIDEO_INFO_PAR_N (&info) = par_n;
  GST_VIDEO_INFO_PAR_D (&info) = par_d;
  return gst_video_info_to_caps (&info);
}

GST_START_TEST (test_encode_lag_in_frames)
{
  GstHarness *h = gst_harness_new_parse ("vp9enc lag-in-frames=5 cpu-used=8 "
      "deadline=1");
  gint i;

  gst_harness_add_src_parse (h, "videotestsrc is-live=true pattern=black ! "
      "capsfilter caps=\"video/x-raw,format=I420,width=320,height=240,framerate=25/1\"",
      TRUE);

  /* Push 20 buffers into the encoder */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_src_crank_and_push_many (h, 20, 20));

  /* Only 5 buffers are allowed to be queued now */
  fail_unless (gst_harness_buffers_received (h) > 15);

  /* EOS will cause the remaining buffers to be drained */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));
  fail_unless_equals_int (gst_harness_buffers_received (h), 20);

  for (i = 0; i < 20; i++) {
    GstBuffer *buffer = gst_harness_pull (h);

    if (i == 0)
      fail_if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));

    fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (buffer),
        gst_util_uint64_scale (i, GST_SECOND, 25));
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));

    gst_buffer_unref (buffer);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;


GST_START_TEST (test_autobitrate_changes_with_caps)
{
  gint bitrate = 0;
  GstHarness *h = gst_harness_new ("vp9enc");
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (1280, 720, 30, 1, 1, 1));

  /* Default settings for 720p @ 30fps ~0.8Mbps */
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 799000);

  /* Change bits-per-pixel 0.036 to give us ~1Mbps */
  g_object_set (h->element, "bits-per-pixel", 0.037, NULL);
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 1022000);

  /* Halving the framerate should halve the auto bitrate */
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (1280, 720, 15, 1, 1, 1));
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 511000);

  /* Halving the resolution should quarter the auto bitrate */
  gst_harness_set_src_caps (h, gst_caps_new_i420_full (640, 360, 15, 1, 1, 1));
  g_object_get (h->element, "target-bitrate", &bitrate, NULL);
  fail_unless_equals_int (bitrate, 127000);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
vp9enc_suite (void)
{
  Suite *s = suite_create ("vp9enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_encode_lag_in_frames);
  tcase_add_test (tc_chain, test_autobitrate_changes_with_caps);

  return s;
}

GST_CHECK_MAIN (vp9enc);

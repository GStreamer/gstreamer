/* GStreamer
 *
 * unit test for videofilter elements
 *
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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

#include <stdarg.h>

#include <gst/video/video.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

static GstBuffer *
create_test_video_buffer_rgba8 (GstVideoInfo * info)
{
  guint8 *data;
  guint i = 0, j, k;
  gsize stride = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

  data = g_malloc0 (info->size);

  for (j = 0; j < GST_VIDEO_INFO_COMP_HEIGHT (info, i); j++) {
    for (k = 0; k < GST_VIDEO_INFO_COMP_WIDTH (info, i); k++) {
      data[(j * stride + 4 * k) + 0] = j % 255;
      data[(j * stride + 4 * k) + 1] = k % 255;
      data[(j * stride + 4 * k) + 2] = (j + k) % 255;
      data[(j * stride + 4 * k) + 3] = 255;
    }
  }

  return gst_buffer_new_wrapped (data, info->size);
}

GST_START_TEST (test_passthrough)
{
  GstHarness *flip = gst_harness_new ("videoflip");
  GstVideoInfo in_info, out_info;
  GstCaps *in_caps, *out_caps;
  GstEvent *e;
  GstBuffer *buf;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, 4, 9);
  in_caps = gst_video_info_to_caps (&in_info);

  gst_harness_set_src_caps (flip, in_caps);

  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_STREAM_START);
  gst_event_unref (e);
  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_CAPS);
  gst_event_parse_caps (e, &out_caps);
  fail_unless (gst_video_info_from_caps (&out_info, out_caps));
  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (&in_info),
      GST_VIDEO_INFO_WIDTH (&out_info));
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (&in_info),
      GST_VIDEO_INFO_HEIGHT (&out_info));
  gst_event_unref (e);

  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_SEGMENT);
  gst_event_unref (e);

  buf = create_test_video_buffer_rgba8 (&in_info);
  buf = gst_harness_push_and_pull (flip, buf);
  fail_unless (buf != NULL);
  gst_buffer_unref (buf);

  gst_harness_teardown (flip);
}

GST_END_TEST;

GST_START_TEST (test_change_method)
{
  GstHarness *flip = gst_harness_new ("videoflip");
  GstVideoInfo in_info, out_info;
  GstCaps *in_caps, *out_caps;
  GstEvent *e;
  GstBuffer *buf;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, 4, 9);
  in_caps = gst_video_info_to_caps (&in_info);

  gst_harness_set_src_caps (flip, in_caps);

  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_STREAM_START);
  gst_event_unref (e);
  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_CAPS);
  gst_event_parse_caps (e, &out_caps);
  fail_unless (gst_video_info_from_caps (&out_info, out_caps));
  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (&in_info),
      GST_VIDEO_INFO_WIDTH (&out_info));
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (&in_info),
      GST_VIDEO_INFO_HEIGHT (&out_info));
  gst_event_unref (e);

  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_SEGMENT);
  gst_event_unref (e);

  buf = create_test_video_buffer_rgba8 (&in_info);
  buf = gst_harness_push_and_pull (flip, buf);
  fail_unless (buf != NULL);
  gst_buffer_unref (buf);

  g_object_set (flip->element, "video-direction", 1 /* 90r */ , NULL);

  buf = create_test_video_buffer_rgba8 (&in_info);
  fail_unless_equals_int (gst_harness_push (flip, buf), GST_FLOW_OK);
  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_CAPS);
  gst_event_parse_caps (e, &out_caps);
  fail_unless (gst_video_info_from_caps (&out_info, out_caps));
  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (&in_info),
      GST_VIDEO_INFO_HEIGHT (&out_info));
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (&in_info),
      GST_VIDEO_INFO_WIDTH (&out_info));
  gst_event_unref (e);
  buf = gst_harness_pull (flip);
  fail_unless (buf != NULL);
  gst_buffer_unref (buf);

  gst_harness_teardown (flip);
}

GST_END_TEST;

GST_START_TEST (test_change_method_twice_same_caps_different_method)
{
  GstHarness *flip = gst_harness_new ("videoflip");
  GstVideoInfo in_info, out_info;
  GstCaps *in_caps, *out_caps;
  GstEvent *e;
  GstBuffer *input, *output, *buf;
  GstMapInfo in_map_info, out_map_info;

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, 4, 9);
  in_caps = gst_video_info_to_caps (&in_info);

  gst_harness_set_src_caps (flip, in_caps);

  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_STREAM_START);
  gst_event_unref (e);
  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_CAPS);
  gst_event_parse_caps (e, &out_caps);
  fail_unless (gst_video_info_from_caps (&out_info, out_caps));
  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (&in_info),
      GST_VIDEO_INFO_WIDTH (&out_info));
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (&in_info),
      GST_VIDEO_INFO_HEIGHT (&out_info));
  gst_event_unref (e);

  e = gst_harness_pull_event (flip);
  fail_unless_equals_int (GST_EVENT_TYPE (e), GST_EVENT_SEGMENT);
  gst_event_unref (e);

  buf = create_test_video_buffer_rgba8 (&in_info);
  buf = gst_harness_push_and_pull (flip, buf);
  fail_unless (buf != NULL);
  gst_buffer_unref (buf);

  g_object_set (flip->element, "video-direction", 1 /* 90r */ , NULL);
  g_object_set (flip->element, "video-direction", 2 /* 180 */ , NULL);

  input = create_test_video_buffer_rgba8 (&in_info);
  fail_unless_equals_int (gst_harness_push (flip, gst_buffer_ref (input)),
      GST_FLOW_OK);
  /* caps will not change and basetransform won't send updated ones so we
   * can't check for them */
  output = gst_harness_pull (flip);
  fail_unless (output != NULL);

  fail_unless (gst_buffer_map (input, &in_map_info, GST_MAP_READ));
  fail_unless (gst_buffer_map (output, &out_map_info, GST_MAP_READ));

  {
    gsize top_right = (GST_VIDEO_INFO_WIDTH (&in_info) - 1) * 4;
    gsize bottom_left =
        (GST_VIDEO_INFO_HEIGHT (&out_info) -
        1) * GST_VIDEO_INFO_PLANE_STRIDE (&out_info, 0);

    fail_unless_equals_int (in_map_info.data[top_right + 0],
        out_map_info.data[bottom_left + 0]);
    fail_unless_equals_int (in_map_info.data[top_right + 1],
        out_map_info.data[bottom_left + 1]);
    fail_unless_equals_int (in_map_info.data[top_right + 2],
        out_map_info.data[bottom_left + 2]);
    fail_unless_equals_int (in_map_info.data[top_right + 3],
        out_map_info.data[bottom_left + 3]);
  }

  gst_buffer_unmap (input, &in_map_info);
  gst_buffer_unmap (output, &out_map_info);

  gst_buffer_unref (input);
  gst_buffer_unref (output);

  gst_harness_teardown (flip);
}

GST_END_TEST;
GST_START_TEST (test_stress_change_method)
{
  GstHarness *flip = gst_harness_new ("videoflip");
  GParamSpec *pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (flip->element),
      "video-direction");
  GstHarnessThread *thread_identity, *thread_90r;
  GValue direction_identity = G_VALUE_INIT, direction_90r = G_VALUE_INIT;
  GstVideoInfo in_info;
  guint i = 0;
#define N_PUSHES 1000

  gst_video_info_set_format (&in_info, GST_VIDEO_FORMAT_RGBA, 4, 9);
  gst_harness_set_src_caps (flip, gst_video_info_to_caps (&in_info));

  g_value_init (&direction_identity, pspec->value_type);
  g_value_init (&direction_90r, pspec->value_type);

  fail_unless (gst_value_deserialize_with_pspec (&direction_identity,
          "identity", pspec));
  fail_unless (gst_value_deserialize_with_pspec (&direction_90r, "90r", pspec));

  thread_identity =
      gst_harness_stress_property_start_full (flip, "video-direction",
      &direction_identity, 210);
  thread_90r =
      gst_harness_stress_property_start_full (flip, "video-direction",
      &direction_90r, 160);

  while (i++ < N_PUSHES) {
    GstBuffer *buf = create_test_video_buffer_rgba8 (&in_info);
    buf = gst_harness_push_and_pull (flip, buf);
    fail_unless (buf != NULL);
    gst_buffer_unref (buf);
    g_usleep (100);
  }

  gst_harness_stress_thread_stop (thread_identity);
  gst_harness_stress_thread_stop (thread_90r);

  g_value_unset (&direction_identity);
  g_value_unset (&direction_90r);

  gst_harness_teardown (flip);
}

GST_END_TEST;

static Suite *
videoflip_suite (void)
{
  Suite *s = suite_create ("videoflip");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_passthrough);
  tcase_add_test (tc_chain, test_change_method);
  tcase_add_test (tc_chain,
      test_change_method_twice_same_caps_different_method);
  tcase_add_test (tc_chain, test_stress_change_method);

  return s;
}

GST_CHECK_MAIN (videoflip);

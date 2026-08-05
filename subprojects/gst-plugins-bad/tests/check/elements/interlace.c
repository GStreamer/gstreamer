/* GStreamer
 * unit test for interlace
 *
 * Copyright (C) 2021 Vivia Nikolaidou <vivia at ahiru dot eu>
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
#include <gst/video/video.h>

GST_START_TEST (test_passthrough)
{
  GstBuffer *buffer;
  GstHarness *h;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 1, "top-field-first", TRUE,
      NULL);
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=interleaved,field-order=top-field-first,format=AYUV,height=1,width=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_set (h, "interlace", "field-pattern", 1, "top-field-first", FALSE,
      NULL);
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=interleaved,field-order=bottom-field-first,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_reject_passthrough_mixed)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("interlace");
  gst_harness_play (h);

  gst_harness_set (h, "interlace", "field-pattern", 3, NULL);
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=mixed,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_field_switch)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 1, "top-field-first", FALSE,
      NULL);
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=interleaved,field-order=top-field-first,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_set (h, "interlace", "field-pattern", 1, "top-field-first", TRUE,
      NULL);
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=interleaved,field-order=bottom-field-first,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_framerate_2_2)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 1, "top-field-first", TRUE,
      NULL);
  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=1/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=1/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=2/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_framerate_1_1)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 0, "top-field-first", TRUE,
      NULL);
  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=1/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=1/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=2/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_framerate_3_2)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 2, NULL);
  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=30/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=24/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=1/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=1/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_framerate_empty_not_negotiated)
{
  GstHarness *h;
  GstBuffer *buffer;

  h = gst_harness_new ("interlace");

  gst_harness_set_sink_caps_str (h, "EMPTY");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=24/1");
  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer),
      GST_FLOW_NOT_NEGOTIATED);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_upstream_not_fixed)
{
  GstBuffer *buffer;
  GstHarness *h;

  h = gst_harness_new_parse ("videorate ! interlace field-pattern=0");

  gst_harness_set_src_caps_str (h,
      "video/x-raw, format=(string)AYUV64, width=(int)1, height=(int)1, multiview-mode=(string)mono, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive");
  gst_harness_set_sink_caps_str (h,
      "video/x-raw,interlace-mode=interleaved,framerate=25/1");

  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_downstream_any)
{
  GstBuffer *buffer;
  GstHarness *h;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 0, NULL);
  gst_harness_set_src_caps_str (h,
      "video/x-raw, format=(string)AYUV, width=(int)1, height=(int)1, framerate=(fraction)25/1, multiview-mode=(string)mono, interlace-mode=(string)progressive, pixel-aspect-ratio=(fraction)1/1");

  buffer = gst_harness_create_buffer (h, 4);
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_timecode_forward_1_1)
{
  GstHarness *h;
  GstBuffer *buffer;
  GstVideoTimeCodeMeta *tc_meta;
  guint i;
  guint next_frames = 0;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 0, NULL);
  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=30/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=60/1");

  for (i = 0; i < 4; i++) {
    buffer = gst_harness_create_buffer (h, 4);
    tc_meta = gst_buffer_add_video_time_code_meta_full (buffer, 60, 1, NULL,
        GST_VIDEO_TIME_CODE_FLAGS_NONE, 0, 0, 0, i, 0);
    fail_unless (tc_meta);
    fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);
  }

  gst_harness_push_event (h, gst_event_new_eos ());

  while (gst_harness_pull_until_eos (h, &buffer)) {
    if (!buffer)
      break;

    tc_meta = gst_buffer_get_video_time_code_meta (buffer);
    fail_unless (tc_meta);

    fail_unless_equals_int (tc_meta->tc.hours, 0);
    fail_unless_equals_int (tc_meta->tc.minutes, 0);
    fail_unless_equals_int (tc_meta->tc.seconds, 0);
    fail_unless_equals_int (tc_meta->tc.frames, next_frames);
    gst_buffer_unref (buffer);

    next_frames++;
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_timecode_forward_2_2)
{
  GstHarness *h;
  GstBuffer *buffer;
  GstVideoTimeCode *tc_list[4];
  GstVideoTimeCodeMeta *tc_meta;
  guint i;

  h = gst_harness_new ("interlace");

  gst_harness_set (h, "interlace", "field-pattern", 1, NULL);
  gst_harness_set_sink_caps_str (h, "video/x-raw,framerate=30/1");
  gst_harness_set_src_caps_str (h,
      "video/x-raw,interlace-mode=progressive,format=AYUV,width=1,height=1,framerate=30/1");

  /* Setup original timecode metas */
  for (i = 0; i < G_N_ELEMENTS (tc_list); i++) {
    tc_list[i] = gst_video_time_code_new (30, 1, NULL,
        GST_VIDEO_TIME_CODE_FLAGS_NONE, 0, 0, 0, 0, 0);
    gst_video_time_code_add_frames (tc_list[i], i);
  }

  for (i = 0; i < G_N_ELEMENTS (tc_list); i++) {
    buffer = gst_harness_create_buffer (h, 4);
    tc_meta = gst_buffer_add_video_time_code_meta (buffer, tc_list[i]);
    fail_unless (tc_meta);
    fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);
  }

  gst_harness_push_event (h, gst_event_new_eos ());

  i = 0;
  while (gst_harness_pull_until_eos (h, &buffer)) {
    GstVideoTimeCode *src_tc;
    GstVideoTimeCode *dst_tc;
    if (!buffer)
      break;

    tc_meta = gst_buffer_get_video_time_code_meta (buffer);
    fail_unless (tc_meta);

    src_tc = tc_list[i];
    dst_tc = &tc_meta->tc;

    /* timecode's time info should be preserved */
    fail_unless_equals_int (src_tc->hours, dst_tc->hours);
    fail_unless_equals_int (src_tc->minutes, dst_tc->minutes);
    fail_unless_equals_int (src_tc->seconds, dst_tc->seconds);
    fail_unless_equals_int (src_tc->frames, dst_tc->frames);

    /* interlace should update field infos */
    fail_unless_equals_int (dst_tc->field_count, 1);
    fail_unless_equals_int (src_tc->
        config.flags | GST_VIDEO_TIME_CODE_FLAGS_INTERLACED,
        dst_tc->config.flags);

    gst_buffer_unref (buffer);
    i++;
  }

  gst_harness_teardown (h);
  for (i = 0; i < G_N_ELEMENTS (tc_list); i++)
    gst_video_time_code_free (tc_list[i]);
}

GST_END_TEST;

static Suite *
interlace_suite (void)
{
  Suite *s = suite_create ("interlace");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_passthrough);
  tcase_add_test (tc_chain, test_reject_passthrough_mixed);
  tcase_add_test (tc_chain, test_field_switch);
  tcase_add_test (tc_chain, test_framerate_2_2);
  tcase_add_test (tc_chain, test_framerate_1_1);
  tcase_add_test (tc_chain, test_framerate_3_2);
  tcase_add_test (tc_chain, test_framerate_empty_not_negotiated);
  tcase_add_test (tc_chain, test_downstream_any);
  tcase_add_test (tc_chain, test_upstream_not_fixed);
  tcase_add_test (tc_chain, test_timecode_forward_1_1);
  tcase_add_test (tc_chain, test_timecode_forward_2_2);

  return s;
}

GST_CHECK_MAIN (interlace);

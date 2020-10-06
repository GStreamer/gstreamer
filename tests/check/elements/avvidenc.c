/* GStreamer
 *
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

GST_START_TEST (test_videoenc_drain)
{
  GstHarness *h;
  GstVideoInfo info;
  GstBuffer *in_buf;
  gint i = 0;
  gint num_output = 0;
  GstFlowReturn ret;
  GstSegment segment;
  GstCaps *caps;

  h = gst_harness_new ("avenc_mjpeg");
  fail_unless (h != NULL);

  caps = gst_caps_from_string ("video/x-raw,format=I420,width=64,height=64");

  gst_harness_set_src_caps (h, gst_caps_copy (caps));
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, 64, 64);

  for (i = 0; i < 2; i++) {
    in_buf = gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&info));

    GST_BUFFER_PTS (in_buf) = i * GST_SECOND;
    GST_BUFFER_DURATION (in_buf) = GST_SECOND;

    ret = gst_harness_push (h, in_buf);

    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));
  }

  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_segment_set_running_time (&segment, GST_FORMAT_TIME,
          2 * GST_SECOND));

  /* Push new eos event to drain encoder */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));

  /* And start new stream */
  fail_unless (gst_harness_push_event (h,
          gst_event_new_stream_start ("new-stream-id")));
  gst_harness_set_src_caps (h, caps);
  fail_unless (gst_harness_push_event (h, gst_event_new_segment (&segment)));

  in_buf = gst_buffer_new_and_alloc (GST_VIDEO_INFO_SIZE (&info));

  GST_BUFFER_PTS (in_buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (in_buf) = GST_SECOND;

  ret = gst_harness_push (h, in_buf);
  fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
      gst_flow_get_name (ret));

  /* Finish encoding and drain again */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));
  do {
    GstBuffer *out_buf = NULL;

    out_buf = gst_harness_try_pull (h);
    if (out_buf) {
      num_output++;
      gst_buffer_unref (out_buf);
      continue;
    }

    break;
  } while (1);

  fail_unless_equals_int (num_output, 3);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
avvidenc_suite (void)
{
  Suite *s = suite_create ("avvidenc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_videoenc_drain);

  return s;
}

GST_CHECK_MAIN (avvidenc)

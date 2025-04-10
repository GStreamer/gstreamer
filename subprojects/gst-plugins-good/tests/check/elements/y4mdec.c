/* GStreamer
 *
 * unit test for y4m
 *
 * Copyright (C) <2025> Igalia, S.L.
 *                Author: Victor M. Jaquez L. <vjaquez@igalia.com>
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

#include "y4mdata.c"

static GstBuffer *
run_test (GstHarness * h, gpointer data, guint size)
{
  GstBuffer *inbuf, *outbuf;
  GstFlowReturn ret;
  gsize len;

  gst_harness_set_src_caps_str (h, "application/x-yuv4mpeg,y4mversion=2");

  inbuf = gst_buffer_new_and_alloc (size);
  fail_unless (inbuf);
  len = gst_buffer_fill (inbuf, 0, data, size);
  fail_unless (len == size);

  ret = gst_harness_push (h, inbuf);
  fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
      gst_flow_get_name (ret));

  outbuf = gst_harness_pull (h);
  fail_unless (outbuf);

  return outbuf;
}

GST_START_TEST (test_y4m_i420_padded_square)
{
  GstHarness *h;
  GstBuffer *outbuf;
  GstVideoInfo info;

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_I420, 15, 15);
  fail_unless (GST_VIDEO_INFO_SIZE (&info) == red_box_i420_15x15_yuv_len);

  h = gst_harness_new ("y4mdec");

  outbuf = run_test (h, red_box_y4m, red_box_y4m_len);

  {
    GstMapInfo map;
    guint i, j;

    fail_unless (gst_buffer_map (outbuf, &map, GST_MAP_READ));
    GST_MEMDUMP ("decoded red box:", map.data, map.size);

    /* Check for a red square */
    /* Y */
    /* 0xf bit per row is padded: ignore */
    for (i = 0; i < 0xf; i++) {
      for (j = 0; j < 0xf; j++) {
        fail_unless (map.data[i * 0x10 + j] == 0x51, "index %x failed",
            i * 0x10 + j);
      }
    }
    /* 0xf0-0xff padded: ignore */
    /* U  */
    for (i = 0x100; i < 0x140; i++)
      fail_unless (map.data[i] == 0x5a);
    /* V */
    for (i = 0x140; i < 0x180; i++)
      fail_unless (map.data[i] == 0xf0);

    gst_buffer_unmap (outbuf, &map);
  }

  gst_buffer_unref (outbuf);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_y4m_y42b_square)
{
  GstHarness *h;
  GstBuffer *outbuf;
  GstVideoInfo info;

  h = gst_harness_new ("y4mdec");

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_Y42B, 16, 16);
  fail_unless (GST_VIDEO_INFO_SIZE (&info) == red_box_y42b_16x16_yuv_len);

  outbuf = run_test (h, red_box_y42b_16x16_y4m, red_box_y42b_16x16_y4m_len);

  {
    GstMapInfo map;
    guint cmp;

    fail_unless (gst_buffer_map (outbuf, &map, GST_MAP_READ));
    GST_MEMDUMP ("decoded red box:", map.data, map.size);
    cmp = memcmp (map.data, red_box_y42b_16x16_yuv, red_box_i420_15x15_yuv_len);
    fail_unless (cmp == 0);
    gst_buffer_unmap (outbuf, &map);
  }

  gst_buffer_unref (outbuf);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
y4mdec_suite (void)
{
  Suite *s = suite_create ("y4mdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_y4m_i420_padded_square);
  tcase_add_test (tc_chain, test_y4m_y42b_square);

  return s;
}

GST_CHECK_MAIN (y4mdec);

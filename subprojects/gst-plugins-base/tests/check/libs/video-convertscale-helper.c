/* GStreamer unit test for video convertscale helpers
 *
 * Copyright (C) <2026> Netflix Inc.
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
#include <gst/video/video.h>

GST_START_TEST (test_fixate_multiple_formats)
{
  /* Test that when multiple formats and resolutions are possible, it picks the
   * passthrough one. */
  GstElement *element = gst_element_factory_make ("videoconvertscale", NULL);
  GstCaps *incaps =
      gst_caps_from_string
      ("video/x-raw,format=YUY2,width=(int)1280,height=(int)720");
  GstCaps *outcaps =
      gst_caps_from_string
      ("video/x-raw,format=YUY2,width=(int)640,height=(int)480;video/x-raw,format=RGB,width=(int)1280,height=(int)720;video/x-raw,format=YUY2,width=(int)1280,height=(int)720");
  GstCaps *expected =
      gst_caps_from_string
      ("video/x-raw,format=YUY2,width=1280,height=720,pixel-aspect-ratio=(fraction)1/1");
  GstCaps *fixed = gst_video_convertscale_fixate_caps (GST_OBJECT (element),
      GST_PAD_SRC, GST_VIDEO_ORIENTATION_IDENTITY, NULL,
      GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT |
      GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE, incaps, outcaps);
  fail_unless (fixed != NULL);
  fail_unless (gst_caps_is_equal (fixed, expected));
  gst_caps_unref (incaps);
  gst_caps_unref (outcaps);
  gst_caps_unref (expected);
  gst_caps_unref (fixed);
  gst_object_unref (element);
}

GST_END_TEST;

static Suite *
video_convertscale_helper_suite (void)
{
  Suite *s = suite_create ("video convertscale helper");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_fixate_multiple_formats);

  return s;
}

GST_CHECK_MAIN (video_convertscale_helper);

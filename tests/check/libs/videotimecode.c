/* GStreamer
 *
 * Copyright (C) 2016 Vivia Nikolaidou <vivia@toolsonair.com>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/video/video.h>


GST_START_TEST (videotimecode_compare_equal)
{
  GstVideoTimeCode *tc1, *tc2;

  tc2 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 0);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_fps_n)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc2 = gst_video_time_code_new (25, 1, NULL, 0, 10, 10, 10, 10, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_fps_d)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc2 = gst_video_time_code_new (50, 2, NULL, 0, 10, 10, 10, 10, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_frames)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc2 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 9, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_seconds)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc2 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 9, 10, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_minutes)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc2 = gst_video_time_code_new (50, 1, NULL, 0, 10, 9, 10, 10, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_hours)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  tc2 = gst_video_time_code_new (50, 1, NULL, 0, 9, 10, 10, 10, 0);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_compare_fieldcounts)
{
  GstVideoTimeCode *tc1, *tc2;

  tc1 =
      gst_video_time_code_new (50, 1, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_INTERLACED, 10, 10, 10, 10, 2);
  tc2 =
      gst_video_time_code_new (50, 1, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_INTERLACED, 10, 10, 10, 10, 1);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  fail_unless (gst_video_time_code_compare (tc2, tc1) == -1);
  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_10)
{
  GstVideoTimeCode *tc1;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 10, 10, 10, 10, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 10);
  fail_unless (tc1->minutes == 10);
  fail_unless (tc1->seconds == 10);
  fail_unless (tc1->frames == 11);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_0)
{
  GstVideoTimeCode *tc1;

  tc1 = gst_video_time_code_new (50, 1, NULL, 0, 0, 0, 0, 0, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 0);
  fail_unless (tc1->minutes == 0);
  fail_unless (tc1->seconds == 0);
  fail_unless (tc1->frames == 1);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_high)
{
  GstVideoTimeCode *tc1;
  /* Make sure nothing overflows */

  tc1 = gst_video_time_code_new (60, 1, NULL, 0, 23, 59, 59, 58, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 23);
  fail_unless (tc1->minutes == 59);
  fail_unless (tc1->seconds == 59);
  fail_unless (tc1->frames == 59);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_dropframe)
{
  GstVideoTimeCode *tc1;

  tc1 =
      gst_video_time_code_new (30000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 10, 10, 10, 10, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 10);
  fail_unless (tc1->minutes == 10);
  fail_unless (tc1->seconds == 10);
  fail_unless (tc1->frames == 11);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_framedropped)
{
  GstVideoTimeCode *tc1;

  tc1 =
      gst_video_time_code_new (30000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 10, 10, 59, 29, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 10);
  fail_unless (tc1->minutes == 11);
  fail_unless (tc1->seconds == 0);
  fail_unless (tc1->frames == 2);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_wrapover)
{
  GstVideoTimeCode *tc1;

  tc1 =
      gst_video_time_code_new (30000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 23, 59, 59, 29, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 0);
  fail_unless (tc1->minutes == 0);
  fail_unless (tc1->seconds == 0);
  fail_unless (tc1->frames == 0);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_60drop_dropframe)
{
  GstVideoTimeCode *tc1;

  tc1 =
      gst_video_time_code_new (60000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 10, 10, 10, 10, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 10);
  fail_unless (tc1->minutes == 10);
  fail_unless (tc1->seconds == 10);
  fail_unless (tc1->frames == 11);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_60drop_framedropped)
{
  GstVideoTimeCode *tc1;

  tc1 =
      gst_video_time_code_new (60000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 10, 10, 59, 59, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 10);
  fail_unless (tc1->minutes == 11);
  fail_unless (tc1->seconds == 0);
  fail_unless (tc1->frames == 4);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_60drop_wrapover)
{
  GstVideoTimeCode *tc1;
  /* Make sure nothing overflows here either */

  tc1 =
      gst_video_time_code_new (60000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 23, 59, 59, 59, 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (tc1->hours == 0);
  fail_unless (tc1->minutes == 0);
  fail_unless (tc1->seconds == 0);
  fail_unless (tc1->frames == 0);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_addframe_loop)
{
  GstVideoTimeCode *tc1;
  guint64 i;
  /* Loop for an hour and a bit, make sure no assertions explode */

  tc1 =
      gst_video_time_code_new (60000, 1001, NULL,
      GST_VIDEO_TIME_CODE_FLAGS_DROP_FRAME, 12, 12, 12, 12, 0);
  for (i = 0; i < 220000; i++)
    gst_video_time_code_increment_frame (tc1);
  gst_video_time_code_init (tc1, 60, 1, NULL, 0, 12, 12, 12, 12, 0);
  for (i = 0; i < 220000; i++)
    gst_video_time_code_increment_frame (tc1);
  gst_video_time_code_free (tc1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_dailyjam_todatetime)
{
  GstVideoTimeCode *tc1;
  GDateTime *dt1, *dt2;

  dt1 = g_date_time_new_utc (2016, 7, 29, 10, 32, 50);

  tc1 =
      gst_video_time_code_new (50, 1, dt1,
      GST_VIDEO_TIME_CODE_FLAGS_NONE, 0, 0, 0, 0, 0);
  /* 1 hour, 4 minutes, 3 seconds, and 2 frames */
  gst_video_time_code_add_frames (tc1, 192152);
  fail_unless (tc1->hours == 1);
  fail_unless (tc1->minutes == 4);
  fail_unless (tc1->seconds == 3);
  fail_unless (tc1->frames == 2);

  dt2 = gst_video_time_code_to_date_time (tc1);
  fail_unless (g_date_time_get_year (dt2) == 2016);
  fail_unless (g_date_time_get_month (dt2) == 7);
  fail_unless (g_date_time_get_day_of_month (dt2) == 29);
  fail_unless (g_date_time_get_hour (dt2) == 11);
  fail_unless (g_date_time_get_minute (dt2) == 36);
  fail_unless (g_date_time_get_seconds (dt2) == 53.04);

  gst_video_time_code_free (tc1);
  g_date_time_unref (dt2);
  g_date_time_unref (dt1);
}

GST_END_TEST;

GST_START_TEST (videotimecode_dailyjam_compare)
{
  GstVideoTimeCode *tc1, *tc2;
  GDateTime *dt1;

  dt1 = g_date_time_new_utc (2016, 7, 29, 10, 32, 50);

  tc1 =
      gst_video_time_code_new (50, 1, dt1,
      GST_VIDEO_TIME_CODE_FLAGS_NONE, 0, 0, 0, 0, 0);
  tc2 = gst_video_time_code_copy (tc1);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 0);
  gst_video_time_code_increment_frame (tc1);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == 1);
  gst_video_time_code_add_frames (tc2, 2);
  fail_unless (gst_video_time_code_compare (tc1, tc2) == -1);

  gst_video_time_code_free (tc1);
  gst_video_time_code_free (tc2);
  g_date_time_unref (dt1);
}

GST_END_TEST;

static Suite *
gst_videotimecode_suite (void)
{
  Suite *s = suite_create ("GstVideoTimeCode");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, videotimecode_compare_equal);
  tcase_add_test (tc, videotimecode_compare_fps_n);
  tcase_add_test (tc, videotimecode_compare_fps_d);
  tcase_add_test (tc, videotimecode_compare_frames);
  tcase_add_test (tc, videotimecode_compare_seconds);
  tcase_add_test (tc, videotimecode_compare_minutes);
  tcase_add_test (tc, videotimecode_compare_hours);
  tcase_add_test (tc, videotimecode_compare_fieldcounts);

  tcase_add_test (tc, videotimecode_addframe_10);
  tcase_add_test (tc, videotimecode_addframe_0);
  tcase_add_test (tc, videotimecode_addframe_high);
  tcase_add_test (tc, videotimecode_addframe_dropframe);
  tcase_add_test (tc, videotimecode_addframe_framedropped);
  tcase_add_test (tc, videotimecode_addframe_wrapover);
  tcase_add_test (tc, videotimecode_addframe_60drop_dropframe);
  tcase_add_test (tc, videotimecode_addframe_60drop_framedropped);
  tcase_add_test (tc, videotimecode_addframe_60drop_wrapover);
  tcase_add_test (tc, videotimecode_addframe_loop);

  tcase_add_test (tc, videotimecode_dailyjam_todatetime);
  tcase_add_test (tc, videotimecode_dailyjam_compare);
  return s;
}

GST_CHECK_MAIN (gst_videotimecode);

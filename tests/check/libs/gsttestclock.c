/*
 * Unit test for a deterministic clock for Gstreamer unit tests
 *
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2012 Sebastian Rasmussen <sebastian.rasmussen@axis.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/check/gstcheck.h>
#include <gst/check/gsttestclock.h>

GST_START_TEST (test_object_flags)
{
  GstClock *clock = gst_test_clock_new ();
  g_assert (GST_OBJECT_FLAG_IS_SET (clock, GST_CLOCK_FLAG_CAN_DO_SINGLE_SYNC));
  g_assert (GST_OBJECT_FLAG_IS_SET (clock, GST_CLOCK_FLAG_CAN_DO_SINGLE_ASYNC));
  g_assert (GST_OBJECT_FLAG_IS_SET (clock,
          GST_CLOCK_FLAG_CAN_DO_PERIODIC_SYNC));
  g_assert (GST_OBJECT_FLAG_IS_SET (clock,
          GST_CLOCK_FLAG_CAN_DO_PERIODIC_ASYNC));
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_resolution_query)
{
  GstClock *clock = gst_test_clock_new ();
  g_assert_cmpuint (gst_clock_get_resolution (clock), ==, 1);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_start_time)
{
  GstClock *clock;
  guint64 start_time;

  clock = gst_test_clock_new ();
  g_assert_cmpuint (gst_clock_get_time (clock), ==, 0);
  g_object_get (clock, "start-time", &start_time, NULL);
  g_assert_cmpuint (start_time, ==, 0);
  gst_object_unref (clock);

  clock = gst_test_clock_new_with_start_time (GST_SECOND);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND);
  g_object_get (clock, "start-time", &start_time, NULL);
  g_assert_cmpuint (start_time, ==, GST_SECOND);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_set_time)
{
  GstClock *clock = gst_test_clock_new_with_start_time (GST_SECOND);
  gst_test_clock_set_time (GST_TEST_CLOCK (clock), GST_SECOND);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND);
  gst_test_clock_set_time (GST_TEST_CLOCK (clock), GST_SECOND + 1);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND + 1);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_advance_time)
{
  GstClock *clock = gst_test_clock_new_with_start_time (GST_SECOND);
  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 0);
  g_assert_cmpuint (gst_clock_get_time (clock), ==, GST_SECOND);
  gst_test_clock_advance_time (GST_TEST_CLOCK (clock), 42 * GST_MSECOND);
  g_assert_cmpuint (gst_clock_get_time (clock), ==,
      GST_SECOND + (42 * GST_MSECOND));
  gst_object_unref (clock);
}

GST_END_TEST;

static Suite *
gst_test_clock_suite (void)
{
  Suite *s = suite_create ("GstTestClock");
  TCase *tc_chain = tcase_create ("testclock");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_object_flags);
  tcase_add_test (tc_chain, test_resolution_query);
  tcase_add_test (tc_chain, test_start_time);
  tcase_add_test (tc_chain, test_set_time);
  tcase_add_test (tc_chain, test_advance_time);

  return s;
}

GST_CHECK_MAIN (gst_test_clock);

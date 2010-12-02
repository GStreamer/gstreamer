/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
 * Copyright (C) 2010 Christian Hergert <chris@dronelabs.com>
 *
 * gstdatetime.c: Unit tests for GstDateTime
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

#include <string.h>
#include <time.h>
#include <gst/check/gstcheck.h>

#define ASSERT_TIME(dt,H,M,S) G_STMT_START { \
  assert_equals_int ((H), gst_date_time_get_hour ((dt))); \
  assert_equals_int ((M), gst_date_time_get_minute ((dt))); \
  assert_equals_int ((S), gst_date_time_get_second ((dt))); \
} G_STMT_END

GST_START_TEST (test_GstDateTime_now)
{
  GstDateTime *dt;
  time_t t;
  struct tm tm;

  memset (&tm, 0, sizeof (tm));
  t = time (NULL);
#ifdef HAVE_LOCALTIME_R
  localtime_r (&t, &tm);
#else
  memcpy (&tm, localtime (&t), sizeof (struct tm));
#endif
  dt = gst_date_time_new_now_local_time ();
  assert_equals_int (gst_date_time_get_year (dt), 1900 + tm.tm_year);
  assert_equals_int (gst_date_time_get_month (dt), 1 + tm.tm_mon);
  assert_equals_int (gst_date_time_get_day (dt), tm.tm_mday);
  assert_equals_int (gst_date_time_get_hour (dt), tm.tm_hour);
  assert_equals_int (gst_date_time_get_minute (dt), tm.tm_min);
  assert_equals_int (gst_date_time_get_second (dt), tm.tm_sec);
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_new_from_unix_epoch_local_time)
{
  GstDateTime *dt;
  struct tm tm;
  time_t t;

  memset (&tm, 0, sizeof (tm));
  t = time (NULL);
#ifdef HAVE_LOCALTIME_R
  localtime_r (&t, &tm);
#else
  memcpy (&tm, localtime (&t), sizeof (struct tm));
#endif
  dt = gst_date_time_new_from_unix_epoch_local_time (t);
  assert_equals_int (gst_date_time_get_year (dt), 1900 + tm.tm_year);
  assert_equals_int (gst_date_time_get_month (dt), 1 + tm.tm_mon);
  assert_equals_int (gst_date_time_get_day (dt), tm.tm_mday);
  assert_equals_int (gst_date_time_get_hour (dt), tm.tm_hour);
  assert_equals_int (gst_date_time_get_minute (dt), tm.tm_min);
  assert_equals_int (gst_date_time_get_second (dt), tm.tm_sec);
  gst_date_time_unref (dt);

  memset (&tm, 0, sizeof (tm));
  tm.tm_year = 70;
  tm.tm_mday = 1;
  tm.tm_mon = 0;
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  t = mktime (&tm);
  dt = gst_date_time_new_from_unix_epoch_local_time (t);
  assert_equals_int (gst_date_time_get_year (dt), 1970);
  assert_equals_int (gst_date_time_get_month (dt), 1);
  assert_equals_int (gst_date_time_get_day (dt), 1);
  assert_equals_int (gst_date_time_get_hour (dt), 0);
  assert_equals_int (gst_date_time_get_minute (dt), 0);
  assert_equals_int (gst_date_time_get_second (dt), 0);
  gst_date_time_unref (dt);
}

GST_END_TEST;


GST_START_TEST (test_GstDateTime_new_from_unix_epoch_utc)
{
  GstDateTime *dt;
  struct tm tm;
  time_t t;

  memset (&tm, 0, sizeof (tm));
  t = time (NULL);
#ifdef HAVE_GMTIME_R
  gmtime_r (&t, &tm);
#else
  memcpy (&tm, gmtime (&t), sizeof (struct tm));
#endif
  dt = gst_date_time_new_from_unix_epoch_utc (t);
  assert_equals_int (gst_date_time_get_year (dt), 1900 + tm.tm_year);
  assert_equals_int (gst_date_time_get_month (dt), 1 + tm.tm_mon);
  assert_equals_int (gst_date_time_get_day (dt), tm.tm_mday);
  assert_equals_int (gst_date_time_get_hour (dt), tm.tm_hour);
  assert_equals_int (gst_date_time_get_minute (dt), tm.tm_min);
  assert_equals_int (gst_date_time_get_second (dt), tm.tm_sec);
  assert_equals_int (gst_date_time_get_time_zone_offset (dt), 0);
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_dmy)
{
  GstDateTime *dt;
  time_t t;
  struct tm tt;

  t = time (NULL);
#ifdef HAVE_LOCALTIME_R
  localtime_r (&t, &tt);
#else
  memcpy (&tt, localtime (&t), sizeof (struct tm));
#endif
  dt = gst_date_time_new_from_unix_epoch_local_time (t);
  assert_equals_int (gst_date_time_get_year (dt), tt.tm_year + 1900);
  assert_equals_int (gst_date_time_get_month (dt), tt.tm_mon + 1);
  assert_equals_int (gst_date_time_get_day (dt), tt.tm_mday);

  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_hour)
{
  GstDateTime *dt;

  dt = gst_date_time_new (0, 2009, 10, 19, 15, 13, 11);
  assert_equals_int (15, gst_date_time_get_hour (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0, 100, 10, 19, 1, 0, 0);
  assert_equals_int (1, gst_date_time_get_hour (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0, 100, 10, 19, 0, 0, 0);
  assert_equals_int (0, gst_date_time_get_hour (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0, 100, 10, 1, 23, 59, 59);
  assert_equals_int (23, gst_date_time_get_hour (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_microsecond)
{
  GTimeVal tv;
  GstDateTime *dt;

  g_get_current_time (&tv);
  dt = gst_date_time_new (0, 2010, 7, 15, 11, 12,
      13 + (tv.tv_usec / 1000000.0));
  assert_equals_int (tv.tv_usec, gst_date_time_get_microsecond (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_minute)
{
  GstDateTime *dt;

  dt = gst_date_time_new (0, 2009, 12, 1, 1, 31, 0);
  assert_equals_int (31, gst_date_time_get_minute (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_second)
{
  GstDateTime *dt;

  dt = gst_date_time_new (0, 2009, 12, 1, 1, 31, 44);
  assert_equals_int (44, gst_date_time_get_second (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_new_full)
{
  GstDateTime *dt;

  dt = gst_date_time_new (0, 2009, 12, 11, 12, 11, 10.001234);
  assert_equals_int (2009, gst_date_time_get_year (dt));
  assert_equals_int (12, gst_date_time_get_month (dt));
  assert_equals_int (11, gst_date_time_get_day (dt));
  assert_equals_int (12, gst_date_time_get_hour (dt));
  assert_equals_int (11, gst_date_time_get_minute (dt));
  assert_equals_int (10, gst_date_time_get_second (dt));
  assert_equals_int (1234, gst_date_time_get_microsecond (dt));
  assert_equals_float (0, gst_date_time_get_time_zone_offset (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (2.5, 2010, 3, 29, 12, 13, 16.5);
  assert_equals_int (2010, gst_date_time_get_year (dt));
  assert_equals_int (3, gst_date_time_get_month (dt));
  assert_equals_int (29, gst_date_time_get_day (dt));
  assert_equals_int (12, gst_date_time_get_hour (dt));
  assert_equals_int (13, gst_date_time_get_minute (dt));
  assert_equals_int (16, gst_date_time_get_second (dt));
  assert_equals_int (500000, gst_date_time_get_microsecond (dt));
  assert_equals_float (2.5, gst_date_time_get_time_zone_offset (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_utc_now)
{
  GstDateTime *dt;
  time_t t;
  struct tm tm;

  t = time (NULL);
#ifdef HAVE_GMTIME_R
  gmtime_r (&t, &tm);
#else
  memcpy (&tm, gmtime (&t), sizeof (struct tm));
#endif
  dt = gst_date_time_new_now_utc ();
  assert_equals_int (tm.tm_year + 1900, gst_date_time_get_year (dt));
  assert_equals_int (tm.tm_mon + 1, gst_date_time_get_month (dt));
  assert_equals_int (tm.tm_mday, gst_date_time_get_day (dt));
  assert_equals_int (tm.tm_hour, gst_date_time_get_hour (dt));
  assert_equals_int (tm.tm_min, gst_date_time_get_minute (dt));
  assert_equals_int (tm.tm_sec, gst_date_time_get_second (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_utc_offset)
{
  GstDateTime *dt;
  gfloat ts;
  struct tm tm;
  time_t t;

  t = time (NULL);
  memset (&tm, 0, sizeof (tm));
#ifdef HAVE_LOCALTIME_R
  localtime_r (&t, &tm);
#else
  memcpy (&tm, localtime (&t), sizeof (struct tm));
#endif

  dt = gst_date_time_new_now_local_time ();
  ts = gst_date_time_get_time_zone_offset (dt);
  assert_equals_int (ts, tm.tm_gmtoff / 3600.0);
  gst_date_time_unref (dt);
}

GST_END_TEST;

static Suite *
gst_date_time_suite (void)
{
  Suite *s = suite_create ("GstDateTime");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_GstDateTime_get_dmy);
  tcase_add_test (tc_chain, test_GstDateTime_get_hour);
  tcase_add_test (tc_chain, test_GstDateTime_get_microsecond);
  tcase_add_test (tc_chain, test_GstDateTime_get_minute);
  tcase_add_test (tc_chain, test_GstDateTime_get_second);
  tcase_add_test (tc_chain, test_GstDateTime_get_utc_offset);
  tcase_add_test (tc_chain, test_GstDateTime_new_from_unix_epoch_local_time);
  tcase_add_test (tc_chain, test_GstDateTime_new_from_unix_epoch_utc);
  tcase_add_test (tc_chain, test_GstDateTime_new_full);
  tcase_add_test (tc_chain, test_GstDateTime_now);
  tcase_add_test (tc_chain, test_GstDateTime_utc_now);

  return s;
}

GST_CHECK_MAIN (gst_date_time);

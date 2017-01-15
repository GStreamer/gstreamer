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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <time.h>
#include <gst/check/gstcheck.h>

#define assert_almost_equals_int(a, b)                            \
G_STMT_START {                                                           \
  int first = a;                                                         \
  int second = b;                                                        \
  fail_unless(ABS (first - second) <= 1,                                 \
    "'" #a "' (%d) is not almost equal to '" #b"' (%d)", first, second); \
} G_STMT_END;

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
  assert_almost_equals_int (gst_date_time_get_second (dt), tm.tm_sec);
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
  assert_almost_equals_int (tv.tv_usec, gst_date_time_get_microsecond (dt));
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
  assert_almost_equals_int (tm.tm_sec, gst_date_time_get_second (dt));
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_get_utc_offset)
{
  struct tm tm;
  time_t t;

  t = time (NULL);
  memset (&tm, 0, sizeof (tm));
#ifdef HAVE_LOCALTIME_R
  localtime_r (&t, &tm);
#else
  memcpy (&tm, localtime (&t), sizeof (struct tm));
#endif

#ifdef HAVE_TM_GMTOFF
  {
    GstDateTime *dt;
    gfloat ts;

    dt = gst_date_time_new_now_local_time ();
    ts = gst_date_time_get_time_zone_offset (dt);
    assert_equals_int (ts, tm.tm_gmtoff / 3600.0);
    gst_date_time_unref (dt);
  }
#endif
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_partial_fields)
{
  GstDateTime *dt;

  ASSERT_CRITICAL (gst_date_time_new (0.0, -1, -1, -1, -1, -1, -1));
  ASSERT_CRITICAL (gst_date_time_new (0.0, 2012, 7, 18, 9, -1, -1));

  dt = gst_date_time_new (0.0, 2012, -1, -1, -1, -1, -1);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless_equals_int (gst_date_time_get_year (dt), 2012);
  fail_if (gst_date_time_has_month (dt));
  ASSERT_CRITICAL (gst_date_time_get_month (dt));
  fail_if (gst_date_time_has_day (dt));
  ASSERT_CRITICAL (gst_date_time_get_day (dt));
  fail_if (gst_date_time_has_time (dt));
  ASSERT_CRITICAL (gst_date_time_get_hour (dt));
  ASSERT_CRITICAL (gst_date_time_get_minute (dt));
  fail_if (gst_date_time_has_second (dt));
  ASSERT_CRITICAL (gst_date_time_get_second (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0.0, 2012, 7, -1, -1, -1, -1);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless_equals_int (gst_date_time_get_year (dt), 2012);
  fail_unless (gst_date_time_has_month (dt));
  fail_unless_equals_int (gst_date_time_get_month (dt), 7);
  fail_if (gst_date_time_has_day (dt));
  ASSERT_CRITICAL (gst_date_time_get_day (dt));
  fail_if (gst_date_time_has_time (dt));
  ASSERT_CRITICAL (gst_date_time_get_hour (dt));
  ASSERT_CRITICAL (gst_date_time_get_minute (dt));
  fail_if (gst_date_time_has_second (dt));
  ASSERT_CRITICAL (gst_date_time_get_second (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0.0, 2012, 7, 1, -1, -1, -1);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless_equals_int (gst_date_time_get_month (dt), 7);
  fail_unless (gst_date_time_has_day (dt));
  fail_unless_equals_int (gst_date_time_get_day (dt), 1);
  fail_if (gst_date_time_has_time (dt));
  fail_if (gst_date_time_has_second (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0.0, 2012, 7, 1, 18, 20, -1);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless_equals_int (gst_date_time_get_year (dt), 2012);
  fail_unless (gst_date_time_has_month (dt));
  fail_unless_equals_int (gst_date_time_get_month (dt), 7);
  fail_unless (gst_date_time_has_day (dt));
  fail_unless_equals_int (gst_date_time_get_day (dt), 1);
  fail_unless (gst_date_time_has_time (dt));
  fail_unless_equals_int (gst_date_time_get_hour (dt), 18);
  fail_unless_equals_int (gst_date_time_get_minute (dt), 20);
  fail_if (gst_date_time_has_second (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new (0.0, 2012, 7, 1, 18, 20, 25.0443);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (gst_date_time_has_time (dt));
  fail_unless (gst_date_time_has_second (dt));
  fail_unless_equals_int (gst_date_time_get_second (dt), 25);
  /* fail_unless_equals_int (gst_date_time_get_microsecond (dt), 443); */
  gst_date_time_unref (dt);
}

GST_END_TEST;

static gboolean
date_times_are_equal (GstDateTime * d1, GstDateTime * d2)
{
  GValue val1 = G_VALUE_INIT;
  GValue val2 = G_VALUE_INIT;
  int ret;

  g_value_init (&val1, GST_TYPE_DATE_TIME);
  g_value_set_boxed (&val1, d1);
  g_value_init (&val2, GST_TYPE_DATE_TIME);
  g_value_set_boxed (&val2, d2);
  ret = gst_value_compare (&val1, &val2);
  g_value_unset (&val2);
  g_value_unset (&val1);

  return ret == GST_VALUE_EQUAL;
}

GST_START_TEST (test_GstDateTime_iso8601)
{
  GstDateTime *dt, *dt2;
  gchar *str, *str2;
  GDateTime *gdt, *gdt2;

  dt = gst_date_time_new_now_utc ();
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (gst_date_time_has_time (dt));
  fail_unless (gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_int (strlen (str), strlen ("2012-06-26T22:46:43Z"));
  fail_unless (g_str_has_suffix (str, "Z"));
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (gst_date_time_get_day (dt) == gst_date_time_get_day (dt2));
  fail_unless (gst_date_time_get_hour (dt) == gst_date_time_get_hour (dt2));
  fail_unless (gst_date_time_get_minute (dt) == gst_date_time_get_minute (dt2));
  fail_unless (gst_date_time_get_second (dt) == gst_date_time_get_second (dt2));
  /* This will succeed because we're not comparing microseconds when
   * checking for equality */
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- year only ---- */

  dt = gst_date_time_new_y (2010);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (!gst_date_time_has_month (dt));
  fail_unless (!gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  fail_unless (!gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- year and month ---- */

  dt = gst_date_time_new_ym (2010, 10);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (!gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  fail_unless (!gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010-10");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- year and month ---- */

  dt = gst_date_time_new_ymd (2010, 10, 30);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  fail_unless (!gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010-10-30");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (gst_date_time_get_day (dt) == gst_date_time_get_day (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- date and time, but no seconds ---- */

  dt = gst_date_time_new (-4.5, 2010, 10, 30, 15, 50, -1);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (gst_date_time_has_time (dt));
  fail_unless (!gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010-10-30T15:50-0430");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (gst_date_time_get_day (dt) == gst_date_time_get_day (dt2));
  fail_unless (gst_date_time_get_hour (dt) == gst_date_time_get_hour (dt2));
  fail_unless (gst_date_time_get_minute (dt) == gst_date_time_get_minute (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- date and time, but no seconds (UTC) ---- */

  dt = gst_date_time_new (0, 2010, 10, 30, 15, 50, -1);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (gst_date_time_has_time (dt));
  fail_unless (!gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010-10-30T15:50Z");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (gst_date_time_get_day (dt) == gst_date_time_get_day (dt2));
  fail_unless (gst_date_time_get_hour (dt) == gst_date_time_get_hour (dt2));
  fail_unless (gst_date_time_get_minute (dt) == gst_date_time_get_minute (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- date and time, with seconds ---- */

  dt = gst_date_time_new (-4.5, 2010, 10, 30, 15, 50, 0);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (gst_date_time_has_time (dt));
  fail_unless (gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010-10-30T15:50:00-0430");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (gst_date_time_get_day (dt) == gst_date_time_get_day (dt2));
  fail_unless (gst_date_time_get_hour (dt) == gst_date_time_get_hour (dt2));
  fail_unless (gst_date_time_get_minute (dt) == gst_date_time_get_minute (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- date and time, with seconds (UTC) ---- */

  dt = gst_date_time_new (0, 2010, 10, 30, 15, 50, 0);
  fail_unless (gst_date_time_has_year (dt));
  fail_unless (gst_date_time_has_month (dt));
  fail_unless (gst_date_time_has_day (dt));
  fail_unless (gst_date_time_has_time (dt));
  fail_unless (gst_date_time_has_second (dt));
  str = gst_date_time_to_iso8601_string (dt);
  fail_unless (str != NULL);
  fail_unless_equals_string (str, "2010-10-30T15:50:00Z");
  dt2 = gst_date_time_new_from_iso8601_string (str);
  fail_unless (gst_date_time_get_year (dt) == gst_date_time_get_year (dt2));
  fail_unless (gst_date_time_get_month (dt) == gst_date_time_get_month (dt2));
  fail_unless (gst_date_time_get_day (dt) == gst_date_time_get_day (dt2));
  fail_unless (gst_date_time_get_hour (dt) == gst_date_time_get_hour (dt2));
  fail_unless (gst_date_time_get_minute (dt) == gst_date_time_get_minute (dt2));
  fail_unless (date_times_are_equal (dt, dt2));
  str2 = gst_date_time_to_iso8601_string (dt2);
  fail_unless_equals_string (str, str2);
  g_free (str2);
  gst_date_time_unref (dt2);
  g_free (str);
  gst_date_time_unref (dt);

  /* ---- date and time, but without the 'T' and without timezone */
  dt = gst_date_time_new_from_iso8601_string ("2010-10-30 15:50");
  fail_unless (gst_date_time_get_year (dt) == 2010);
  fail_unless (gst_date_time_get_month (dt) == 10);
  fail_unless (gst_date_time_get_day (dt) == 30);
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (!gst_date_time_has_second (dt));
  gst_date_time_unref (dt);

  /* ---- date and time+secs, but without the 'T' and without timezone */
  dt = gst_date_time_new_from_iso8601_string ("2010-10-30 15:50:33");
  fail_unless (gst_date_time_get_year (dt) == 2010);
  fail_unless (gst_date_time_get_month (dt) == 10);
  fail_unless (gst_date_time_get_day (dt) == 30);
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (gst_date_time_get_second (dt) == 33);
  gst_date_time_unref (dt);

  /* ---- dates with 00s */
  dt = gst_date_time_new_from_iso8601_string ("2010-10-00");
  fail_unless (gst_date_time_get_year (dt) == 2010);
  fail_unless (gst_date_time_get_month (dt) == 10);
  fail_unless (!gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new_from_iso8601_string ("2010-00-00");
  fail_unless (gst_date_time_get_year (dt) == 2010);
  fail_unless (!gst_date_time_has_month (dt));
  fail_unless (!gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new_from_iso8601_string ("2010-00-30");
  fail_unless (gst_date_time_get_year (dt) == 2010);
  fail_unless (!gst_date_time_has_month (dt));
  fail_unless (!gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  gst_date_time_unref (dt);

  /* completely invalid */
  dt = gst_date_time_new_from_iso8601_string ("0000-00-00");
  fail_unless (dt == NULL);

  /* partially invalid - here we'll just extract the year */
  dt = gst_date_time_new_from_iso8601_string ("2010/05/30");
  fail_unless (gst_date_time_get_year (dt) == 2010);
  fail_unless (!gst_date_time_has_month (dt));
  fail_unless (!gst_date_time_has_day (dt));
  fail_unless (!gst_date_time_has_time (dt));
  gst_date_time_unref (dt);


  /* only time provided - we assume today's date */
  gdt = g_date_time_new_now_utc ();

  dt = gst_date_time_new_from_iso8601_string ("15:50:33");
  fail_unless (gst_date_time_get_year (dt) == g_date_time_get_year (gdt));
  fail_unless (gst_date_time_get_month (dt) == g_date_time_get_month (gdt));
  fail_unless (gst_date_time_get_day (dt) ==
      g_date_time_get_day_of_month (gdt));
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (gst_date_time_get_second (dt) == 33);
  gst_date_time_unref (dt);

  dt = gst_date_time_new_from_iso8601_string ("15:50:33Z");
  fail_unless (gst_date_time_get_year (dt) == g_date_time_get_year (gdt));
  fail_unless (gst_date_time_get_month (dt) == g_date_time_get_month (gdt));
  fail_unless (gst_date_time_get_day (dt) ==
      g_date_time_get_day_of_month (gdt));
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (gst_date_time_get_second (dt) == 33);
  gst_date_time_unref (dt);

  dt = gst_date_time_new_from_iso8601_string ("15:50");
  fail_unless (gst_date_time_get_year (dt) == g_date_time_get_year (gdt));
  fail_unless (gst_date_time_get_month (dt) == g_date_time_get_month (gdt));
  fail_unless (gst_date_time_get_day (dt) ==
      g_date_time_get_day_of_month (gdt));
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (!gst_date_time_has_second (dt));
  gst_date_time_unref (dt);

  dt = gst_date_time_new_from_iso8601_string ("15:50Z");
  fail_unless (gst_date_time_get_year (dt) == g_date_time_get_year (gdt));
  fail_unless (gst_date_time_get_month (dt) == g_date_time_get_month (gdt));
  fail_unless (gst_date_time_get_day (dt) ==
      g_date_time_get_day_of_month (gdt));
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (!gst_date_time_has_second (dt));
  gst_date_time_unref (dt);

  gdt2 = g_date_time_add_minutes (gdt, -270);
  g_date_time_unref (gdt);

  dt = gst_date_time_new_from_iso8601_string ("15:50:33-0430");
  fail_unless (gst_date_time_get_year (dt) == g_date_time_get_year (gdt2));
  fail_unless (gst_date_time_get_month (dt) == g_date_time_get_month (gdt2));
  fail_unless (gst_date_time_get_day (dt) ==
      g_date_time_get_day_of_month (gdt2));
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (gst_date_time_get_second (dt) == 33);
  gst_date_time_unref (dt);

  dt = gst_date_time_new_from_iso8601_string ("15:50-0430");
  fail_unless (gst_date_time_get_year (dt) == g_date_time_get_year (gdt2));
  fail_unless (gst_date_time_get_month (dt) == g_date_time_get_month (gdt2));
  fail_unless (gst_date_time_get_day (dt) ==
      g_date_time_get_day_of_month (gdt2));
  fail_unless (gst_date_time_get_hour (dt) == 15);
  fail_unless (gst_date_time_get_minute (dt) == 50);
  fail_unless (!gst_date_time_has_second (dt));
  gst_date_time_unref (dt);

  /* some bogus ones, make copy to detect out of bound read in valgrind/asan */
  {
    gchar *s = g_strdup ("0002000000T00000:00+0");
    dt = gst_date_time_new_from_iso8601_string (s);
    gst_date_time_unref (dt);
    g_free (s);
  }

  g_date_time_unref (gdt2);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_to_g_date_time)
{
  GDateTime *gdt1;
  GDateTime *gdt2;
  GstDateTime *dt;

  gdt1 = g_date_time_new_now_utc ();
  g_date_time_ref (gdt1);       /* keep it alive for compare below */
  dt = gst_date_time_new_from_g_date_time (gdt1);
  gdt2 = gst_date_time_to_g_date_time (dt);

  fail_unless (g_date_time_compare (gdt1, gdt2) == 0);

  g_date_time_unref (gdt1);
  g_date_time_unref (gdt2);
  gst_date_time_unref (dt);
}

GST_END_TEST;

GST_START_TEST (test_GstDateTime_new_from_g_date_time)
{
  GDateTime *gdt;
  GstDateTime *dt;

  gdt = g_date_time_new_now_utc ();
  g_date_time_ref (gdt);        /* keep it alive for compare below */
  dt = gst_date_time_new_from_g_date_time (gdt);

  assert_equals_int (gst_date_time_get_year (dt), g_date_time_get_year (gdt));
  assert_equals_int (gst_date_time_get_month (dt), g_date_time_get_month (gdt));
  assert_equals_int (gst_date_time_get_day (dt),
      g_date_time_get_day_of_month (gdt));
  assert_equals_int (gst_date_time_get_hour (dt), g_date_time_get_hour (gdt));
  assert_equals_int (gst_date_time_get_minute (dt),
      g_date_time_get_minute (gdt));
  assert_equals_int (gst_date_time_get_second (dt),
      g_date_time_get_second (gdt));
  assert_equals_int (gst_date_time_get_microsecond (dt),
      g_date_time_get_microsecond (gdt));

  g_date_time_unref (gdt);
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
  tcase_add_test (tc_chain, test_GstDateTime_partial_fields);
  tcase_add_test (tc_chain, test_GstDateTime_iso8601);
  tcase_add_test (tc_chain, test_GstDateTime_to_g_date_time);
  tcase_add_test (tc_chain, test_GstDateTime_new_from_g_date_time);

  return s;
}

GST_CHECK_MAIN (gst_date_time)

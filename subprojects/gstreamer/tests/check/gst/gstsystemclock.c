/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstsystemclock.c: Unit test for GstSystemClock
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

static GMutex af_lock;
static GCond af_cond;

/* see if the defines make sense */
GST_START_TEST (test_range)
{
  GstClockTime time, time2;

  time = GST_SECOND;
  fail_unless (time == G_GUINT64_CONSTANT (1000000000));

  time2 = time / 1000;
  fail_unless (time2 == 1000000);
  fail_unless (time2 == GST_MSECOND);
  fail_unless (time2 == GST_TIME_AS_USECONDS (time));

  time2 = time / 1000000;
  fail_unless (time2 == 1000);
  fail_unless (time2 == GST_USECOND);
  fail_unless (time2 == GST_TIME_AS_MSECONDS (time));
}

GST_END_TEST;

GST_START_TEST (test_signedness)
{
  GstClockTime time[] = { 0, 1, G_MAXUINT64 / GST_SECOND };
  GstClockTimeDiff diff[] =
      { 0, 1, -1, G_MAXINT64 / GST_SECOND, G_MININT64 / GST_SECOND };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (time); i++) {
    fail_if (time[i] != (time[i] * GST_SECOND / GST_SECOND));
  }
  for (i = 0; i < G_N_ELEMENTS (diff); i++) {
    fail_if (diff[i] != (diff[i] * GST_SECOND / GST_SECOND));
  }
}

GST_END_TEST;

GST_START_TEST (test_set_default)
{
  GstClock *clock, *static_clock;

  /* obtain the default system clock, which keeps a static ref and bumps the
   * refcount before returning */
  static_clock = gst_system_clock_obtain ();
  fail_unless (static_clock != NULL, "Could not create default system clock");
  g_assert_cmpint (GST_OBJECT_REFCOUNT (static_clock), ==, 2);

  /* set a new default clock to a different instance which should replace the
   * static clock with this one, and unref the static clock */
  clock = g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", "TestClock", NULL);
  gst_object_ref_sink (clock);
  gst_system_clock_set_default (clock);
  g_assert_cmpint (GST_OBJECT_REFCOUNT (static_clock), ==, 1);
  gst_object_unref (static_clock);
  static_clock = gst_system_clock_obtain ();
  fail_unless (static_clock == clock);
  g_assert_cmpint (GST_OBJECT_REFCOUNT (clock), ==, 3);
  gst_object_unref (static_clock);

  /* Reset the default clock to the static one */
  gst_system_clock_set_default (NULL);
  static_clock = gst_system_clock_obtain ();
  fail_unless (static_clock != clock);
  g_assert_cmpint (GST_OBJECT_REFCOUNT (clock), ==, 1);
  g_assert_cmpint (GST_OBJECT_REFCOUNT (static_clock), ==, 2);
  gst_object_unref (clock);
  gst_object_unref (static_clock);
}

GST_END_TEST;

GST_START_TEST (test_diff)
{
  GstClockTime time1[] = { 0, (GstClockTime) - 1, 0, 1, 2 * GST_SECOND,
    (GstClockTime) - GST_SECOND, (GstClockTime) - GST_SECOND
  };
  GstClockTime time2[] =
      { 0, 1, 1, 0, 1 * GST_SECOND, (GstClockTime) - GST_SECOND, GST_SECOND };
  GstClockTimeDiff d[] = { 0, 2, 1, -1, -GST_SECOND, 0, 2 * GST_SECOND };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (d); i++) {
    fail_if (d[i] != GST_CLOCK_DIFF (time1[i], time2[i]));
  }
}

GST_END_TEST;

static gboolean
test_async_full_slave_callback (GstClock * master, GstClockTime time,
    GstClockID id, GstClock * clock)
{
  GstClockTime stime, mtime;
  gdouble r_squared;

  /* notify the test case that we started */
  GST_INFO ("callback started");
  g_mutex_lock (&af_lock);
  g_cond_signal (&af_cond);

  /* wait for the test case to unref "clock" and signal */
  GST_INFO ("waiting for test case to signal");
  g_cond_wait (&af_cond, &af_lock);

  stime = gst_clock_get_internal_time (clock);
  mtime = gst_clock_get_time (master);

  gst_clock_add_observation (clock, stime, mtime, &r_squared);

  g_cond_signal (&af_cond);
  g_mutex_unlock (&af_lock);
  GST_INFO ("callback finished");

  return TRUE;
}

GST_START_TEST (test_async_full)
{
  GstClock *master, *slave;
  GstClockID *clockid;

  /* create master and slave */
  master =
      g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", "TestClockMaster", NULL);
  gst_object_ref_sink (master);
  slave = g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", "TestClockMaster", NULL);
  gst_object_ref_sink (slave);
  GST_OBJECT_FLAG_SET (slave, GST_CLOCK_FLAG_CAN_SET_MASTER);
  g_object_set (slave, "timeout", 50 * GST_MSECOND, NULL);

  fail_unless (GST_OBJECT_REFCOUNT (master) == 1);
  fail_unless (GST_OBJECT_REFCOUNT (slave) == 1);

  /* register a periodic shot on the master to calibrate the slave */
  g_mutex_lock (&af_lock);
  clockid = gst_clock_new_periodic_id (master,
      gst_clock_get_time (master), gst_clock_get_timeout (slave));
  gst_clock_id_wait_async (clockid,
      (GstClockCallback) test_async_full_slave_callback,
      gst_object_ref (slave), (GDestroyNotify) gst_object_unref);

  /* wait for the shot to be fired and test_async_full_slave_callback to be
   * called */
  GST_INFO ("waiting for the slave callback to start");
  g_cond_wait (&af_cond, &af_lock);
  GST_INFO ("slave callback running, unreffing slave");

  /* unref the slave clock while the slave_callback is running. This should be
   * safe since the master clock now stores a ref to the slave */
  gst_object_unref (slave);

  /* unref the clock entry. This should be safe as well since the clock thread
   * refs the entry before executing it */
  gst_clock_id_unschedule (clockid);
  gst_clock_id_unref (clockid);

  /* signal and wait for the callback to complete */
  g_cond_signal (&af_cond);

  GST_INFO ("waiting for callback to finish");
  g_cond_wait (&af_cond, &af_lock);
  GST_INFO ("callback finished");
  g_mutex_unlock (&af_lock);

  gst_object_unref (master);
}

GST_END_TEST;

GST_START_TEST (test_resolution)
{
  GstClock *clock;
  GstClockTime now_t, prev_t, resolution;
  int i;

  now_t = prev_t = GST_CLOCK_TIME_NONE;
  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create default system clock");
  resolution = gst_clock_get_resolution (clock);
  fail_unless (resolution != GST_CLOCK_TIME_NONE);

  for (i = 0; i < 100000; ++i) {
    now_t = gst_clock_get_internal_time (clock);
    fail_unless (now_t != GST_CLOCK_TIME_NONE);
    if (prev_t != GST_CLOCK_TIME_NONE) {
      GstClockTime diff;
      fail_unless (now_t >= prev_t);
      diff = now_t - prev_t;
      fail_unless (diff == 0 || diff >= resolution);
    }
    prev_t = now_t;
    g_thread_yield ();
  }
  gst_object_unref (clock);
  clock = NULL;
}

GST_END_TEST;

typedef struct
{
  GThread *thread_wait;
  GThread *thread_unschedule;
  GMutex lock;
  gboolean running;
  GstClockID id;
  gboolean unschedule;
  gint32 time_offset_min;
  gint32 time_offset_max;
  gboolean dont_unschedule_positive_offset;
} WaitUnscheduleData;

static gpointer
single_shot_wait_thread_func (gpointer data)
{
  WaitUnscheduleData *d = data;
  GstClock *clock = gst_system_clock_obtain ();

  while (d->running) {
    GstClockTime now;
    gint offset;
    GstClockID id;

    now = gst_clock_get_time (clock);
    offset = g_random_int_range (d->time_offset_min, d->time_offset_max);

    g_mutex_lock (&d->lock);
    d->unschedule = d->dont_unschedule_positive_offset ? offset < 0 : TRUE;
    id = d->id =
        gst_clock_new_single_shot_id (clock, now + (GstClockTime) offset);
    g_mutex_unlock (&d->lock);

    fail_unless (id != NULL, "Could not create single shot id");

    gst_clock_id_wait (id, NULL);

    g_mutex_lock (&d->lock);
    gst_clock_id_unref (id);
    d->id = NULL;
    g_mutex_unlock (&d->lock);
  }

  gst_object_unref (clock);

  return NULL;
}

static gpointer
unschedule_thread_func (gpointer data)
{
  WaitUnscheduleData *d = data;

  while (d->running) {
    g_mutex_lock (&d->lock);
    if (d->id && d->unschedule) {
      g_thread_yield ();
      gst_clock_id_unschedule (d->id);
    }
    g_mutex_unlock (&d->lock);
    g_thread_yield ();
  }

  return NULL;
}

GST_START_TEST (test_stress_cleanup_unschedule)
{
  WaitUnscheduleData *data;
  gint i, num;

  num = g_get_num_processors () * 6;
  data = g_newa (WaitUnscheduleData, num);

  for (i = 0; i < num; i++) {
    WaitUnscheduleData *d = &data[i];

    /* Don't unschedule waits with positive offsets in order to trigger
     * gst_system_clock_wait_wakeup() */
    d->dont_unschedule_positive_offset = TRUE;
    /* Overweight of negative offsets in order to trigger GST_CLOCK_EARLY more
     * frequently */
    d->time_offset_min = -GST_MSECOND;
    d->time_offset_max = GST_MSECOND / 10;

    /* Initialize test */
    d->id = NULL;
    d->running = TRUE;
    g_mutex_init (&d->lock);
    d->thread_wait = g_thread_new ("wait", single_shot_wait_thread_func, d);
    d->thread_unschedule = g_thread_new ("unschedule", unschedule_thread_func,
        d);
  }

  /* Test duration */
  g_usleep (G_USEC_PER_SEC);

  /* Stop and free test data */
  for (i = 0; i < num; i++) {
    WaitUnscheduleData *d = &data[i];
    d->running = FALSE;
  }
  for (i = 0; i < num; i++) {
    WaitUnscheduleData *d = &data[i];
    g_thread_join (d->thread_wait);
    g_thread_join (d->thread_unschedule);
    g_mutex_clear (&d->lock);
  }
}

GST_END_TEST;


GST_START_TEST (test_stress_reschedule)
{
  WaitUnscheduleData *data;
  gint i, num;

  num = g_get_num_processors () * 6;
  data = g_newa (WaitUnscheduleData, num);

  for (i = 0; i < num; i++) {
    WaitUnscheduleData *d = &data[i];

    /* Try to unschedule all waits */
    d->dont_unschedule_positive_offset = FALSE;
    /* Small positive offsets in order to have both negative and positive
     * diffs when a reschedule is needed. */
    d->time_offset_min = 0;
    d->time_offset_max = GST_MSECOND;

    d->id = NULL;
    d->running = TRUE;
    g_mutex_init (&d->lock);
    d->thread_wait = g_thread_new ("wait", single_shot_wait_thread_func, d);
    d->thread_unschedule = g_thread_new ("unschedule", unschedule_thread_func,
        d);
  }

  /* Test duration */
  g_usleep (G_USEC_PER_SEC);

  /* Stop and free test data */
  for (i = 0; i < num; i++) {
    WaitUnscheduleData *d = &data[i];
    d->running = FALSE;
  }
  for (i = 0; i < num; i++) {
    WaitUnscheduleData *d = &data[i];
    g_thread_join (d->thread_wait);
    g_thread_join (d->thread_unschedule);
    g_mutex_clear (&d->lock);
  }
}

GST_END_TEST;


static Suite *
gst_systemclock_suite (void)
{
  Suite *s = suite_create ("GstSystemClock");
  TCase *tc_chain = tcase_create ("waiting");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_range);
  tcase_add_test (tc_chain, test_signedness);
  tcase_add_test (tc_chain, test_diff);
  tcase_add_test (tc_chain, test_async_full);
  tcase_add_test (tc_chain, test_set_default);
  tcase_add_test (tc_chain, test_resolution);
  tcase_add_test (tc_chain, test_stress_cleanup_unschedule);
  tcase_add_test (tc_chain, test_stress_reschedule);

  return s;
}

GST_CHECK_MAIN (gst_systemclock);

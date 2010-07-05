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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/check/gstcheck.h>

static GMutex *af_lock;
static GCond *af_cond;

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

#define TIME_UNIT (GST_SECOND / 5)
static void
gst_clock_debug (GstClock * clock)
{
  GstClockTime time;

  time = gst_clock_get_time (clock);
  GST_DEBUG ("Clock info: time %" GST_TIME_FORMAT, GST_TIME_ARGS (time));
}

static gboolean
ok_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  GST_LOG ("unlocked async id %p", id);
  return FALSE;
}

static gboolean
error_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  GST_WARNING ("unlocked unscheduled async id %p, this is wrong", id);
  fail_if (TRUE);

  return FALSE;
}

GMutex *store_lock;

static gboolean
store_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  GList **list = user_data;

  GST_DEBUG ("unlocked async id %p", id);
  g_mutex_lock (store_lock);
  *list = g_list_append (*list, id);
  g_mutex_unlock (store_lock);
  return FALSE;
}

static gboolean
notify_callback (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  gboolean *ret = (gboolean *) user_data;

  if (ret != NULL)
    *ret = TRUE;

  return FALSE;
}

GST_START_TEST (test_single_shot)
{
  GstClock *clock;
  GstClockID id, id2;
  GstClockTime base;
  GstClockReturn result;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  id = gst_clock_new_single_shot_id (clock, base + TIME_UNIT);
  fail_unless (id != NULL, "Could not create single shot id");

  GST_DEBUG ("waiting one time unit");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK (result=%d)",
      result);
  fail_unless (gst_clock_get_time (clock) > (base + TIME_UNIT),
      "target time has not been reached");

  GST_DEBUG ("waiting in the past");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_EARLY,
      "Waiting did not return EARLY(result=%d)", result);
  gst_clock_id_unref (id);

  id = gst_clock_new_single_shot_id (clock, base + 2 * TIME_UNIT);
  GST_DEBUG ("waiting one second async id %p", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));
  gst_clock_id_unschedule (id);
  gst_clock_id_unref (id);

  id = gst_clock_new_single_shot_id (clock, base + 5 * TIME_UNIT);
  GST_DEBUG ("waiting one second async, with cancel on id %p", id);
  result = gst_clock_id_wait_async (id, error_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));
  GST_DEBUG ("cancel id %p after half a time unit", id);
  gst_clock_id_unschedule (id);
  gst_clock_id_unref (id);
  GST_DEBUG ("canceled id %p", id);

  GST_DEBUG ("waiting multiple one second async, with cancel");
  id = gst_clock_new_single_shot_id (clock, base + 5 * TIME_UNIT);
  id2 = gst_clock_new_single_shot_id (clock, base + 6 * TIME_UNIT);
  GST_DEBUG ("waiting id %p", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

  GST_DEBUG ("waiting id %p", id2);
  result = gst_clock_id_wait_async (id2, error_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));
  GST_DEBUG ("cancel id %p after half a time unit", id2);
  gst_clock_id_unschedule (id2);
  GST_DEBUG ("canceled id %p", id2);
  gst_clock_id_unref (id2);

  /* wait for the entry to time out */
  g_usleep (TIME_UNIT / 1000 * 5);
  fail_unless (((GstClockEntry *) id)->status == GST_CLOCK_OK,
      "Waiting did not finish");
  gst_clock_id_unref (id);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_periodic_shot)
{
  GstClock *clock;
  GstClockID id, id2;
  GstClockTime base;
  GstClockReturn result;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  /* signal every half a time unit */
  id = gst_clock_new_periodic_id (clock, base + TIME_UNIT, TIME_UNIT / 2);
  fail_unless (id != NULL, "Could not create periodic id");

  GST_DEBUG ("waiting one time unit");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

  GST_DEBUG ("waiting for the next");
  result = gst_clock_id_wait (id, NULL);
  gst_clock_debug (clock);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

  GST_DEBUG ("waiting for the next async %p", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  GST_DEBUG ("waiting some more for the next async %p", id);
  result = gst_clock_id_wait_async (id, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  id2 = gst_clock_new_periodic_id (clock, base + TIME_UNIT, TIME_UNIT / 2);
  fail_unless (id2 != NULL, "Could not create second periodic id");

  GST_DEBUG ("waiting some more for another async %p", id2);
  result = gst_clock_id_wait_async (id2, ok_callback, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));

  GST_DEBUG ("unschedule %p", id);
  gst_clock_id_unschedule (id);

  /* entry cannot be used again */
  result = gst_clock_id_wait_async (id, error_callback, NULL);
  fail_unless (result == GST_CLOCK_UNSCHEDULED,
      "Waiting did not return UNSCHEDULED");
  result = gst_clock_id_wait (id, NULL);
  fail_unless (result == GST_CLOCK_UNSCHEDULED,
      "Waiting did not return UNSCHEDULED");
  g_usleep (TIME_UNIT / (2 * 1000));

  /* clean up */
  gst_clock_id_unref (id);
  gst_clock_id_unschedule (id2);
  gst_clock_id_unref (id2);

  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_async_order)
{
  GstClock *clock;
  GstClockID id1, id2;
  GList *cb_list = NULL, *next;
  GstClockTime base;
  GstClockReturn result;

  store_lock = g_mutex_new ();

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  id1 = gst_clock_new_single_shot_id (clock, base + 2 * TIME_UNIT);
  id2 = gst_clock_new_single_shot_id (clock, base + 1 * TIME_UNIT);
  result = gst_clock_id_wait_async (id1, store_callback, &cb_list);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / (2 * 1000));
  result = gst_clock_id_wait_async (id2, store_callback, &cb_list);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  g_usleep (TIME_UNIT / 1000);
  /* at this point at least one of the timers should have timed out */
  g_mutex_lock (store_lock);
  fail_unless (cb_list != NULL, "expected notification");
  fail_unless (cb_list->data == id2,
      "Expected notification for id2 to come first");
  g_mutex_unlock (store_lock);
  g_usleep (TIME_UNIT / 1000);
  g_mutex_lock (store_lock);
  /* now both should have timed out */
  next = g_list_next (cb_list);
  fail_unless (next != NULL, "expected second notification");
  fail_unless (next->data == id1, "Missing notification for id1");
  g_mutex_unlock (store_lock);

  gst_clock_id_unref (id1);
  gst_clock_id_unref (id2);
  g_list_free (cb_list);

  gst_object_unref (clock);
  g_mutex_free (store_lock);
}

GST_END_TEST;

struct test_async_sync_interaction_data
{
  GMutex *lock;

  GstClockID sync_id;
  GstClockID sync_id2;

  GstClockID async_id;
  GstClockID async_id2;
  GstClockID async_id3;
};

static gboolean
test_async_sync_interaction_cb (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  struct test_async_sync_interaction_data *td =
      (struct test_async_sync_interaction_data *) (user_data);

  g_mutex_lock (td->lock);
  /* The first async callback is ignored */
  if (id == td->async_id)
    goto out;

  if (id != td->async_id2 && id != td->async_id3)
    goto out;

  /* Unschedule the sync callback */
  if (id == td->async_id3) {
    gst_clock_id_unschedule (td->sync_id);
    gst_clock_id_unschedule (td->async_id2);
  }
out:
  g_mutex_unlock (td->lock);
  return FALSE;
}

GST_START_TEST (test_async_sync_interaction)
{
  /* This test schedules an async callback, then before it completes, schedules
   * an earlier async callback, and quickly unschedules the first, and inserts
   * a THIRD even earlier async callback. It then attempts to wait on a
   * sync clock ID. While that's sleeping, the 3rd async callback should fire
   * and unschedule it. This tests for problems with unscheduling async and
   * sync callbacks on the system clock. */
  GstClock *clock;
  GstClockReturn result;
  GstClockTime base;
  GstClockTimeDiff jitter;
  struct test_async_sync_interaction_data td;
  int i;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  td.lock = g_mutex_new ();

  for (i = 0; i < 50; i++) {
    gst_clock_debug (clock);
    base = gst_clock_get_time (clock);
    g_mutex_lock (td.lock);
    td.async_id = gst_clock_new_single_shot_id (clock, base + 40 * GST_MSECOND);
    td.async_id2 =
        gst_clock_new_single_shot_id (clock, base + 30 * GST_MSECOND);
    td.async_id3 =
        gst_clock_new_single_shot_id (clock, base + 20 * GST_MSECOND);
    td.sync_id2 = gst_clock_new_single_shot_id (clock, base + 10 * GST_MSECOND);
    td.sync_id = gst_clock_new_single_shot_id (clock, base + 50 * GST_MSECOND);
    g_mutex_unlock (td.lock);

    result = gst_clock_id_wait_async (td.async_id,
        test_async_sync_interaction_cb, &td);
    fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

    /* Wait 10ms, then unschedule async_id and schedule async_id2 */
    result = gst_clock_id_wait (td.sync_id2, &jitter);
    fail_unless (result == GST_CLOCK_OK || result == GST_CLOCK_EARLY,
        "Waiting did not return OK or EARLY");
    /* async_id2 is earlier than async_id - should become head of the queue */
    result = gst_clock_id_wait_async (td.async_id2,
        test_async_sync_interaction_cb, &td);
    fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
    gst_clock_id_unschedule (td.async_id);

    /* async_id3 is earlier than async_id2 - should become head of the queue */
    result = gst_clock_id_wait_async (td.async_id3,
        test_async_sync_interaction_cb, &td);
    fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");

    /* While this is sleeping, the async3 id should fire and unschedule it */
    result = gst_clock_id_wait (td.sync_id, &jitter);
    fail_unless (result == GST_CLOCK_UNSCHEDULED || result == GST_CLOCK_EARLY,
        "Waiting did not return UNSCHEDULED (was %d)", result);

    gst_clock_id_unschedule (td.async_id3);
    g_mutex_lock (td.lock);

    gst_clock_id_unref (td.sync_id);
    gst_clock_id_unref (td.sync_id2);
    gst_clock_id_unref (td.async_id);
    gst_clock_id_unref (td.async_id2);
    gst_clock_id_unref (td.async_id3);
    g_mutex_unlock (td.lock);
  }

  g_mutex_free (td.lock);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_periodic_multi)
{
  GstClock *clock;
  GstClockID clock_id;
  GstClockID clock_id_async;
  GstClockTime base;
  GstClockReturn result;
  gboolean got_callback = FALSE;

  clock = gst_system_clock_obtain ();
  fail_unless (clock != NULL, "Could not create instance of GstSystemClock");

  gst_clock_debug (clock);
  base = gst_clock_get_time (clock);

  clock_id = gst_clock_new_periodic_id (clock, base + TIME_UNIT, TIME_UNIT);
  gst_clock_id_wait (clock_id, NULL);
  fail_unless (gst_clock_get_time (clock) >= base + TIME_UNIT);
  fail_unless (gst_clock_get_time (clock) < base + 2 * TIME_UNIT);

  /* now perform a concurrent wait and wait_async */

  clock_id_async =
      gst_clock_new_periodic_id (clock, base + TIME_UNIT, TIME_UNIT);
  result =
      gst_clock_id_wait_async (clock_id_async, notify_callback, &got_callback);
  fail_unless (result == GST_CLOCK_OK, "Async waiting did not return OK");

  result = gst_clock_id_wait (clock_id, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  fail_unless (gst_clock_get_time (clock) >= base + 2 * TIME_UNIT);
  /* give the async thread some time to call our callback: */
  g_usleep (TIME_UNIT / (10 * 1000));
  fail_unless (got_callback == TRUE, "got no async callback (1)");
  fail_unless (gst_clock_get_time (clock) < base + 3 * TIME_UNIT);
  got_callback = FALSE;

  result = gst_clock_id_wait (clock_id, NULL);
  fail_unless (result == GST_CLOCK_OK, "Waiting did not return OK");
  fail_unless (gst_clock_get_time (clock) >= base + 3 * TIME_UNIT);
  /* give the async thread some time to call our callback: */
  g_usleep (TIME_UNIT / (10 * 1000));
  fail_unless (got_callback == TRUE, "got no async callback (2)");
  fail_unless (gst_clock_get_time (clock) < base + 4 * TIME_UNIT);

  /* clean up */
  gst_clock_id_unref (clock_id);
  gst_clock_id_unschedule (clock_id_async);
  gst_clock_id_unref (clock_id_async);
  gst_object_unref (clock);
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

/* test if a blocking wait, unblocked by an async entry continues to be
 * scheduled */
typedef struct
{
  GstClock *clock;
  GstClockID id;
  GstClockTimeDiff jitter;
  GstClockReturn ret;
} MixedInfo;

static gpointer
mixed_thread (MixedInfo * info)
{
  info->ret = gst_clock_id_wait (info->id, &info->jitter);
  return NULL;
}

static gboolean
mixed_async_cb (GstClock * clock, GstClockTime time,
    GstClockID id, gpointer user_data)
{
  return TRUE;
}

GST_START_TEST (test_mixed)
{
  GThread *thread;
  GError *error = NULL;
  MixedInfo info;
  GstClockTime base;
  GstClockID id;

  info.clock = gst_system_clock_obtain ();
  fail_unless (info.clock != NULL,
      "Could not create instance of GstSystemClock");

  /* get current time of the clock as base time */
  base = gst_clock_get_time (info.clock);

  /* create entry to wait for 1 second */
  info.id = gst_clock_new_single_shot_id (info.clock, base + GST_SECOND);

  /* make and start an entry that is scheduled every 10ms */
  id = gst_clock_new_periodic_id (info.clock, base, 10 * GST_MSECOND);

  /* start waiting for the entry */
  thread = g_thread_create ((GThreadFunc) mixed_thread, &info, TRUE, &error);
  fail_unless (error == NULL, "error creating thread");
  fail_unless (thread != NULL, "Could not create thread");

  /* wait half a second so we are sure to be in the thread */
  g_usleep (G_USEC_PER_SEC / 2);

  /* start scheduling the entry */
  gst_clock_id_wait_async (id, mixed_async_cb, NULL);

  /* wait for thread to finish */
  g_thread_join (thread);
  /* entry must have timed out correctly */
  fail_unless (info.ret == GST_CLOCK_OK, "clock return was %d", info.ret);

  gst_clock_id_unschedule (id);
  gst_clock_id_unref (id);
  gst_clock_id_unref (info.id);
  gst_object_unref (info.clock);
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
  g_mutex_lock (af_lock);
  g_cond_signal (af_cond);

  /* wait for the test case to unref "clock" and signal */
  GST_INFO ("waiting for test case to signal");
  g_cond_wait (af_cond, af_lock);

  stime = gst_clock_get_internal_time (clock);
  mtime = gst_clock_get_time (master);

  gst_clock_add_observation (clock, stime, mtime, &r_squared);

  g_cond_signal (af_cond);
  g_mutex_unlock (af_lock);
  GST_INFO ("callback finished");

  return TRUE;
}

GST_START_TEST (test_async_full)
{
  GstClock *master, *slave;
  GstClockID *clockid;

  af_lock = g_mutex_new ();
  af_cond = g_cond_new ();

  /* create master and slave */
  master =
      g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", "TestClockMaster", NULL);
  slave = g_object_new (GST_TYPE_SYSTEM_CLOCK, "name", "TestClockMaster", NULL);
  GST_OBJECT_FLAG_SET (slave, GST_CLOCK_FLAG_CAN_SET_MASTER);
  g_object_set (slave, "timeout", 50 * GST_MSECOND, NULL);

  fail_unless (GST_OBJECT_REFCOUNT (master) == 1);
  fail_unless (GST_OBJECT_REFCOUNT (slave) == 1);

  /* register a periodic shot on the master to calibrate the slave */
  g_mutex_lock (af_lock);
  clockid = gst_clock_new_periodic_id (master,
      gst_clock_get_time (master), slave->timeout);
  gst_clock_id_wait_async_full (clockid,
      (GstClockCallback) test_async_full_slave_callback,
      gst_object_ref (slave), (GDestroyNotify) gst_object_unref);

  /* wait for the shot to be fired and test_async_full_slave_callback to be
   * called */
  GST_INFO ("waiting for the slave callback to start");
  g_cond_wait (af_cond, af_lock);
  GST_INFO ("slave callback running, unreffing slave");

  /* unref the slave clock while the slave_callback is running. This should be
   * safe since the master clock now stores a ref to the slave */
  gst_object_unref (slave);

  /* unref the clock entry. This should be safe as well since the clock thread
   * refs the entry before executing it */
  gst_clock_id_unschedule (clockid);
  gst_clock_id_unref (clockid);

  /* signal and wait for the callback to complete */
  g_cond_signal (af_cond);

  GST_INFO ("waiting for callback to finish");
  g_cond_wait (af_cond, af_lock);
  GST_INFO ("callback finished");
  g_mutex_unlock (af_lock);

  gst_object_unref (master);

  g_mutex_free (af_lock);
  g_cond_free (af_cond);
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
  tcase_add_test (tc_chain, test_single_shot);
  tcase_add_test (tc_chain, test_periodic_shot);
  tcase_add_test (tc_chain, test_periodic_multi);
  tcase_add_test (tc_chain, test_async_order);
  tcase_add_test (tc_chain, test_async_sync_interaction);
  tcase_add_test (tc_chain, test_diff);
  tcase_add_test (tc_chain, test_mixed);
  tcase_add_test (tc_chain, test_async_full);

  return s;
}

GST_CHECK_MAIN (gst_systemclock);

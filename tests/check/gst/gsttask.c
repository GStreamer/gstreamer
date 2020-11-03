/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gsttask.c: Unit test for GstTask
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

static GMutex task_lock;
static GCond task_cond;

static GRecMutex task_mutex;

#define TEST_RACE_ITERATIONS 1000

static void
task_resume_func (void *data)
{
  g_mutex_lock (&task_lock);
  g_cond_signal (&task_cond);
  g_mutex_unlock (&task_lock);
}

GST_START_TEST (test_resume)
{
  GstTask *t;

  t = gst_task_new (task_resume_func, &t, NULL);
  fail_if (t == NULL);

  g_rec_mutex_init (&task_mutex);
  gst_task_set_lock (t, &task_mutex);

  g_cond_init (&task_cond);
  g_mutex_init (&task_lock);

  g_mutex_lock (&task_lock);

  /* Pause the task, and resume it. */
  fail_unless (gst_task_pause (t));
  fail_unless (gst_task_resume (t));

  while (GST_TASK_STATE (t) != GST_TASK_STARTED)
    g_cond_wait (&task_cond, &task_lock);

  fail_unless (gst_task_stop (t));
  g_mutex_unlock (&task_lock);
  fail_unless (gst_task_join (t));

  /* Make sure we cannot resume from stopped. */
  fail_if (gst_task_resume (t));

  gst_object_unref (t);
}

GST_END_TEST;

static void
task_signal_pause_func (void *data)
{
  GstTask **t = data;

  g_mutex_lock (&task_lock);
  GST_DEBUG ("signal");
  g_cond_signal (&task_cond);

  gst_task_pause (*t);
  g_mutex_unlock (&task_lock);
}

GST_START_TEST (test_pause_stop_race)
{
  guint it = TEST_RACE_ITERATIONS;
  GstTask *t;
  gboolean ret;

  t = gst_task_new (task_signal_pause_func, &t, NULL);
  fail_if (t == NULL);

  g_rec_mutex_init (&task_mutex);
  gst_task_set_lock (t, &task_mutex);

  g_cond_init (&task_cond);
  g_mutex_init (&task_lock);

  while (it-- > 0) {
    g_mutex_lock (&task_lock);
    GST_DEBUG ("starting");
    ret = gst_task_start (t);
    fail_unless (ret == TRUE);
    /* wait for it to spin up */
    GST_DEBUG ("waiting");
    g_cond_wait (&task_cond, &task_lock);
    GST_DEBUG ("done waiting");
    g_mutex_unlock (&task_lock);

    GST_DEBUG ("starting");
    ret = gst_task_stop (t);
    fail_unless (ret == TRUE);

    GST_DEBUG ("joining");
    ret = gst_task_join (t);
    fail_unless (ret == TRUE);
  }

  g_cond_clear (&task_cond);
  g_mutex_clear (&task_lock);

  gst_object_unref (t);
}

GST_END_TEST;

static void
task_func2 (void *data)
{
  gboolean ret;
  GstTask *t = *((GstTask **) data);

  g_mutex_lock (&task_lock);
  GST_DEBUG ("signal");
  g_cond_signal (&task_cond);
  g_mutex_unlock (&task_lock);

  ASSERT_WARNING (ret = gst_task_join (t));
  fail_unless (ret == FALSE);
}

GST_START_TEST (test_join)
{
  GstTask *t;
  gboolean ret;

  t = gst_task_new (task_func2, &t, NULL);
  fail_if (t == NULL);

  g_rec_mutex_init (&task_mutex);
  gst_task_set_lock (t, &task_mutex);

  g_cond_init (&task_cond);
  g_mutex_init (&task_lock);

  g_mutex_lock (&task_lock);
  GST_DEBUG ("starting");
  ret = gst_task_start (t);
  fail_unless (ret == TRUE);
  /* wait for it to spin up */
  GST_DEBUG ("waiting");
  g_cond_wait (&task_cond, &task_lock);
  GST_DEBUG ("done waiting");
  g_mutex_unlock (&task_lock);

  GST_DEBUG ("joining");
  ret = gst_task_join (t);
  fail_unless (ret == TRUE);

  gst_task_cleanup_all ();

  gst_object_unref (t);
}

GST_END_TEST;

static void
task_func (void *data)
{
  g_mutex_lock (&task_lock);
  GST_DEBUG ("signal");
  g_cond_signal (&task_cond);
  g_mutex_unlock (&task_lock);
}

GST_START_TEST (test_lock_start)
{
  GstTask *t;
  gboolean ret;

  t = gst_task_new (task_func, NULL, NULL);
  fail_if (t == NULL);

  g_rec_mutex_init (&task_mutex);
  gst_task_set_lock (t, &task_mutex);

  g_cond_init (&task_cond);
  g_mutex_init (&task_lock);

  g_mutex_lock (&task_lock);
  GST_DEBUG ("starting");
  ret = gst_task_start (t);
  fail_unless (ret == TRUE);
  /* wait for it to spin up */
  GST_DEBUG ("waiting");
  g_cond_wait (&task_cond, &task_lock);
  GST_DEBUG ("done waiting");
  g_mutex_unlock (&task_lock);

  /* cannot set mutex now */
  ASSERT_WARNING (gst_task_set_lock (t, &task_mutex));

  GST_DEBUG ("joining");
  ret = gst_task_join (t);
  fail_unless (ret == TRUE);

  gst_task_cleanup_all ();

  gst_object_unref (t);
}

GST_END_TEST;

GST_START_TEST (test_lock)
{
  GstTask *t;
  gboolean ret;

  t = gst_task_new (task_func, NULL, NULL);
  fail_if (t == NULL);

  g_rec_mutex_init (&task_mutex);
  gst_task_set_lock (t, &task_mutex);

  GST_DEBUG ("pause");
  ret = gst_task_pause (t);
  fail_unless (ret == TRUE);

  g_usleep (1 * G_USEC_PER_SEC / 2);

  GST_DEBUG ("joining");
  ret = gst_task_join (t);
  fail_unless (ret == TRUE);

  g_usleep (1 * G_USEC_PER_SEC / 2);

  gst_object_unref (t);
}

GST_END_TEST;

GST_START_TEST (test_no_lock)
{
  GstTask *t;
  gboolean ret;

  t = gst_task_new (task_func, NULL, NULL);
  fail_if (t == NULL);

  /* stop should be possible without lock */
  gst_task_stop (t);

  /* pause should give a warning */
  ASSERT_WARNING (ret = gst_task_pause (t));
  fail_unless (ret == FALSE);

  /* start should give a warning */
  ASSERT_WARNING (ret = gst_task_start (t));
  fail_unless (ret == FALSE);

  /* stop should be possible without lock */
  gst_task_stop (t);

  gst_object_unref (t);
}

GST_END_TEST;

GST_START_TEST (test_create)
{
  GstTask *t;

  t = gst_task_new (task_func, NULL, NULL);
  fail_if (t == NULL);

  gst_object_unref (t);
}

GST_END_TEST;

typedef struct
{
  gboolean called;
  gpointer caller_thread;

  GCond blocked_cond;
  GMutex blocked_lock;
  gboolean blocked;

  GCond unblock_cond;
  GMutex unblock_lock;
  gboolean unblock;
} TaskData;

static void
task_cb (TaskData * tdata)
{
  tdata->called = TRUE;
  tdata->caller_thread = g_thread_self ();

  g_mutex_lock (&tdata->blocked_lock);
  tdata->blocked = TRUE;
  g_cond_signal (&tdata->blocked_cond);
  g_mutex_unlock (&tdata->blocked_lock);

  g_mutex_lock (&tdata->unblock_lock);
  while (!tdata->unblock)
    g_cond_wait (&tdata->unblock_cond, &tdata->unblock_lock);

  g_mutex_unlock (&tdata->unblock_lock);
}

static void
init_task_data (TaskData * tdata)
{
  tdata->called = FALSE;
  tdata->caller_thread = NULL;
  tdata->unblock = FALSE;
  g_cond_init (&tdata->unblock_cond);
  g_mutex_init (&tdata->unblock_lock);

  tdata->blocked = FALSE;
  g_cond_init (&tdata->blocked_cond);
  g_mutex_init (&tdata->blocked_lock);
}

static void
cleanup_task_data (TaskData * tdata)
{
  g_mutex_clear (&tdata->unblock_lock);
  g_cond_clear (&tdata->unblock_cond);
  g_mutex_clear (&tdata->blocked_lock);
  g_cond_clear (&tdata->blocked_cond);
}

/* In this test, we use a shared task pool with max-threads=1 and verify
 * that the caller thread for two tasks is the same */
GST_START_TEST (test_shared_task_pool_shared_thread)
{
  GstTaskPool *pool;
  gpointer handle, handle2;
  GError *err = NULL;
  TaskData tdata, tdata2;

  init_task_data (&tdata);
  init_task_data (&tdata2);

  pool = gst_shared_task_pool_new ();
  gst_task_pool_prepare (pool, &err);

  fail_unless (err == NULL);

  /* We request that two tasks be executed, and our task function is blocking.
   * This means no new thread is available to spawn, and the second task should
   * be queued up on the first thread */
  handle =
      gst_task_pool_push (pool, (GstTaskPoolFunction) task_cb, &tdata, &err);
  fail_unless (err == NULL);
  handle2 =
      gst_task_pool_push (pool, (GstTaskPoolFunction) task_cb, &tdata2, &err);
  fail_unless (err == NULL);

  g_mutex_lock (&tdata.unblock_lock);
  tdata.unblock = TRUE;
  g_cond_signal (&tdata.unblock_cond);
  g_mutex_unlock (&tdata.unblock_lock);

  g_mutex_lock (&tdata2.unblock_lock);
  tdata2.unblock = TRUE;
  g_cond_signal (&tdata2.unblock_cond);
  g_mutex_unlock (&tdata2.unblock_lock);

  gst_task_pool_join (pool, handle);
  gst_task_pool_join (pool, handle2);

  fail_unless (tdata.called == TRUE);
  fail_unless (tdata2.called == TRUE);
  fail_unless (tdata.caller_thread == tdata2.caller_thread);

  cleanup_task_data (&tdata);
  cleanup_task_data (&tdata2);

  gst_task_pool_cleanup (pool);

  g_object_unref (pool);
}

GST_END_TEST;

/* In this test, we use a shared task pool with max-threads=2 and verify
 * that the caller thread for two tasks is different */
GST_START_TEST (test_shared_task_pool_two_threads)
{
  GstTaskPool *pool;
  gpointer handle, handle2;
  GError *err = NULL;
  TaskData tdata, tdata2;

  init_task_data (&tdata);
  init_task_data (&tdata2);

  pool = gst_shared_task_pool_new ();
  gst_shared_task_pool_set_max_threads (GST_SHARED_TASK_POOL (pool), 2);
  gst_task_pool_prepare (pool, &err);

  fail_unless (err == NULL);

  /* We request that two tasks be executed, and our task function is blocking.
   * This means the pool will have to spawn a new thread to handle the task */
  handle =
      gst_task_pool_push (pool, (GstTaskPoolFunction) task_cb, &tdata, &err);
  fail_unless (err == NULL);
  handle2 =
      gst_task_pool_push (pool, (GstTaskPoolFunction) task_cb, &tdata2, &err);
  fail_unless (err == NULL);

  /* Make sure that the second task has started executing before unblocking */
  g_mutex_lock (&tdata2.blocked_lock);
  while (!tdata2.blocked) {
    g_cond_wait (&tdata2.blocked_cond, &tdata2.blocked_lock);
  }
  g_mutex_unlock (&tdata2.blocked_lock);

  g_mutex_lock (&tdata.unblock_lock);
  tdata.unblock = TRUE;
  g_cond_signal (&tdata.unblock_cond);
  g_mutex_unlock (&tdata.unblock_lock);

  g_mutex_lock (&tdata2.unblock_lock);
  tdata2.unblock = TRUE;
  g_cond_signal (&tdata2.unblock_cond);
  g_mutex_unlock (&tdata2.unblock_lock);

  gst_task_pool_join (pool, handle);
  gst_task_pool_join (pool, handle2);

  fail_unless (tdata.called == TRUE);
  fail_unless (tdata2.called == TRUE);

  fail_unless (tdata.caller_thread != tdata2.caller_thread);

  cleanup_task_data (&tdata);
  cleanup_task_data (&tdata2);

  gst_task_pool_cleanup (pool);

  g_object_unref (pool);
}

GST_END_TEST;

static Suite *
gst_task_suite (void)
{
  Suite *s = suite_create ("GstTask");
  TCase *tc_chain = tcase_create ("task tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_create);
  tcase_add_test (tc_chain, test_no_lock);
  tcase_add_test (tc_chain, test_lock);
  tcase_add_test (tc_chain, test_lock_start);
  tcase_add_test (tc_chain, test_join);
  tcase_add_test (tc_chain, test_pause_stop_race);
  tcase_add_test (tc_chain, test_resume);
  tcase_add_test (tc_chain, test_shared_task_pool_shared_thread);
  tcase_add_test (tc_chain, test_shared_task_pool_two_threads);

  return s;
}

GST_CHECK_MAIN (gst_task);

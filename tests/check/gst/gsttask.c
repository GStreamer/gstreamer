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

  return s;
}

GST_CHECK_MAIN (gst_task);

/* GStreamer
 * unit tests for GstRTSPThreadPool
 * Copyright (C) 2013 Axis Communications <dev-gstreamer at axis dot com>
 * @author Ognyan Tonchev <ognyan at axis dot com>
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

#include <rtsp-thread-pool.h>

GST_START_TEST (test_pool_get_thread)
{
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  pool = gst_rtsp_thread_pool_new ();
  fail_unless (GST_IS_RTSP_THREAD_POOL (pool));

  thread = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread));
  /* one ref is hold by the pool */
  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT (thread), 2);

  gst_rtsp_thread_stop (thread);
  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_pool_get_media_thread)
{
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  pool = gst_rtsp_thread_pool_new ();
  fail_unless (GST_IS_RTSP_THREAD_POOL (pool));

  thread = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_MEDIA,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread));
  /* one ref is hold by the pool */
  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT (thread), 2);

  gst_rtsp_thread_stop (thread);
  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_pool_get_thread_reuse)
{
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread1;
  GstRTSPThread *thread2;

  pool = gst_rtsp_thread_pool_new ();
  fail_unless (GST_IS_RTSP_THREAD_POOL (pool));

  gst_rtsp_thread_pool_set_max_threads (pool, 1);

  thread1 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread1));

  thread2 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread2));

  fail_unless (thread2 == thread1);
  /* one ref is hold by the pool */
  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT (thread1), 3);

  gst_rtsp_thread_stop (thread1);
  gst_rtsp_thread_stop (thread2);
  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

static void
do_test_pool_max_thread (gboolean use_property)
{
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread1;
  GstRTSPThread *thread2;
  GstRTSPThread *thread3;
  gint max_threads;

  pool = gst_rtsp_thread_pool_new ();
  fail_unless (GST_IS_RTSP_THREAD_POOL (pool));

  if (use_property) {
    g_object_get (pool, "max-threads", &max_threads, NULL);
    fail_unless_equals_int (max_threads, 1);
  } else {
    fail_unless_equals_int (gst_rtsp_thread_pool_get_max_threads (pool), 1);
  }

  thread1 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread1));

  thread2 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread2));

  fail_unless (thread1 == thread2);

  gst_rtsp_thread_stop (thread1);
  gst_rtsp_thread_stop (thread2);

  if (use_property) {
    g_object_set (pool, "max-threads", 2, NULL);
    g_object_get (pool, "max-threads", &max_threads, NULL);
    fail_unless_equals_int (max_threads, 2);
  } else {
    gst_rtsp_thread_pool_set_max_threads (pool, 2);
    fail_unless_equals_int (gst_rtsp_thread_pool_get_max_threads (pool), 2);
  }

  thread1 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread1));

  thread2 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread2));

  thread3 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread3));

  fail_unless (thread2 != thread1);
  fail_unless (thread3 == thread2 || thread3 == thread1);

  gst_rtsp_thread_stop (thread1);
  gst_rtsp_thread_stop (thread2);
  gst_rtsp_thread_stop (thread3);

  if (use_property) {
    g_object_set (pool, "max-threads", 0, NULL);
    g_object_get (pool, "max-threads", &max_threads, NULL);
    fail_unless_equals_int (max_threads, 0);
  } else {
    gst_rtsp_thread_pool_set_max_threads (pool, 0);
    fail_unless_equals_int (gst_rtsp_thread_pool_get_max_threads (pool), 0);
  }

  thread1 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_if (GST_IS_RTSP_THREAD (thread1));

  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_START_TEST (test_pool_max_threads)
{
  do_test_pool_max_thread (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_pool_max_threads_property)
{
  do_test_pool_max_thread (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_pool_thread_copy)
{
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread1;
  GstRTSPThread *thread2;

  pool = gst_rtsp_thread_pool_new ();
  fail_unless (GST_IS_RTSP_THREAD_POOL (pool));

  thread1 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (GST_IS_RTSP_THREAD (thread1));
  fail_unless (GST_IS_MINI_OBJECT_TYPE (thread1, GST_TYPE_RTSP_THREAD));

  thread2 = GST_RTSP_THREAD (gst_mini_object_copy (GST_MINI_OBJECT (thread1)));
  fail_unless (GST_IS_RTSP_THREAD (thread2));
  fail_unless (GST_IS_MINI_OBJECT_TYPE (thread2, GST_TYPE_RTSP_THREAD));

  gst_rtsp_thread_stop (thread1);
  gst_rtsp_thread_stop (thread2);
  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

static Suite *
rtspthreadpool_suite (void)
{
  Suite *s = suite_create ("rtspthreadpool");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_pool_get_thread);
  tcase_add_test (tc, test_pool_get_media_thread);
  tcase_add_test (tc, test_pool_get_thread_reuse);
  tcase_add_test (tc, test_pool_max_threads);
  tcase_add_test (tc, test_pool_max_threads_property);
  tcase_add_test (tc, test_pool_thread_copy);

  return s;
}

GST_CHECK_MAIN (rtspthreadpool);

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
  fail_unless (pool != NULL);

  thread = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (thread != NULL);
  /* one ref is hold by the pool */
  fail_unless (GST_MINI_OBJECT_REFCOUNT (thread) == 2);

  gst_rtsp_thread_stop (thread);
  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_pool_get_thread_reuse)
{
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPThread *thread2;

  pool = gst_rtsp_thread_pool_new ();
  fail_unless (pool != NULL);

  gst_rtsp_thread_pool_set_max_threads (pool, 1);

  thread = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (thread != NULL);

  thread2 = gst_rtsp_thread_pool_get_thread (pool, GST_RTSP_THREAD_TYPE_CLIENT,
      NULL);
  fail_unless (thread2 != NULL);

  fail_unless (thread == thread2);
  /* one ref is hold by the pool */
  fail_unless (GST_MINI_OBJECT_REFCOUNT (thread) == 3);

  gst_rtsp_thread_stop (thread);
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
  tcase_add_test (tc, test_pool_get_thread_reuse);

  return s;
}

GST_CHECK_MAIN (rtspthreadpool);

/* GStreamer
 * Copyright (C) 2014 Sebastian Rasmussen <sebras@hotmail.com>
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
#include <rtsp-session-pool.h>

typedef struct
{
  GstRTSPSession *sessions[3];
  GstRTSPFilterResult response[3];
} Responses;

static GstRTSPFilterResult
filter_func (GstRTSPSessionPool * pool, GstRTSPSession * session,
    gpointer user_data)
{
  Responses *responses = (Responses *) user_data;
  gint i;

  for (i = 0; i < 3; i++)
    if (session == responses->sessions[i])
      return responses->response[i];

  return GST_RTSP_FILTER_KEEP;
}

GST_START_TEST (test_pool)
{
  GstRTSPSessionPool *pool;
  GstRTSPSession *session1, *session2, *session3;
  GstRTSPSession *compare;
  gchar *session1id, *session2id, *session3id;
  GList *list;
  guint maxsessions;
  GSource *source;
  guint sourceid;

  pool = gst_rtsp_session_pool_new ();
  fail_unless_equals_int (gst_rtsp_session_pool_get_n_sessions (pool), 0);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 0);

  gst_rtsp_session_pool_set_max_sessions (pool, 3);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 3);

  session1 = gst_rtsp_session_pool_create (pool);
  fail_unless (GST_IS_RTSP_SESSION (session1));
  fail_unless_equals_int (gst_rtsp_session_pool_get_n_sessions (pool), 1);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 3);
  session1id = g_strdup (gst_rtsp_session_get_sessionid (session1));

  session2 = gst_rtsp_session_pool_create (pool);
  fail_unless (GST_IS_RTSP_SESSION (session2));
  fail_unless_equals_int (gst_rtsp_session_pool_get_n_sessions (pool), 2);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 3);
  session2id = g_strdup (gst_rtsp_session_get_sessionid (session2));

  session3 = gst_rtsp_session_pool_create (pool);
  fail_unless (GST_IS_RTSP_SESSION (session3));
  fail_unless_equals_int (gst_rtsp_session_pool_get_n_sessions (pool), 3);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 3);
  session3id = g_strdup (gst_rtsp_session_get_sessionid (session3));

  fail_if (GST_IS_RTSP_SESSION (gst_rtsp_session_pool_create (pool)));

  compare = gst_rtsp_session_pool_find (pool, session1id);
  fail_unless (compare == session1);
  g_object_unref (compare);
  compare = gst_rtsp_session_pool_find (pool, session2id);
  fail_unless (compare == session2);
  g_object_unref (compare);
  compare = gst_rtsp_session_pool_find (pool, session3id);
  fail_unless (compare == session3);
  g_object_unref (compare);
  fail_unless (gst_rtsp_session_pool_find (pool, "") == NULL);

  fail_unless (gst_rtsp_session_pool_remove (pool, session2));
  g_object_unref (session2);
  fail_unless_equals_int (gst_rtsp_session_pool_get_n_sessions (pool), 2);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 3);

  gst_rtsp_session_pool_set_max_sessions (pool, 2);
  fail_unless_equals_int (gst_rtsp_session_pool_get_n_sessions (pool), 2);
  fail_unless_equals_int (gst_rtsp_session_pool_get_max_sessions (pool), 2);

  session2 = gst_rtsp_session_pool_create (pool);
  fail_if (GST_IS_RTSP_SESSION (session2));

  {
    list = gst_rtsp_session_pool_filter (pool, NULL, NULL);
    fail_unless_equals_int (g_list_length (list), 2);
    fail_unless (g_list_find (list, session1) != NULL);
    fail_unless (g_list_find (list, session3) != NULL);
    g_list_free_full (list, (GDestroyNotify) g_object_unref);
  }

  {
    Responses responses = {
      {session1, session2, session3}
      ,
      {GST_RTSP_FILTER_KEEP, GST_RTSP_FILTER_KEEP, GST_RTSP_FILTER_KEEP}
      ,
    };

    list = gst_rtsp_session_pool_filter (pool, filter_func, &responses);
    fail_unless (list == NULL);
  }

  {
    Responses responses = {
      {session1, session2, session3}
      ,
      {GST_RTSP_FILTER_REF, GST_RTSP_FILTER_KEEP, GST_RTSP_FILTER_KEEP}
      ,
    };

    list = gst_rtsp_session_pool_filter (pool, filter_func, &responses);
    fail_unless_equals_int (g_list_length (list), 1);
    fail_unless (g_list_nth_data (list, 0) == session1);
    g_list_free_full (list, (GDestroyNotify) g_object_unref);
  }

  {
    Responses responses = {
      {session1, session2, session3}
      ,
      {GST_RTSP_FILTER_KEEP, GST_RTSP_FILTER_KEEP, GST_RTSP_FILTER_REMOVE}
      ,
    };

    list = gst_rtsp_session_pool_filter (pool, filter_func, &responses);
    fail_unless_equals_int (g_list_length (list), 0);
    g_list_free (list);
  }

  compare = gst_rtsp_session_pool_find (pool, session1id);
  fail_unless (compare == session1);
  g_object_unref (compare);
  fail_unless (gst_rtsp_session_pool_find (pool, session2id) == NULL);
  fail_unless (gst_rtsp_session_pool_find (pool, session3id) == NULL);

  g_object_get (pool, "max-sessions", &maxsessions, NULL);
  fail_unless_equals_int (maxsessions, 2);

  g_object_set (pool, "max-sessions", 3, NULL);
  g_object_get (pool, "max-sessions", &maxsessions, NULL);
  fail_unless_equals_int (maxsessions, 3);

  fail_unless_equals_int (gst_rtsp_session_pool_cleanup (pool), 0);

  gst_rtsp_session_set_timeout (session1, 1);

  source = gst_rtsp_session_pool_create_watch (pool);
  fail_unless (source != NULL);

  sourceid = g_source_attach (source, NULL);
  fail_unless (sourceid != 0);

  while (!g_main_context_iteration (NULL, TRUE));

  g_source_unref (source);

  g_object_unref (session1);
  g_object_unref (session3);

  g_free (session1id);
  g_free (session2id);
  g_free (session3id);

  g_object_unref (pool);
}

GST_END_TEST;

static Suite *
rtspsessionpool_suite (void)
{
  Suite *s = suite_create ("rtspsessionpool");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 15);
  tcase_add_test (tc, test_pool);

  return s;
}

GST_CHECK_MAIN (rtspsessionpool);

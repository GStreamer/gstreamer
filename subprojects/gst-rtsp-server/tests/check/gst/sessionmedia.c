/* GStreamer
 * Copyright (C) 2013 Branko Subasic <branko.subasic@axis.com>
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

#include <rtsp-media-factory.h>
#include <rtsp-session-media.h>

#define TEST_PATH "rtsp://localhost:8554/test"
#define SETUP_URL1 TEST_PATH "/stream=0"
#define SETUP_URL2 TEST_PATH "/stream=1"

GST_START_TEST (test_setup_url)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url, *setup_url;
  GstRTSPStream *stream;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPSessionMedia *sm;
  GstRTSPStreamTransport *trans;
  GstRTSPTransport *ct;
  gint match_len;
  gchar *url_str, *url_str2;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse (TEST_PATH, &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (GST_IS_RTSP_STREAM (stream));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  /* create session media and make sure it matches test path
   * note that gst_rtsp_session_media_new takes ownership of the media
   * thus no need to unref it at the bottom of function */
  sm = gst_rtsp_session_media_new (TEST_PATH, media);
  fail_unless (GST_IS_RTSP_SESSION_MEDIA (sm));
  fail_unless (gst_rtsp_session_media_matches (sm, TEST_PATH, &match_len));
  fail_unless (match_len == strlen (TEST_PATH));
  fail_unless (gst_rtsp_session_media_get_media (sm) == media);

  /* make a transport for the stream */
  gst_rtsp_transport_new (&ct);
  trans = gst_rtsp_session_media_set_transport (sm, stream, ct);
  fail_unless (gst_rtsp_session_media_get_transport (sm, 0) == trans);

  /* make sure there's no setup url stored initially */
  fail_unless (gst_rtsp_stream_transport_get_url (trans) == NULL);

  /* now store a setup url and make sure it can be retrieved and that it's correct */
  fail_unless (gst_rtsp_url_parse (SETUP_URL1, &setup_url) == GST_RTSP_OK);
  gst_rtsp_stream_transport_set_url (trans, setup_url);

  url_str = gst_rtsp_url_get_request_uri (setup_url);
  url_str2 =
      gst_rtsp_url_get_request_uri (gst_rtsp_stream_transport_get_url (trans));
  fail_if (g_strcmp0 (url_str, url_str2) != 0);
  g_free (url_str);
  g_free (url_str2);

  /* check that it's ok to try to store the same url again */
  gst_rtsp_stream_transport_set_url (trans, setup_url);

  fail_unless (gst_rtsp_media_unprepare (media));

  gst_rtsp_url_free (setup_url);
  gst_rtsp_url_free (url);

  gst_rtsp_media_unlock (media);
  g_object_unref (sm);

  g_object_unref (factory);
  g_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_rtsp_state)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPSessionMedia *sm;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse (TEST_PATH, &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (GST_IS_RTSP_STREAM (stream));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  sm = gst_rtsp_session_media_new (TEST_PATH, media);
  fail_unless (GST_IS_RTSP_SESSION_MEDIA (sm));
  fail_unless_equals_int (gst_rtsp_session_media_get_rtsp_state (sm),
      GST_RTSP_STATE_INIT);

  gst_rtsp_session_media_set_rtsp_state (sm, GST_RTSP_STATE_READY);
  fail_unless_equals_int (gst_rtsp_session_media_get_rtsp_state (sm),
      GST_RTSP_STATE_READY);

  gst_rtsp_session_media_set_rtsp_state (sm, GST_RTSP_STATE_SEEKING);
  fail_unless_equals_int (gst_rtsp_session_media_get_rtsp_state (sm),
      GST_RTSP_STATE_SEEKING);

  gst_rtsp_session_media_set_rtsp_state (sm, GST_RTSP_STATE_PLAYING);
  fail_unless_equals_int (gst_rtsp_session_media_get_rtsp_state (sm),
      GST_RTSP_STATE_PLAYING);

  gst_rtsp_session_media_set_rtsp_state (sm, GST_RTSP_STATE_RECORDING);
  fail_unless_equals_int (gst_rtsp_session_media_get_rtsp_state (sm),
      GST_RTSP_STATE_RECORDING);

  fail_unless (gst_rtsp_media_unprepare (media));

  gst_rtsp_url_free (url);

  gst_rtsp_media_unlock (media);
  g_object_unref (sm);

  g_object_unref (factory);
  g_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_transports)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream1, *stream2;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPSessionMedia *sm;
  GstRTSPStreamTransport *trans;
  GstRTSPTransport *ct1, *ct2, *ct3, *ct4;
  gint match_len;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse (TEST_PATH, &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 audiotestsrc ! rtpgstpay pt=97 name=pay1 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 2);

  stream1 = gst_rtsp_media_get_stream (media, 0);
  fail_unless (GST_IS_RTSP_STREAM (stream1));

  stream2 = gst_rtsp_media_get_stream (media, 1);
  fail_unless (GST_IS_RTSP_STREAM (stream2));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  sm = gst_rtsp_session_media_new (TEST_PATH, media);
  fail_unless (GST_IS_RTSP_SESSION_MEDIA (sm));
  fail_unless (gst_rtsp_session_media_matches (sm, TEST_PATH, &match_len));
  fail_unless (match_len == strlen (TEST_PATH));

  gst_rtsp_transport_new (&ct1);
  trans = gst_rtsp_session_media_set_transport (sm, stream1, ct1);
  fail_unless (gst_rtsp_session_media_get_transport (sm, 0) == trans);

  gst_rtsp_transport_new (&ct2);
  trans = gst_rtsp_session_media_set_transport (sm, stream1, ct2);
  fail_unless (gst_rtsp_session_media_get_transport (sm, 0) == trans);

  gst_rtsp_transport_new (&ct3);
  trans = gst_rtsp_session_media_set_transport (sm, stream2, ct3);
  fail_unless (gst_rtsp_session_media_get_transport (sm, 1) == trans);

  gst_rtsp_transport_new (&ct4);
  trans = gst_rtsp_session_media_set_transport (sm, stream2, ct4);
  fail_unless (gst_rtsp_session_media_get_transport (sm, 1) == trans);

  fail_unless (gst_rtsp_media_unprepare (media));

  gst_rtsp_url_free (url);

  gst_rtsp_media_unlock (media);
  g_object_unref (sm);

  g_object_unref (factory);
  g_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_time_and_rtpinfo)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream1, *stream2;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPSessionMedia *sm;
  GstClockTime base_time;
  gchar *rtpinfo;
  GstRTSPTransport *ct1;
  GstRTSPStreamTransport *trans;
  GstRTSPUrl *setup_url;
  gchar **streaminfo;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse (TEST_PATH, &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc do-timestamp=true timestamp-offset=0 ! rtpvrawpay pt=96 name=pay0 "
      "audiotestsrc do-timestamp=true timestamp-offset=1000000000 ! rtpgstpay pt=97 name=pay1 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 2);

  stream1 = gst_rtsp_media_get_stream (media, 0);
  fail_unless (GST_IS_RTSP_STREAM (stream1));

  stream2 = gst_rtsp_media_get_stream (media, 1);
  fail_unless (GST_IS_RTSP_STREAM (stream2));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  sm = gst_rtsp_session_media_new (TEST_PATH, media);
  fail_unless (GST_IS_RTSP_SESSION_MEDIA (sm));

  base_time = gst_rtsp_session_media_get_base_time (sm);
  fail_unless_equals_int64 (base_time, 0);

  rtpinfo = gst_rtsp_session_media_get_rtpinfo (sm);
  fail_unless (rtpinfo == NULL);

  gst_rtsp_transport_new (&ct1);
  trans = gst_rtsp_session_media_set_transport (sm, stream1, ct1);
  fail_unless (gst_rtsp_session_media_get_transport (sm, 0) == trans);
  fail_unless (gst_rtsp_url_parse (SETUP_URL1, &setup_url) == GST_RTSP_OK);
  gst_rtsp_stream_transport_set_url (trans, setup_url);

  base_time = gst_rtsp_session_media_get_base_time (sm);
  fail_unless_equals_int64 (base_time, 0);

  rtpinfo = gst_rtsp_session_media_get_rtpinfo (sm);
  streaminfo = g_strsplit (rtpinfo, ",", 1);
  g_free (rtpinfo);

  fail_unless (g_strstr_len (streaminfo[0], -1, "url=") != NULL);
  fail_unless (g_strstr_len (streaminfo[0], -1, "seq=") != NULL);
  fail_unless (g_strstr_len (streaminfo[0], -1, "rtptime=") != NULL);
  fail_unless (g_strstr_len (streaminfo[0], -1, SETUP_URL1) != NULL);

  g_strfreev (streaminfo);

  fail_unless (gst_rtsp_media_unprepare (media));

  rtpinfo = gst_rtsp_session_media_get_rtpinfo (sm);
  fail_unless (rtpinfo == NULL);

  gst_rtsp_url_free (setup_url);
  gst_rtsp_url_free (url);

  gst_rtsp_media_unlock (media);
  g_object_unref (sm);

  g_object_unref (factory);
  g_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_allocate_channels)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPSessionMedia *sm;
  GstRTSPRange range;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse (TEST_PATH, &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (GST_IS_RTSP_STREAM (stream));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  sm = gst_rtsp_session_media_new (TEST_PATH, media);
  fail_unless (GST_IS_RTSP_SESSION_MEDIA (sm));

  fail_unless (gst_rtsp_session_media_alloc_channels (sm, &range));
  fail_unless_equals_int (range.min, 0);
  fail_unless_equals_int (range.max, 1);

  fail_unless (gst_rtsp_session_media_alloc_channels (sm, &range));
  fail_unless_equals_int (range.min, 2);
  fail_unless_equals_int (range.max, 3);

  fail_unless (gst_rtsp_media_unprepare (media));

  gst_rtsp_url_free (url);

  gst_rtsp_media_unlock (media);
  g_object_unref (sm);

  g_object_unref (factory);
  g_object_unref (pool);
}

GST_END_TEST;
static Suite *
rtspsessionmedia_suite (void)
{
  Suite *s = suite_create ("rtspsessionmedia");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_setup_url);
  tcase_add_test (tc, test_rtsp_state);
  tcase_add_test (tc, test_transports);
  tcase_add_test (tc, test_time_and_rtpinfo);
  tcase_add_test (tc, test_allocate_channels);

  return s;
}

GST_CHECK_MAIN (rtspsessionmedia);

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
#define SETUP_URL TEST_PATH "/stream=0"

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
  fail_unless (stream != NULL);

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  /* create session media and make sure it matches test path
   * note that gst_rtsp_session_media_new takes ownership of the media
   * thus no need to unref it at the bottom of function */
  sm = gst_rtsp_session_media_new (TEST_PATH, media);
  fail_unless (sm != NULL);
  fail_unless (gst_rtsp_session_media_matches (sm, TEST_PATH, &match_len));
  fail_unless (match_len == strlen (TEST_PATH));

  /* make a transport for the stream */
  gst_rtsp_transport_new (&ct);
  trans = gst_rtsp_session_media_set_transport (sm, stream, ct);

  /* make sure there's no setup url stored initially */
  fail_unless (gst_rtsp_stream_transport_get_url (trans) == NULL);

  /* now store a setup url and make sure it can be retrieved and that it's correct */
  fail_unless (gst_rtsp_url_parse (SETUP_URL, &setup_url) == GST_RTSP_OK);
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

  return s;
}

GST_CHECK_MAIN (rtspsessionmedia);

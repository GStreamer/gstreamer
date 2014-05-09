/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
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

GST_START_TEST (test_launch)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;
  GstRTSPTimeRange *range;
  gchar *str;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);

  /* fails, need to be prepared */
  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (str == NULL);

  fail_unless (gst_rtsp_range_parse ("npt=5.0-", &range) == GST_RTSP_OK);
  /* fails, need to be prepared */
  fail_if (gst_rtsp_media_seek (media, range));

  pool = gst_rtsp_thread_pool_new ();
  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);

  fail_unless (gst_rtsp_media_prepare (media, thread));

  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=0-"));
  g_free (str);

  str = gst_rtsp_media_get_range_string (media, TRUE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=0-"));
  g_free (str);

  fail_unless (gst_rtsp_media_seek (media, range));

  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=5-"));
  g_free (str);

  str = gst_rtsp_media_get_range_string (media, TRUE, GST_RTSP_RANGE_NPT);
  fail_unless (g_str_equal (str, "npt=5-"));
  g_free (str);

  fail_unless (gst_rtsp_media_unprepare (media));

  /* should fail again */
  str = gst_rtsp_media_get_range_string (media, FALSE, GST_RTSP_RANGE_NPT);
  fail_unless (str == NULL);
  fail_if (gst_rtsp_media_seek (media, range));

  gst_rtsp_range_free (range);
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);

  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_media)
{
  GstRTSPMedia *media;
  GstElement *bin, *e1, *e2;

  bin = gst_bin_new ("bin");
  fail_if (bin == NULL);

  e1 = gst_element_factory_make ("videotestsrc", NULL);
  fail_if (e1 == NULL);

  e2 = gst_element_factory_make ("rtpvrawpay", "pay0");
  fail_if (e2 == NULL);
  g_object_set (e2, "pt", 96, NULL);

  gst_bin_add_many (GST_BIN_CAST (bin), e1, e2, NULL);
  gst_element_link_many (e1, e2, NULL);

  media = gst_rtsp_media_new (bin);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  g_object_unref (media);
}

GST_END_TEST;

static void
test_prepare_reusable (GstRTSPThreadPool * pool, const gchar * launch_line)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThread *thread;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory, launch_line);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  g_object_set (G_OBJECT (media), "reusable", TRUE, NULL);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_unprepare (media));

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_START_TEST (test_media_prepare)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  pool = gst_rtsp_thread_pool_new ();

  /* test non-reusable media first */
  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_if (gst_rtsp_media_prepare (media, thread));

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);

  /* test reusable media */
  test_prepare_reusable (pool, "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");
  test_prepare_reusable (pool,
      "( videotestsrc is-live=true ! rtpvrawpay pt=96 name=pay0 )");

  g_object_unref (pool);
  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

static void
on_notify_caps (GstPad * pad, GParamSpec * pspec, GstElement * pay)
{
  GstCaps *caps;
  static gboolean have_caps = FALSE;

  g_object_get (pad, "caps", &caps, NULL);

  GST_DEBUG ("notify %" GST_PTR_FORMAT, caps);

  if (caps) {
    if (!have_caps) {
      g_signal_emit_by_name (pay, "pad-added", pad);
      g_signal_emit_by_name (pay, "no-more-pads", NULL);
      have_caps = TRUE;
    }
    gst_caps_unref (caps);
  } else {
    if (have_caps) {
      g_signal_emit_by_name (pay, "pad-removed", pad);
      have_caps = FALSE;
    }
  }
}

GST_START_TEST (test_media_dyn_prepare)
{
  GstRTSPMedia *media;
  GstElement *bin, *src, *pay;
  GstElement *pipeline;
  GstPad *srcpad;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  bin = gst_bin_new ("bin");
  fail_if (bin == NULL);

  src = gst_element_factory_make ("videotestsrc", NULL);
  fail_if (src == NULL);

  pay = gst_element_factory_make ("rtpvrawpay", "dynpay0");
  fail_if (pay == NULL);
  g_object_set (pay, "pt", 96, NULL);

  gst_bin_add_many (GST_BIN_CAST (bin), src, pay, NULL);
  gst_element_link_many (src, pay, NULL);

  media = gst_rtsp_media_new (bin);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  g_object_set (G_OBJECT (media), "reusable", TRUE, NULL);

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  gst_rtsp_media_collect_streams (media);

  srcpad = gst_element_get_static_pad (pay, "src");

  g_signal_connect (srcpad, "notify::caps", (GCallback) on_notify_caps, pay);

  pool = gst_rtsp_thread_pool_new ();

  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_n_streams (media) == 1);
  fail_unless (gst_rtsp_media_unprepare (media));
  fail_unless (gst_rtsp_media_n_streams (media) == 0);

  gst_object_unref (srcpad);
  g_object_unref (media);
  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

GST_START_TEST (test_media_prepare_port_alloc_fail)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;
  GstRTSPAddressPool *addrpool;

  pool = gst_rtsp_thread_pool_new ();

  factory = gst_rtsp_media_factory_new ();
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( fakesrc is-live=true ! text/plain ! rtpgstpay name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  addrpool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (addrpool, "192.168.1.1",
          "192.168.1.1", 6000, 6001, 0));
  gst_rtsp_media_set_address_pool (media, addrpool);

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_if (gst_rtsp_media_prepare (media, thread));

  g_object_unref (media);
  g_object_unref (addrpool);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
  g_object_unref (pool);
}

GST_END_TEST;

GST_START_TEST (test_media_take_pipeline)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstElement *pipeline;

  factory = gst_rtsp_media_factory_new ();
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);
  gst_rtsp_media_factory_set_launch (factory,
      "( fakesrc ! text/plain ! rtpgstpay name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_media_reset)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPThreadPool *pool;
  GstRTSPThread *thread;

  pool = gst_rtsp_thread_pool_new ();

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  gst_rtsp_url_parse ("rtsp://localhost:8554/test", &url);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_suspend (media));
  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  thread = gst_rtsp_thread_pool_get_thread (pool,
      GST_RTSP_THREAD_TYPE_MEDIA, NULL);
  gst_rtsp_media_set_suspend_mode (media, GST_RTSP_SUSPEND_MODE_RESET);
  fail_unless (gst_rtsp_media_prepare (media, thread));
  fail_unless (gst_rtsp_media_suspend (media));
  fail_unless (gst_rtsp_media_unprepare (media));
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
  g_object_unref (pool);

  gst_rtsp_thread_pool_cleanup ();
}

GST_END_TEST;

static Suite *
rtspmedia_suite (void)
{
  Suite *s = suite_create ("rtspmedia");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_launch);
  tcase_add_test (tc, test_media);
  tcase_add_test (tc, test_media_prepare);
  tcase_add_test (tc, test_media_dyn_prepare);
  tcase_add_test (tc, test_media_prepare_port_alloc_fail);
  tcase_add_test (tc, test_media_take_pipeline);
  tcase_add_test (tc, test_media_reset);

  return s;
}

GST_CHECK_MAIN (rtspmedia);

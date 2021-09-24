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

GST_START_TEST (test_parse_error)
{
  GstRTSPMediaFactory *factory;
  GstRTSPUrl *url;

  factory = gst_rtsp_media_factory_new ();

  gst_rtsp_media_factory_set_launch (factory, "foo");
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);
  ASSERT_CRITICAL (gst_rtsp_media_factory_create_element (factory, url));
  ASSERT_CRITICAL (gst_rtsp_media_factory_construct (factory, url));

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_launch)
{
  GstRTSPMediaFactory *factory;
  GstElement *element;
  GstRTSPUrl *url;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  element = gst_rtsp_media_factory_create_element (factory, url);
  fail_unless (GST_IS_BIN (element));
  fail_if (GST_IS_PIPELINE (element));
  gst_object_unref (element);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_launch_construct)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media, *media2;
  GstRTSPUrl *url;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  media2 = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media2));
  fail_if (media == media2);

  g_object_unref (media);
  g_object_unref (media2);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_shared)
{
  GstRTSPMediaFactory *factory;
  GstElement *element;
  GstRTSPMedia *media, *media2;
  GstRTSPUrl *url;

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  fail_unless (gst_rtsp_media_factory_is_shared (factory));

  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  element = gst_rtsp_media_factory_create_element (factory, url);
  fail_unless (GST_IS_BIN (element));
  fail_if (GST_IS_PIPELINE (element));
  gst_object_unref (element);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  media2 = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media2));
  fail_unless (media == media2);

  g_object_unref (media);
  g_object_unref (media2);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_addresspool)
{
  GstRTSPMediaFactory *factory;
  GstElement *element;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPAddressPool *pool, *tmppool;
  GstRTSPStream *stream;
  GstRTSPAddress *addr;

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 "
      " audiotestsrc ! audioconvert ! rtpL16pay name=pay1 )");

  pool = gst_rtsp_address_pool_new ();
  fail_unless (gst_rtsp_address_pool_add_range (pool,
          "233.252.0.1", "233.252.0.1", 5000, 5001, 3));

  gst_rtsp_media_factory_set_address_pool (factory, pool);

  tmppool = gst_rtsp_media_factory_get_address_pool (factory);
  fail_unless (pool == tmppool);
  g_object_unref (tmppool);

  element = gst_rtsp_media_factory_create_element (factory, url);
  fail_unless (GST_IS_BIN (element));
  fail_if (GST_IS_PIPELINE (element));
  gst_object_unref (element);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  tmppool = gst_rtsp_media_get_address_pool (media);
  fail_unless (pool == tmppool);
  g_object_unref (tmppool);

  fail_unless (gst_rtsp_media_n_streams (media) == 2);

  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);

  tmppool = gst_rtsp_stream_get_address_pool (stream);
  fail_unless (pool == tmppool);
  g_object_unref (tmppool);

  addr = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV4);
  fail_unless (addr != NULL);
  fail_unless (addr->port == 5000);
  fail_unless (addr->n_ports == 2);
  fail_unless (addr->ttl == 3);
  gst_rtsp_address_free (addr);

  stream = gst_rtsp_media_get_stream (media, 1);
  fail_unless (stream != NULL);

  tmppool = gst_rtsp_stream_get_address_pool (stream);
  fail_unless (pool == tmppool);
  g_object_unref (tmppool);

  addr = gst_rtsp_stream_get_multicast_address (stream, G_SOCKET_FAMILY_IPV4);
  fail_unless (addr == NULL);


  g_object_unref (media);

  g_object_unref (pool);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_permissions)
{
  GstRTSPMediaFactory *factory;
  GstRTSPPermissions *perms;
  GstRTSPMedia *media;
  GstRTSPUrl *url;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  gst_rtsp_media_factory_add_role (factory, "admin",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, TRUE, NULL);

  perms = gst_rtsp_media_factory_get_permissions (factory);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin",
          "media.factory.access"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin",
          "media.factory.construct"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "missing",
          "media.factory.access"));
  gst_rtsp_permissions_unref (perms);

  perms = gst_rtsp_permissions_new ();
  gst_rtsp_permissions_add_role (perms, "user",
      "media.factory.access", G_TYPE_BOOLEAN, TRUE,
      "media.factory.construct", G_TYPE_BOOLEAN, FALSE, NULL);
  gst_rtsp_media_factory_set_permissions (factory, perms);
  gst_rtsp_permissions_unref (perms);

  perms = gst_rtsp_media_factory_get_permissions (factory);
  fail_if (gst_rtsp_permissions_is_allowed (perms, "admin",
          "media.factory.access"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "admin",
          "media.factory.construct"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user",
          "media.factory.access"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user",
          "media.factory.construct"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "missing",
          "media.factory.access"));
  gst_rtsp_permissions_unref (perms);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  perms = gst_rtsp_media_get_permissions (media);
  fail_if (gst_rtsp_permissions_is_allowed (perms, "admin",
          "media.factory.access"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "admin",
          "media.factory.construct"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user",
          "media.factory.access"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user",
          "media.factory.construct"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "missing",
          "media.factory.access"));
  gst_rtsp_permissions_unref (perms);
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_reset)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;

  factory = gst_rtsp_media_factory_new ();
  fail_if (gst_rtsp_media_factory_is_shared (factory));
  gst_rtsp_url_parse ("rtsp://localhost:8554/test", &url);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 )");

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_if (gst_rtsp_media_get_suspend_mode (media) !=
      GST_RTSP_SUSPEND_MODE_NONE);
  g_object_unref (media);

  gst_rtsp_media_factory_set_suspend_mode (factory,
      GST_RTSP_SUSPEND_MODE_RESET);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));
  fail_if (gst_rtsp_media_get_suspend_mode (media) !=
      GST_RTSP_SUSPEND_MODE_RESET);
  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

GST_START_TEST (test_mcast_ttl)
{
  GstRTSPMediaFactory *factory;
  GstElement *element;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 "
      " audiotestsrc ! audioconvert ! rtpL16pay name=pay1 )");

  /* try to set an invalid ttl and make sure that the default ttl value (255) is
   * set */
  gst_rtsp_media_factory_set_max_mcast_ttl (factory, 0);
  fail_unless (gst_rtsp_media_factory_get_max_mcast_ttl (factory) == 255);
  gst_rtsp_media_factory_set_max_mcast_ttl (factory, 300);
  fail_unless (gst_rtsp_media_factory_get_max_mcast_ttl (factory) == 255);

  /* set a valid ttl value */
  gst_rtsp_media_factory_set_max_mcast_ttl (factory, 3);
  fail_unless (gst_rtsp_media_factory_get_max_mcast_ttl (factory) == 3);

  element = gst_rtsp_media_factory_create_element (factory, url);
  fail_unless (GST_IS_BIN (element));
  fail_if (GST_IS_PIPELINE (element));
  gst_object_unref (element);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  fail_unless (gst_rtsp_media_n_streams (media) == 2);
  fail_unless (gst_rtsp_media_get_max_mcast_ttl (media) == 3);

  /* verify that the correct ttl value has been propageted to the media
   * streams */
  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);
  fail_unless (gst_rtsp_stream_get_max_mcast_ttl (stream) == 3);

  stream = gst_rtsp_media_get_stream (media, 1);
  fail_unless (stream != NULL);
  fail_unless (gst_rtsp_stream_get_max_mcast_ttl (stream) == 3);

  g_object_unref (media);

  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;


GST_START_TEST (test_allow_bind_mcast)
{
  GstRTSPMediaFactory *factory;
  GstRTSPMedia *media;
  GstRTSPUrl *url;
  GstRTSPStream *stream;

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_shared (factory, TRUE);
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);

  gst_rtsp_media_factory_set_launch (factory,
      "( videotestsrc ! rtpvrawpay pt=96 name=pay0 "
      " audiotestsrc ! audioconvert ! rtpL16pay name=pay1 )");

  /* verify that by default binding sockets to multicast addresses is not enabled */
  fail_unless (gst_rtsp_media_factory_is_bind_mcast_address (factory) == FALSE);

  /* allow multicast sockets to be bound to multicast addresses */
  gst_rtsp_media_factory_set_bind_mcast_address (factory, TRUE);
  /* verify that the socket binding to multicast address has been enabled */
  fail_unless (gst_rtsp_media_factory_is_bind_mcast_address (factory) == TRUE);

  media = gst_rtsp_media_factory_construct (factory, url);
  fail_unless (GST_IS_RTSP_MEDIA (media));

  /* verify that the correct socket binding configuration has been propageted to the media */
  fail_unless (gst_rtsp_media_is_bind_mcast_address (media) == TRUE);

  fail_unless (gst_rtsp_media_n_streams (media) == 2);

  /* verify that the correct socket binding configuration has been propageted to the media streams */
  stream = gst_rtsp_media_get_stream (media, 0);
  fail_unless (stream != NULL);
  fail_unless (gst_rtsp_stream_is_bind_mcast_address (stream) == TRUE);

  stream = gst_rtsp_media_get_stream (media, 1);
  fail_unless (stream != NULL);
  fail_unless (gst_rtsp_stream_is_bind_mcast_address (stream) == TRUE);

  g_object_unref (media);
  gst_rtsp_url_free (url);
  g_object_unref (factory);
}

GST_END_TEST;

static Suite *
rtspmediafactory_suite (void)
{
  Suite *s = suite_create ("rtspmediafactory");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_parse_error);
  tcase_add_test (tc, test_launch);
  tcase_add_test (tc, test_launch_construct);
  tcase_add_test (tc, test_shared);
  tcase_add_test (tc, test_addresspool);
  tcase_add_test (tc, test_permissions);
  tcase_add_test (tc, test_reset);
  tcase_add_test (tc, test_mcast_ttl);
  tcase_add_test (tc, test_allow_bind_mcast);

  return s;
}

GST_CHECK_MAIN (rtspmediafactory);

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

#include <rtsp-mount-points.h>

GST_START_TEST (test_create)
{
  GstRTSPMountPoints *mounts;
  GstRTSPUrl *url, *url2;
  GstRTSPMediaFactory *factory;

  mounts = gst_rtsp_mount_points_new ();

  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test",
          &url) == GST_RTSP_OK);
  fail_unless (gst_rtsp_url_parse ("rtsp://localhost:8554/test2",
          &url2) == GST_RTSP_OK);

  fail_unless (gst_rtsp_mount_points_match (mounts, url->abspath,
          NULL) == NULL);

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  fail_unless (gst_rtsp_mount_points_match (mounts, url->abspath,
          NULL) == factory);
  g_object_unref (factory);
  fail_unless (gst_rtsp_mount_points_match (mounts, url2->abspath,
          NULL) == NULL);

  gst_rtsp_mount_points_remove_factory (mounts, "/test");

  fail_unless (gst_rtsp_mount_points_match (mounts, url->abspath,
          NULL) == NULL);
  fail_unless (gst_rtsp_mount_points_match (mounts, url2->abspath,
          NULL) == NULL);

  gst_rtsp_url_free (url);
  gst_rtsp_url_free (url2);

  g_object_unref (mounts);
}

GST_END_TEST;

static const gchar *paths[] = {
  "/test",
  "/booz/fooz",
  "/booz/foo/zoop",
  "/tark/bar",
  "/tark/bar/baz",
  "/tark/bar/baz/t",
  "/boozop",
  "/raw",
  "/raw/video",
  "/raw/snapshot",
};

GST_START_TEST (test_match)
{
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *f[G_N_ELEMENTS (paths)], *tmp;
  gint i, matched;

  mounts = gst_rtsp_mount_points_new ();

  for (i = 0; i < G_N_ELEMENTS (paths); i++) {
    f[i] = gst_rtsp_media_factory_new ();
    gst_rtsp_mount_points_add_factory (mounts, paths[i], f[i]);
  }

  tmp = gst_rtsp_mount_points_match (mounts, "/test", &matched);
  fail_unless (tmp == f[0]);
  fail_unless (matched == 5);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/test/stream=1", &matched);
  fail_unless (tmp == f[0]);
  fail_unless (matched == 5);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/booz", &matched);
  fail_unless (tmp == NULL);
  tmp = gst_rtsp_mount_points_match (mounts, "/booz/foo", &matched);
  fail_unless (tmp == NULL);
  tmp = gst_rtsp_mount_points_match (mounts, "/booz/fooz", &matched);
  fail_unless (tmp == f[1]);
  fail_unless (matched == 10);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/booz/fooz/zoo", &matched);
  fail_unless (tmp == f[1]);
  fail_unless (matched == 10);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/booz/foo/zoop", &matched);
  fail_unless (tmp == f[2]);
  fail_unless (matched == 14);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/tark/bar", &matched);
  fail_unless (tmp == f[3]);
  fail_unless (matched == 9);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/tark/bar/boo", &matched);
  fail_unless (tmp == f[3]);
  fail_unless (matched == 9);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/tark/bar/ba", &matched);
  fail_unless (tmp == f[3]);
  fail_unless (matched == 9);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/tark/bar/baz", &matched);
  fail_unless (tmp == f[4]);
  fail_unless (matched == 13);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/raw/video", &matched);
  fail_unless (tmp == f[8]);
  fail_unless (matched == 10);
  g_object_unref (tmp);
  tmp = gst_rtsp_mount_points_match (mounts, "/raw/snapshot", &matched);
  fail_unless (tmp == f[9]);
  fail_unless (matched == 13);
  g_object_unref (tmp);

  g_object_unref (mounts);
}

GST_END_TEST;

static Suite *
rtspmountpoints_suite (void)
{
  Suite *s = suite_create ("rtspmountpoints");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_create);
  tcase_add_test (tc, test_match);

  return s;
}

GST_CHECK_MAIN (rtspmountpoints);

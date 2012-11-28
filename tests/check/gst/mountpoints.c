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

  fail_unless (gst_rtsp_mount_points_find_factory (mounts, url) == NULL);

  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_mount_points_add_factory (mounts, "/test", factory);

  fail_unless (gst_rtsp_mount_points_find_factory (mounts, url) == factory);
  g_object_unref (factory);
  fail_unless (gst_rtsp_mount_points_find_factory (mounts, url2) == NULL);

  gst_rtsp_mount_points_remove_factory (mounts, "/test");

  fail_unless (gst_rtsp_mount_points_find_factory (mounts, url) == NULL);
  fail_unless (gst_rtsp_mount_points_find_factory (mounts, url2) == NULL);

  gst_rtsp_url_free (url);
  gst_rtsp_url_free (url2);

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

  return s;
}

GST_CHECK_MAIN (rtspmountpoints);

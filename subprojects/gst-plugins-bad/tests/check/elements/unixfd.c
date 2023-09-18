/* GStreamer unix file-descriptor source/sink tests
 *
 * Copyright (C) 2023 Netflix Inc.
 *  Author: Xavier Claessens <xavier.claessens@collabora.com>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <glib/gstdio.h>

static void
wait_preroll (GstElement * element)
{
  GstStateChangeReturn state_res =
      gst_element_set_state (element, GST_STATE_PLAYING);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE);
  state_res = gst_element_get_state (element, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (state_res == GST_STATE_CHANGE_SUCCESS);
}

GST_START_TEST (test_unixfd_videotestsrc)
{
  GError *error = NULL;

  /* Ensure we don't have socket from previous failed test */
  gchar *socket_path =
      g_strdup_printf ("%s/unixfd-test-socket", g_get_user_runtime_dir ());
  if (g_file_test (socket_path, G_FILE_TEST_EXISTS)) {
    g_unlink (socket_path);
  }

  /* Setup source */
  gchar *pipeline_str =
      g_strdup_printf ("videotestsrc ! unixfdsink socket-path=%s", socket_path);
  GstElement *pipeline_service = gst_parse_launch (pipeline_str, &error);
  g_assert_no_error (error);
  g_free (pipeline_str);
  wait_preroll (pipeline_service);

  /* Setup sink */
  pipeline_str =
      g_strdup_printf ("unixfdsrc socket-path=%s ! fakesink", socket_path);
  GstElement *pipeline_client_1 = gst_parse_launch (pipeline_str, &error);
  g_assert_no_error (error);
  wait_preroll (pipeline_client_1);

  /* disconnect, reconnect */
  fail_unless (gst_element_set_state (pipeline_client_1,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);
  wait_preroll (pipeline_client_1);

  /* Connect 2nd sink */
  GstElement *pipeline_client_2 = gst_parse_launch (pipeline_str, &error);
  g_assert_no_error (error);
  wait_preroll (pipeline_client_2);

  /* Teardown */
  fail_unless (gst_element_set_state (pipeline_client_1,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (gst_element_set_state (pipeline_client_2,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (gst_element_set_state (pipeline_service,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  fail_if (g_file_test (socket_path, G_FILE_TEST_EXISTS));

  gst_object_unref (pipeline_service);
  gst_object_unref (pipeline_client_1);
  gst_object_unref (pipeline_client_2);
  g_free (socket_path);
  g_free (pipeline_str);
}

GST_END_TEST;

static Suite *
unixfd_suite (void)
{
  Suite *s = suite_create ("unixfd");
  TCase *tc = tcase_create ("unixfd");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_unixfd_videotestsrc);

  return s;
}

GST_CHECK_MAIN (unixfd);

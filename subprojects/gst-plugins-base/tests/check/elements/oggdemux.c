/* GStreamer
* Copyright (C) 2026 Collabora Ltd.
*   @author: Jakub Adam <jakub.adam@collabora.com>
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

#include <glib/gstdio.h>
#include <gst/check/gstcheck.h>

/* Adding queue before oggdemux forces push mode. */
#define PUSH_MODE_DURATION_PIPELINE "filesrc name=src ! queue ! oggdemux ! fakesink"
#define PUSH_MODE_DURATION_PIPELINE_QUEUE2 "filesrc name=src ! queue2 ring-buffer-max-size=10000000 ! queue ! oggdemux ! fakesink"

gchar *path = NULL;

static void
setup_file (void)
{
  path = g_build_filename (GST_TEST_FILES_PATH, "opus-5s.ogg", NULL);
}

static void
cleanup_file (void)
{
  g_clear_pointer (&path, g_free);
}

static void
setup_file_truncated (void)
{
  gchar *orig_path =
      g_build_filename (GST_TEST_FILES_PATH, "opus-5s.ogg", NULL);
  gchar *contents = NULL;
  gsize length = 0;
  gboolean ret;
  gint fd;

  ret = g_file_get_contents (orig_path, &contents, &length, NULL);
  g_clear_pointer (&orig_path, g_free);
  fail_unless (ret);

  fd = g_file_open_tmp ("opus-5s-truncated.ogg-XXXXXX", &path, NULL);
  fail_if (fd == -1);

  ret = g_file_set_contents (path, contents, length - 100, NULL);
  g_clear_pointer (&contents, g_free);
  if (!ret) {
    g_unlink (path);
    fail ();
  }
}

static void
cleanup_file_truncated (void)
{
  g_unlink (path);
  g_clear_pointer (&path, g_free);
}

static void
push_mode_duration_test (const gchar * pipeline_desc,
    gboolean should_find_duration)
{
  GstElement *pipeline, *src;
  GstStateChangeReturn ret;
  GstMessage *msg;
  gint64 duration;

  pipeline = gst_parse_launch (pipeline_desc, NULL);
  fail_unless (pipeline);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_unless (src);

  g_object_set (src, "location", path, NULL);
  gst_clear_object (&src);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  /* Wait for duration change or error */
  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_DURATION_CHANGED | GST_MESSAGE_EOS);
  fail_unless (msg);
  if (should_find_duration) {
    fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_DURATION_CHANGED);
    gst_element_query_duration (pipeline, GST_FORMAT_TIME, &duration);
    fail_unless_equals_int64 (duration, 5 * GST_SECOND);
  } else {
    fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  }

  gst_clear_message (&msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_clear_object (&pipeline);
}

GST_START_TEST (test_push_mode_duration)
{
  push_mode_duration_test (PUSH_MODE_DURATION_PIPELINE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_push_mode_duration_queue2)
{
  push_mode_duration_test (PUSH_MODE_DURATION_PIPELINE_QUEUE2, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_push_mode_duration_truncated_file)
{
  push_mode_duration_test (PUSH_MODE_DURATION_PIPELINE, FALSE);
}

GST_END_TEST;

GST_START_TEST (test_push_mode_duration_truncated_file_queue2)
{
  push_mode_duration_test (PUSH_MODE_DURATION_PIPELINE_QUEUE2, FALSE);
}

GST_END_TEST;

static Suite *
oggdemux_suite (void)
{
  Suite *s = suite_create ("oggdemux");
  TCase *tc_chain;

  tc_chain = tcase_create ("general");
  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup_file, cleanup_file);
  tcase_add_test (tc_chain, test_push_mode_duration);
  tcase_add_test (tc_chain, test_push_mode_duration_queue2);

  /* Getting duration of truncated Ogg files will fail, but it should not hang
   * the pipeline. */
  tc_chain = tcase_create ("truncated");
  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup_file_truncated,
      cleanup_file_truncated);
  /* Pipelines with queue2 can hang indefinitely on test failure, but duration
   * should be determined very quickly. Don't let meson test wait the whole 180s
   * before timing out. */
  tcase_set_timeout (tc_chain, 5);
  tcase_add_test (tc_chain, test_push_mode_duration_truncated_file);
  tcase_add_test (tc_chain, test_push_mode_duration_truncated_file_queue2);

  return s;
}

GST_CHECK_MAIN (oggdemux);

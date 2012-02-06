/* GStreamer unit test for multifile plugin
 *
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <glib/gstdio.h>
#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <stdlib.h>
#include <unistd.h>

static void
run_pipeline (GstElement * pipeline)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, -1);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* FIXME too lazy */
  g_usleep (1000000);
  gst_element_set_state (pipeline, GST_STATE_NULL);
}

GST_START_TEST (test_multifilesink_key_frame)
{
  GstElement *pipeline;
  GstElement *mfs;
  int i;
  const gchar *tmpdir;
  gchar *my_tmpdir;
  gchar *template;
  gchar *mfs_pattern;

  tmpdir = g_get_tmp_dir ();
  template = g_build_filename (tmpdir, "multifile-test-XXXXXX", NULL);
  my_tmpdir = g_mkdtemp (template);
  fail_if (my_tmpdir == NULL);

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=10 ! video/x-raw,format=(string)I420,width=320,height=240 ! multifilesink name=mfs",
      NULL);
  fail_if (pipeline == NULL);
  mfs = gst_bin_get_by_name (GST_BIN (pipeline), "mfs");
  fail_if (mfs == NULL);
  mfs_pattern = g_build_filename (my_tmpdir, "%05d", NULL);
  g_object_set (G_OBJECT (mfs), "location", mfs_pattern, NULL);
  g_object_unref (mfs);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  for (i = 0; i < 10; i++) {
    char *s;

    s = g_strdup_printf (mfs_pattern, i);
    fail_if (g_remove (s) != 0);
    g_free (s);
  }
  fail_if (g_remove (my_tmpdir) != 0);

  g_free (mfs_pattern);
  g_free (my_tmpdir);
}

GST_END_TEST;

GST_START_TEST (test_multifilesink_max_files)
{
  GstElement *pipeline;
  GstElement *mfs;
  int i;
  const gchar *tmpdir;
  gchar *my_tmpdir;
  gchar *template;
  gchar *mfs_pattern;

  tmpdir = g_get_tmp_dir ();
  template = g_build_filename (tmpdir, "multifile-test-XXXXXX", NULL);
  my_tmpdir = g_mkdtemp (template);
  fail_if (my_tmpdir == NULL);

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=10 ! video/x-raw,format=(string)I420,width=320,height=240 ! multifilesink name=mfs",
      NULL);
  fail_if (pipeline == NULL);
  mfs = gst_bin_get_by_name (GST_BIN (pipeline), "mfs");
  fail_if (mfs == NULL);
  mfs_pattern = g_build_filename (my_tmpdir, "%05d", NULL);
  g_object_set (G_OBJECT (mfs), "location", mfs_pattern, "max-files", 3, NULL);
  g_object_unref (mfs);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  for (i = 0; i < 7; i++) {
    char *s;

    s = g_strdup_printf (mfs_pattern, i);
    fail_unless (g_remove (s) != 0);
    g_free (s);
  }
  for (i = 7; i < 10; i++) {
    char *s;

    s = g_strdup_printf (mfs_pattern, i);
    fail_if (g_remove (s) != 0);
    g_free (s);
  }
  fail_if (g_remove (my_tmpdir) != 0);

  g_free (mfs_pattern);
  g_free (my_tmpdir);
}

GST_END_TEST;

GST_START_TEST (test_multifilesink_key_unit)
{
  GstElement *mfs;
  int i;
  const gchar *tmpdir;
  gchar *my_tmpdir;
  gchar *template;
  gchar *mfs_pattern;
  GstBuffer *buf;
  GstPad *sink;

  tmpdir = g_get_tmp_dir ();
  template = g_build_filename (tmpdir, "multifile-test-XXXXXX", NULL);
  my_tmpdir = g_mkdtemp (template);
  fail_if (my_tmpdir == NULL);

  mfs = gst_element_factory_make ("multifilesink", NULL);
  fail_if (mfs == NULL);
  mfs_pattern = g_build_filename (my_tmpdir, "%05d", NULL);
  g_object_set (G_OBJECT (mfs), "location", mfs_pattern, "next-file", 3, NULL);
  fail_if (gst_element_set_state (mfs,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  sink = gst_element_get_static_pad (mfs, "sink");
  buf = gst_buffer_new_and_alloc (4);

  gst_buffer_fill (buf, 0, "foo", 4);
  fail_if (gst_pad_chain (sink, gst_buffer_copy (buf)) != GST_FLOW_OK);

  gst_buffer_fill (buf, 0, "bar", 4);
  fail_if (gst_pad_chain (sink, gst_buffer_copy (buf)) != GST_FLOW_OK);

  fail_unless (gst_pad_send_event (sink,
          gst_video_event_new_downstream_force_key_unit (GST_CLOCK_TIME_NONE,
              GST_CLOCK_TIME_NONE, GST_CLOCK_TIME_NONE, TRUE, 1)));

  gst_buffer_fill (buf, 0, "baz", 4);
  fail_if (gst_pad_chain (sink, buf) != GST_FLOW_OK);

  fail_if (gst_element_set_state (mfs,
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  for (i = 0; i < 2; i++) {
    char *s;

    s = g_strdup_printf (mfs_pattern, i);
    fail_if (g_remove (s) != 0);
    g_free (s);
  }
  fail_if (g_remove (my_tmpdir) != 0);

  g_free (mfs_pattern);
  g_free (my_tmpdir);
  gst_object_unref (sink);
  gst_object_unref (mfs);
}

GST_END_TEST;

GST_START_TEST (test_multifilesrc)
{
  GstElement *pipeline;
  GstElement *mfs;
  int i;
  const gchar *tmpdir;
  gchar *my_tmpdir;
  gchar *template;
  gchar *mfs_pattern;

  tmpdir = g_get_tmp_dir ();
  template = g_build_filename (tmpdir, "multifile-test-XXXXXX", NULL);
  my_tmpdir = g_mkdtemp (template);
  fail_if (my_tmpdir == NULL);

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=10 ! video/x-raw,format=(string)I420,width=320,height=240 ! multifilesink name=mfs",
      NULL);
  fail_if (pipeline == NULL);
  mfs = gst_bin_get_by_name (GST_BIN (pipeline), "mfs");
  fail_if (mfs == NULL);
  mfs_pattern = g_build_filename (my_tmpdir, "%05d", NULL);
  g_object_set (G_OBJECT (mfs), "location", mfs_pattern, NULL);
  g_free (mfs_pattern);
  g_object_unref (mfs);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  pipeline =
      gst_parse_launch
      ("multifilesrc ! video/x-raw,format=(string)I420,width=320,height=240,framerate=10/1 ! fakesink",
      NULL);
  fail_if (pipeline == NULL);
  mfs = gst_bin_get_by_name (GST_BIN (pipeline), "multifilesrc0");
  fail_if (mfs == NULL);
  mfs_pattern = g_build_filename (my_tmpdir, "%05d", NULL);
  g_object_set (G_OBJECT (mfs), "location", mfs_pattern, NULL);
  g_object_unref (mfs);
  run_pipeline (pipeline);
  gst_object_unref (pipeline);

  for (i = 0; i < 10; i++) {
    char *s;

    s = g_strdup_printf (mfs_pattern, i);
    fail_if (g_remove (s) != 0);
    g_free (s);
  }
  fail_if (g_remove (my_tmpdir) != 0);

  g_free (mfs_pattern);
  g_free (my_tmpdir);
}

GST_END_TEST;

static Suite *
libvisual_suite (void)
{
  Suite *s = suite_create ("multifile");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_multifilesink_key_frame);
  tcase_add_test (tc_chain, test_multifilesink_max_files);
  tcase_add_test (tc_chain, test_multifilesink_key_unit);
  tcase_add_test (tc_chain, test_multifilesrc);

  return s;
}

GST_CHECK_MAIN (libvisual);

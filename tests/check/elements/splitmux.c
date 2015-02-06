/* GStreamer unit test for splitmuxsrc/sink elements
 *
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2015 Jan Schmidt <jan@centricular.com>
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
#  include "config.h"
#endif

#include <glib/gstdio.h>
#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <stdlib.h>
#include <unistd.h>

gchar *tmpdir = NULL;

static void
tempdir_setup (void)
{
  const gchar *systmp = g_get_tmp_dir ();
  tmpdir = g_build_filename (systmp, "splitmux-test-XXXXXX", NULL);
  /* Rewrites tmpdir template input: */
  tmpdir = g_mkdtemp (tmpdir);
}

static void
tempdir_cleanup (void)
{
  GDir *d;
  const gchar *f;

  fail_if (tmpdir == NULL);

  d = g_dir_open (tmpdir, 0, NULL);
  fail_if (d == NULL);

  while ((f = g_dir_read_name (d)) != NULL)
    fail_if (g_remove (f) != 0);
  g_dir_close (d);

  fail_if (g_remove (tmpdir) != 0);

  g_free (tmpdir);
  tmpdir = NULL;
}

static GstMessage *
run_pipeline (GstElement * pipeline)
{
  GstBus *bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  GstMessage *msg;

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (bus);

  return msg;
}

GST_START_TEST (test_splitmuxsrc)
{
  GstMessage *msg;
  GstElement *pipeline;
  GstElement *fakesink;
  gchar *in_pattern;
  gchar *uri;

  pipeline = gst_element_factory_make ("playbin", NULL);
  fail_if (pipeline == NULL);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink == NULL);
  g_object_set (G_OBJECT (pipeline), "video-sink", fakesink, NULL);

  in_pattern = g_build_filename (GST_TEST_FILES_PATH, "splitvideo*.ogg", NULL);
  uri = g_strdup_printf ("splitmux://%s", in_pattern);
  g_free (in_pattern);

  g_object_set (G_OBJECT (pipeline), "uri", uri, NULL);
  g_free (uri);

  msg = run_pipeline (pipeline);

  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
  gst_message_unref (msg);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
splitmux_suite (void)
{
  Suite *s = suite_create ("splitmux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_checked_fixture (tc_chain, tempdir_setup, tempdir_cleanup);

  tcase_add_test (tc_chain, test_splitmuxsrc);

  return s;
}

GST_CHECK_MAIN (splitmux);

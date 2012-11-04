/* GStreamer unit tests for flvdemux
 *
 * Copyright (C) 2009 Tim-Philipp Müller  <tim centricular net>
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

#include <gst/gst.h>

static void
pad_added_cb (GstElement * flvdemux, GstPad * pad, GstBin * pipeline)
{
  GstElement *sink;

  sink = gst_bin_get_by_name (pipeline, "fakesink");
  fail_unless (gst_element_link (flvdemux, sink));
  gst_object_unref (sink);

  gst_element_set_state (sink, GST_STATE_PAUSED);
}

static GstBusSyncReply
error_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    const gchar *file = (const gchar *) user_data;
    GError *err = NULL;
    gchar *dbg = NULL;

    gst_message_parse_error (msg, &err, &dbg);
    g_error ("ERROR for %s: %s\n%s\n", file, err->message, dbg);
  }

  return GST_BUS_PASS;
}

static void
handoff_cb (GstElement * element, GstBuffer * buf, GstPad * pad,
    gint * p_counter)
{
  *p_counter += 1;
  GST_LOG ("counter = %d", *p_counter);
}

static void
process_file (const gchar * file, gboolean push_mode, gint repeat,
    gint num_buffers)
{
  GstElement *src, *sep, *sink, *flvdemux, *pipeline;
  GstBus *bus;
  gchar *path;
  gint counter;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL, "Failed to create pipeline!");

  bus = gst_element_get_bus (pipeline);

  /* kids, don't use a sync handler for this at home, really; we do because
   * we just want to abort and nothing else */
  gst_bus_set_sync_handler (bus, error_cb, (gpointer) file, NULL);

  src = gst_element_factory_make ("filesrc", "filesrc");
  fail_unless (src != NULL, "Failed to create 'filesrc' element!");

  if (push_mode) {
    sep = gst_element_factory_make ("queue", "queue");
    fail_unless (sep != NULL, "Failed to create 'queue' element");
  } else {
    sep = gst_element_factory_make ("identity", "identity");
    fail_unless (sep != NULL, "Failed to create 'identity' element");
  }

  flvdemux = gst_element_factory_make ("flvdemux", "flvdemux");
  fail_unless (flvdemux != NULL, "Failed to create 'flvdemux' element!");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Failed to create 'fakesink' element!");

  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (handoff_cb), &counter);

  gst_bin_add_many (GST_BIN (pipeline), src, sep, flvdemux, sink, NULL);

  fail_unless (gst_element_link (src, sep));
  fail_unless (gst_element_link (sep, flvdemux));

  /* can't link flvdemux and sink yet, do that later */
  g_signal_connect (flvdemux, "pad-added", G_CALLBACK (pad_added_cb), pipeline);

  path = g_build_filename (GST_TEST_FILES_PATH, file, NULL);
  GST_LOG ("processing file '%s'", path);
  g_object_set (src, "location", path, NULL);

  do {
    GstStateChangeReturn state_ret;
    GstMessage *msg;

    GST_LOG ("repeat=%d", repeat);

    counter = 0;

    state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
    fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

    if (state_ret == GST_STATE_CHANGE_ASYNC) {
      GST_LOG ("waiting for pipeline to reach PAUSED state");
      state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
      fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
    }

    GST_LOG ("PAUSED, let's read all of it");

    state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

    msg = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
    fail_unless (msg != NULL, "Expected EOS message on bus! (%s)", file);

    gst_message_unref (msg);

    if (num_buffers >= 0) {
      fail_unless_equals_int (counter, num_buffers);
    }

    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
        GST_STATE_CHANGE_SUCCESS);

    --repeat;
  } while (repeat > 0);

  gst_object_unref (bus);
  gst_object_unref (pipeline);

  g_free (path);
}

GST_START_TEST (test_reuse_pull)
{
  process_file ("pcm16sine.flv", FALSE, 3, 129);
  gst_task_cleanup_all ();
}

GST_END_TEST;

GST_START_TEST (test_reuse_push)
{
  process_file ("pcm16sine.flv", TRUE, 3, 129);
  gst_task_cleanup_all ();
}

GST_END_TEST;

static Suite *
flvdemux_suite (void)
{
  Suite *s = suite_create ("flvdemux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_reuse_push);
  tcase_add_test (tc_chain, test_reuse_pull);

  return s;
}

GST_CHECK_MAIN (flvdemux)

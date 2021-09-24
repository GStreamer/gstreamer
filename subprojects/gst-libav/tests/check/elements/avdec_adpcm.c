/* GStreamer unit tests for avdec_adpcm
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
pad_added_cb (GstElement * decodebin, GstPad * pad, GstBin * pipeline)
{
  GstElement *sink;

  GST_INFO_OBJECT (pad, "got pad");

  sink = gst_bin_get_by_name (pipeline, "fakesink");
  fail_unless (gst_element_link (decodebin, sink));
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

static gboolean
decode_file (const gchar * file, gboolean push_mode)
{
  GstStateChangeReturn state_ret;
  GstElement *sink, *src, *dec, *queue, *pipeline;
  GstMessage *msg;
  GstBus *bus;
  gchar *path;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL, "Failed to create pipeline!");

  src = gst_element_factory_make ("filesrc", "filesrc");
  fail_unless (src != NULL, "Failed to create filesrc!");

  if (push_mode) {
    queue = gst_element_factory_make ("queue", "queue");
  } else {
    queue = gst_element_factory_make ("identity", "identity");
  }

  dec = gst_element_factory_make ("decodebin", "decodebin");
  fail_unless (dec != NULL, "Failed to create decodebin!");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Failed to create fakesink!");

  bus = gst_element_get_bus (pipeline);

  /* kids, don't use a sync handler for this at home, really; we do because
   * we just want to abort and nothing else */
  gst_bus_set_sync_handler (bus, error_cb, (gpointer) file, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, queue, dec, sink, NULL);
  gst_element_link_many (src, queue, dec, NULL);

  path = g_build_filename (GST_TEST_FILES_PATH, file, NULL);
  GST_LOG ("reading file '%s'", path);
  g_object_set (src, "location", path, NULL);

  /* can't link uridecodebin and sink yet, do that later */
  g_signal_connect (dec, "pad-added", G_CALLBACK (pad_added_cb), pipeline);

  state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  if (state_ret == GST_STATE_CHANGE_ASYNC) {
    GST_LOG ("waiting for pipeline to reach PAUSED state");
    state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
    fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
  }

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  GST_LOG ("PAUSED, let's decode");
  msg = gst_bus_timed_pop_filtered (bus, 10 * GST_SECOND, GST_MESSAGE_EOS);
  GST_LOG ("Done, got EOS message");
  fail_unless (msg != NULL);
  gst_message_unref (msg);
  gst_object_unref (bus);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);

  g_free (path);

  return TRUE;
}

static void
run_check_for_file (const gchar * filename)
{
  gboolean ret;

  /* first, pull-based */
  ret = decode_file (filename, FALSE);
  fail_unless (ret == TRUE, "Failed to decode '%s' (pull mode)", filename);

  /* second, push-based */
  ret = decode_file (filename, TRUE);
  fail_unless (ret == TRUE, "Failed to decode '%s' (push mode)", filename);
}

GST_START_TEST (test_low_sample_rate_adpcm)
{
#define MIN_VERSION GST_VERSION_MAJOR, GST_VERSION_MINOR, 0
  if (!gst_registry_check_feature_version (gst_registry_get (), "wavparse",
          MIN_VERSION)
      || !gst_registry_check_feature_version (gst_registry_get (), "decodebin",
          MIN_VERSION)) {
    g_printerr ("skipping test_low_sample_rate_adpcm: required element "
        "wavparse or element decodebin not found\n");
    return;
  }

  run_check_for_file ("591809.wav");
}

GST_END_TEST;

static Suite *
avdec_adpcm_suite (void)
{
  Suite *s = suite_create ("avdec_adpcm");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_skip_broken_test (tc_chain, test_low_sample_rate_adpcm);

  return s;
}

GST_CHECK_MAIN (avdec_adpcm)

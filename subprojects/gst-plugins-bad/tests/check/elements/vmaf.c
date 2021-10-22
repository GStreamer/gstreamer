/* GStreamer
 *
 * Copyright (C) 2025 Fluendo S.A. <contact@fluendo.com>
 *   Authors: Diego Nieto <dnieto@fluendo.com>
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
#include <gst/video/video.h>
#include <string.h>

typedef struct
{
  GMainLoop *loop;
  gboolean eos;
  gboolean score_received;
  GstStructure *score_structure;
  gdouble vmaf_score;
} TestData;

static void
on_element_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  TestData *data = (TestData *) user_data;
  const GstStructure *structure;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return;

  structure = gst_message_get_structure (message);
  if (gst_structure_has_name (structure, "VMAF")) {
    data->score_received = TRUE;
    if (data->score_structure)
      gst_structure_free (data->score_structure);
    data->score_structure = gst_structure_copy (structure);

    if (gst_structure_get_double (structure, "score", &data->vmaf_score)) {
      GST_DEBUG ("Received VMAF score: %f", data->vmaf_score);
    }
  }
}

static void
on_message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  TestData *data = (TestData *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_assert_not_reached ();
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (data->loop);
      data->eos = TRUE;
      break;
    case GST_MESSAGE_ELEMENT:
      on_element_message (bus, message, user_data);
      break;
    default:
      break;
  }
}

static void
run_vmaf_test (const gchar * pipeline_string, gboolean check_additional_metrics)
{
  GstElement *pipeline;
  GstBus *bus;
  GMainLoop *loop;
  TestData data = { NULL, };
  GstStateChangeReturn ret;

  GST_DEBUG ("Testing VMAF pipeline");

  pipeline = gst_parse_launch (pipeline_string, NULL);
  fail_unless (pipeline != NULL);
  g_object_set (G_OBJECT (pipeline), "async-handling", TRUE, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_signal_watch (bus);

  data.loop = loop;
  data.eos = FALSE;
  data.score_received = FALSE;
  data.score_structure = NULL;

  g_signal_connect (bus, "message", (GCallback) on_message_cb, &data);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS
      || ret == GST_STATE_CHANGE_ASYNC);

  g_main_loop_run (loop);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  fail_unless (data.eos == TRUE);

  fail_unless (data.score_received, "Score message was not received");
  fail_unless (data.score_structure != NULL, "Score structure is NULL");

  fail_unless (gst_structure_has_name (data.score_structure, "VMAF"));
  fail_unless (gst_structure_has_field_typed (data.score_structure, "timestamp",
          G_TYPE_UINT64));
  fail_unless (gst_structure_has_field_typed (data.score_structure,
          "stream-time", G_TYPE_UINT64));
  fail_unless (gst_structure_has_field_typed (data.score_structure,
          "running-time", G_TYPE_UINT64));
  fail_unless (gst_structure_has_field_typed (data.score_structure, "duration",
          G_TYPE_UINT64));
  fail_unless (gst_structure_has_field_typed (data.score_structure, "score",
          G_TYPE_DOUBLE));
  fail_unless (gst_structure_has_field_typed (data.score_structure, "type",
          G_TYPE_STRING));

  if (data.score_structure)
    gst_structure_free (data.score_structure);

  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);
}

GST_START_TEST (test_vmaf_identical_frames)
{
  gchar *pipeline;

  pipeline =
      g_strdup_printf
      ("videotestsrc num-buffers=5 pattern=solid-color foreground-color=0x00ff0000 ! "
      "video/x-raw,format=I420,width=320,height=180,framerate=25/1 ! v.ref_sink "
      "vmaf name=v frame-message=true threads=0 ! " "fakesink "
      "videotestsrc num-buffers=5 pattern=solid-color foreground-color=0x00ff0000 ! "
      "video/x-raw,format=I420,width=320,height=180,framerate=25/1 ! "
      "v.dist_sink");

  run_vmaf_test (pipeline, FALSE);
  g_free (pipeline);
}

GST_END_TEST;

static Suite *
vmaf_suite (void)
{
  Suite *s = suite_create ("vmaf");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 60);

  tcase_add_test (tc, test_vmaf_identical_frames);

  return s;
}

GST_CHECK_MAIN (vmaf);

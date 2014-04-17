/* GStreamer
 *
 * Copyright (C) 2010 Alessandro Decina <alessandro.decina@collabora.co.uk>
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
#include <gst/base/gstbasesink.h>

GST_START_TEST (basesink_last_sample_enabled)
{
  GstElement *src, *sink, *pipeline;
  GstBus *bus;
  GstMessage *msg;
  GstSample *last_sample;

  pipeline = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  fail_unless (gst_bin_add (GST_BIN (pipeline), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink) == TRUE);
  fail_unless (gst_element_link (src, sink) == TRUE);

  bus = gst_element_get_bus (pipeline);

  /* try with enable-last-sample set to TRUE */
  g_object_set (src, "num-buffers", 1, NULL);
  fail_unless (gst_element_set_state (pipeline, GST_STATE_PLAYING)
      != GST_STATE_CHANGE_FAILURE);
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* last-sample should be != NULL */
  fail_unless (gst_base_sink_is_last_sample_enabled (GST_BASE_SINK (sink))
      == TRUE);
  g_object_get (sink, "last-sample", &last_sample, NULL);
  fail_unless (last_sample != NULL);
  gst_sample_unref (last_sample);

  /* set enable-last-sample to FALSE now, this should set last-sample to NULL */
  g_object_set (sink, "enable-last-sample", FALSE, NULL);
  fail_unless (gst_base_sink_is_last_sample_enabled (GST_BASE_SINK (sink))
      == FALSE);
  g_object_get (sink, "last-sample", &last_sample, NULL);
  fail_unless (last_sample == NULL);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_INFO ("stopped");

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (basesink_last_sample_disabled)
{
  GstElement *src, *sink, *pipeline;
  GstBus *bus;
  GstMessage *msg;
  GstSample *last_sample;

  pipeline = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  fail_unless (gst_bin_add (GST_BIN (pipeline), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink) == TRUE);
  fail_unless (gst_element_link (src, sink) == TRUE);

  bus = gst_element_get_bus (pipeline);

  /* set enable-last-sample to FALSE */
  g_object_set (src, "num-buffers", 1, NULL);
  gst_base_sink_set_last_sample_enabled (GST_BASE_SINK (sink), FALSE);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* last-sample should be NULL */
  g_object_get (sink, "last-sample", &last_sample, NULL);
  fail_unless (last_sample == NULL);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_INFO ("stopped");

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (basesink_test_gap)
{
  GstElement *sink, *pipeline;
  GstPad *pad;
  GstBus *bus;
  GstMessage *msg;
  GstEvent *ev;
  GstSegment segment;

  pipeline = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (sink, "sync", TRUE, NULL);

  pad = gst_element_get_static_pad (sink, "sink");

  fail_unless (gst_bin_add (GST_BIN (pipeline), sink) == TRUE);

  bus = gst_element_get_bus (pipeline);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.stop = 120 * GST_SECOND;
  ev = gst_event_new_segment (&segment);

  fail_unless (gst_pad_send_event (pad, ev));

  ev = gst_event_new_gap (200 * GST_MSECOND, GST_CLOCK_TIME_NONE);
  fail_unless (gst_pad_send_event (pad, ev));

  ev = gst_event_new_eos ();
  fail_unless (gst_pad_send_event (pad, ev));

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  GST_INFO ("stopped");

  gst_object_unref (pad);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static gpointer
send_eos_event (gpointer data)
{
  GstPad *pad = data;
  GstEvent *ev;
  GstSegment segment;

  ev = gst_event_new_stream_start ("test");
  fail_unless (gst_pad_send_event (pad, ev));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  ev = gst_event_new_segment (&segment);
  fail_unless (gst_pad_send_event (pad, ev));

  ev = gst_event_new_eos ();
  gst_pad_send_event (pad, ev);

  return NULL;
}

GST_START_TEST (basesink_test_eos_after_playing)
{
  GstElement *pipeline, *sink;
  GstPad *pad;
  GstBus *bus;
  GstMessage *msg;
  GThread *thread;
  gboolean reached_playing = FALSE;

  sink = gst_element_factory_make ("fakesink", "sink");
  g_object_set (sink, "sync", TRUE, NULL);
  pipeline = gst_pipeline_new (NULL);

  gst_bin_add (GST_BIN (pipeline), sink);

  pad = gst_element_get_static_pad (sink, "sink");

  bus = gst_element_get_bus (pipeline);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  thread = g_thread_new ("push-thread", send_eos_event, pad);

  while ((msg = gst_bus_timed_pop (bus, -1))) {
    if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_STATE_CHANGED
        && GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
      GstState new_state;

      gst_message_parse_state_changed (msg, NULL, &new_state, NULL);
      if (new_state == GST_STATE_PLAYING)
        reached_playing = TRUE;
    } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ASYNC_DONE) {
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
    } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
      fail_unless (reached_playing);
      gst_element_set_state (pipeline, GST_STATE_NULL);
      gst_message_unref (msg);
      break;
    }

    gst_message_unref (msg);
  }

  g_thread_join (thread);

  gst_object_unref (pad);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  GST_INFO ("stopped");
}

GST_END_TEST;

static Suite *
gst_basesrc_suite (void)
{
  Suite *s = suite_create ("GstBaseSink");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, basesink_last_sample_enabled);
  tcase_add_test (tc, basesink_last_sample_disabled);
  tcase_add_test (tc, basesink_test_gap);
  tcase_add_test (tc, basesink_test_eos_after_playing);

  return s;
}

GST_CHECK_MAIN (gst_basesrc);

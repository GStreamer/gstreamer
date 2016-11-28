/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstpipeline.c: Unit test for GstPipeline
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

#include <gst/check/gstcheck.h>
#include <gst/gst.h>

#define WAIT_TIME (300 * GST_MSECOND)

/* an empty pipeline can go to PLAYING in one go */
GST_START_TEST (test_async_state_change_empty)
{
  GstPipeline *pipeline;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_SUCCESS);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_async_state_change_fake_ready)
{
  GstPipeline *pipeline;
  GstElement *src, *sink;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_async_state_change_fake)
{
  GstPipeline *pipeline;
  GstElement *src, *sink;
  GstBus *bus;
  gboolean done = FALSE;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  bus = gst_pipeline_get_bus (pipeline);

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *message;
    GstState old, new, pending;

    message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);
    if (message) {
      gst_message_parse_state_changed (message, &old, &new, &pending);
      GST_DEBUG_OBJECT (message->src, "state change from %d to %d", old, new);
      if (message->src == GST_OBJECT (pipeline) && new == GST_STATE_PLAYING)
        done = TRUE;
      gst_message_unref (message);
    }
  }

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  /* here we don't get the state change messages, because of auto-flush in 
   * the bus */

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_get_bus)
{
  GstPipeline *pipeline;
  GstBus *bus;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  bus = gst_pipeline_get_bus (pipeline);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline after get_bus", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);

  /* bindings don't like the floating flag to be set here */
  fail_if (g_object_is_floating (bus));

  gst_object_unref (pipeline);

  ASSERT_OBJECT_REFCOUNT (bus, "bus after unref pipeline", 1);
  gst_object_unref (bus);
}

GST_END_TEST;

static GMainLoop *loop = NULL;

static gboolean
message_received (GstBus * bus, GstMessage * message, gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);
  GstMessageType type = message->type;

  GST_DEBUG ("message received");
  switch (type) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;

      GST_DEBUG ("state change message received");
      gst_message_parse_state_changed (message, &old, &new, &pending);
      GST_DEBUG ("new state %d", new);
      if (message->src == GST_OBJECT (pipeline) && new == GST_STATE_PLAYING) {
        GST_DEBUG ("quitting main loop");
        g_main_loop_quit (loop);
      }
    }
      break;
    case GST_MESSAGE_ERROR:
    {
      g_print ("error\n");
    }
      break;
    default:
      break;
  }

  return TRUE;
}

GST_START_TEST (test_bus)
{
  GstElement *pipeline;
  GstElement *src, *sink;
  GstBus *bus;
  guint id;
  GstState current;
  GstStateChangeReturn ret;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src != NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline after get_bus", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);

  id = gst_bus_add_watch (bus, message_received, pipeline);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline after add_watch", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus after add_watch", 3);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC);

  loop = g_main_loop_new (NULL, FALSE);
  GST_DEBUG ("going into main loop");
  g_main_loop_run (loop);
  GST_DEBUG ("left main loop");

  /* PLAYING now */

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "pipeline after gone to playing", 1,
      3);

  /* cleanup */
  GST_DEBUG ("cleanup");

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);
  ret = gst_element_get_state (pipeline, &current, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);
  fail_unless (current == GST_STATE_NULL, "state is not NULL but %d", current);

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline at start of cleanup", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus at start of cleanup", 3);

  fail_unless (g_source_remove (id));
  ASSERT_OBJECT_REFCOUNT (bus, "bus after removing source", 2);

  GST_DEBUG ("unreffing pipeline");
  gst_object_unref (pipeline);

  ASSERT_OBJECT_REFCOUNT (bus, "bus after unref pipeline", 1);
  gst_object_unref (bus);
}

GST_END_TEST;

static GMutex probe_lock;
static GCond probe_cond;

static GstPadProbeReturn
sink_pad_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstClockTime *first_timestamp = user_data;
  GstBuffer *buffer;

  fail_unless ((GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_BUFFER));

  buffer = GST_BUFFER (info->data);

  GST_LOG_OBJECT (pad, "buffer with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  fail_if (GST_BUFFER_TIMESTAMP (buffer) == GST_CLOCK_TIME_NONE,
      "testing if buffer timestamps are right, but got CLOCK_TIME_NONE");

  if (*first_timestamp == GST_CLOCK_TIME_NONE) {
    *first_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  }

  g_mutex_lock (&probe_lock);
  g_cond_signal (&probe_cond);
  g_mutex_unlock (&probe_lock);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_base_time)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstPad *sink;
  GstClockTime observed, lower, upper, base, stream;
  GstClock *clock;

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  fail_unless (pipeline && fakesrc && fakesink, "couldn't make elements");

  g_object_set (fakesrc, "is-live", (gboolean) TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  sink = gst_element_get_static_pad (fakesink, "sink");
  gst_pad_add_probe (sink, GST_PAD_PROBE_TYPE_BUFFER, sink_pad_probe,
      &observed, NULL);

  fail_unless (gst_element_set_state (pipeline, GST_STATE_PAUSED)
      == GST_STATE_CHANGE_NO_PREROLL, "expected no-preroll from live pipeline");

  clock = gst_system_clock_obtain ();
  fail_unless (clock && GST_IS_CLOCK (clock), "i want a clock dammit");
  gst_pipeline_use_clock (GST_PIPELINE (pipeline), clock);

  fail_unless (gst_element_get_start_time (pipeline) == 0,
      "stream time doesn't start off at 0");

  /* test the first: that base time is being distributed correctly, timestamps
     are correct relative to the running clock and base time */
  {
    lower = gst_clock_get_time (clock);

    observed = GST_CLOCK_TIME_NONE;

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_SUCCESS, "failed state change");

    g_mutex_lock (&probe_lock);
    while (observed == GST_CLOCK_TIME_NONE)
      g_cond_wait (&probe_cond, &probe_lock);
    g_mutex_unlock (&probe_lock);

    /* now something a little more than lower was distributed as the base time,
     * and the buffer was timestamped between 0 and upper-base
     */

    base = gst_element_get_base_time (pipeline);
    fail_if (base == GST_CLOCK_TIME_NONE);

    /* set stream time */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);

    /* pulling upper here makes sure that the pipeline's new stream time has
       already been computed */
    upper = gst_clock_get_time (clock);

    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_NO_PREROLL, "failed state change");

    fail_if (observed == GST_CLOCK_TIME_NONE, "no timestamp recorded");

    fail_unless (base >= lower, "early base time: %" GST_TIME_FORMAT " < %"
        GST_TIME_FORMAT, GST_TIME_ARGS (base), GST_TIME_ARGS (lower));
    fail_unless (upper >= base, "bogus base time: %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (base), GST_TIME_ARGS (upper));

    stream = gst_element_get_start_time (pipeline);

    fail_unless (stream > 0, "bogus new stream time: %" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (stream), GST_TIME_ARGS (0));
    fail_unless (stream <= upper,
        "bogus new stream time: %" GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (stream), GST_TIME_ARGS (upper));

    fail_unless (observed <= stream, "timestamps outrun stream time: %"
        GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (stream));
    fail_unless (observed != GST_CLOCK_TIME_NONE, "early timestamp: %"
        GST_TIME_FORMAT " < %" GST_TIME_FORMAT, GST_TIME_ARGS (observed),
        GST_TIME_ARGS (lower - base));
    fail_unless (observed <= upper - base,
        "late timestamp: %" GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (upper - base));
  }

  /* test the second: that the base time is redistributed when we go to PLAYING
     again */
  {
    GstClockID clock_id;
    GstClockTime oldbase = base, oldstream = stream;

    /* let some time pass */
    clock_id = gst_clock_new_single_shot_id (clock, upper + WAIT_TIME);
    fail_unless (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_OK,
        "unexpected clock_id_wait return");
    gst_clock_id_unref (clock_id);

    lower = gst_clock_get_time (clock);
    fail_if (lower == GST_CLOCK_TIME_NONE);

    observed = GST_CLOCK_TIME_NONE;

    fail_unless (lower >= upper + WAIT_TIME, "clock did not advance?");

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_SUCCESS, "failed state change");

    g_mutex_lock (&probe_lock);
    while (observed == GST_CLOCK_TIME_NONE)
      g_cond_wait (&probe_cond, &probe_lock);
    g_mutex_unlock (&probe_lock);

    /* now the base time should have advanced by more than WAIT_TIME compared
     * to what it was. The buffer will be timestamped between the last stream
     * time and upper minus base.
     */

    base = gst_element_get_base_time (pipeline);
    fail_if (base == GST_CLOCK_TIME_NONE);

    /* set stream time */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);

    /* new stream time already set */
    upper = gst_clock_get_time (clock);

    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_NO_PREROLL, "failed state change");

    fail_if (observed == GST_CLOCK_TIME_NONE, "no timestamp recorded");

    stream = gst_element_get_start_time (pipeline);

    fail_unless (base >= oldbase + WAIT_TIME, "base time not reset");
    fail_unless (upper >= base + stream, "bogus base time: %"
        GST_TIME_FORMAT " > %" GST_TIME_FORMAT, GST_TIME_ARGS (base),
        GST_TIME_ARGS (upper));

    fail_unless (lower >= base);
    fail_unless (observed >= lower - base, "early timestamp: %"
        GST_TIME_FORMAT " < %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (lower - base));
    fail_unless (observed <= upper - base, "late timestamp: %"
        GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (upper - base));
    fail_unless (stream - oldstream <= upper - lower,
        "insufficient stream time: %" GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (upper));
  }

  /* test the third: that if I set CLOCK_TIME_NONE as the stream time, that the
     base time is not changed */
  {
    GstClockID clock_id;
    GstClockTime oldbase = base, oldobserved = observed;
    GstClockReturn ret;

    /* let some time pass */
    clock_id = gst_clock_new_single_shot_id (clock, upper + WAIT_TIME);
    ret = gst_clock_id_wait (clock_id, NULL);
    fail_unless (ret == GST_CLOCK_OK,
        "unexpected clock_id_wait return %d", ret);
    gst_clock_id_unref (clock_id);

    lower = gst_clock_get_time (clock);

    observed = GST_CLOCK_TIME_NONE;

    fail_unless (lower >= upper + WAIT_TIME, "clock did not advance?");

    /* bling */
    gst_element_set_start_time (pipeline, GST_CLOCK_TIME_NONE);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_SUCCESS, "failed state change");

    g_mutex_lock (&probe_lock);
    while (observed == GST_CLOCK_TIME_NONE)
      g_cond_wait (&probe_cond, &probe_lock);
    g_mutex_unlock (&probe_lock);

    /* now the base time should be the same as it was, and the timestamp should
     * be more than WAIT_TIME past what it was.
     */

    base = gst_element_get_base_time (pipeline);

    /* set stream time */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);

    /* new stream time already set */
    upper = gst_clock_get_time (clock);

    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_NO_PREROLL, "failed state change");

    fail_if (observed == GST_CLOCK_TIME_NONE, "no timestamp recorded");

    fail_unless (gst_element_get_start_time (pipeline)
        == GST_CLOCK_TIME_NONE, "stream time was reset");

    fail_unless (base == oldbase, "base time was reset");

    fail_unless (observed >= lower - base, "early timestamp: %"
        GST_TIME_FORMAT " < %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (lower - base));
    fail_unless (observed <= upper - base, "late timestamp: %"
        GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (upper - base));
    fail_unless (observed - oldobserved >= WAIT_TIME,
        "insufficient tstamp delta: %" GST_TIME_FORMAT " > %" GST_TIME_FORMAT,
        GST_TIME_ARGS (observed), GST_TIME_ARGS (oldobserved));
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (sink);
  gst_object_unref (clock);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static gpointer
pipeline_thread (gpointer data)
{
  GstElement *pipeline, *src, *sink;

  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", 20, NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_object_set (sink, "sync", TRUE, NULL);
  pipeline = gst_pipeline_new (NULL);
  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_link (src, sink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_usleep (G_USEC_PER_SEC / 10);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return NULL;
}

GST_START_TEST (test_concurrent_create)
{
  GThread *threads[30];
  int i;

  for (i = 0; i < G_N_ELEMENTS (threads); ++i) {
    threads[i] = g_thread_try_new ("gst-check", pipeline_thread, NULL, NULL);
  }
  for (i = 0; i < G_N_ELEMENTS (threads); ++i) {
    if (threads[i])
      g_thread_join (threads[i]);
  }
}

GST_END_TEST;

GST_START_TEST (test_pipeline_in_pipeline)
{
  GstElement *pipeline, *bin, *fakesrc, *fakesink;
  GstMessage *msg;

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  bin = gst_element_factory_make ("pipeline", "pipeline-as-bin");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  fail_unless (pipeline && bin && fakesrc && fakesink);

  g_object_set (fakesrc, "num-buffers", 100, NULL);

  gst_bin_add (GST_BIN (pipeline), bin);
  gst_bin_add_many (GST_BIN (bin), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline), -1,
      GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_pipeline_reset_start_time)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstState state;
  GstClock *clock;
  GstClockID id;
  gint64 position;

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  g_object_set (fakesrc, "do-timestamp", TRUE, "format", GST_FORMAT_TIME, NULL);

  fail_unless (pipeline && fakesrc && fakesink);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  fail_unless (gst_element_get_start_time (fakesink) == 0);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (state, GST_STATE_PLAYING);

  /* We just started and never paused, start time must be 0 */
  fail_unless (gst_element_get_start_time (fakesink) == 0);

  clock = gst_pipeline_get_clock (GST_PIPELINE (pipeline));
  fail_unless (clock != NULL);
  id = gst_clock_new_single_shot_id (clock,
      gst_element_get_base_time (pipeline) + 55 * GST_MSECOND);
  gst_clock_id_wait (id, NULL);
  gst_clock_id_unref (id);
  gst_object_unref (clock);

  /* We waited 50ms, so the position should be now >= 50ms */
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  fail_unless (position >= 50 * GST_MSECOND,
      "Pipeline position is not at least 50millisecond (reported %"
      G_GUINT64_FORMAT " nanoseconds)", position);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (state, GST_STATE_PAUSED);

  /* And now after pausing the start time should be bigger than the last
   * position */
  fail_unless (gst_element_get_start_time (fakesink) >= 50 * GST_MSECOND);
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  fail_unless (position >= 50 * GST_MSECOND);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  /* When going back to ready the start time should be reset everywhere */
  fail_unless (gst_element_get_start_time (fakesink) == 0);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (state, GST_STATE_PAUSED);

  /* And the start time should still be set to 0 when we go to paused for the
   * first time. Same goes for the position */
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  fail_unless (position < 50 * GST_MSECOND);

  fail_unless (gst_element_get_start_time (fakesink) == 0);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
gst_pipeline_suite (void)
{
  Suite *s = suite_create ("GstPipeline");
  TCase *tc_chain = tcase_create ("pipeline tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_async_state_change_empty);
  tcase_add_test (tc_chain, test_async_state_change_fake_ready);
  tcase_add_test (tc_chain, test_async_state_change_fake);
  tcase_add_test (tc_chain, test_get_bus);
  tcase_add_test (tc_chain, test_bus);
  tcase_add_test (tc_chain, test_base_time);
  tcase_add_test (tc_chain, test_concurrent_create);
  tcase_add_test (tc_chain, test_pipeline_in_pipeline);
  tcase_add_test (tc_chain, test_pipeline_reset_start_time);

  return s;
}

GST_CHECK_MAIN (gst_pipeline);

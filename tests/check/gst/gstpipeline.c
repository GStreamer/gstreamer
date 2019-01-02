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
#include <gst/check/gsttestclock.h>

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

  /* We have to ensure that all background threads from thread pools are shut
   * down, or otherwise they might not have had a chance yet to drop
   * their last reference to the pipeline and then the assertion below fails
   */
  gst_task_cleanup_all ();

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

GST_START_TEST (test_base_time)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstClockTime base, start;
  gint64 position;
  GstClock *clock;

  clock = gst_test_clock_new ();
  gst_test_clock_set_time (GST_TEST_CLOCK (clock), 100 * GST_SECOND);

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gst_pipeline_use_clock (GST_PIPELINE (pipeline), clock);

  fail_unless (pipeline && fakesrc && fakesink, "couldn't make elements");

  g_object_set (fakesrc, "is-live", (gboolean) TRUE, "do-timestamp", TRUE,
      "format", GST_FORMAT_TIME, "sizetype", 2, "sizemax", 4096, "datarate",
      4096 * 100, NULL);
  g_object_set (fakesink, "sync", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  fail_unless (gst_element_set_state (pipeline, GST_STATE_PAUSED)
      == GST_STATE_CHANGE_NO_PREROLL, "expected no-preroll from live pipeline");

  fail_unless_equals_uint64 (gst_element_get_start_time (pipeline), 0);

  /* test the first: that base time is being distributed correctly, timestamps
     are correct relative to the running clock and base time */
  {
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_SUCCESS, "failed state change");

    /* Wait for time for 1s to pass */
    for (;;) {
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
      if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
          101 * GST_SECOND)
        break;
      gst_test_clock_crank (GST_TEST_CLOCK (clock));
    }

    /* Now the base time should be exactly the clock time when we start and the
     * position should be at 1s because we waited 1s */
    base = gst_element_get_base_time (pipeline);
    fail_unless_equals_uint64 (base, 100 * GST_SECOND);

    fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
            &position));
    fail_unless_equals_uint64 (position, 1 * GST_SECOND);

    /* wait for another 1s of buffers to pass */
    for (;;) {
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
      if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
          102 * GST_SECOND)
        break;
      gst_test_clock_crank (GST_TEST_CLOCK (clock));
    }

    /* set start time by pausing */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);

    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_NO_PREROLL, "failed state change");

    start = gst_element_get_start_time (pipeline);
    /* start time should be exactly 2s as that much time advanced, it's
     * the current running time */
    fail_unless_equals_uint64 (start, 2 * GST_SECOND);

    fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
            &position));
    fail_unless_equals_uint64 (position, 2 * GST_SECOND);
  }

  /* test the second: that the base time is redistributed when we go to PLAYING
     again */
  {
    /* Set time to 99s in the future */
    gst_test_clock_set_time (GST_TEST_CLOCK (clock), 200 * GST_SECOND);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_SUCCESS, "failed state change");

    /* wait for 1s of buffers to pass */
    for (;;) {
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
      if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
          201 * GST_SECOND)
        break;
      gst_test_clock_crank (GST_TEST_CLOCK (clock));
    }

    /* now the base time should have advanced by 98s compared to what it was
     * before (we played 2s between previous and current play and 100s passed) */
    base = gst_element_get_base_time (pipeline);
    fail_unless_equals_uint64 (base, 198 * GST_SECOND);

    /* wait for 1s of buffers to pass */
    for (;;) {
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
      if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
          202 * GST_SECOND)
        break;
      gst_test_clock_crank (GST_TEST_CLOCK (clock));
    }

    /* set start time by pausing */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_NO_PREROLL, "failed state change");

    /* start time should now be 4s as that's the amount of time that has
     * passed since we started until we paused above */
    start = gst_element_get_start_time (pipeline);
    fail_unless_equals_uint64 (start, 4 * GST_SECOND);

    fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
            &position));
    fail_unless_equals_uint64 (position, 4 * GST_SECOND);
  }

  /* test the third: that if I set CLOCK_TIME_NONE as the stream time, that the
     base time is not changed */
  {
    GstClockTime oldbase;

    /* bling */
    oldbase = gst_element_get_base_time (pipeline);
    gst_element_set_start_time (pipeline, GST_CLOCK_TIME_NONE);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_SUCCESS, "failed state change");

    /* wait for 1s of buffers to pass */
    for (;;) {
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
      if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
          203 * GST_SECOND)
        break;
      gst_test_clock_crank (GST_TEST_CLOCK (clock));
    }

    /* now the base time should be the same as it was */

    base = gst_element_get_base_time (pipeline);

    /* wait for 1s of buffers to pass */
    for (;;) {
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
      if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
          204 * GST_SECOND)
        break;
      gst_test_clock_crank (GST_TEST_CLOCK (clock));
    }

    /* set start time by pausing */
    gst_element_set_state (pipeline, GST_STATE_PAUSED);

    fail_unless (gst_element_get_state (pipeline, NULL, NULL,
            GST_CLOCK_TIME_NONE)
        == GST_STATE_CHANGE_NO_PREROLL, "failed state change");

    fail_unless (gst_element_get_start_time (pipeline)
        == GST_CLOCK_TIME_NONE, "stream time was reset");

    fail_unless (base == oldbase, "base time was reset");
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  gst_object_unref (clock);
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
  gint64 position;

  clock = gst_test_clock_new ();
  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gst_pipeline_use_clock (GST_PIPELINE (pipeline), clock);

  /* no more than 100 buffers per second */
  g_object_set (fakesrc, "do-timestamp", TRUE, "format", GST_FORMAT_TIME,
      "sizetype", 2, "sizemax", 4096, "datarate", 4096 * 100, NULL);

  g_object_set (fakesink, "sync", TRUE, NULL);

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

  /* Wait for time to reach 50 msecs */
  for (;;) {
    gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
    if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
        50 * GST_MSECOND)
      break;
    gst_test_clock_crank (GST_TEST_CLOCK (clock));
  }

  /* We waited 50ms, so the position should be now == 50ms */
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  fail_unless_equals_uint64 (position, 50 * GST_MSECOND);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_ASYNC);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (state, GST_STATE_PAUSED);

  /* And now after pausing the start time should be bigger than the last
   * position */
  fail_unless_equals_uint64 (gst_element_get_start_time (fakesink),
      50 * GST_MSECOND);
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  fail_unless_equals_uint64 (position, 50 * GST_MSECOND);

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
  fail_unless_equals_uint64 (position, 0 * GST_MSECOND);

  fail_unless (gst_element_get_start_time (fakesink) == 0);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  gst_object_unref (clock);
}

GST_END_TEST;

GST_START_TEST (test_pipeline_processing_deadline)
{
  GstElement *pipeline, *fakesrc, *queue, *fakesink;
  GstState state;
  GstClock *clock;
  gint64 position;
  GstQuery *q;
  gboolean live;
  GstClockTime min, max;
  GstBus *bus;
  GstMessage *msg;

  clock = gst_test_clock_new ();
  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  queue = gst_element_factory_make ("queue", "queue");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gst_pipeline_use_clock (GST_PIPELINE (pipeline), clock);

  /* no more than 100 buffers per second */
  g_object_set (fakesrc, "do-timestamp", TRUE, "format", GST_FORMAT_TIME,
      "sizetype", 2, "sizemax", 4096, "datarate", 4096 * 100, "is-live", TRUE,
      NULL);

  g_object_set (fakesink, "sync", TRUE, NULL);

  fail_unless (pipeline && fakesrc && queue && fakesink);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, queue, fakesink, NULL);
  gst_element_link_many (fakesrc, queue, fakesink, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (state, GST_STATE_PLAYING);

  q = gst_query_new_latency ();
  fail_unless (gst_element_query (pipeline, q));
  gst_query_parse_latency (q, &live, &min, &max);
  fail_unless (live == TRUE);
  fail_unless (min == 20 * GST_MSECOND);
  fail_unless (max >= min);
  gst_query_unref (q);

  /* Wait for time to reach 50 msecs */
  for (;;) {
    gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (clock), NULL);
    if (gst_test_clock_get_next_entry_time (GST_TEST_CLOCK (clock)) >
        50 * GST_MSECOND)
      break;
    gst_test_clock_crank (GST_TEST_CLOCK (clock));
  }

  /* We waited 50ms, but the position should be 50ms - 20ms latency == 30ms */
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  fail_unless_equals_uint64 (position, 30 * GST_MSECOND);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_NO_PREROLL);
  fail_unless_equals_int (state, GST_STATE_PAUSED);

  /* And now after pausing the start time should be exactly 50ms */
  fail_unless_equals_uint64 (gst_element_get_start_time (fakesink),
      50 * GST_MSECOND);
  fail_unless (gst_element_query_position (fakesink, GST_FORMAT_TIME,
          &position));
  /* but position should still be exactly 50ms - 20ms latency == 30ms */
  fail_unless_equals_uint64 (position, 30 * GST_MSECOND);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_NO_PREROLL);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_NO_PREROLL);
  fail_unless_equals_int (state, GST_STATE_PAUSED);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_WARNING);
  fail_unless (msg == NULL);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  gst_object_unref (clock);
}

GST_END_TEST;


GST_START_TEST (test_pipeline_processing_deadline_no_queue)
{
  GstElement *pipeline, *fakesrc, *fakesink;
  GstState state;
  GstBus *bus;
  GstMessage *msg;
  GError *gerror = NULL;

  pipeline = gst_element_factory_make ("pipeline", "pipeline");
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  /* no more than 100 buffers per second */
  g_object_set (fakesrc, "do-timestamp", TRUE, "format", GST_FORMAT_TIME,
      "sizetype", 2, "sizemax", 4096, "datarate", 4096 * 100, "is-live", TRUE,
      NULL);

  g_object_set (fakesink, "sync", TRUE, "processing-deadline", 60 * GST_MSECOND,
      NULL);

  fail_unless (pipeline && fakesrc && fakesink);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);
  fail_unless_equals_int (gst_element_get_state (pipeline, &state, NULL, -1),
      GST_STATE_CHANGE_SUCCESS);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_WARNING);
  fail_unless (msg != NULL);
  gst_message_parse_warning (msg, &gerror, NULL);
  fail_unless (g_error_matches (gerror, GST_CORE_ERROR, GST_CORE_ERROR_CLOCK));
  gst_message_unref (msg);
  gst_object_unref (bus);
  g_clear_error (&gerror);

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
  tcase_add_test (tc_chain, test_pipeline_processing_deadline);
  tcase_add_test (tc_chain, test_pipeline_processing_deadline_no_queue);

  return s;
}

GST_CHECK_MAIN (gst_pipeline);

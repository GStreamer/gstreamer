/* GStreamer unit tests for queue2
 *
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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

static GstElement *
setup_queue2 (GstElement * pipe, GstElement * input, GstElement * output)
{
  GstElement *queue2;
  GstPad *sinkpad, *srcpad;

  queue2 = gst_element_factory_make ("queue2", NULL);
  fail_unless (queue2 != NULL, "failed to create 'queue2' element");

  gst_bin_add (GST_BIN (pipe), queue2);
  gst_bin_add (GST_BIN (pipe), input);
  gst_bin_add (GST_BIN (pipe), output);

  sinkpad = gst_element_get_static_pad (queue2, "sink");
  fail_unless (sinkpad != NULL, "failed to get queue2 sink pad");

  srcpad = gst_element_get_static_pad (input, "src");
  fail_unless (srcpad != NULL, "failed to find src pad for input element");

  fail_unless_equals_int (GST_PAD_LINK_OK, gst_pad_link (srcpad, sinkpad));
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_static_pad (queue2, "src");
  fail_unless (srcpad != NULL);

  sinkpad = gst_element_get_static_pad (output, "sink");
  fail_unless (sinkpad != NULL, "failed to find sink pad of output element");

  fail_unless_equals_int (GST_PAD_LINK_OK, gst_pad_link (srcpad, sinkpad));

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  return queue2;
}

GST_START_TEST (test_simple_pipeline)
{
  GstElement *pipe, *input, *output;
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  input = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (input != NULL, "failed to create 'fakesrc' element");
  g_object_set (input, "num-buffers", 256, "sizetype", 3, NULL);

  output = gst_element_factory_make ("fakesink", NULL);
  fail_unless (output != NULL, "failed to create 'fakesink' element");

  setup_queue2 (pipe, input, output);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("Got EOS, cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_simple_pipeline_ringbuffer)
{
  GstElement *pipe, *queue2, *input, *output;
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  input = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (input != NULL, "failed to create 'fakesrc' element");
  g_object_set (input, "num-buffers", 256, "sizetype", 3, NULL);

  output = gst_element_factory_make ("fakesink", NULL);
  fail_unless (output != NULL, "failed to create 'fakesink' element");

  queue2 = setup_queue2 (pipe, input, output);
  g_object_set (queue2, "ring-buffer-max-size", (guint64) 1024 * 50, NULL);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("Got EOS, cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static void
do_test_simple_shutdown_while_running (guint64 ring_buffer_max_size)
{
  GstElement *pipe, *q2;
  GstElement *input;
  GstElement *output;
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  input = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (input != NULL, "failed to create 'fakesrc' element");
  g_object_set (input, "format", GST_FORMAT_TIME, "sizetype", 2,
      "sizemax", 10, NULL);

  output = gst_element_factory_make ("fakesink", NULL);
  fail_unless (output != NULL, "failed to create 'fakesink' element");

  q2 = setup_queue2 (pipe, input, output);

  if (ring_buffer_max_size > 0) {
    g_object_set (q2, "ring-buffer-max-size", ring_buffer_max_size,
        "temp-template", NULL, NULL);
  }

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* wait until pipeline is up and running */
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_ERROR | GST_MESSAGE_ASYNC_DONE, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR, "Got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("pipeline is running now");
  gst_element_set_state (pipe, GST_STATE_PLAYING);
  g_usleep (G_USEC_PER_SEC / 20);

  /* now shut down only the sink, so the queue gets a wrong-state flow return */
  gst_element_set_state (output, GST_STATE_NULL);
  GST_LOG ("Cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_START_TEST (test_simple_shutdown_while_running)
{
  int i;

  /* run a couple of iterations, gives higher chance of different code paths
   * being executed at time the flush is detected (esp. useful to make sure
   * things are cleaned up properly when running under valgrind) */
  for (i = 0; i < 10; ++i) {
    do_test_simple_shutdown_while_running (0);
  }
}

GST_END_TEST;

GST_START_TEST (test_simple_shutdown_while_running_ringbuffer)
{
  int i;

  /* run a couple of iterations, gives higher chance of different code paths
   * being executed at time the flush is detected (esp. useful to make sure
   * things are cleaned up properly when running under valgrind) */
  for (i = 0; i < 10; ++i) {
    do_test_simple_shutdown_while_running (1024 * 1024);
  }
}

GST_END_TEST;

GST_START_TEST (test_simple_create_destroy)
{
  GstElement *queue2;

  queue2 = gst_element_factory_make ("queue2", NULL);
  gst_object_unref (queue2);
}

GST_END_TEST;

static gboolean
queue2_dummypad_query (GstPad * sinkpad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = (filter ? gst_caps_ref (filter) : gst_caps_new_any ());
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      break;
    }
    default:
      res = gst_pad_query_default (sinkpad, parent, query);
      break;
  }
  return res;
}

static gpointer
pad_push_datablock_thread (gpointer data)
{
  GstPad *pad = data;
  GstBuffer *buf;

  buf = gst_buffer_new_allocate (NULL, 80 * 1000, NULL);
  gst_pad_push (pad, buf);

  return NULL;
}

static GstPadProbeReturn
block_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

#define CHECK_FOR_BUFFERING_MSG(PIPELINE, EXPECTED_PERC) \
  G_STMT_START { \
    gint buf_perc; \
    GstMessage *msg; \
    GST_LOG ("waiting for %d%% buffering message", (EXPECTED_PERC)); \
    msg = gst_bus_poll (GST_ELEMENT_BUS (PIPELINE), \
        GST_MESSAGE_BUFFERING | GST_MESSAGE_ERROR, -1); \
    fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR, \
        "Expected BUFFERING message, got ERROR message"); \
    gst_message_parse_buffering (msg, &buf_perc); \
    gst_message_unref (msg); \
    fail_unless (buf_perc == (EXPECTED_PERC), \
        "Got incorrect percentage: %d%% expected: %d%%", buf_perc, \
        (EXPECTED_PERC)); \
  } G_STMT_END

GST_START_TEST (test_watermark_and_fill_level)
{
  /* This test checks the behavior of the fill level and
   * the low/high watermarks. It also checks if the
   * low/high-percent and low/high-watermark properties
   * are coupled together properly. */

  GstElement *pipe;
  GstElement *queue2, *fakesink;
  GstPad *inputpad;
  GstPad *queue2_sinkpad;
  GstPad *sinkpad;
  GstSegment segment;
  GThread *thread;
  gint low_perc, high_perc;


  /* Setup test pipeline with one multiqueue and one fakesink */

  pipe = gst_pipeline_new ("pipeline");
  queue2 = gst_element_factory_make ("queue2", NULL);
  fail_unless (queue2 != NULL);
  gst_bin_add (GST_BIN (pipe), queue2);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  gst_bin_add (GST_BIN (pipe), fakesink);

  /* Block fakesink sinkpad flow to ensure the queue isn't emptied
   * by the prerolling sink */
  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BLOCK, block_probe, NULL,
      NULL);
  gst_object_unref (sinkpad);

  g_object_set (queue2,
      "use-buffering", (gboolean) TRUE,
      "max-size-bytes", (guint) 1000 * 1000,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "low-watermark", (gdouble) 0.01, "high-watermark", (gdouble) 0.10, NULL);

  g_object_get (queue2, "low-percent", &low_perc,
      "high-percent", &high_perc, NULL);

  /* Check that low/high-watermark and low/high-percent are
   * coupled properly. (low/high-percent are deprecated and
   * exist for backwards compatibility.) */
  fail_unless_equals_int (low_perc, 1);
  fail_unless_equals_int (high_perc, 10);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  gst_pad_set_query_function (inputpad, queue2_dummypad_query);

  queue2_sinkpad = gst_element_get_static_pad (queue2, "sink");
  fail_unless (queue2_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, queue2_sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_object_unref (queue2_sinkpad);

  fail_unless (gst_element_link (queue2, fakesink));

  /* Start pipeline in paused state to ensure the sink remains
   * in preroll mode and blocks */
  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* When the use-buffering property is set to TRUE, a buffering
   * message is posted. Since the queue is empty at that point,
   * the buffering message contains a value of 0%. */
  CHECK_FOR_BUFFERING_MSG (pipe, 0);

  /* Feed data. queue will be filled to 80% (because it pushes 80000 bytes),
   * which is below the high-threshold, provoking a buffering message. */
  thread = g_thread_new ("push1", pad_push_datablock_thread, inputpad);
  g_thread_join (thread);

  /* Check for the buffering message; it should indicate 80% fill level
   * (Note that the percentage from the message is normalized) */
  CHECK_FOR_BUFFERING_MSG (pipe, 80);

  /* Increase the buffer size and lower the watermarks to test
   * if <1% watermarks are supported. */
  g_object_set (queue2,
      "max-size-bytes", (guint) 20 * 1000 * 1000,
      "low-watermark", (gdouble) 0.0001, "high-watermark", (gdouble) 0.005,
      NULL);

  /* First buffering message is posted after the max-size-bytes limit
   * is set to 20000000 bytes & the low-watermark is set. Since the
   * queue contains 80000 bytes, and the high watermark still is
   * 0.1 at this point, and the buffer level 80000 / 20000000 = 0.004 is
   * normalized by 0.1: 0.004 / 0.1 => buffering percentage 4%. */
  CHECK_FOR_BUFFERING_MSG (pipe, 4);
  /* Second buffering message is posted after the high-watermark limit
   * is set to 0.005. This time, the buffer level is normalized this way:
   * 0.004 / 0.005 => buffering percentage 80%. */
  CHECK_FOR_BUFFERING_MSG (pipe, 80);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
  gst_object_unref (inputpad);
}

GST_END_TEST;

static gpointer
push_buffer (GstPad * sinkpad)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new_and_alloc (1 * 1024);

  gst_pad_chain (sinkpad, buffer);

  return NULL;
}

GST_START_TEST (test_filled_read)
{
  GstElement *queue2;
  GstBuffer *buffer;
  GstPad *sinkpad, *srcpad;
  GThread *thread;
  GstSegment segment;

  queue2 = gst_element_factory_make ("queue2", NULL);
  sinkpad = gst_element_get_static_pad (queue2, "sink");
  srcpad = gst_element_get_static_pad (queue2, "src");

  g_object_set (queue2, "ring-buffer-max-size", (guint64) 5 * 1024,
      "use-buffering", FALSE,
      "max-size-buffers", (guint) 0, "max-size-time", (guint64) 0,
      "max-size-bytes", (guint) 4 * 1024, NULL);

  gst_pad_activate_mode (srcpad, GST_PAD_MODE_PULL, TRUE);
  gst_element_set_state (queue2, GST_STATE_PLAYING);

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* fill up the buffer */
  buffer = gst_buffer_new_and_alloc (4 * 1024);
  fail_unless (gst_pad_chain (sinkpad, buffer) == GST_FLOW_OK);

  thread =
      g_thread_try_new ("gst-check", (GThreadFunc) push_buffer, sinkpad, NULL);
  fail_unless (thread != NULL);

  buffer = NULL;
  fail_unless (gst_pad_get_range (srcpad, 1024, 4 * 1024,
          &buffer) == GST_FLOW_OK);

  fail_unless (gst_buffer_get_size (buffer) == 4 * 1024);
  gst_buffer_unref (buffer);

  gst_element_set_state (queue2, GST_STATE_NULL);

  g_thread_join (thread);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (queue2);
}

GST_END_TEST;


static GstPadProbeReturn
block_callback (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_percent_overflow)
{
  GstElement *queue2;
  GstBuffer *buffer;
  GstPad *sinkpad, *srcpad;
  gulong block_probe;
  guint64 i;
  guint64 current_level_time;
  GstSegment segment;

  queue2 = gst_element_factory_make ("queue2", NULL);
  sinkpad = gst_element_get_static_pad (queue2, "sink");
  srcpad = gst_element_get_static_pad (queue2, "src");

  block_probe = gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      block_callback, NULL, NULL);

  g_object_set (queue2, "use-buffering", TRUE,
      "use-rate-estimate", FALSE,
      "max-size-buffers", 0,
      "max-size-time", 2 * GST_SECOND, "max-size-bytes", 0, NULL);

  gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE);
  gst_element_set_state (queue2, GST_STATE_PAUSED);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.time = 0;
  segment.position = 0;
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test"));
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));

  /* push 2 seconds of data with valid but excessively high timestamps */
  for (i = 0; i < 20; i++) {
    buffer = gst_buffer_new_and_alloc (1024);
    GST_BUFFER_PTS (buffer) =
        G_GUINT64_CONSTANT (18446744071709551616) + i * (GST_SECOND / 10);
    GST_BUFFER_DTS (buffer) =
        G_GUINT64_CONSTANT (18446744071709551616) + i * (GST_SECOND / 10);
    GST_BUFFER_DURATION (buffer) = (GST_SECOND / 10);
    fail_unless (gst_pad_chain (sinkpad, buffer) == GST_FLOW_OK);
  }

  g_object_get (queue2, "current-level-time", &current_level_time, NULL);

  gst_pad_remove_probe (srcpad, block_probe);

  gst_element_set_state (queue2, GST_STATE_NULL);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (queue2);
}

GST_END_TEST;

GST_START_TEST (test_small_ring_buffer)
{
  GstElement *pipeline;
  GstElement *queue2;
  const gchar *desc;
  GstBus *bus;
  GstMessage *msg;

  /* buffer too small to seek used to crash, test for regression */
  desc = "fakesrc sizetype=2 sizemax=4096 num-buffers=100 datarate=1000 ! "
      "queue2 ring-buffer-max-size=1000 name=q2 ! fakesink sync=true";

  pipeline = gst_parse_launch (desc, NULL);
  fail_if (pipeline == NULL);

  queue2 = gst_bin_get_by_name (GST_BIN (pipeline), "q2");
  fail_if (queue2 == NULL);

  /* bring the pipeline to PLAYING, then start switching */
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* Wait for the pipeline to hit playing */
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* now wait for completion or error */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (queue2);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
queue2_suite (void)
{
  Suite *s = suite_create ("queue2");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_create_destroy);
  tcase_add_test (tc_chain, test_simple_pipeline);
  tcase_add_test (tc_chain, test_simple_pipeline_ringbuffer);
  tcase_add_test (tc_chain, test_simple_shutdown_while_running);
  tcase_add_test (tc_chain, test_simple_shutdown_while_running_ringbuffer);
  tcase_add_test (tc_chain, test_watermark_and_fill_level);
  tcase_add_test (tc_chain, test_filled_read);
  tcase_add_test (tc_chain, test_percent_overflow);
  tcase_add_test (tc_chain, test_small_ring_buffer);

  return s;
}

GST_CHECK_MAIN (queue2)

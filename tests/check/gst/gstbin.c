/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstbin.c: Unit test for GstBin
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
#include <gst/base/gstbasesrc.h>

static void
pop_async_done (GstBus * bus)
{
  GstMessage *message;

  GST_DEBUG ("popping async-done message");
  message = gst_bus_poll (bus, GST_MESSAGE_ASYNC_DONE, -1);

  fail_unless (message && GST_MESSAGE_TYPE (message)
      == GST_MESSAGE_ASYNC_DONE, "did not get GST_MESSAGE_ASYNC_DONE");

  gst_message_unref (message);
  GST_DEBUG ("popped message");
}

static void
pop_messages (GstBus * bus, int count)
{
  GstMessage *message;

  int i;

  GST_DEBUG ("popping %d messages", count);
  for (i = 0; i < count; ++i) {
    message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);

    fail_unless (message && GST_MESSAGE_TYPE (message)
        == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

    gst_message_unref (message);
  }
  GST_DEBUG ("popped %d messages", count);
}

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gpointer
push_one_eos (GstPad * pad)
{
  GST_DEBUG_OBJECT (pad, "Pushing EOS event");
  gst_pad_push_event (pad, gst_event_new_eos ());

  return NULL;
}

static gpointer
push_one_stream_start (GstPad * pad)
{
  GST_DEBUG_OBJECT (pad, "Pushing STREAM_START event");
  gst_pad_push_event (pad, gst_event_new_stream_start ("test"));

  return NULL;
}

GST_START_TEST (test_interface)
{
  GstBin *bin, *bin2;
  GstElement *filesrc;
  GstIterator *it;
  GValue item = { 0, };

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");

  filesrc = gst_element_factory_make ("filesrc", NULL);
  fail_unless (filesrc != NULL, "Could not create filesrc");
  fail_unless (GST_IS_URI_HANDLER (filesrc), "Filesrc not a URI handler");
  gst_bin_add (bin, filesrc);

  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  gst_object_unref (filesrc);

  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (it != NULL);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (g_value_get_object (&item) == (gpointer) filesrc);
  g_value_reset (&item);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL), NULL);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (it != NULL);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (g_value_get_object (&item) == (gpointer) filesrc);
  g_value_reset (&item);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  bin2 = bin;
  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL);
  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      GST_ELEMENT (bin2), gst_element_factory_make ("identity", NULL), NULL);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (g_value_get_object (&item) == (gpointer) filesrc);
  g_value_reset (&item);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add (bin, gst_element_factory_make ("filesrc", NULL));
  gst_bin_add (bin2, gst_element_factory_make ("filesrc", NULL));
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_value_reset (&item);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_value_reset (&item);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  g_value_reset (&item);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  g_value_unset (&item);
  gst_iterator_free (it);

  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_eos)
{
  GstBus *bus;
  GstElement *pipeline, *sink1, *sink2;
  GstMessage *message;
  GstPad *pad1, *pad2;
  GThread *thread1, *thread2;

  pipeline = gst_pipeline_new ("test_eos");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  sink1 = gst_element_factory_make ("fakesink", "sink1");
  sink2 = gst_element_factory_make ("fakesink", "sink2");

  gst_bin_add_many (GST_BIN (pipeline), sink1, sink2, NULL);

  pad1 = gst_check_setup_src_pad_by_name (sink1, &srctemplate, "sink");
  pad2 = gst_check_setup_src_pad_by_name (sink2, &srctemplate, "sink");

  gst_pad_set_active (pad1, TRUE);
  gst_pad_set_active (pad2, TRUE);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  /* Send one EOS to sink1 */
  thread1 = g_thread_new ("thread1", (GThreadFunc) push_one_eos, pad1);

  /* Make sure the EOS message is not sent */
  message =
      gst_bus_poll (bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS, 2 * GST_SECOND);
  fail_if (message != NULL);

  /* Send one EOS to sink2 */
  thread2 = g_thread_new ("thread2", (GThreadFunc) push_one_eos, pad2);

  /* Make sure the EOS message is sent then */
  message = gst_bus_poll (bus, GST_MESSAGE_ERROR | GST_MESSAGE_EOS, -1);
  fail_if (message == NULL);
  fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS);
  gst_message_unref (message);

  /* Cleanup */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_pad_set_active (pad1, FALSE);
  gst_pad_set_active (pad2, FALSE);
  gst_check_teardown_src_pad (sink1);
  gst_check_teardown_src_pad (sink2);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_stream_start)
{
  GstBus *bus;
  GstElement *pipeline, *sink1, *sink2;
  GstMessage *message;
  GstPad *pad1, *pad2;
  GThread *thread1, *thread2;

  pipeline = gst_pipeline_new ("test_stream_start");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  sink1 = gst_element_factory_make ("fakesink", "sink1");
  sink2 = gst_element_factory_make ("fakesink", "sink2");

  gst_bin_add_many (GST_BIN (pipeline), sink1, sink2, NULL);

  pad1 = gst_check_setup_src_pad_by_name (sink1, &srctemplate, "sink");
  pad2 = gst_check_setup_src_pad_by_name (sink2, &srctemplate, "sink");

  gst_pad_set_active (pad1, TRUE);
  gst_pad_set_active (pad2, TRUE);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  /* Send one STREAM_START to sink1 */
  thread1 = g_thread_new ("thread1", (GThreadFunc) push_one_stream_start, pad1);

  /* Make sure the STREAM_START message is not sent */
  message =
      gst_bus_poll (bus, GST_MESSAGE_ERROR | GST_MESSAGE_STREAM_START,
      2 * GST_SECOND);
  fail_if (message != NULL);

  /* Send one STREAM_START to sink2 */
  thread2 = g_thread_new ("thread2", (GThreadFunc) push_one_stream_start, pad2);

  /* Make sure the STREAM_START message is sent then */
  message =
      gst_bus_poll (bus, GST_MESSAGE_ERROR | GST_MESSAGE_STREAM_START, -1);
  fail_if (message == NULL);
  fail_unless (GST_MESSAGE_TYPE (message) == GST_MESSAGE_STREAM_START);
  gst_message_unref (message);

  /* Cleanup */
  g_thread_join (thread1);
  g_thread_join (thread2);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_pad_set_active (pad1, FALSE);
  gst_pad_set_active (pad2, FALSE);
  gst_check_teardown_src_pad (sink1);
  gst_check_teardown_src_pad (sink2);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_message_state_changed)
{
  GstBin *bin;
  GstBus *bus;
  GstMessage *message;
  GstStateChangeReturn ret;

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  bus = g_object_new (gst_bus_get_type (), NULL);
  gst_element_set_bus (GST_ELEMENT_CAST (bin), bus);

  /* change state, spawning a message, causing an incref on the bin */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  ASSERT_OBJECT_REFCOUNT (bin, "bin", 2);

  /* get and unref the message, causing a decref on the bin */
  message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);

  fail_unless (message && GST_MESSAGE_TYPE (message)
      == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

  gst_message_unref (message);

  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  gst_bus_set_flushing (bus, TRUE);

  /* clean up */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_message_state_changed_child)
{
  GstBin *bin;
  GstElement *src;
  GstBus *bus;
  GstMessage *message;
  GstStateChangeReturn ret;

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  bus = g_object_new (gst_bus_get_type (), NULL);
  gst_element_set_bus (GST_ELEMENT_CAST (bin), bus);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  gst_bin_add (bin, src);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  /* change state, spawning two messages:
   * - first for fakesrc, forwarded to bin's bus, causing incref on fakesrc
   * - second for bin, causing an incref on the bin */
  GST_DEBUG ("setting bin to READY");
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  ASSERT_OBJECT_REFCOUNT (src, "src", 2);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 2);

  /* get and unref the message, causing a decref on the src */
  message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);
  fail_unless (message && GST_MESSAGE_TYPE (message)
      == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

  fail_unless (message->src == GST_OBJECT (src));
  gst_message_unref (message);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 2);

  /* get and unref message 2, causing a decref on the bin */
  message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);
  fail_unless (message && GST_MESSAGE_TYPE (message)
      == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

  fail_unless (message->src == GST_OBJECT (bin));
  gst_message_unref (message);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (bin, "bin", 1);

  gst_bus_set_flushing (bus, TRUE);

  /* clean up */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_message_state_changed_children)
{
  GstPipeline *pipeline;
  GstElement *src, *sink;
  GstBus *bus;
  GstStateChangeReturn ret;
  GstState current, pending;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  /* need to silence the element as the deep_notify refcounts the
   * parents while running */
  g_object_set (G_OBJECT (src), "silent", TRUE, NULL);
  gst_bin_add (GST_BIN (pipeline), src);

  sink = gst_element_factory_make ("fakesink", NULL);
  /* need to silence the element as the deep_notify refcounts the
   * parents while running */
  g_object_set (G_OBJECT (sink), "silent", TRUE, NULL);
  fail_if (sink == NULL, "Could not create fakesink");
  gst_bin_add (GST_BIN (pipeline), sink);

  fail_unless (gst_element_link (src, sink), "could not link src and sink");

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);

  bus = gst_pipeline_get_bus (pipeline);

  /* change state to READY, spawning three messages */
  GST_DEBUG ("setting pipeline to READY");
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  /* each object is referenced by a message */
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 2);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 2);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 2);

  pop_messages (bus, 3);
  fail_if (gst_bus_have_pending (bus), "unexpected pending messages");

  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* change state to PAUSED, spawning four messages */
  /* STATE_CHANGED (NULL => READY)
   * STREAM_START
   * ASYNC_DONE
   * STATE_CHANGED (READY => PAUSED)
   */
  GST_DEBUG ("setting pipeline to PAUSED");
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC);
  ret =
      gst_element_get_state (GST_ELEMENT (pipeline), &current, &pending,
      GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);
  fail_unless (current == GST_STATE_PAUSED);
  fail_unless (pending == GST_STATE_VOID_PENDING);

  /* wait for async thread to settle down */
  GST_DEBUG ("waiting for refcount");
  while (GST_OBJECT_REFCOUNT_VALUE (pipeline) > 4)
    THREAD_SWITCH ();
  GST_DEBUG ("refcount <= 4 now");

  /* each object is referenced by a message;
   * base_src is blocked in the push and has an extra refcount.
   * base_sink_chain has taken a refcount on the sink, and is blocked on
   * preroll
   * The stream-status messages holds 2 more refs to the element */
  ASSERT_OBJECT_REFCOUNT (src, "src", 4);
  /* refcount can be 4 if the bin is still processing the async_done message of
   * the sink. */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (sink, "sink", 2, 3);
  /* 3 or 4 is valid, because the pipeline might still be posting 
   * its state_change message */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "pipeline", 3, 4);

  pop_messages (bus, 3);
  pop_async_done (bus);
  fail_if ((gst_bus_pop (bus)) != NULL);

  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 2);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* change state to PLAYING, spawning three messages */
  GST_DEBUG ("setting pipeline to PLAYING");
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);
  ret =
      gst_element_get_state (GST_ELEMENT (pipeline), &current, &pending,
      GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);
  fail_unless (current == GST_STATE_PLAYING);
  fail_unless (pending == GST_STATE_VOID_PENDING);

  /* each object is referenced by one message
   * src might have an extra reference if it's still pushing
   * sink might have an extra reference if it's still blocked on preroll
   * pipeline posted a new-clock message too. */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (src, "src", 2, 3);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (sink, "sink", 2, 4);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 3);

  pop_messages (bus, 3);
  fail_if ((gst_bus_pop (bus)) != NULL);

  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);
  /* src might have an extra reference if it's still pushing */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (src, "src", 1, 2);
  /* sink might have an extra reference if it's still blocked on preroll */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (sink, "sink", 1, 3);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* go back to READY, spawning six messages */
  GST_DEBUG ("setting pipeline to READY");
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  /* each object is referenced by two messages, the source also has the
   * stream-status message referencing it */
  ASSERT_OBJECT_REFCOUNT (src, "src", 4);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 3);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 3);

  pop_messages (bus, 6);
  fail_if ((gst_bus_pop (bus)) != NULL);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* setting pipeline to NULL flushes the bus automatically */
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* clean up */
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_watch_for_state_change)
{
  GstElement *src, *sink, *bin;
  GstBus *bus;
  GstStateChangeReturn ret;

  bin = gst_element_factory_make ("bin", NULL);
  fail_unless (bin != NULL, "Could not create bin");

  bus = g_object_new (gst_bus_get_type (), NULL);
  gst_element_set_bus (GST_ELEMENT_CAST (bin), bus);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink == NULL, "Could not create fakesink");

  gst_bin_add (GST_BIN (bin), sink);
  gst_bin_add (GST_BIN (bin), src);

  fail_unless (gst_element_link (src, sink), "could not link src and sink");

  /* change state, spawning two times three messages */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC);
  ret =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  pop_messages (bus, 6);
  pop_async_done (bus);

  fail_unless (gst_bus_have_pending (bus) == FALSE,
      "Unexpected messages on bus");

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  pop_messages (bus, 3);

  /* this one might return either SUCCESS or ASYNC, likely SUCCESS */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
  gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_CLOCK_TIME_NONE);

  pop_messages (bus, 3);
  if (ret == GST_STATE_CHANGE_ASYNC)
    pop_async_done (bus);

  fail_unless (gst_bus_have_pending (bus) == FALSE,
      "Unexpected messages on bus");

  gst_bus_set_flushing (bus, TRUE);

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  /* clean up */
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_state_change_error_message)
{
  GstElement *src, *sink, *bin;
  GstBus *bus;
  GstStateChangeReturn ret;

  bin = gst_element_factory_make ("bin", NULL);
  fail_unless (bin != NULL, "Could not create bin");

  bus = g_object_new (gst_bus_get_type (), NULL);
  gst_element_set_bus (GST_ELEMENT_CAST (bin), bus);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink == NULL, "Could not create fakesink");

  /* add but don't link elements */
  gst_bin_add (GST_BIN (bin), sink);
  gst_bin_add (GST_BIN (bin), src);

  /* change state, this should succeed */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC);

  /* now wait, the streaming thread will error because the source is not
   * linked. */
  ret = gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_FAILURE);

  gst_bus_set_flushing (bus, TRUE);

  /* setting bin to NULL flushes the bus automatically */
  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  /* clean up */
  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;


/* adding an element with linked pads to a bin unlinks the
 * pads */
GST_START_TEST (test_add_linked)
{
  GstElement *src, *sink;
  GstPad *srcpad, *sinkpad;
  GstElement *pipeline;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink == NULL, "Could not create fakesink");

  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless (sinkpad != NULL);

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK);

  /* pads are linked now */
  fail_unless (gst_pad_is_linked (srcpad));
  fail_unless (gst_pad_is_linked (sinkpad));

  /* adding element to bin voids hierarchy so pads are unlinked */
  gst_bin_add (GST_BIN (pipeline), src);

  /* check if pads really are unlinked */
  fail_unless (!gst_pad_is_linked (srcpad));
  fail_unless (!gst_pad_is_linked (sinkpad));

  /* cannot link pads in wrong hierarchy */
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_WRONG_HIERARCHY);

  /* adding other element to bin as well */
  gst_bin_add (GST_BIN (pipeline), sink);

  /* now we can link again */
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK);

  /* check if pads really are linked */
  fail_unless (gst_pad_is_linked (srcpad));
  fail_unless (gst_pad_is_linked (sinkpad));

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* adding ourself should fail */
GST_START_TEST (test_add_self)
{
  GstElement *bin;

  bin = gst_bin_new (NULL);
  fail_unless (bin != NULL, "Could not create bin");

  ASSERT_CRITICAL (gst_bin_add (GST_BIN (bin), bin));

  gst_object_unref (bin);
}

GST_END_TEST;


/* g_print ("%10s: %4d => %4d\n", GST_OBJECT_NAME (msg->src), old, new); */

#define ASSERT_STATE_CHANGE_MSG(bus,element,old_state,new_state,num)          \
  {                                                                           \
    GstMessage *msg;                                                          \
    GstState old = 0, new = 0, pending = 0;                                   \
    msg = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, GST_SECOND);          \
    fail_if (msg == NULL, "No state change message within 1 second (#"        \
        G_STRINGIFY (num) ")");                                               \
    gst_message_parse_state_changed (msg, &old, &new, &pending);              \
    fail_if (msg->src != GST_OBJECT (element), G_STRINGIFY(element)           \
        " should have changed state next (#" G_STRINGIFY (num) ")");          \
    fail_if (old != old_state || new != new_state, "state change is not "     \
        G_STRINGIFY (old_state) " => " G_STRINGIFY (new_state));              \
    gst_message_unref (msg);                                                  \
  }

GST_START_TEST (test_children_state_change_order_flagged_sink)
{
  GstElement *src, *identity, *sink, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstBus *bus;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL, "Pipeline has no bus?!");

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");
  g_object_set (src, "num-buffers", 5, NULL);

  identity = gst_element_factory_make ("identity", NULL);
  fail_if (identity == NULL, "Could not create identity");

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink == NULL, "Could not create fakesink");

  gst_bin_add_many (GST_BIN (pipeline), src, identity, sink, NULL);

  fail_unless (gst_element_link (src, identity) == TRUE);
  fail_unless (gst_element_link (identity, sink) == TRUE);

  /* (1) Test state change with fakesink being a regular sink */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_if (ret != GST_STATE_CHANGE_ASYNC,
      "State change to PLAYING did not return ASYNC");
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to PLAYING failed");
  fail_if (current != GST_STATE_PLAYING, "State change to PLAYING failed");
  fail_if (pending != GST_STATE_VOID_PENDING, "State change to PLAYING failed");

  /* NULL => READY */
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_NULL, GST_STATE_READY, 101);
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_NULL, GST_STATE_READY, 102);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_NULL, GST_STATE_READY, 103);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_NULL, GST_STATE_READY, 104);

  /* READY => PAUSED */
  /* because of pre-rolling, sink will return ASYNC on state
   * change and change state later when it has a buffer */
  GST_DEBUG ("popping READY -> PAUSED messages");
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_READY, GST_STATE_PAUSED,
      105);
#if 0
  /* From here on, all bets are off. Usually the source changes state next,
   * but it might just as well be that the first buffer produced by the
   * source reaches the sink before the source has finished its state change,
   * in which case the sink will commit its new state before the source ...  */
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_READY, GST_STATE_PAUSED, 106);
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_READY, GST_STATE_PAUSED, 107);
#else

  pop_messages (bus, 2);        /* pop remaining ready => paused messages off the bus */
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_READY, GST_STATE_PAUSED,
      108);
  pop_async_done (bus);
#endif
  /* PAUSED => PLAYING */
  GST_DEBUG ("popping PAUSED -> PLAYING messages");
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_PAUSED, GST_STATE_PLAYING, 109);
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_PAUSED, GST_STATE_PLAYING,
      110);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_PAUSED, GST_STATE_PLAYING, 111);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_PAUSED, GST_STATE_PLAYING,
      112);

  /* don't set to NULL that will set the bus flushing and kill our messages */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to READY failed");
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to READY failed");

  /* TODO: do we need to check downwards state change order as well? */
  pop_messages (bus, 4);        /* pop playing => paused messages off the bus */
  pop_messages (bus, 4);        /* pop paused => ready messages off the bus */

  while (GST_OBJECT_REFCOUNT_VALUE (pipeline) > 1)
    THREAD_SWITCH ();

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to NULL failed");

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;


GST_START_TEST (test_children_state_change_order_semi_sink)
{
  GstElement *src, *identity, *sink, *pipeline;
  GstStateChangeReturn ret;
  GstState current, pending;
  GstBus *bus;

  /* (2) Now again, but check other code path where we don't have
   *     a proper sink correctly flagged as such, but a 'semi-sink' */
  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL, "Pipeline has no bus?!");

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");

  identity = gst_element_factory_make ("identity", NULL);
  fail_if (identity == NULL, "Could not create identity");

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink == NULL, "Could not create fakesink");

  gst_bin_add_many (GST_BIN (pipeline), src, identity, sink, NULL);

  fail_unless (gst_element_link (src, identity) == TRUE);
  fail_unless (gst_element_link (identity, sink) == TRUE);

  /* this is not very nice but should work just fine in this case. */
  GST_OBJECT_FLAG_UNSET (sink, GST_ELEMENT_FLAG_SINK);  /* <======== */

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_if (ret != GST_STATE_CHANGE_ASYNC, "State change to PLAYING not ASYNC");
  ret =
      gst_element_get_state (pipeline, &current, &pending, GST_CLOCK_TIME_NONE);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to PLAYING failed");
  fail_if (current != GST_STATE_PLAYING, "State change to PLAYING failed");
  fail_if (pending != GST_STATE_VOID_PENDING, "State change to PLAYING failed");

  /* NULL => READY */
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_NULL, GST_STATE_READY, 201);
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_NULL, GST_STATE_READY, 202);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_NULL, GST_STATE_READY, 203);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_NULL, GST_STATE_READY, 204);

  /* READY => PAUSED */
  /* because of pre-rolling, sink will return ASYNC on state
   * change and change state later when it has a buffer */
  GST_DEBUG ("popping READY -> PAUSED messages");
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_READY, GST_STATE_PAUSED,
      205);
#if 0
  /* From here on, all bets are off. Usually the source changes state next,
   * but it might just as well be that the first buffer produced by the
   * source reaches the sink before the source has finished its state change,
   * in which case the sink will commit its new state before the source ...  */
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_READY, GST_STATE_PAUSED, 206);
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_READY, GST_STATE_PAUSED, 207);
#else
  pop_messages (bus, 2);        /* pop remaining ready => paused messages off the bus */
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_READY, GST_STATE_PAUSED,
      208);
  pop_async_done (bus);

  /* PAUSED => PLAYING */
  GST_DEBUG ("popping PAUSED -> PLAYING messages");
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_PAUSED, GST_STATE_PLAYING, 209);
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_PAUSED, GST_STATE_PLAYING,
      210);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_PAUSED, GST_STATE_PLAYING, 211);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_PAUSED, GST_STATE_PLAYING,
      212);
#endif

  /* don't set to NULL that will set the bus flushing and kill our messages */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to READY failed");

  /* TODO: do we need to check downwards state change order as well? */
  pop_messages (bus, 4);        /* pop playing => paused messages off the bus */
  pop_messages (bus, 4);        /* pop paused => ready messages off the bus */

  GST_DEBUG ("waiting for pipeline to reach refcount 1");
  while (GST_OBJECT_REFCOUNT_VALUE (pipeline) > 1)
    THREAD_SWITCH ();

  GST_DEBUG ("checking refcount");
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to NULL failed");

  GST_DEBUG ("checking refcount");
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  GST_DEBUG ("cleanup");
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_children_state_change_order_two_sink)
{
  GstElement *src, *tee, *identity, *sink1, *sink2, *pipeline;
  GstStateChangeReturn ret;
  GstBus *bus;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL, "Pipeline has no bus?!");

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");

  tee = gst_element_factory_make ("tee", NULL);
  fail_if (tee == NULL, "Could not create tee");

  identity = gst_element_factory_make ("identity", NULL);
  fail_if (identity == NULL, "Could not create identity");

  sink1 = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink1 == NULL, "Could not create fakesink1");

  sink2 = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink2 == NULL, "Could not create fakesink2");

  gst_bin_add_many (GST_BIN (pipeline), src, tee, identity, sink1, sink2, NULL);

  fail_unless (gst_element_link (src, tee) == TRUE);
  fail_unless (gst_element_link (tee, identity) == TRUE);
  fail_unless (gst_element_link (identity, sink1) == TRUE);
  fail_unless (gst_element_link (tee, sink2) == TRUE);

  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to READY failed");

  /* NULL => READY */
  {
    GstMessage *msg;
    GstState old = 0, new = 0, pending = 0;
    GstObject *first, *second;

    msg = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, GST_SECOND);
    fail_if (msg == NULL, "No state change message within 1 second (#201)");

    gst_message_parse_state_changed (msg, &old, &new, &pending);
    first = gst_object_ref (msg->src);

    fail_if (first != GST_OBJECT (sink1) && first != GST_OBJECT (sink2),
        "sink1 or sink2 should have changed state next #(202)");
    gst_message_unref (msg);

    msg = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, GST_SECOND);
    fail_if (msg == NULL, "No state change message within 1 second (#201)");

    gst_message_parse_state_changed (msg, &old, &new, &pending);
    second = gst_object_ref (msg->src);

    fail_if (second != GST_OBJECT (sink1) && second != GST_OBJECT (sink2),
        "sink1 or sink2 should have changed state next #(202)");
    gst_message_unref (msg);

    fail_if (second == first, "got state change from same object");

    gst_object_unref (first);
    gst_object_unref (second);
  }
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_NULL, GST_STATE_READY, 203);
  ASSERT_STATE_CHANGE_MSG (bus, tee, GST_STATE_NULL, GST_STATE_READY, 204);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_NULL, GST_STATE_READY, 205);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_NULL, GST_STATE_READY, 206);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (tee, "tee", 1);
  ASSERT_OBJECT_REFCOUNT (identity, "identity", 1);
  ASSERT_OBJECT_REFCOUNT (sink1, "sink1", 1);
  ASSERT_OBJECT_REFCOUNT (sink2, "sink2", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to NULL failed");

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (tee, "tee", 1);
  ASSERT_OBJECT_REFCOUNT (identity, "identity", 1);
  ASSERT_OBJECT_REFCOUNT (sink1, "sink1", 1);
  ASSERT_OBJECT_REFCOUNT (sink2, "sink2", 1);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_iterate_sorted)
{
  GstElement *src, *tee, *identity, *sink1, *sink2, *pipeline, *bin;
  GstIterator *it;
  GValue elem = { 0, };

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  bin = gst_bin_new (NULL);
  fail_unless (bin != NULL, "Could not create bin");

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL, "Could not create fakesrc");

  tee = gst_element_factory_make ("tee", NULL);
  fail_if (tee == NULL, "Could not create tee");

  sink1 = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink1 == NULL, "Could not create fakesink1");

  gst_bin_add_many (GST_BIN (bin), src, tee, sink1, NULL);

  fail_unless (gst_element_link (src, tee) == TRUE);
  fail_unless (gst_element_link (tee, sink1) == TRUE);

  identity = gst_element_factory_make ("identity", NULL);
  fail_if (identity == NULL, "Could not create identity");

  sink2 = gst_element_factory_make ("fakesink", NULL);
  fail_if (sink2 == NULL, "Could not create fakesink2");

  gst_bin_add_many (GST_BIN (pipeline), bin, identity, sink2, NULL);

  fail_unless (gst_element_link (tee, identity) == TRUE);
  fail_unless (gst_element_link (identity, sink2) == TRUE);

  it = gst_bin_iterate_sorted (GST_BIN (pipeline));
  fail_unless (gst_iterator_next (it, &elem) == GST_ITERATOR_OK);
  fail_unless (g_value_get_object (&elem) == (gpointer) sink2);
  g_value_reset (&elem);

  fail_unless (gst_iterator_next (it, &elem) == GST_ITERATOR_OK);
  fail_unless (g_value_get_object (&elem) == (gpointer) identity);
  g_value_reset (&elem);

  fail_unless (gst_iterator_next (it, &elem) == GST_ITERATOR_OK);
  fail_unless (g_value_get_object (&elem) == (gpointer) bin);
  g_value_reset (&elem);

  g_value_unset (&elem);
  gst_iterator_free (it);

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static void
test_link_structure_change_state_changed_sync_cb (GstBus * bus,
    GstMessage * message, gpointer data)
{
  GstPipeline *pipeline = GST_PIPELINE (data);
  GstElement *src, *identity, *sink;
  GstState old, snew, pending;

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (sink != NULL, "Could not get sink");

  gst_message_parse_state_changed (message, &old, &snew, &pending);
  if (message->src != GST_OBJECT (sink) || snew != GST_STATE_READY) {
    gst_object_unref (sink);
    return;
  }

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_unless (src != NULL, "Could not get src");

  identity = gst_bin_get_by_name (GST_BIN (pipeline), "identity");
  fail_unless (identity != NULL, "Could not get identity");

  /* link src to identity, the pipeline should detect the new link and
   * resync the state change */
  fail_unless (gst_element_link (src, identity) == TRUE);

  gst_object_unref (src);
  gst_object_unref (identity);
  gst_object_unref (sink);
}

GST_START_TEST (test_link_structure_change)
{
  GstElement *src, *identity, *sink, *pipeline;
  GstBus *bus;
  GstState state;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL, "Could not get bus");

  /* use the sync signal handler to link elements while the pipeline is still
   * doing the state change */
  gst_bus_set_sync_handler (bus, gst_bus_sync_signal_handler, pipeline, NULL);
  g_object_connect (bus, "signal::sync-message::state-changed",
      G_CALLBACK (test_link_structure_change_state_changed_sync_cb), pipeline,
      NULL);

  src = gst_element_factory_make ("fakesrc", "src");
  fail_if (src == NULL, "Could not create fakesrc");

  identity = gst_element_factory_make ("identity", "identity");
  fail_if (identity == NULL, "Could not create identity");

  sink = gst_element_factory_make ("fakesink", "sink");
  fail_if (sink == NULL, "Could not create fakesink1");

  gst_bin_add_many (GST_BIN (pipeline), src, identity, sink, NULL);

  gst_element_set_state (pipeline, GST_STATE_READY);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* the state change will be done on src only if the pipeline correctly resyncs
   * after that fakesrc has been linked to identity */
  gst_element_get_state (src, &state, NULL, 0);
  fail_unless_equals_int (state, GST_STATE_READY);

  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static GstBusSyncReply
sync_handler_remove_sink (GstBus * bus, GstMessage * message, gpointer data)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    GstElement *child;

    child = gst_bin_get_by_name (GST_BIN (data), "fakesink");
    fail_unless (child != NULL, "Could not find fakesink");

    gst_bin_remove (GST_BIN (data), child);
    gst_object_unref (child);
  }
  return GST_BUS_PASS;
}

GST_START_TEST (test_state_failure_remove)
{
  GstElement *src, *sink, *pipeline;
  GstBus *bus;
  GstStateChangeReturn ret;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  src = gst_element_factory_make ("fakesrc", "fakesrc");
  fail_unless (src != NULL, "Could not create fakesrc");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Could not create fakesink");

  g_object_set (sink, "state-error", 1, NULL);

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_link (src, sink);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL, "Could not get bus");

  gst_bus_set_sync_handler (bus, sync_handler_remove_sink, pipeline, NULL);

  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS,
      "did not get state change success");

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_many_bins)
{
  GstStateChangeReturn ret;
  GstElement *src, *sink, *pipeline, *last_bin = NULL;
  gint i;

#define NUM_BINS 2000

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  src = gst_element_factory_make ("fakesrc", "fakesrc");
  fail_unless (src != NULL, "Could not create fakesrc");
  g_object_set (src, "num-buffers", 3, NULL);

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Could not create fakesink");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), sink);

  for (i = 0; i < NUM_BINS; ++i) {
    GstElement *bin, *identity;
    GstPad *srcpad, *sinkpad;

    bin = gst_bin_new (NULL);
    fail_unless (bin != NULL, "Could not create bin %d", i);
    identity = gst_element_factory_make ("identity", "identity");
    fail_unless (identity != NULL, "Could not create identity %d", i);
    g_object_set (identity, "silent", TRUE, NULL);
    gst_bin_add (GST_BIN (bin), identity);
    sinkpad = gst_element_get_static_pad (identity, "sink");
    srcpad = gst_element_get_static_pad (identity, "src");
    gst_element_add_pad (bin, gst_ghost_pad_new ("sink", sinkpad));
    gst_element_add_pad (bin, gst_ghost_pad_new ("src", srcpad));
    gst_object_unref (sinkpad);
    gst_object_unref (srcpad);

    gst_bin_add (GST_BIN (pipeline), bin);

    if (last_bin == NULL) {
      srcpad = gst_element_get_static_pad (src, "src");
    } else {
      srcpad = gst_element_get_static_pad (last_bin, "src");
    }
    sinkpad = gst_element_get_static_pad (bin, "sink");
    gst_pad_link_full (srcpad, sinkpad, GST_PAD_LINK_CHECK_NOTHING);
    gst_object_unref (sinkpad);
    gst_object_unref (srcpad);


    last_bin = bin;

    /* insert some queues to limit the number of function calls in a row */
    if ((i % 100) == 0) {
      GstElement *q = gst_element_factory_make ("queue", NULL);

      GST_LOG ("bin #%d, inserting queue", i);
      gst_bin_add (GST_BIN (pipeline), q);
      fail_unless (gst_element_link (last_bin, q));
      last_bin = q;
    }
  }

  fail_unless (gst_element_link (last_bin, sink));

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  for (i = 0; i < 15; ++i) {
    GST_INFO ("waiting for preroll ...");
    ret = gst_element_get_state (pipeline, NULL, NULL, GST_SECOND);
    if (ret != GST_STATE_CHANGE_ASYNC)
      break;
  }
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static GstPadProbeReturn
fakesrc_pad_blocked_cb (GstPad * pad, GstPadProbeInfo * info, void *arg)
{
  GstPipeline *pipeline = (GstPipeline *) arg;
  GstElement *src, *sink;

  src = gst_bin_get_by_name (GST_BIN (pipeline), "fakesrc");
  fail_unless (src != NULL, "Could not get fakesrc");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Could not create fakesink");

  g_object_set (sink, "state-error", 1, NULL);
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_link (src, sink);
  gst_element_sync_state_with_parent (sink);
  gst_object_unref (src);

  return GST_PAD_PROBE_REMOVE;
}

GST_START_TEST (test_state_failure_unref)
{
  GstElement *src, *pipeline;
  GstPad *srcpad;
  GstBus *bus;
  GstStateChangeReturn ret;
  GstMessage *msg;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  src = gst_element_factory_make ("fakesrc", "fakesrc");
  fail_unless (src != NULL, "Could not create fakesrc");

  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL, "Could not get fakesrc srcpad");

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      fakesrc_pad_blocked_cb, pipeline, NULL);
  gst_object_unref (srcpad);

  gst_bin_add (GST_BIN (pipeline), src);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL, "Could not get bus");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait for an error message from our fakesink (added from the
     pad block callback). */
  msg = gst_bus_poll (bus, GST_MESSAGE_ERROR, GST_SECOND);
  fail_if (msg == NULL, "No error message within 1 second");
  gst_message_unref (msg);

  /* Check that after this failure, we can still stop, and then unref, the
     pipeline. This should always be possible. */
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "downward state change failed");

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static void
on_sync_bus_error (GstBus * bus, GstMessage * msg)
{
  fail_if (msg != NULL);
}

GST_START_TEST (test_state_change_skip)
{
  GstElement *sink, *pipeline;
  GstStateChangeReturn ret;
  GstBus *bus;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  fail_unless (bus != NULL, "Could not get bus");

  /* no errors */
  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (bus, "sync-message::error", (GCallback) on_sync_bus_error,
      NULL);

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Could not create fakesink");
  gst_element_set_state (sink, GST_STATE_PAUSED);

  g_object_set (sink, "state-error", 5, NULL);

  gst_bin_add (GST_BIN (pipeline), sink);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_object_set (sink, "state-error", 0, NULL);

  /* Check that after this failure, we can still stop, and then unref, the
     pipeline. This should always be possible. */
  ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "downward state change failed");

  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_END_TEST;

GST_START_TEST (test_duration_is_max)
{
  GstElement *bin, *src[3], *sink[3];
  GstStateChangeReturn state_res;
  GstFormat format = GST_FORMAT_BYTES;
  gboolean res;
  gint64 duration;
  GstBus *bus;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");

  /* 3 sources, an adder and a fakesink */
  src[0] = gst_element_factory_make ("fakesrc", NULL);
  src[1] = gst_element_factory_make ("fakesrc", NULL);
  src[2] = gst_element_factory_make ("fakesrc", NULL);
  sink[0] = gst_element_factory_make ("fakesink", NULL);
  sink[1] = gst_element_factory_make ("fakesink", NULL);
  sink[2] = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add_many (GST_BIN (bin), src[0], src[1], src[2], sink[0], sink[1],
      sink[2], NULL);

  gst_element_link (src[0], sink[0]);
  gst_element_link (src[1], sink[1]);
  gst_element_link (src[2], sink[2]);

  /* irks, duration is reset on basesrc */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* set durations on src */
  GST_BASE_SRC (src[0])->segment.duration = 1000;
  GST_BASE_SRC (src[1])->segment.duration = 3000;
  GST_BASE_SRC (src[2])->segment.duration = 2000;

  /* set to playing */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* wait for completion */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  res = gst_element_query_duration (GST_ELEMENT (bin), format, &duration);
  fail_unless (res, NULL);

  ck_assert_int_eq (duration, 3000);

  bus = gst_element_get_bus (bin);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_duration_unknown_overrides)
{
  GstElement *bin, *src[3], *sink[3];
  GstStateChangeReturn state_res;
  GstFormat format = GST_FORMAT_BYTES;
  gboolean res;
  gint64 duration;
  GstBus *bus;

  GST_INFO ("preparing test");

  /* build pipeline */
  bin = gst_pipeline_new ("pipeline");

  /* 3 sources, an adder and a fakesink */
  src[0] = gst_element_factory_make ("fakesrc", NULL);
  src[1] = gst_element_factory_make ("fakesrc", NULL);
  src[2] = gst_element_factory_make ("fakesrc", NULL);
  sink[0] = gst_element_factory_make ("fakesink", NULL);
  sink[1] = gst_element_factory_make ("fakesink", NULL);
  sink[2] = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add_many (GST_BIN (bin), src[0], src[1], src[2], sink[0], sink[1],
      sink[2], NULL);

  gst_element_link (src[0], sink[0]);
  gst_element_link (src[1], sink[1]);
  gst_element_link (src[2], sink[2]);

  /* irks, duration is reset on basesrc */
  state_res = gst_element_set_state (bin, GST_STATE_PAUSED);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* set durations on src */
  GST_BASE_SRC (src[0])->segment.duration = GST_CLOCK_TIME_NONE;
  GST_BASE_SRC (src[1])->segment.duration = 3000;
  GST_BASE_SRC (src[2])->segment.duration = 2000;

  /* set to playing */
  state_res = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  /* wait for completion */
  state_res =
      gst_element_get_state (GST_ELEMENT (bin), NULL, NULL,
      GST_CLOCK_TIME_NONE);
  fail_unless (state_res != GST_STATE_CHANGE_FAILURE, NULL);

  res = gst_element_query_duration (GST_ELEMENT (bin), format, &duration);
  fail_unless (res, NULL);

  ck_assert_int_eq (duration, GST_CLOCK_TIME_NONE);

  bus = gst_element_get_bus (bin);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bin);
}

GST_END_TEST;

static gboolean
element_in_list (GList ** list, GstElement * element)
{
  GList *l = g_list_find (*list, element);

  if (l == NULL)
    return FALSE;

  *list = g_list_delete_link (*list, l);
  return TRUE;
}

#define element_was_added(e) element_in_list(&added,e)
#define element_was_removed(e) element_in_list(&removed,e)

static void
add_cb (GstBin * pipeline, GstBin * bin, GstElement * element, GList ** list)
{
  fail_unless (GST_OBJECT_PARENT (element) == GST_OBJECT_CAST (bin));

  *list = g_list_prepend (*list, element);
}

static void
remove_cb (GstBin * pipeline, GstBin * bin, GstElement * element, GList ** list)
{
  *list = g_list_prepend (*list, element);
}

GST_START_TEST (test_deep_added_removed)
{
  GstElement *pipe, *e, *bin0, *bin1;
  gulong id_removed, id_added;
  GList *removed = NULL;
  GList *added = NULL;

  pipe = gst_pipeline_new (NULL);

  id_added = g_signal_connect (pipe, "deep-element-added",
      G_CALLBACK (add_cb), &added);
  id_removed = g_signal_connect (pipe, "deep-element-removed",
      G_CALLBACK (remove_cb), &removed);

  /* simple add/remove */
  e = gst_element_factory_make ("identity", NULL);
  gst_bin_add (GST_BIN (pipe), e);
  fail_unless (element_was_added (e));
  gst_bin_remove (GST_BIN (pipe), e);
  fail_unless (element_was_removed (e));

  /* let's try with a deeper hierarchy, construct it from top-level down */
  bin0 = gst_bin_new (NULL);
  gst_bin_add (GST_BIN (pipe), bin0);
  bin1 = gst_bin_new (NULL);
  gst_bin_add (GST_BIN (bin0), bin1);
  e = gst_element_factory_make ("identity", NULL);
  gst_bin_add (GST_BIN (bin1), e);
  fail_unless (element_was_added (bin0));
  fail_unless (element_was_added (bin1));
  fail_unless (element_was_added (e));
  fail_unless (added == NULL);
  fail_unless (removed == NULL);

  gst_object_ref (e);           /* keep e alive */
  gst_bin_remove (GST_BIN (bin1), e);
  fail_unless (element_was_removed (e));
  fail_unless (added == NULL);
  fail_unless (removed == NULL);

  /* now add existing bin hierarchy to pipeline (first remove it so we can re-add it) */
  gst_object_ref (bin0);        /* keep bin0 alive */
  gst_bin_remove (GST_BIN (pipe), bin0);
  fail_unless (element_was_removed (bin0));
  fail_unless (element_was_removed (bin1));
  fail_unless (added == NULL);
  fail_unless (removed == NULL);

  /* re-adding element to removed bin should not trigger our callbacks */
  gst_bin_add (GST_BIN (bin1), e);
  fail_unless (added == NULL);
  fail_unless (removed == NULL);

  gst_bin_add (GST_BIN (pipe), bin0);
  fail_unless (element_was_added (bin0));
  fail_unless (element_was_added (bin1));
  fail_unless (element_was_added (e));
  fail_unless (added == NULL);
  fail_unless (removed == NULL);
  gst_object_unref (bin0);
  gst_object_unref (e);

  /* disconnect signals, unref will trigger remove callbacks otherwise */
  g_signal_handler_disconnect (pipe, id_added);
  g_signal_handler_disconnect (pipe, id_removed);

  gst_object_unref (pipe);
}

GST_END_TEST;

#define _GST_CHECK_BIN_SUPPRESSED_FLAGS(element_flags, suppressed_flags, \
    expected_flags) \
G_STMT_START { \
  GstBin *bin = GST_BIN (gst_bin_new ("test-bin")); \
  GstElement *element = gst_element_factory_make ("identity", "test-i"); \
  GstElementFlags natural_flags = GST_OBJECT_FLAGS (bin); \
  GST_OBJECT_FLAG_SET (element, element_flags); \
  gst_bin_set_suppressed_flags (bin, suppressed_flags); \
  gst_bin_add (bin, element); \
  fail_unless ((natural_flags | GST_OBJECT_FLAGS (bin)) \
      == expected_flags); \
  gst_object_unref (bin); \
} G_STMT_END

GST_START_TEST (test_suppressed_flags)
{
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SOURCE,
      0, GST_ELEMENT_FLAG_SOURCE);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SOURCE,
      GST_ELEMENT_FLAG_SOURCE, 0);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SOURCE,
      GST_ELEMENT_FLAG_SINK, GST_ELEMENT_FLAG_SOURCE);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SOURCE |
      GST_ELEMENT_FLAG_PROVIDE_CLOCK,
      GST_ELEMENT_FLAG_PROVIDE_CLOCK, GST_ELEMENT_FLAG_SOURCE);

  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SINK,
      0, GST_ELEMENT_FLAG_SINK);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SINK,
      GST_ELEMENT_FLAG_SINK, 0);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_SINK,
      GST_ELEMENT_FLAG_SOURCE, GST_ELEMENT_FLAG_SINK);

  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_PROVIDE_CLOCK,
      0, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_PROVIDE_CLOCK,
      GST_ELEMENT_FLAG_PROVIDE_CLOCK, 0);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_PROVIDE_CLOCK,
      GST_ELEMENT_FLAG_REQUIRE_CLOCK, GST_ELEMENT_FLAG_PROVIDE_CLOCK);

  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_REQUIRE_CLOCK,
      0, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_REQUIRE_CLOCK,
      GST_ELEMENT_FLAG_REQUIRE_CLOCK, 0);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS (GST_ELEMENT_FLAG_REQUIRE_CLOCK,
      GST_ELEMENT_FLAG_PROVIDE_CLOCK, GST_ELEMENT_FLAG_REQUIRE_CLOCK);
}

GST_END_TEST;


#define _GST_CHECK_BIN_SUPPRESSED_FLAGS_REMOVAL(suppressed_flags) \
G_STMT_START { \
  GstBin *bin = GST_BIN (gst_bin_new ("test-bin")); \
  GstElement *element = gst_element_factory_make ("identity", "test-i"); \
  GST_OBJECT_FLAG_SET (bin, suppressed_flags); \
  gst_bin_set_suppressed_flags (bin, suppressed_flags); \
  GST_OBJECT_FLAG_SET (element, suppressed_flags); \
  fail_unless ((suppressed_flags & GST_OBJECT_FLAGS (bin)) \
      == suppressed_flags); \
  gst_bin_add (bin, element); \
  fail_unless ((suppressed_flags & GST_OBJECT_FLAGS (bin)) \
      == suppressed_flags); \
  gst_bin_remove (bin, element); \
  fail_unless ((suppressed_flags & GST_OBJECT_FLAGS (bin)) \
      == suppressed_flags); \
  gst_object_unref (bin); \
} G_STMT_END

GST_START_TEST (test_suppressed_flags_when_removing)
{
  _GST_CHECK_BIN_SUPPRESSED_FLAGS_REMOVAL (GST_ELEMENT_FLAG_SOURCE);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS_REMOVAL (GST_ELEMENT_FLAG_SINK);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS_REMOVAL (GST_ELEMENT_FLAG_REQUIRE_CLOCK);
  _GST_CHECK_BIN_SUPPRESSED_FLAGS_REMOVAL (GST_ELEMENT_FLAG_PROVIDE_CLOCK);
}

GST_END_TEST;

static Suite *
gst_bin_suite (void)
{
  Suite *s = suite_create ("GstBin");
  TCase *tc_chain = tcase_create ("bin tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_interface);
  tcase_add_test (tc_chain, test_eos);
  tcase_add_test (tc_chain, test_stream_start);
  tcase_add_test (tc_chain, test_children_state_change_order_flagged_sink);
  tcase_add_test (tc_chain, test_children_state_change_order_semi_sink);
  tcase_add_test (tc_chain, test_children_state_change_order_two_sink);
  tcase_add_test (tc_chain, test_message_state_changed);
  tcase_add_test (tc_chain, test_message_state_changed_child);
  tcase_add_test (tc_chain, test_message_state_changed_children);
  tcase_add_test (tc_chain, test_watch_for_state_change);
  tcase_add_test (tc_chain, test_state_change_error_message);
  tcase_add_test (tc_chain, test_add_linked);
  tcase_add_test (tc_chain, test_add_self);
  tcase_add_test (tc_chain, test_iterate_sorted);
  tcase_add_test (tc_chain, test_link_structure_change);
  tcase_add_test (tc_chain, test_state_failure_remove);
  tcase_add_test (tc_chain, test_state_failure_unref);
  tcase_add_test (tc_chain, test_state_change_skip);
  tcase_add_test (tc_chain, test_duration_is_max);
  tcase_add_test (tc_chain, test_duration_unknown_overrides);
  tcase_add_test (tc_chain, test_deep_added_removed);
  tcase_add_test (tc_chain, test_suppressed_flags);
  tcase_add_test (tc_chain, test_suppressed_flags_when_removing);

  /* fails on OSX build bot for some reason, and is a bit silly anyway */
  if (0)
    tcase_add_test (tc_chain, test_many_bins);

  return s;
}

GST_CHECK_MAIN (gst_bin);

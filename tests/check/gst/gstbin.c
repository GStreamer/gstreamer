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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/check/gstcheck.h>

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

GST_START_TEST (test_interface)
{
  GstBin *bin, *bin2;
  GstElement *filesrc;
  GstIterator *it;
  gpointer item;

  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL, "Could not create bin");

  filesrc = gst_element_factory_make ("filesrc", NULL);
  fail_unless (filesrc != NULL, "Could not create filesrc");
  fail_unless (GST_IS_URI_HANDLER (filesrc), "Filesrc not a URI handler");
  gst_bin_add (bin, filesrc);

  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (it != NULL);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (item == (gpointer) filesrc);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL), NULL);
  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (it != NULL);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (item == (gpointer) filesrc);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  bin2 = bin;
  bin = GST_BIN (gst_bin_new (NULL));
  fail_unless (bin != NULL);
  gst_bin_add_many (bin,
      gst_element_factory_make ("identity", NULL),
      gst_element_factory_make ("identity", NULL),
      GST_ELEMENT (bin2), gst_element_factory_make ("identity", NULL), NULL);
  fail_unless (gst_bin_get_by_interface (bin, GST_TYPE_URI_HANDLER) == filesrc);
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (item == (gpointer) filesrc);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_bin_add (bin, gst_element_factory_make ("filesrc", NULL));
  gst_bin_add (bin2, gst_element_factory_make ("filesrc", NULL));
  it = gst_bin_iterate_all_by_interface (bin, GST_TYPE_URI_HANDLER);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  gst_object_unref (bin);
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

  /* change state to PAUSED, spawning three messages */
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
  while (GST_OBJECT_REFCOUNT_VALUE (pipeline) > 2)
    THREAD_SWITCH ();

  /* each object is referenced by a message;
   * base_src is blocked in the push and has an extra refcount.
   * base_sink_chain has taken a refcount on the sink, and is blocked on
   * preroll */
  ASSERT_OBJECT_REFCOUNT (src, "src", 3);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 3);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 2);

  pop_messages (bus, 3);
  fail_if ((gst_bus_pop (bus)) != NULL);

  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);
  ASSERT_OBJECT_REFCOUNT (src, "src", 2);
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
  ASSERT_OBJECT_REFCOUNT_BETWEEN (sink, "sink", 2, 3);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 3);

  pop_messages (bus, 3);
  fail_if ((gst_bus_pop (bus)) != NULL);

  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);
  /* src might have an extra reference if it's still pushing */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (src, "src", 1, 2);
  /* sink might have an extra reference if it's still blocked on preroll */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (sink, "sink", 1, 2);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  /* go back to READY, spawning six messages */
  GST_DEBUG ("setting pipeline to READY");
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  /* each object is referenced by two messages */
  ASSERT_OBJECT_REFCOUNT (src, "src", 3);
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

  fail_unless (gst_bus_have_pending (bus) == FALSE,
      "Unexpected messages on bus");

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  pop_messages (bus, 3);

  /* this one might return either SUCCESS or ASYNC, likely SUCCESS */
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
  gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_CLOCK_TIME_NONE);

  pop_messages (bus, 3);

  fail_unless (gst_bus_have_pending (bus) == FALSE,
      "Unexpected messages on bus");

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

  srcpad = gst_element_get_pad (src, "src");
  fail_unless (srcpad != NULL);
  sinkpad = gst_element_get_pad (sink, "sink");
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
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_READY, GST_STATE_PAUSED,
      105);
#if 0
  /* From here on, all bets are off. Usually the source changes state next,
   * but it might just as well be that the first buffer produced by the
   * source reaches the sink before the source has finished its state change,
   * in which case the sink will commit its new state before the source ...  */
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_READY, GST_STATE_PAUSED, 106);
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_READY, GST_STATE_PAUSED, 107);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_READY, GST_STATE_PAUSED,
      108);

  /* PAUSED => PLAYING */
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_PAUSED, GST_STATE_PLAYING, 109);
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_PAUSED, GST_STATE_PLAYING,
      110);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_PAUSED, GST_STATE_PLAYING, 111);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_PAUSED, GST_STATE_PLAYING,
      112);
#else
  pop_messages (bus, 3);        /* pop remaining ready => paused messages off the bus */
  pop_messages (bus, 4);        /* pop paused => playing messages off the bus */
#endif

  /* don't set to NULL that will set the bus flushing and kill our messages */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to READY failed");
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_if (ret != GST_STATE_CHANGE_SUCCESS, "State change to READY failed");

  /* TODO: do we need to check downwards state change order as well? */
  pop_messages (bus, 4);        /* pop playing => paused messages off the bus */
  pop_messages (bus, 4);        /* pop paused => ready messages off the bus */

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
  GST_OBJECT_FLAG_UNSET (sink, GST_ELEMENT_IS_SINK);    /* <======== */

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
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_READY, GST_STATE_PAUSED,
      205);
#if 0
  /* From here on, all bets are off. Usually the source changes state next,
   * but it might just as well be that the first buffer produced by the
   * source reaches the sink before the source has finished its state change,
   * in which case the sink will commit its new state before the source ...  */
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_READY, GST_STATE_PAUSED, 206);
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_READY, GST_STATE_PAUSED, 207);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_READY, GST_STATE_PAUSED,
      208);

  /* PAUSED => PLAYING */
  ASSERT_STATE_CHANGE_MSG (bus, sink, GST_STATE_PAUSED, GST_STATE_PLAYING, 209);
  ASSERT_STATE_CHANGE_MSG (bus, identity, GST_STATE_PAUSED, GST_STATE_PLAYING,
      210);
  ASSERT_STATE_CHANGE_MSG (bus, src, GST_STATE_PAUSED, GST_STATE_PLAYING, 211);
  ASSERT_STATE_CHANGE_MSG (bus, pipeline, GST_STATE_PAUSED, GST_STATE_PLAYING,
      212);
#else
  pop_messages (bus, 3);        /* pop remaining ready => paused messages off the bus */
  pop_messages (bus, 4);        /* pop paused => playing messages off the bus */
#endif

  /* don't set to NULL that will set the bus flushing and kill our messages */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
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

Suite *
gst_bin_suite (void)
{
  Suite *s = suite_create ("GstBin");
  TCase *tc_chain = tcase_create ("bin tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_interface);
  tcase_add_test (tc_chain, test_children_state_change_order_flagged_sink);
  tcase_add_test (tc_chain, test_children_state_change_order_semi_sink);
  tcase_add_test (tc_chain, test_children_state_change_order_two_sink);
  tcase_add_test (tc_chain, test_message_state_changed);
  tcase_add_test (tc_chain, test_message_state_changed_child);
  tcase_add_test (tc_chain, test_message_state_changed_children);
  tcase_add_test (tc_chain, test_watch_for_state_change);
  tcase_add_test (tc_chain, test_add_linked);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_bin_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

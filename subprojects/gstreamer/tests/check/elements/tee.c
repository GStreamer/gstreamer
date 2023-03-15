/* GStreamer
 *
 * unit test for tee
 *
 * Copyright (C) <2007> Wim Taymans <wim dot taymans at gmail dot com>
 * Copyright (C) <2008> Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) <2008> Christian Berentsen <christian.berentsen@tandberg.com>
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

static void
handoff (GstElement * fakesink, GstBuffer * buf, GstPad * pad, guint * count)
{
  *count = *count + 1;
}

/* construct fakesrc num-buffers=3 ! tee name=t ! queue ! fakesink t. ! queue !
 * fakesink. Each fakesink should exactly receive 3 buffers.
 */
GST_START_TEST (test_num_buffers)
{
#define NUM_SUBSTREAMS 15
#define NUM_BUFFERS 3
  GstElement *pipeline, *src, *tee;
  GstElement *queues[NUM_SUBSTREAMS];
  GstElement *sinks[NUM_SUBSTREAMS];
  GstPad *req_pads[NUM_SUBSTREAMS];
  guint counts[NUM_SUBSTREAMS];
  GstBus *bus;
  GstMessage *msg;
  gint i;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, NULL);
  tee = gst_check_setup_element ("tee");
  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), tee));
  fail_unless (gst_element_link (src, tee));

  for (i = 0; i < NUM_SUBSTREAMS; ++i) {
    GstPad *qpad;
    gchar name[32];

    counts[i] = 0;

    queues[i] = gst_check_setup_element ("queue");
    g_snprintf (name, 32, "queue%d", i);
    gst_object_set_name (GST_OBJECT (queues[i]), name);
    fail_unless (gst_bin_add (GST_BIN (pipeline), queues[i]));

    sinks[i] = gst_check_setup_element ("fakesink");
    g_snprintf (name, 32, "sink%d", i);
    gst_object_set_name (GST_OBJECT (sinks[i]), name);
    fail_unless (gst_bin_add (GST_BIN (pipeline), sinks[i]));
    fail_unless (gst_element_link (queues[i], sinks[i]));
    g_object_set (sinks[i], "signal-handoffs", TRUE, NULL);
    g_signal_connect (sinks[i], "handoff", (GCallback) handoff, &counts[i]);

    req_pads[i] = gst_element_request_pad_simple (tee, "src_%u");
    fail_unless (req_pads[i] != NULL);

    qpad = gst_element_get_static_pad (queues[i], "sink");
    fail_unless_equals_int (gst_pad_link (req_pads[i], qpad), GST_PAD_LINK_OK);
    gst_object_unref (qpad);
  }

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  for (i = 0; i < NUM_SUBSTREAMS; ++i) {
    fail_unless_equals_int (counts[i], NUM_BUFFERS);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);

  for (i = 0; i < NUM_SUBSTREAMS; ++i) {
    gst_element_release_request_pad (tee, req_pads[i]);
    gst_object_unref (req_pads[i]);
  }
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* we use fakesrc ! tee ! fakesink and then randomly request/release and link
 * some pads from tee. This should happily run without any errors. */
GST_START_TEST (test_stress)
{
  GstElement *pipeline;
  GstElement *tee;
  const gchar *desc;
  GstBus *bus;
  GstMessage *msg;
  gint i;

  /* Pump 1000 buffers (10 bytes each) per second through tee for 5 secs */
  desc = "fakesrc datarate=10000 sizemin=10 sizemax=10 num-buffers=5000 ! "
      "video/x-raw,framerate=25/1 ! tee name=t ! "
      "queue max-size-buffers=2 ! fakesink sync=true";

  pipeline = gst_parse_launch (desc, NULL);
  fail_if (pipeline == NULL);

  tee = gst_bin_get_by_name (GST_BIN (pipeline), "t");
  fail_if (tee == NULL);

  /* bring the pipeline to PLAYING, then start switching */
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  /* Wait for the pipeline to hit playing so that parse_launch can do the
   * initial link, otherwise we perform linking from multiple threads and cause
   * trouble */
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  for (i = 0; i < 50000; i++) {
    GstPad *pad;

    pad = gst_element_request_pad_simple (tee, "src_%u");
    gst_element_release_request_pad (tee, pad);
    gst_object_unref (pad);

    if ((msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, 0)))
      break;
  }

  /* now wait for completion or error */
  if (msg == NULL)
    msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (tee);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

typedef struct
{
  GstElement *tee;
  GstCaps *caps;
  GstPad *start_srcpad;
  GstPad *tee_sinkpad;
  GstPad *tee_srcpad;
  GstPad *final_sinkpad;
  GThread *app_thread;
  gint countdown;
  gboolean app_thread_prepped;
  gboolean bufferalloc_blocked;
} BufferAllocHarness;

static void
buffer_alloc_harness_setup (BufferAllocHarness * h, gint countdown)
{
  h->app_thread = NULL;

  h->tee = gst_check_setup_element ("tee");
  fail_if (h->tee == NULL);

  h->countdown = countdown;

  fail_unless_equals_int (gst_element_set_state (h->tee, GST_STATE_PLAYING),
      TRUE);

  h->caps = gst_caps_new_empty_simple ("video/x-raw");

  h->start_srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (h->start_srcpad == NULL);
  fail_unless (gst_pad_set_active (h->start_srcpad, TRUE) == TRUE);
  fail_unless (gst_pad_set_caps (h->start_srcpad, h->caps) == TRUE);

  h->tee_sinkpad = gst_element_get_static_pad (h->tee, "sink");
  fail_if (h->tee_sinkpad == NULL);

  h->tee_srcpad = gst_element_request_pad_simple (h->tee, "src_%u");
  fail_if (h->tee_srcpad == NULL);

  h->final_sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (h->final_sinkpad == NULL);
  fail_unless (gst_pad_set_active (h->final_sinkpad, TRUE) == TRUE);
  fail_unless (gst_pad_set_caps (h->final_sinkpad, h->caps) == TRUE);
  g_object_set_qdata (G_OBJECT (h->final_sinkpad),
      g_quark_from_static_string ("buffer-alloc-harness"), h);

  fail_unless_equals_int (gst_pad_link (h->start_srcpad, h->tee_sinkpad),
      GST_PAD_LINK_OK);
  fail_unless_equals_int (gst_pad_link (h->tee_srcpad, h->final_sinkpad),
      GST_PAD_LINK_OK);
}

static void
buffer_alloc_harness_teardown (BufferAllocHarness * h)
{
  if (h->app_thread)
    g_thread_join (h->app_thread);

  gst_pad_set_active (h->final_sinkpad, FALSE);
  gst_object_unref (h->final_sinkpad);
  gst_object_unref (h->tee_srcpad);
  gst_object_unref (h->tee_sinkpad);
  gst_pad_set_active (h->start_srcpad, FALSE);
  gst_object_unref (h->start_srcpad);
  gst_caps_unref (h->caps);
  gst_check_teardown_element (h->tee);
}

#if 0
static gpointer
app_thread_func (gpointer data)
{
  BufferAllocHarness *h = data;

  /* Signal that we are about to call release_request_pad(). */
  g_mutex_lock (&check_mutex);
  h->app_thread_prepped = TRUE;
  g_cond_signal (&check_cond);
  g_mutex_unlock (&check_mutex);

  /* Simulate that the app releases the pad while the streaming thread is in
   * buffer_alloc below. */
  gst_element_release_request_pad (h->tee, h->tee_srcpad);

  /* Signal the bufferalloc function below if it's still waiting. */
  g_mutex_lock (&check_mutex);
  h->bufferalloc_blocked = FALSE;
  g_cond_signal (&check_cond);
  g_mutex_unlock (&check_mutex);

  return NULL;
}
#endif

#if 0
static GstFlowReturn
final_sinkpad_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  BufferAllocHarness *h;
  gint64 deadline;

  h = g_object_get_qdata (G_OBJECT (pad),
      g_quark_from_static_string ("buffer-alloc-harness"));
  g_assert (h != NULL);

  if (--(h->countdown) == 0) {
    /* Time to make the app release the pad. */
    h->app_thread_prepped = FALSE;
    h->bufferalloc_blocked = TRUE;

    h->app_thread = g_thread_try_new ("gst-check", app_thread_func, h, NULL);
    fail_if (h->app_thread == NULL);

    /* Wait for the app thread to get ready to call release_request_pad(). */
    g_mutex_lock (&check_mutex);
    while (!h->app_thread_prepped)
      g_cond_wait (&check_cond, &check_mutex);
    g_mutex_unlock (&check_mutex);

    /* Now wait for it to do that within a second, to avoid deadlocking
     * in the event of future changes to the locking semantics. */
    g_mutex_lock (&check_mutex);
    deadline = g_get_monotonic_time ();
    deadline += G_USEC_PER_SEC;
    while (h->bufferalloc_blocked) {
      if (!g_cond_wait_until (&check_cond, &check_mutex, deadline))
        break;
    }
    g_mutex_unlock (&check_mutex);
  }

  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);

  return GST_FLOW_OK;
}
#endif

/* Simulate an app releasing the pad while the first alloc_buffer() is in
 * progress. */
GST_START_TEST (test_release_while_buffer_alloc)
{
  BufferAllocHarness h;

  buffer_alloc_harness_setup (&h, 1);

  buffer_alloc_harness_teardown (&h);
}

GST_END_TEST;

/* Simulate an app releasing the pad while the second alloc_buffer() is in
 * progress. */
GST_START_TEST (test_release_while_second_buffer_alloc)
{
  BufferAllocHarness h;

  buffer_alloc_harness_setup (&h, 2);

  buffer_alloc_harness_teardown (&h);
}

GST_END_TEST;

/* Check the internal pads of tee */
GST_START_TEST (test_internal_links)
{
  GstElement *tee;
  GstPad *sinkpad, *srcpad1, *srcpad2;
  GstIterator *it;
  GstIteratorResult res;
  GValue val1 = { 0, }
  , val2 = {
    0,
  };

  tee = gst_check_setup_element ("tee");

  sinkpad = gst_element_get_static_pad (tee, "sink");
  fail_unless (sinkpad != NULL);
  it = gst_pad_iterate_internal_links (sinkpad);
  fail_unless (it != NULL);

  /* iterator should not return anything */
  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_DONE);
  fail_unless (g_value_get_object (&val1) == NULL);

  srcpad1 = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (srcpad1 != NULL);

  /* iterator should resync */
  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_RESYNC);
  fail_unless (g_value_get_object (&val1) == NULL);
  gst_iterator_resync (it);

  /* we should get something now */
  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_OK);
  fail_unless (GST_PAD_CAST (g_value_get_object (&val1)) == srcpad1);

  g_value_reset (&val1);

  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_DONE);
  fail_unless (g_value_get_object (&val1) == NULL);

  srcpad2 = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (srcpad2 != NULL);

  /* iterator should resync */
  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_RESYNC);
  fail_unless (g_value_get_object (&val1) == NULL);
  gst_iterator_resync (it);

  /* we should get one of the 2 pads now */
  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_OK);
  fail_unless (GST_PAD_CAST (g_value_get_object (&val1)) == srcpad1
      || GST_PAD_CAST (g_value_get_object (&val1)) == srcpad2);

  /* and the other */
  res = gst_iterator_next (it, &val2);
  fail_unless (res == GST_ITERATOR_OK);
  fail_unless (GST_PAD_CAST (g_value_get_object (&val2)) == srcpad1
      || GST_PAD_CAST (g_value_get_object (&val2)) == srcpad2);
  fail_unless (g_value_get_object (&val1) != g_value_get_object (&val2));
  g_value_reset (&val1);
  g_value_reset (&val2);

  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_DONE);
  fail_unless (g_value_get_object (&val1) == NULL);

  gst_iterator_free (it);

  /* get an iterator for the other direction */
  it = gst_pad_iterate_internal_links (srcpad1);
  fail_unless (it != NULL);

  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_OK);
  fail_unless (GST_PAD_CAST (g_value_get_object (&val1)) == sinkpad);
  g_value_reset (&val1);

  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_DONE);
  gst_iterator_free (it);

  it = gst_pad_iterate_internal_links (srcpad2);
  fail_unless (it != NULL);

  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_OK);
  fail_unless (GST_PAD_CAST (g_value_get_object (&val1)) == sinkpad);
  g_value_reset (&val1);

  res = gst_iterator_next (it, &val1);
  fail_unless (res == GST_ITERATOR_DONE);

  g_value_unset (&val1);
  g_value_unset (&val2);
  gst_iterator_free (it);
  gst_object_unref (srcpad1);
  gst_object_unref (srcpad2);
  gst_object_unref (sinkpad);
  gst_object_unref (tee);
}

GST_END_TEST;

static GstFlowReturn
_fake_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static GstFlowReturn
_fake_chain_error (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;
}

GST_START_TEST (test_flow_aggregation)
{
  GstPad *mysrc, *mysink1, *mysink2;
  GstPad *teesink, *teesrc1, *teesrc2;
  GstElement *tee;
  GstBuffer *buffer;
  GstSegment segment;
  GstCaps *caps;

  caps = gst_caps_new_empty_simple ("test/test");

  tee = gst_element_factory_make ("tee", NULL);
  fail_unless (tee != NULL);
  teesink = gst_element_get_static_pad (tee, "sink");
  fail_unless (teesink != NULL);
  teesrc1 = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (teesrc1 != NULL);
  teesrc2 = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (teesrc2 != NULL);

  GST_DEBUG ("Creating mysink1");
  mysink1 = gst_pad_new ("mysink1", GST_PAD_SINK);
  gst_pad_set_chain_function (mysink1, _fake_chain);
  gst_pad_set_active (mysink1, TRUE);

  GST_DEBUG ("Creating mysink2");
  mysink2 = gst_pad_new ("mysink2", GST_PAD_SINK);
  gst_pad_set_chain_function (mysink2, _fake_chain);
  gst_pad_set_active (mysink2, TRUE);

  GST_DEBUG ("Creating mysrc");
  mysrc = gst_pad_new ("mysrc", GST_PAD_SRC);
  gst_pad_set_active (mysrc, TRUE);

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrc, gst_event_new_stream_start ("test"));
  gst_pad_set_caps (mysrc, caps);
  gst_pad_push_event (mysrc, gst_event_new_segment (&segment));

  fail_unless (gst_pad_link (mysrc, teesink) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (teesrc1, mysink1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (teesrc2, mysink2) == GST_PAD_LINK_OK);

  fail_unless (gst_element_set_state (tee,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  buffer = gst_buffer_new ();
#if 0
  gst_buffer_set_caps (buffer, caps);
#endif

  GST_DEBUG ("Try to push a buffer");
  /* First check if everything works in normal state */
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  /* One pad being in wrong state must result in wrong state */
  GST_DEBUG ("Trying to push with mysink2 disabled");
  gst_pad_set_active (mysink2, FALSE);
  fail_unless (gst_pad_push (mysrc,
          gst_buffer_ref (buffer)) == GST_FLOW_FLUSHING);

  GST_DEBUG ("Trying to push with mysink2 disabled");
  gst_pad_set_active (mysink1, FALSE);
  gst_pad_set_active (mysink2, TRUE);
  fail_unless (gst_pad_push (mysrc,
          gst_buffer_ref (buffer)) == GST_FLOW_FLUSHING);

  GST_DEBUG ("Trying to push with mysink2 and mysink1 disabled");
  gst_pad_set_active (mysink2, FALSE);
  fail_unless (gst_pad_push (mysrc,
          gst_buffer_ref (buffer)) == GST_FLOW_FLUSHING);

  /* Test if everything still works in normal state */
  GST_DEBUG ("Reactivate both pads and try pushing");
  gst_pad_set_active (mysink1, TRUE);
  gst_pad_set_active (mysink2, TRUE);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  /* One unlinked pad must return OK, two unlinked pads must return NOT_LINKED */
  GST_DEBUG ("Pushing with mysink1 unlinked");
  fail_unless (gst_pad_unlink (teesrc1, mysink1) == TRUE);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  GST_DEBUG ("Pushing with mysink2 unlinked");
  fail_unless (gst_pad_link (teesrc1, mysink1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_unlink (teesrc2, mysink2) == TRUE);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  GST_DEBUG ("Pushing with mysink1 AND mysink2 unlinked");
  fail_unless (gst_pad_unlink (teesrc1, mysink1) == TRUE);
  fail_unless (gst_pad_push (mysrc,
          gst_buffer_ref (buffer)) == GST_FLOW_NOT_LINKED);

  /* Test if everything still works in normal state */
  GST_DEBUG ("Relink both pads and try pushing");
  fail_unless (gst_pad_link (teesrc1, mysink1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (teesrc2, mysink2) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  /* One pad returning ERROR should result in ERROR */
  GST_DEBUG ("Pushing with mysink1 returning GST_FLOW_ERROR");
  gst_pad_set_chain_function (mysink1, _fake_chain_error);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_ERROR);

  GST_DEBUG ("Pushing with mysink2 returning GST_FLOW_ERROR");
  gst_pad_set_chain_function (mysink1, _fake_chain);
  gst_pad_set_chain_function (mysink2, _fake_chain_error);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_ERROR);

  GST_DEBUG ("Pushing with mysink1 AND mysink2 returning GST_FLOW_ERROR");
  gst_pad_set_chain_function (mysink1, _fake_chain_error);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_ERROR);

  /* And now everything still needs to work */
  GST_DEBUG ("Try pushing with everything ok");
  gst_pad_set_chain_function (mysink1, _fake_chain);
  gst_pad_set_chain_function (mysink2, _fake_chain);
  fail_unless (gst_pad_push (mysrc, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  fail_unless (gst_element_set_state (tee,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (gst_pad_unlink (mysrc, teesink) == TRUE);
  fail_unless (gst_pad_unlink (teesrc1, mysink1) == TRUE);
  fail_unless (gst_pad_unlink (teesrc2, mysink2) == TRUE);


  gst_object_unref (teesink);
  gst_object_unref (teesrc1);
  gst_object_unref (teesrc2);
  gst_element_release_request_pad (tee, teesrc1);
  gst_element_release_request_pad (tee, teesrc2);
  gst_object_unref (tee);

  gst_object_unref (mysink1);
  gst_object_unref (mysink2);
  gst_object_unref (mysrc);
  gst_caps_unref (caps);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_request_pads)
{
  GstElement *tee;
  GstPad *srcpad1, *srcpad2, *srcpad3, *srcpad4;

  tee = gst_check_setup_element ("tee");

  srcpad1 = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (srcpad1 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (srcpad1), "src_0");
  srcpad2 = gst_element_request_pad_simple (tee, "src_100");
  fail_unless (srcpad2 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (srcpad2), "src_100");
  srcpad3 = gst_element_request_pad_simple (tee, "src_10");
  fail_unless (srcpad3 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (srcpad3), "src_10");
  srcpad4 = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (srcpad4 != NULL);

  gst_object_unref (srcpad1);
  gst_object_unref (srcpad2);
  gst_object_unref (srcpad3);
  gst_object_unref (srcpad4);
  gst_object_unref (tee);
}

GST_END_TEST;

GST_START_TEST (test_allow_not_linked)
{
  GstElement *tee;
  GstPad *src1, *src2;
  GstBuffer *buffer;
  GstPad *srcpad;
  GstCaps *caps;
  GstSegment segment;

  static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  caps = gst_caps_new_empty_simple ("test/test");

  tee = gst_check_setup_element ("tee");
  fail_unless (tee);
  g_object_set (tee, "allow-not-linked", TRUE, NULL);

  srcpad = gst_check_setup_src_pad (tee, &srctemplate);
  gst_pad_set_active (srcpad, TRUE);

  gst_pad_push_event (srcpad, gst_event_new_stream_start ("test"));
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (srcpad, gst_event_new_stream_start ("test"));
  gst_pad_set_caps (srcpad, caps);
  gst_caps_unref (caps);
  gst_pad_push_event (srcpad, gst_event_new_segment (&segment));

  fail_unless (gst_element_set_state (tee,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  buffer = gst_buffer_new ();
  fail_unless (buffer);

  fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  src1 = gst_element_request_pad_simple (tee, "src_%u");

  fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  src2 = gst_element_request_pad_simple (tee, "src_%u");

  fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);

  g_object_set (tee, "allow-not-linked", FALSE, NULL);

  fail_unless (gst_pad_push (srcpad,
          gst_buffer_ref (buffer)) == GST_FLOW_NOT_LINKED);

  gst_element_release_request_pad (tee, src1);

  fail_unless (gst_pad_push (srcpad,
          gst_buffer_ref (buffer)) == GST_FLOW_NOT_LINKED);

  gst_element_release_request_pad (tee, src2);
  gst_object_unref (src1);
  gst_object_unref (src2);

  fail_unless (gst_pad_push (srcpad,
          gst_buffer_ref (buffer)) == GST_FLOW_NOT_LINKED);

  gst_pad_set_active (srcpad, FALSE);
  gst_check_teardown_src_pad (tee);
  gst_check_teardown_element (tee);

  fail_if (buffer->mini_object.refcount != 1);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

static gboolean
allocation_query_empty (GstPad * pad, GstObject * parent, GstQuery * query)
{
  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return gst_pad_query_default (pad, parent, query);

  return TRUE;
}

static gboolean
allocation_query1 (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAllocationParams param = { 0, 15, 1, 1 };

  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return gst_pad_query_default (pad, parent, query);

  gst_query_add_allocation_pool (query, NULL, 128, 2, 10);
  gst_query_add_allocation_param (query, NULL, &param);
  gst_query_add_allocation_meta (query, GST_PARENT_BUFFER_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_REFERENCE_TIMESTAMP_META_API_TYPE,
      NULL);
  gst_query_add_allocation_meta (query, GST_PROTECTION_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
allocation_query2 (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstAllocationParams param = { 0, 7, 2, 1 };

  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return gst_pad_query_default (pad, parent, query);

  gst_query_add_allocation_pool (query, NULL, 129, 1, 15);
  gst_query_add_allocation_param (query, NULL, &param);
  gst_query_add_allocation_meta (query, GST_PARENT_BUFFER_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_REFERENCE_TIMESTAMP_META_API_TYPE,
      NULL);
  gst_query_add_allocation_meta (query, GST_PROTECTION_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
allocation_query3 (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstStructure *s;
  GstAllocationParams param = { 0, 7, 1, 2 };

  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return gst_pad_query_default (pad, parent, query);

  gst_query_add_allocation_pool (query, NULL, 130, 1, 20);
  gst_query_add_allocation_param (query, NULL, &param);
  gst_query_add_allocation_meta (query, GST_PARENT_BUFFER_META_API_TYPE, NULL);
  s = gst_structure_new_empty ("test/test");
  gst_query_add_allocation_meta (query, GST_PROTECTION_META_API_TYPE, s);
  gst_structure_free (s);

  return TRUE;
}

static gboolean
allocation_query_fail (GstPad * pad, GstObject * parent, GstQuery * query)
{
  if (GST_QUERY_TYPE (query) != GST_QUERY_ALLOCATION)
    return gst_pad_query_default (pad, parent, query);

  return FALSE;
}

static void
add_sink_pad_and_setup_query_func (GstElement * tee,
    GstPadQueryFunction query_func)
{
  GstPad *sink;
  static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  sink = gst_check_setup_sink_pad_by_name (tee, &sinktemplate, "src_%u");
  fail_unless (sink != NULL);
  gst_pad_set_query_function (sink, query_func);
}

GST_START_TEST (test_allocation_query_aggregation)
{
  GstElement *tee;
  GstPad *sinkpad;
  GstCaps *caps;
  GstQuery *query;
  guint size, min, max;
  GstAllocationParams param;

  tee = gst_check_setup_element ("tee");
  fail_unless (tee);

  sinkpad = gst_element_get_static_pad (tee, "sink");
  add_sink_pad_and_setup_query_func (tee, allocation_query1);
  add_sink_pad_and_setup_query_func (tee, allocation_query2);
  add_sink_pad_and_setup_query_func (tee, allocation_query3);

  caps = gst_caps_new_empty_simple ("test/test");
  query = gst_query_new_allocation (caps, TRUE);
  fail_unless (gst_pad_query (sinkpad, query));

  ck_assert_int_eq (gst_query_get_n_allocation_pools (query), 1);
  gst_query_parse_nth_allocation_pool (query, 0, NULL, &size, &min, &max);
  fail_unless (size == 130);
  /* The tee will allocate one more buffer when multiplexing */
  fail_unless (min == 2 + 1);
  fail_unless (max == 0);

  fail_unless (gst_query_get_n_allocation_params (query), 1);
  gst_query_parse_nth_allocation_param (query, 0, NULL, &param);
  fail_unless (param.align == 15);
  fail_unless (param.prefix == 2);
  fail_unless (param.padding == 2);

  fail_unless (gst_query_get_n_allocation_metas (query), 1);
  fail_unless (gst_query_parse_nth_allocation_meta (query, 0, NULL) ==
      GST_PARENT_BUFFER_META_API_TYPE);

  gst_caps_unref (caps);
  gst_query_unref (query);
  gst_check_teardown_pad_by_name (tee, "src_0");
  gst_check_teardown_pad_by_name (tee, "src_1");
  gst_check_teardown_pad_by_name (tee, "src_2");
  gst_object_unref (sinkpad);
  gst_check_teardown_element (tee);
}

GST_END_TEST;


GST_START_TEST (test_allocation_query_allow_not_linked)
{
  GstElement *tee;
  GstPad *sinkpad, *srcpad;
  GstCaps *caps;
  GstQuery *query;

  tee = gst_check_setup_element ("tee");
  fail_unless (tee);

  sinkpad = gst_element_get_static_pad (tee, "sink");
  add_sink_pad_and_setup_query_func (tee, allocation_query1);
  add_sink_pad_and_setup_query_func (tee, allocation_query2);
  add_sink_pad_and_setup_query_func (tee, allocation_query3);
  /* This unlinked pad is what will make a difference between having
   * allow-not-linked set or not */
  srcpad = gst_element_request_pad_simple (tee, "src_%u");
  caps = gst_caps_new_empty_simple ("test/test");

  /* Without allow-not-linked the query should fail */
  query = gst_query_new_allocation (caps, TRUE);
  fail_if (gst_pad_query (sinkpad, query));

  /* While with allow-not-linked it should succeed (ignoring that pad) */
  g_object_set (tee, "allow-not-linked", TRUE, NULL);
  gst_query_unref (query);
  query = gst_query_new_allocation (caps, TRUE);
  fail_unless (gst_pad_query (sinkpad, query));

  gst_caps_unref (caps);
  gst_query_unref (query);
  gst_check_teardown_pad_by_name (tee, "src_0");
  gst_check_teardown_pad_by_name (tee, "src_1");
  gst_check_teardown_pad_by_name (tee, "src_2");
  gst_element_release_request_pad (tee, srcpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
  gst_check_teardown_element (tee);
}

GST_END_TEST;


GST_START_TEST (test_allocation_query_failure)
{
  GstElement *tee;
  GstPad *sinkpad;
  GstCaps *caps;
  GstQuery *query;

  tee = gst_check_setup_element ("tee");
  fail_unless (tee);
  g_object_set (tee, "allow-not-linked", TRUE, NULL);

  sinkpad = gst_element_get_static_pad (tee, "sink");
  add_sink_pad_and_setup_query_func (tee, allocation_query1);
  add_sink_pad_and_setup_query_func (tee, allocation_query2);
  add_sink_pad_and_setup_query_func (tee, allocation_query_fail);

  caps = gst_caps_new_empty_simple ("test/test");
  query = gst_query_new_allocation (caps, TRUE);
  fail_if (gst_pad_query (sinkpad, query));

  gst_caps_unref (caps);
  gst_query_unref (query);
  gst_check_teardown_pad_by_name (tee, "src_0");
  gst_check_teardown_pad_by_name (tee, "src_1");
  gst_check_teardown_pad_by_name (tee, "src_2");
  gst_object_unref (sinkpad);
  gst_check_teardown_element (tee);
}

GST_END_TEST;


GST_START_TEST (test_allocation_query_empty)
{
  GstElement *tee;
  GstPad *sinkpad;
  GstCaps *caps;
  GstQuery *query;

  tee = gst_check_setup_element ("tee");
  fail_unless (tee);

  sinkpad = gst_element_get_static_pad (tee, "sink");
  add_sink_pad_and_setup_query_func (tee, allocation_query_empty);

  caps = gst_caps_new_empty_simple ("test/test");

  query = gst_query_new_allocation (caps, TRUE);
  fail_unless (gst_pad_query (sinkpad, query));

  ck_assert_int_eq (gst_query_get_n_allocation_pools (query), 0);
  ck_assert_int_eq (gst_query_get_n_allocation_params (query), 0);

  gst_caps_unref (caps);
  gst_query_unref (query);
  gst_check_teardown_pad_by_name (tee, "src_0");
  gst_object_unref (sinkpad);
  gst_check_teardown_element (tee);
}

GST_END_TEST;


static Suite *
tee_suite (void)
{
  Suite *s = suite_create ("tee");
  TCase *tc_chain = tcase_create ("general");

  /* Set the timeout to a much larger time - 3 minutes */
  tcase_set_timeout (tc_chain, 180);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_stress);
  tcase_add_test (tc_chain, test_release_while_buffer_alloc);
  tcase_add_test (tc_chain, test_release_while_second_buffer_alloc);
  tcase_add_test (tc_chain, test_internal_links);
  tcase_add_test (tc_chain, test_flow_aggregation);
  tcase_add_test (tc_chain, test_request_pads);
  tcase_add_test (tc_chain, test_allow_not_linked);
  tcase_add_test (tc_chain, test_allocation_query_aggregation);
  tcase_add_test (tc_chain, test_allocation_query_allow_not_linked);
  tcase_add_test (tc_chain, test_allocation_query_failure);
  tcase_add_test (tc_chain, test_allocation_query_empty);

  return s;
}

GST_CHECK_MAIN (tee);

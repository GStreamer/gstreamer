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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

    req_pads[i] = gst_element_get_request_pad (tee, "src%d");
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
  gchar *desc;
  GstBus *bus;
  GstMessage *msg;
  gint i;

  /* Pump 1000 buffers (10 bytes each) per second through tee for 5 secs */
  desc = "fakesrc datarate=10000 sizemin=10 sizemax=10 num-buffers=5000 ! "
      "video/x-raw-rgb,framerate=25/1 ! tee name=t ! "
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

    pad = gst_element_get_request_pad (tee, "src%d");
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

static GstFlowReturn
final_sinkpad_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf);

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

void
buffer_alloc_harness_setup (BufferAllocHarness * h, gint countdown)
{
  h->tee = gst_check_setup_element ("tee");
  fail_if (h->tee == NULL);

  h->countdown = countdown;

  fail_unless_equals_int (gst_element_set_state (h->tee, GST_STATE_PLAYING),
      TRUE);

  h->caps = gst_caps_new_simple ("video/x-raw-yuv", NULL);

  h->start_srcpad = gst_pad_new ("src", GST_PAD_SRC);
  fail_if (h->start_srcpad == NULL);
  fail_unless (gst_pad_set_caps (h->start_srcpad, h->caps) == TRUE);
  fail_unless (gst_pad_set_active (h->start_srcpad, TRUE) == TRUE);

  h->tee_sinkpad = gst_element_get_static_pad (h->tee, "sink");
  fail_if (h->tee_sinkpad == NULL);

  h->tee_srcpad = gst_element_get_request_pad (h->tee, "src%d");
  fail_if (h->tee_srcpad == NULL);

  h->final_sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  fail_if (h->final_sinkpad == NULL);
  gst_pad_set_bufferalloc_function (h->final_sinkpad,
      final_sinkpad_bufferalloc);
  fail_unless (gst_pad_set_caps (h->final_sinkpad, h->caps) == TRUE);
  fail_unless (gst_pad_set_active (h->final_sinkpad, TRUE) == TRUE);
  g_object_set_qdata (G_OBJECT (h->final_sinkpad),
      g_quark_from_static_string ("buffer-alloc-harness"), h);

  fail_unless_equals_int (gst_pad_link (h->start_srcpad, h->tee_sinkpad),
      GST_PAD_LINK_OK);
  fail_unless_equals_int (gst_pad_link (h->tee_srcpad, h->final_sinkpad),
      GST_PAD_LINK_OK);
}

void
buffer_alloc_harness_teardown (BufferAllocHarness * h)
{
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

static gpointer
app_thread_func (gpointer data)
{
  BufferAllocHarness *h = data;

  /* Signal that we are about to call release_request_pad(). */
  g_mutex_lock (check_mutex);
  h->app_thread_prepped = TRUE;
  g_cond_signal (check_cond);
  g_mutex_unlock (check_mutex);

  /* Simulate that the app releases the pad while the streaming thread is in
   * buffer_alloc below. */
  gst_element_release_request_pad (h->tee, h->tee_srcpad);

  /* Signal the bufferalloc function below if it's still waiting. */
  g_mutex_lock (check_mutex);
  h->bufferalloc_blocked = FALSE;
  g_cond_signal (check_cond);
  g_mutex_unlock (check_mutex);

  return NULL;
}

static GstFlowReturn
final_sinkpad_bufferalloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  BufferAllocHarness *h;
  GTimeVal deadline;

  h = g_object_get_qdata (G_OBJECT (pad),
      g_quark_from_static_string ("buffer-alloc-harness"));
  g_assert (h != NULL);

  if (--(h->countdown) == 0) {
    /* Time to make the app release the pad. */
    h->app_thread_prepped = FALSE;
    h->bufferalloc_blocked = TRUE;

    h->app_thread = g_thread_create (app_thread_func, h, TRUE, NULL);
    fail_if (h->app_thread == NULL);

    /* Wait for the app thread to get ready to call release_request_pad(). */
    g_mutex_lock (check_mutex);
    while (!h->app_thread_prepped)
      g_cond_wait (check_cond, check_mutex);
    g_mutex_unlock (check_mutex);

    /* Now wait for it to do that within a second, to avoid deadlocking
     * in the event of future changes to the locking semantics. */
    g_mutex_lock (check_mutex);
    g_get_current_time (&deadline);
    deadline.tv_sec += 1;
    while (h->bufferalloc_blocked) {
      if (!g_cond_timed_wait (check_cond, check_mutex, &deadline))
        break;
    }
    g_mutex_unlock (check_mutex);
  }

  *buf = gst_buffer_new_and_alloc (size);
  gst_buffer_set_caps (*buf, caps);

  return GST_FLOW_OK;
}

/* Simulate an app releasing the pad while the first alloc_buffer() is in
 * progress. */
GST_START_TEST (test_release_while_buffer_alloc)
{
  BufferAllocHarness h;
  GstBuffer *buf;

  buffer_alloc_harness_setup (&h, 1);

  fail_unless_equals_int (gst_pad_alloc_buffer (h.start_srcpad, 0, 1, h.caps,
          &buf), GST_FLOW_OK);
  gst_buffer_unref (buf);

  buffer_alloc_harness_teardown (&h);
}

GST_END_TEST;

/* Simulate an app releasing the pad while the second alloc_buffer() is in
 * progress. */
GST_START_TEST (test_release_while_second_buffer_alloc)
{
  BufferAllocHarness h;
  GstBuffer *buf;

  buffer_alloc_harness_setup (&h, 2);

  fail_unless_equals_int (gst_pad_alloc_buffer (h.start_srcpad, 0, 1, h.caps,
          &buf), GST_FLOW_OK);
  gst_buffer_unref (buf);

  fail_unless_equals_int (gst_pad_alloc_buffer (h.start_srcpad, 0, 1, h.caps,
          &buf), GST_FLOW_OK);
  gst_buffer_unref (buf);

  buffer_alloc_harness_teardown (&h);
}

GST_END_TEST;

static Suite *
tee_suite (void)
{
  Suite *s = suite_create ("tee");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_stress);
  tcase_add_test (tc_chain, test_release_while_buffer_alloc);
  tcase_add_test (tc_chain, test_release_while_second_buffer_alloc);

  return s;
}

GST_CHECK_MAIN (tee);

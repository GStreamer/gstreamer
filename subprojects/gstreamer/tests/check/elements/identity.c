/* GStreamer
 *
 * unit test for identity
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2015> Havard Graff           <havard@pexip.com>
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
#include <gst/check/gstharness.h>

GST_START_TEST (test_one_buffer)
{
  GstHarness *h = gst_harness_new ("identity");
  GstBuffer *buffer_in;
  GstBuffer *buffer_out;

  gst_harness_set_src_caps_str (h, "mycaps");

  buffer_in = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer_in, "buffer", 1);

  gst_buffer_fill (buffer_in, 0, "data", 4);

  /* pushing gives away my reference ... */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer_in));

  /* ... but it should end up being collected on GstHarness queue */
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  buffer_out = gst_harness_pull (h);

  fail_unless (buffer_in == buffer_out);
  ASSERT_BUFFER_REFCOUNT (buffer_out, "buffer", 1);

  /* cleanup */
  gst_buffer_unref (buffer_out);
  gst_harness_teardown (h);
}

GST_END_TEST;

static void
handoff_func (GstElement * identity, GstBuffer * buf, GstBuffer ** ret)
{
  (void) identity;
  *ret = buf;
}

GST_START_TEST (test_signal_handoffs)
{
  GstHarness *h = gst_harness_new ("identity");
  GstBuffer *buffer_in;
  GstBuffer *buffer_signaled = NULL;
  gst_harness_set_src_caps_str (h, "mycaps");

  /* connect to the handoff signal */
  g_signal_connect (h->element, "handoff",
      G_CALLBACK (handoff_func), &buffer_signaled);

  /* first, turn off signal-handoffs */
  g_object_set (h->element, "signal-handoffs", FALSE, NULL);

  /* then push a buffer */
  buffer_in = gst_buffer_new_and_alloc (4);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer_in));

  /* verify that we got no buffer signaled */
  fail_unless (buffer_signaled == NULL);

  /* now turn on signal-handoffs */
  g_object_set (h->element, "signal-handoffs", TRUE, NULL);

  /* then push another buffer */
  buffer_in = gst_buffer_new_and_alloc (4);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer_in));

  /* verify the buffer signaled is equal to the one pushed in */
  fail_unless (buffer_signaled == buffer_in);
  ASSERT_BUFFER_REFCOUNT (buffer_signaled, "buffer", 1);

  /* cleanup */
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_sync_on_timestamp)
{
  /* the reason to use the queue in front of the identity element
     is to effectively make gst_harness_push asynchronous, not locking
     up the test, waiting for gst_clock_id_wait */
  GstHarness *h = gst_harness_new_parse ("queue ! identity sync=1");
  GstBuffer *buf;
  GstClock *clock;
  GstClockTime timestamp = 123456789;

  /* use testclock */
  gst_harness_use_testclock (h);
  gst_harness_set_src_caps_str (h, "mycaps");

  /* make a buffer and set the timestamp */
  buf = gst_buffer_new ();
  GST_BUFFER_PTS (buf) = timestamp;

  /* push the buffer, and verify it does *not* make it through */
  gst_harness_push (h, buf);
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  /* verify the identity element has registered exactly one GstClockID */
  fail_unless (gst_harness_wait_for_clock_id_waits (h, 1, 42));

  /* crank the clock and pull the buffer */
  gst_harness_crank_single_clock_wait (h);
  buf = gst_harness_pull (h);

  /* verify that the buffer has the right timestamp, and that the time on
     the clock is equal to the timestamp */
  fail_unless_equals_int64 (timestamp, GST_BUFFER_PTS (buf));
  clock = gst_element_get_clock (h->element);
  fail_unless_equals_int64 (timestamp, gst_clock_get_time (clock));

  /* cleanup */
  gst_object_unref (clock);
  gst_buffer_unref (buf);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_stopping_element_unschedules_sync)
{
  /* the reason to use the queue in front of the identity element
     is to effectively make gst_harness_push asynchronous, not locking
     up the test, waiting for gst_clock_id_wait */
  GstHarness *h = gst_harness_new_parse ("queue ! identity sync=1");
  GstBuffer *buf;
  GstClockTime timestamp = 123456789;

  /* use testclock */
  gst_harness_use_testclock (h);
  gst_harness_set_src_caps_str (h, "mycaps");

  /* make a buffer and set the timestamp */
  buf = gst_buffer_new ();
  GST_BUFFER_PTS (buf) = timestamp;

  /* push the buffer, and verify it does *not* make it through */
  gst_harness_push (h, buf);
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  /* verify the identity element has registered exactly one GstClockID */
  fail_unless (gst_harness_wait_for_clock_id_waits (h, 1, 42));

  /* setting identity to READY should unschedule the sync */
  gst_element_set_state (h->element, GST_STATE_READY);

  /* verify the identity element no longer waits on the clock */
  fail_unless (gst_harness_wait_for_clock_id_waits (h, 0, 42));

  /* and that the waiting buffer was dropped */
  fail_unless_equals_int (0, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
identity_suite (void)
{
  Suite *s = suite_create ("identity");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_one_buffer);
  tcase_add_test (tc_chain, test_signal_handoffs);
  tcase_add_test (tc_chain, test_sync_on_timestamp);
  tcase_add_test (tc_chain, test_stopping_element_unschedules_sync);


  return s;
}

GST_CHECK_MAIN (identity);

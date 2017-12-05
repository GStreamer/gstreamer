/*
 * Tests and examples of GstHarness
 *
 * Copyright (C) 2015 Havard Graff <havard@pexip.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

static void
create_destroy_element_harness (gpointer data, gpointer user_data)
{
  GstElement *element = user_data;
  GstHarness *h = gst_harness_new_with_element (element, NULL, NULL);
  gst_harness_teardown (h);
}

GST_START_TEST (test_harness_element_ref)
{
  GstHarness *h = gst_harness_new ("identity");
  GstHarnessThread *threads[100];
  gint i;

  for (i = 0; i < G_N_ELEMENTS (threads); i++)
    threads[i] = gst_harness_stress_custom_start (h, NULL,
        create_destroy_element_harness, h->element, 0);
  for (i = 0; i < G_N_ELEMENTS (threads); i++)
    gst_harness_stress_thread_stop (threads[i]);

  fail_unless_equals_int (G_OBJECT (h->element)->ref_count, 1);

  gst_harness_teardown (h);
}

GST_END_TEST;


GST_START_TEST (test_src_harness)
{
  GstHarness *h = gst_harness_new ("identity");

  /* add a fakesrc that syncs to the clock and a
     capsfilter that adds some caps to it */
  gst_harness_add_src_parse (h,
      "fakesrc sync=1 ! capsfilter caps=\"mycaps\"", TRUE);

  /* this cranks the clock and transfers the resulting buffer
     from the src-harness into the identity element */
  gst_harness_push_from_src (h);

  /* verify that identity outputs a buffer by pulling and unreffing */
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_src_harness_no_forwarding)
{
  GstHarness *h = gst_harness_new ("identity");

  /* turn of forwarding of necessary events */
  gst_harness_set_forwarding (h, FALSE);

  /* add a fakesrc that syncs to the clock and a
     capsfilter that adds some caps to it */
  gst_harness_add_src_parse (h,
      "fakesrc sync=1 ! capsfilter caps=\"mycaps\"", TRUE);

  /* start the fakesrc to produce the first events */
  gst_harness_play (h->src_harness);

  /* transfer STREAM_START event */
  gst_harness_src_push_event (h);

  /* crank the clock to produce the CAPS and SEGMENT events */
  gst_harness_crank_single_clock_wait (h->src_harness);

  /* transfer CAPS event */
  gst_harness_src_push_event (h);

  /* transfer SEGMENT event */
  gst_harness_src_push_event (h);

  /* now transfer the buffer produced by exploiting
     the ability to say 0 cranks but 1 push */
  gst_harness_src_crank_and_push_many (h, 0, 1);

  /* and verify that the identity element outputs it */
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_add_sink_harness_without_sinkpad)
{
  GstHarness *h = gst_harness_new ("fakesink");

  gst_harness_add_sink (h, "fakesink");

  gst_harness_teardown (h);
}

GST_END_TEST;

static GstEvent *
create_new_stream_start_event (GstHarness * h, gpointer data)
{
  guint *counter = data;
  gchar *stream_id = g_strdup_printf ("streamid/%d", *counter);
  GstEvent *event = gst_event_new_stream_start (stream_id);
  g_free (stream_id);
  (*counter)++;
  return event;
}

static void
push_query (gpointer data, gpointer user_data)
{
  GstHarness *h = user_data;
  GstCaps *caps = gst_caps_new_empty_simple ("mycaps");
  GstQuery *query = gst_query_new_allocation (caps, FALSE);
  gst_caps_unref (caps);
  gst_pad_peer_query (h->srcpad, query);
  gst_query_unref (query);
}

GST_START_TEST (test_forward_event_and_query_to_sink_harness_while_teardown)
{
  GstHarness *h = gst_harness_new ("identity");
  guint counter = 0;
  GstHarnessThread *e_thread = gst_harness_stress_push_event_with_cb_start (h,
      create_new_stream_start_event, &counter, NULL);
  GstHarnessThread *q_thread = gst_harness_stress_custom_start (h, NULL,
      push_query, h, 0);
  gdouble duration = 1.0;
  GTimer *timer = g_timer_new ();

  while (g_timer_elapsed (timer, NULL) < duration) {
    gst_harness_add_sink (h, "fakesink");
    g_thread_yield ();
  }

  g_timer_destroy (timer);
  gst_harness_stress_thread_stop (q_thread);
  gst_harness_stress_thread_stop (e_thread);
  gst_harness_teardown (h);
}

GST_END_TEST;

static GstHarness *
harness_new_and_fill_with_data (void)
{
  GstHarness *h = gst_harness_new_parse ("fakesrc num-buffers=5 "
      "filltype=pattern-span sizetype=fixed sizemin=10 sizemax=10");
  gboolean have_eos = FALSE;

  gst_harness_play (h);

  do {
    GstEvent *e = gst_harness_pull_event (h);
    have_eos = GST_EVENT_TYPE (e) == GST_EVENT_EOS;
    gst_event_unref (e);
  } while (!have_eos);

  return h;
}

GST_START_TEST (test_get_all_data)
{
  guint8 expected[50];
  const guint8 *cdata;
  GstHarness *h;
  GstBuffer *buf;
  GBytes *bytes;
  guint8 *data;
  gsize size;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (expected); ++i)
    expected[i] = i;

  h = harness_new_and_fill_with_data ();
  buf = gst_harness_take_all_data_as_buffer (h);
  fail_unless (buf != NULL);
  fail_unless_equals_int (gst_buffer_get_size (buf), 5 * 10);
  fail_unless (gst_buffer_memcmp (buf, 0, expected, 5 * 10) == 0);
  gst_buffer_unref (buf);
  /* There should be nothing left now. We should still get a non-NULL buffer */
  buf = gst_harness_take_all_data_as_buffer (h);
  fail_unless (buf != NULL);
  fail_unless_equals_int (gst_buffer_get_size (buf), 0);
  gst_buffer_unref (buf);
  gst_harness_teardown (h);

  h = harness_new_and_fill_with_data ();
  bytes = gst_harness_take_all_data_as_bytes (h);
  fail_unless (bytes != NULL);
  cdata = g_bytes_get_data (bytes, &size);
  fail_unless_equals_int (size, 5 * 10);
  fail_unless (memcmp (cdata, expected, 50) == 0);
  g_bytes_unref (bytes);
  /* There should be nothing left now. We should still get a non-NULL bytes */
  bytes = gst_harness_take_all_data_as_bytes (h);
  fail_unless (bytes != NULL);
  cdata = g_bytes_get_data (bytes, &size);
  fail_unless (cdata == NULL);
  fail_unless_equals_int (size, 0);
  g_bytes_unref (bytes);
  gst_harness_teardown (h);

  h = harness_new_and_fill_with_data ();
  data = gst_harness_take_all_data (h, &size);
  fail_unless (data != NULL);
  fail_unless_equals_int (size, 5 * 10);
  fail_unless (memcmp (data, expected, 50) == 0);
  g_free (data);
  /* There should be nothing left now. */
  data = gst_harness_take_all_data (h, &size);
  fail_unless (data == NULL);
  fail_unless_equals_int (size, 0);
  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
gst_harness_suite (void)
{
  Suite *s = suite_create ("GstHarness");
  TCase *tc_chain = tcase_create ("harness");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_harness_element_ref);
  tcase_add_test (tc_chain, test_src_harness);
  tcase_add_test (tc_chain, test_src_harness_no_forwarding);
  tcase_add_test (tc_chain, test_add_sink_harness_without_sinkpad);

  tcase_add_test (tc_chain,
      test_forward_event_and_query_to_sink_harness_while_teardown);
  tcase_add_test (tc_chain, test_get_all_data);

  return s;
}

GST_CHECK_MAIN (gst_harness);

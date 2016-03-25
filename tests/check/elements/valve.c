/* GStreamer
 *
 * unit test for the valve element
 *
 * Copyright 2009 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2009 Nokia Corp.
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
#include <gst/check/gstharness.h>
#include <gst/gst.h>

GST_START_TEST (test_valve_basic)
{
  GstHarness *h = gst_harness_new ("valve");

  gst_harness_set_src_caps_str (h, "mycaps");

  /* when not dropping, we don't drop buffers.... */
  g_object_set (h->element, "drop", FALSE, NULL);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, gst_buffer_new ()));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, gst_buffer_new ()));
  fail_unless_equals_int (2, gst_harness_buffers_received (h));

  /* when dropping, the buffers don't make it through */
  g_object_set (h->element, "drop", TRUE, NULL);
  fail_unless_equals_int (3, gst_harness_events_received (h));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, gst_buffer_new ()));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, gst_buffer_new ()));
  fail_unless_equals_int (2, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_valve_upstream_events_dont_send_sticky)
{
  GstHarness *h = gst_harness_new ("valve");

  /* set to drop */
  g_object_set (h->element, "drop", TRUE, NULL);

  /* set caps to trigger sticky-events being pushed to valve */
  gst_harness_set_src_caps_str (h, "mycaps");

  /* verify no events have made it through yet */
  fail_unless_equals_int (0, gst_harness_events_received (h));

  /* stop dropping */
  g_object_set (h->element, "drop", FALSE, NULL);

  /* send an upstream event and verify that no
     downstream events was pushed as a result of this */
  gst_harness_push_upstream_event (h, gst_event_new_reconfigure ());
  fail_unless_equals_int (0, gst_harness_events_received (h));

  /* push a buffer, and verify this pushes the sticky events */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, gst_buffer_new ()));
  fail_unless_equals_int (3, gst_harness_events_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
valve_suite (void)
{
  Suite *s = suite_create ("valve");
  TCase *tc_chain;

  tc_chain = tcase_create ("valve_basic");
  tcase_add_test (tc_chain, test_valve_basic);
  tcase_add_test (tc_chain, test_valve_upstream_events_dont_send_sticky);

  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (valve)

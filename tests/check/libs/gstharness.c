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

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

GST_START_TEST(test_src_harness)
{
  GstHarness * h = gst_harness_new ("identity");

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

GST_START_TEST(test_src_harness_no_forwarding)
{
  GstHarness * h = gst_harness_new ("identity");

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

static Suite *
gst_harness_suite (void)
{
  Suite *s = suite_create ("GstHarness");
  TCase *tc_chain = tcase_create ("harness");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_src_harness);
  tcase_add_test (tc_chain, test_src_harness_no_forwarding);

  return s;
}

GST_CHECK_MAIN (gst_harness);

/* GStreamer
 *
 * unit test for sinks
 *
 * Copyright (C) <2005> Wim Taymans <wim at fluendo dot com>
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

/* a sink should go ASYNC to PAUSE and PLAYING. */
GST_START_TEST (test_sink)
{
  GstElement *sink, *src;
  GstStateChangeReturn ret;
  GstState current, pending;
  GTimeVal tv;

  sink = gst_element_factory_make ("fakesink", "sink");

  ret = gst_element_set_state (sink, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "no async state return");

  GST_TIME_TO_TIMEVAL ((GstClockTime) 0, tv);

  ret = gst_element_get_state (sink, &current, &pending, &tv);
  fail_unless (ret == GST_STATE_CHANGE_ASYNC, "not changing state async");
  fail_unless (current == GST_STATE_READY, "bad current state");
  fail_unless (pending == GST_STATE_PLAYING, "bad pending state");

  src = gst_element_factory_make ("fakesrc", "src");
  gst_element_link (src, sink);

  ret = gst_element_set_state (src, GST_STATE_PLAYING);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "no success state return");

  /* now wait for final state */
  ret = gst_element_get_state (sink, &current, &pending, NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to change state");
  fail_unless (current == GST_STATE_PLAYING, "bad current state");
  fail_unless (pending == GST_STATE_VOID_PENDING, "bad pending state");

  ret = gst_element_set_state (sink, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to go to null");

  ret = gst_element_set_state (src, GST_STATE_NULL);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS, "failed to go to null");

  gst_object_unref (sink);
  gst_object_unref (src);
}

GST_END_TEST
/* test: try changing state of sinks */
    Suite * gst_object_suite (void)
{
  Suite *s = suite_create ("Sinks");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_sink);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_object_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

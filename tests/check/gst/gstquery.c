/* GStreamer
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 *
 * gstevent.c: Unit test for event handling
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

GST_START_TEST (test_queries)
{
  GstBin *bin;
  GstElement *src, *sink;
  GstStateChangeReturn ret;
  GstPad *pad;
  GstQuery *dur, *pos;

  fail_unless ((bin = (GstBin *) gst_pipeline_new (NULL)) != NULL,
      "Could not create pipeline");
  fail_unless ((src = gst_element_factory_make ("fakesrc", NULL)) != NULL,
      "Could not create fakesrc");
  g_object_set (src, "datarate", 200, "sizetype", 2, NULL);

  fail_unless ((sink = gst_element_factory_make ("fakesink", NULL)) != NULL,
      "Could not create fakesink");
  g_object_set (sink, "sync", TRUE, NULL);
  fail_unless ((dur = gst_query_new_duration (GST_FORMAT_BYTES)) != NULL,
      "Could not prepare duration query");
  fail_unless ((pos = gst_query_new_position (GST_FORMAT_BYTES)) != NULL,
      "Could not prepare position query");

  fail_unless (gst_bin_add (bin, src), "Could not add src to bin");
  fail_unless (gst_bin_add (bin, sink), "Could not add sink to bin");
  fail_unless (gst_element_link (src, sink), "could not link src and sink");

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);
  fail_if (ret == GST_STATE_CHANGE_FAILURE, "Failed to set pipeline PLAYING");
  if (ret == GST_STATE_CHANGE_ASYNC)
    gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_CLOCK_TIME_NONE);

  /* Query the bin */
  fail_unless (gst_element_query (GST_ELEMENT (bin), pos),
      "Could not query pipeline position");
  fail_unless (gst_element_query (GST_ELEMENT (bin), dur),
      "Could not query pipeline duration");

  /* Query elements */
  fail_unless (gst_element_query (GST_ELEMENT (src), pos),
      "Could not query position of fakesrc");
  fail_unless (gst_element_query (GST_ELEMENT (src), pos),
      "Could not query duration of fakesrc");

  fail_unless (gst_element_query (GST_ELEMENT (sink), pos),
      "Could not query position of fakesink");
  fail_unless (gst_element_query (GST_ELEMENT (sink), pos),
      "Could not query duration of fakesink");

  /* Query pads */
  fail_unless ((pad = gst_element_get_pad (src, "src")) != NULL,
      "Could not get source pad of fakesrc");
  fail_unless (gst_pad_query (pad, pos),
      "Could not query position of fakesrc src pad");
  fail_unless (gst_pad_query (pad, dur),
      "Could not query duration of fakesrc src pad");
  gst_object_unref (pad);

  /* We don't query the sink pad of fakesink, it doesn't 
   * handle downstream queries atm, but it might later, who knows? */

  ret = gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  fail_if (ret == GST_STATE_CHANGE_FAILURE, "Failed to set pipeline NULL");
  if (ret == GST_STATE_CHANGE_ASYNC)
    gst_element_get_state (GST_ELEMENT (bin), NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_query_unref (dur);
  gst_query_unref (pos);
  gst_object_unref (bin);
}

GST_END_TEST;

Suite *
gstquery_suite (void)
{
  Suite *s = suite_create ("GstQuery");
  TCase *tc_chain = tcase_create ("queries");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_queries);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstquery_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

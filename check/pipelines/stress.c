/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * simple_launch_lines.c: Unit test for simple pipelines
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


GST_START_TEST (test_stress)
{
  GstElement *fakesrc, *fakesink, *pipeline;
  gint i;

  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  pipeline = gst_element_factory_make ("pipeline", NULL);

  g_return_if_fail (fakesrc && fakesink && pipeline);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  i = 100;
  while (i--) {
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_READY);
    gst_element_set_state (pipeline, GST_STATE_PAUSED);
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  gst_object_unref (pipeline);
}

GST_END_TEST Suite *
simple_launch_lines_suite (void)
{
  Suite *s = suite_create ("stress");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_stress);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = simple_launch_lines_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

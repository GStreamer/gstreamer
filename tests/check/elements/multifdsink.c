/* GStreamer
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <gst/check/gstcheck.h>

GstPad *mysrcpad;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-gdp")
    );

GstElement *
setup_multifdsink ()
{
  GstElement *multifdsink;

  GST_DEBUG ("setup_multifdsink");
  multifdsink = gst_check_setup_element ("multifdsink");
  mysrcpad = gst_check_setup_src_pad (multifdsink, &srctemplate, NULL);

  return multifdsink;
}

void
cleanup_multifdsink (GstElement * multifdsink)
{
  GST_DEBUG ("cleanup_multifdsink");

  gst_check_teardown_src_pad (multifdsink);
  gst_check_teardown_element (multifdsink);
}

GST_START_TEST (test_no_clients)
{
  GstElement *sink;
  GstBuffer *buffer;

  sink = setup_multifdsink ();

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  GST_DEBUG ("cleaning up multifdsink");
  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_multifdsink (sink);
}

GST_END_TEST;

Suite *
multifdsink_suite (void)
{
  Suite *s = suite_create ("multifdsink");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_no_clients);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = multifdsink_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

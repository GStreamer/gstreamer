/* GStreamer unit tests for the interleave element
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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
# include "config.h"
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_create_and_unref)
{
  GstElement *interleave;

  interleave = gst_element_factory_make ("interleave", NULL);
  fail_unless (interleave != NULL);

  gst_element_set_state (interleave, GST_STATE_NULL);
  gst_object_unref (interleave);
}

GST_END_TEST;

GST_START_TEST (test_request_pads)
{
  GstElement *interleave;
  GstPad *pad1, *pad2;

  interleave = gst_element_factory_make ("interleave", NULL);
  fail_unless (interleave != NULL);

  pad1 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (pad1 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (pad1), "sink0");

  pad2 = gst_element_get_request_pad (interleave, "sink%d");
  fail_unless (pad2 != NULL);
  fail_unless_equals_string (GST_OBJECT_NAME (pad2), "sink1");

  gst_element_release_request_pad (interleave, pad2);
  gst_object_unref (pad2);
  gst_element_release_request_pad (interleave, pad1);
  gst_object_unref (pad1);

  gst_element_set_state (interleave, GST_STATE_NULL);
  gst_object_unref (interleave);
}

GST_END_TEST;

static Suite *
interleave_suite (void)
{
  Suite *s = suite_create ("interleave");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_create_and_unref);
  tcase_add_test (tc_chain, test_request_pads);

  return s;
}

GST_CHECK_MAIN (interleave);

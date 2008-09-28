/* GStreamer unit test for capsfilter
 * Copyright (C) <2008> Tim-Philipp Müller <tim centricular net>
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

#define CAPS_TEMPLATE_STRING            \
    "audio/x-raw-int, "                 \
    "channels = (int) [ 1, 2], "        \
    "rate = (int) [ 1,  MAX ]"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_TEMPLATE_STRING)
    );

GST_START_TEST (test_unfixed_downstream_caps)
{
  GstElement *pipe, *src, *filter;
  GstCaps *filter_caps;
  GstPad *mysinkpad;
  GstMessage *msg;

  pipe = gst_check_setup_element ("pipeline");

  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "sizetype", 2, "sizemax", 1024, "num-buffers", 1, NULL);

  filter = gst_check_setup_element ("capsfilter");
  filter_caps = gst_caps_from_string ("audio/x-raw-int, rate=(int)44100");
  fail_unless (filter_caps != NULL);
  g_object_set (filter, "caps", filter_caps, NULL);

  gst_bin_add_many (GST_BIN (pipe), src, filter, NULL);
  fail_unless (gst_element_link (src, filter));

  mysinkpad = gst_check_setup_sink_pad (filter, &sinktemplate, NULL);
  gst_pad_set_active (mysinkpad, TRUE);

  fail_unless_equals_int (gst_element_set_state (pipe, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  /* wait for error on bus */
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR,
      "Expected ERROR message, got EOS message");
  gst_message_unref (msg);

  /* We don't expect any output buffers unless the check fails */
  fail_unless (buffers == NULL);

  /* cleanup */
  GST_DEBUG ("cleanup");

  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (filter);
  gst_check_teardown_element (pipe);
  gst_caps_unref (filter_caps);
}

GST_END_TEST;

static Suite *
capsfilter_suite (void)
{
  Suite *s = suite_create ("capsfilter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_unfixed_downstream_caps);

  return s;
}

GST_CHECK_MAIN (capsfilter)

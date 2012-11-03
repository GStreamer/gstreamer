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
#include <gst/gst.h>


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw"));

gboolean event_received = FALSE;
gboolean buffer_allocated = FALSE;

static gboolean
event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  event_received = TRUE;
  gst_event_unref (event);
  return TRUE;
}

GST_START_TEST (test_valve_basic)
{
  GstElement *valve;
  GstPad *sink;
  GstPad *src;
  GstCaps *caps, *templ_caps;

  valve = gst_check_setup_element ("valve");

  sink = gst_check_setup_sink_pad_by_name (valve, &sinktemplate, "src");
  src = gst_check_setup_src_pad_by_name (valve, &srctemplate, "sink");
  gst_pad_set_event_function (sink, event_func);
  gst_pad_set_active (src, TRUE);
  gst_pad_set_active (sink, TRUE);
  gst_element_set_state (valve, GST_STATE_PLAYING);

  g_object_set (valve, "drop", FALSE, NULL);

  fail_unless (gst_pad_push_event (src, gst_event_new_eos ()) == TRUE);
  fail_unless (event_received == TRUE);
  fail_unless (gst_pad_push (src, gst_buffer_new ()) == GST_FLOW_EOS);
  fail_unless (gst_pad_push (src, gst_buffer_new ()) == GST_FLOW_EOS);
  fail_unless (buffers == NULL);
  caps = gst_pad_query_caps (src, NULL);
  templ_caps = gst_pad_get_pad_template_caps (src);
  fail_unless (caps && gst_caps_is_equal (caps, templ_caps));
  gst_caps_unref (templ_caps);
  gst_caps_unref (caps);

  gst_check_drop_buffers ();
  fail_unless (gst_pad_push_event (src, gst_event_new_flush_start ()) == TRUE);
  fail_unless (gst_pad_push_event (src,
          gst_event_new_flush_stop (TRUE)) == TRUE);
  event_received = buffer_allocated = FALSE;

  g_object_set (valve, "drop", TRUE, NULL);
  fail_unless (gst_pad_push_event (src, gst_event_new_eos ()) == TRUE);
  fail_unless (event_received == FALSE);
  fail_unless (gst_pad_push (src, gst_buffer_new ()) == GST_FLOW_EOS);
  fail_unless (gst_pad_push (src, gst_buffer_new ()) == GST_FLOW_EOS);
  fail_unless (buffers == NULL);
  caps = gst_pad_query_caps (src, NULL);
  templ_caps = gst_pad_get_pad_template_caps (src);
  fail_unless (caps && gst_caps_is_equal (caps, templ_caps));
  gst_caps_unref (templ_caps);
  gst_caps_unref (caps);

  gst_pad_set_active (src, FALSE);
  gst_pad_set_active (sink, FALSE);
  gst_check_teardown_src_pad (valve);
  gst_check_teardown_sink_pad (valve);
  gst_check_teardown_element (valve);
}

GST_END_TEST;

static Suite *
valve_suite (void)
{
  Suite *s = suite_create ("valve");
  TCase *tc_chain;

  tc_chain = tcase_create ("valve_basic");
  tcase_add_test (tc_chain, test_valve_basic);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (valve)

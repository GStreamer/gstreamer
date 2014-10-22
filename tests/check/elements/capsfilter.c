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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/check/gstcheck.h>

#define CAPS_TEMPLATE_STRING            \
    "audio/x-raw, "                     \
    "channels = (int) [ 1, 2], "        \
    "rate = (int) [ 1,  MAX ]"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_TEMPLATE_STRING)
    );

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_TEMPLATE_STRING)
    );

static GstStaticPadTemplate any_sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate any_srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GList *events = NULL;

static gboolean
test_pad_eventfunc (GstPad * pad, GstObject * parent, GstEvent * event)
{
  events = g_list_append (events, event);
  return TRUE;
}

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
  filter_caps = gst_caps_from_string ("audio/x-raw, rate=(int)44100");
  fail_unless (filter_caps != NULL);
  g_object_set (filter, "caps", filter_caps, NULL);

  gst_bin_add_many (GST_BIN (pipe), src, filter, NULL);
  fail_unless (gst_element_link (src, filter));

  mysinkpad = gst_check_setup_sink_pad (filter, &sinktemplate);
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

GST_START_TEST (test_caps_property)
{
  GstElement *filter;
  GstCaps *filter_caps, *caps;
  const gchar *caps_str;

  filter = gst_check_setup_element ("capsfilter");

  /* verify that the set caps are actually set */
  caps_str = "audio/x-raw, rate=(int)44100, channels=(int)1";

  filter_caps = gst_caps_from_string (caps_str);
  fail_unless (GST_IS_CAPS (filter_caps));
  g_object_set (filter, "caps", filter_caps, NULL);

  g_object_get (filter, "caps", &caps, NULL);
  fail_unless (gst_caps_is_equal (caps, filter_caps));
  gst_caps_unref (caps);
  gst_caps_unref (filter_caps);

  /* verify that new caps set replace the old ones */
  caps_str = "video/x-raw, width=(int)320, height=(int)240";

  filter_caps = gst_caps_from_string (caps_str);
  fail_unless (GST_IS_CAPS (filter_caps));
  g_object_set (filter, "caps", filter_caps, NULL);

  g_object_get (filter, "caps", &caps, NULL);
  fail_unless (gst_caps_is_equal (caps, filter_caps));
  gst_caps_unref (caps);
  gst_caps_unref (filter_caps);

  /* make sure that NULL caps is interpreted as ANY */
  g_object_set (filter, "caps", NULL, NULL);

  g_object_get (filter, "caps", &filter_caps, NULL);
  fail_unless (gst_caps_is_any (filter_caps));
  gst_caps_unref (filter_caps);

  gst_object_unref (filter);
}

GST_END_TEST;

GST_START_TEST (test_caps_query)
{
  GstElement *filter;
  GstCaps *filter_caps;
  const gchar *caps_str;
  GstQuery *query;
  GstCaps *caps;

  filter = gst_check_setup_element ("capsfilter");

  /* set some caps, do a caps query with a filter resulting in no
   * intersecting caps */
  caps_str = "audio/x-raw, rate=(int)44100, channels=(int)1";

  filter_caps = gst_caps_from_string (caps_str);
  fail_unless (GST_IS_CAPS (filter_caps));
  g_object_set (filter, "caps", filter_caps, NULL);
  gst_caps_unref (filter_caps);

  caps_str = "video/x-raw, width=(int)320, height=(int)240";
  filter_caps = gst_caps_from_string (caps_str);
  query = gst_query_new_caps (filter_caps);
  gst_caps_unref (filter_caps);
  fail_unless (gst_element_query (filter, query));
  gst_query_parse_caps_result (query, &caps);
  fail_unless (gst_caps_is_empty (caps));
  gst_query_unref (query);

  gst_object_unref (filter);
}

GST_END_TEST;

GST_START_TEST (test_accept_caps_query)
{
  GstElement *filter;
  GstCaps *filter_caps;
  const gchar *caps_str;
  GstQuery *query;
  gboolean accepted;
  GstPad *sinkpad;
  GstPad *srcpad;

  filter = gst_check_setup_element ("capsfilter");

  /* set some caps on (both pads of) the capsfilter */
  caps_str = "audio/x-raw, rate=(int)44100, channels=(int)1";

  filter_caps = gst_caps_from_string (caps_str);
  fail_unless (GST_IS_CAPS (filter_caps));
  g_object_set (filter, "caps", filter_caps, NULL);
  gst_caps_unref (filter_caps);

  sinkpad = gst_element_get_static_pad (filter, "sink");

  /* check that the set caps are acceptable on the sinkpad */
  caps_str = "audio/x-raw, rate=(int)44100, channels=(int)1";
  filter_caps = gst_caps_from_string (caps_str);
  query = gst_query_new_accept_caps (filter_caps);
  gst_caps_unref (filter_caps);
  fail_unless (gst_pad_query (sinkpad, query));
  gst_query_parse_accept_caps_result (query, &accepted);
  fail_unless (accepted);
  gst_query_unref (query);

  /* and that unrelated caps are not acceptable */
  caps_str = "video/x-raw, width=(int)320, height=(int)240";
  filter_caps = gst_caps_from_string (caps_str);
  query = gst_query_new_accept_caps (filter_caps);
  gst_caps_unref (filter_caps);
  fail_unless (gst_pad_query (sinkpad, query));
  gst_query_parse_accept_caps_result (query, &accepted);
  fail_unless (!accepted);
  gst_query_unref (query);

  gst_object_unref (sinkpad);

  /* now do the same for the src pad (which has the same caps) */
  srcpad = gst_element_get_static_pad (filter, "src");

  caps_str = "audio/x-raw, rate=(int)44100, channels=(int)1";
  filter_caps = gst_caps_from_string (caps_str);
  query = gst_query_new_accept_caps (filter_caps);
  gst_caps_unref (filter_caps);
  fail_unless (gst_pad_query (srcpad, query));
  gst_query_parse_accept_caps_result (query, &accepted);
  fail_unless (accepted);
  gst_query_unref (query);

  caps_str = "video/x-raw, width=(int)320, height=(int)240";
  filter_caps = gst_caps_from_string (caps_str);
  query = gst_query_new_accept_caps (filter_caps);
  gst_caps_unref (filter_caps);
  fail_unless (gst_pad_query (srcpad, query));
  gst_query_parse_accept_caps_result (query, &accepted);
  fail_unless (!accepted);
  gst_query_unref (query);

  gst_object_unref (srcpad);

  gst_object_unref (filter);
}

GST_END_TEST;

GST_START_TEST (test_push_pending_events)
{
  GstElement *filter;
  GstPad *mysinkpad;
  GstPad *mysrcpad;
  GstSegment segment;
  GstTagList *tags;
  GstBuffer *buffer;
  GstEvent *event;
  GstCaps *caps;

  filter = gst_check_setup_element ("capsfilter");
  mysinkpad = gst_check_setup_sink_pad (filter, &sinktemplate);
  gst_pad_set_event_function (mysinkpad, test_pad_eventfunc);
  gst_pad_set_active (mysinkpad, TRUE);
  mysrcpad = gst_check_setup_src_pad (filter, &srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);

  fail_unless_equals_int (gst_element_set_state (filter, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  /* push the stream start */
  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_stream_start ("test-stream")));
  fail_unless (g_list_length (events) == 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* the tag should get trapped as we haven't pushed a caps yet */
  tags = gst_tag_list_new (GST_TAG_COMMENT, "testcomment", NULL);
  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_tag (gst_tag_list_ref (tags))));
  fail_unless (g_list_length (events) == 0);

  /* push a caps */
  caps = gst_caps_from_string ("audio/x-raw, "
      "channels=(int)2, " "rate = (int)44100");
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
  fail_unless (g_list_length (events) == 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CAPS);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless (g_list_length (events) == 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a buffer, the tag should now be pushed downstream */
  buffer = gst_buffer_new_wrapped (g_malloc0 (1024), 1024);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  fail_unless (g_list_length (events) == 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_TAG);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* We don't expect any output buffers unless the check fails */
  fail_unless (g_list_length (buffers) == 1);
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  /* cleanup */
  GST_DEBUG ("cleanup");

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (filter);
  gst_check_teardown_sink_pad (filter);
  gst_check_teardown_element (filter);
  gst_tag_list_unref (tags);
}

GST_END_TEST;

GST_START_TEST (test_caps_change_mode_delayed)
{
  GstElement *filter;
  GstPad *mysinkpad;
  GstPad *mysrcpad;
  GstSegment segment;
  GstEvent *event;
  GstCaps *caps;

  filter = gst_check_setup_element ("capsfilter");
  mysinkpad = gst_check_setup_sink_pad (filter, &any_sinktemplate);
  gst_pad_set_event_function (mysinkpad, test_pad_eventfunc);
  gst_pad_set_active (mysinkpad, TRUE);
  mysrcpad = gst_check_setup_src_pad (filter, &any_srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);

  g_object_set (filter, "caps-change-mode", 1, NULL);

  fail_unless_equals_int (gst_element_set_state (filter, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  /* push the stream start */
  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_stream_start ("test-stream")));
  fail_unless_equals_int (g_list_length (events), 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a caps */
  caps = gst_caps_from_string ("audio/x-raw, "
      "channels=(int)2, " "rate = (int)44100");
  g_object_set (filter, "caps", caps, NULL);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
  fail_unless_equals_int (g_list_length (events), 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CAPS);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (events), 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a buffer */
  fail_unless_equals_int (gst_pad_push (mysrcpad,
          gst_buffer_new_wrapped (g_malloc0 (1024), 1024)), GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  /* Set new incompatible caps */
  caps = gst_caps_from_string ("audio/x-raw, "
      "channels=(int)2, " "rate = (int)48000");
  g_object_set (filter, "caps", caps, NULL);

  /* push a buffer without updating the caps */
  fail_unless_equals_int (gst_pad_push (mysrcpad,
          gst_buffer_new_wrapped (g_malloc0 (1024), 1024)), GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  /* No caps event here, we're still at the old caps */
  fail_unless (g_list_length (events) == 0);

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
  fail_unless_equals_int (g_list_length (events), 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CAPS);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* Push a new buffer, now we have the new caps */
  fail_unless_equals_int (gst_pad_push (mysrcpad,
          gst_buffer_new_wrapped (g_malloc0 (1024), 1024)), GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  /* Set back old caps */
  caps = gst_caps_from_string ("audio/x-raw, "
      "channels=(int)2, " "rate = (int)44100");
  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* push a buffer without updating the caps */
  fail_unless_equals_int (gst_pad_push (mysrcpad,
          gst_buffer_new_wrapped (g_malloc0 (1024), 1024)), GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  /* Now set new caps again but the old caps are currently pushed */
  caps = gst_caps_from_string ("audio/x-raw, "
      "channels=(int)2, " "rate = (int)48000");
  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);
  /* Race condition simulation here! */
  caps = gst_caps_from_string ("audio/x-raw, "
      "channels=(int)2, " "rate = (int)44100");
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
  fail_unless_equals_int (g_list_length (events), 1);
  event = events->data;
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CAPS);
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  fail_unless_equals_int (gst_pad_push (mysrcpad,
          gst_buffer_new_wrapped (g_malloc0 (1024), 1024)), GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  /* cleanup */
  GST_DEBUG ("cleanup");

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (filter);
  gst_check_teardown_sink_pad (filter);
  gst_check_teardown_element (filter);
}

GST_END_TEST;

static Suite *
capsfilter_suite (void)
{
  Suite *s = suite_create ("capsfilter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_unfixed_downstream_caps);
  tcase_add_test (tc_chain, test_caps_property);
  tcase_add_test (tc_chain, test_caps_query);
  tcase_add_test (tc_chain, test_accept_caps_query);
  tcase_add_test (tc_chain, test_push_pending_events);
  tcase_add_test (tc_chain, test_caps_change_mode_delayed);

  return s;
}

GST_CHECK_MAIN (capsfilter)

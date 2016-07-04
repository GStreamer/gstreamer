/* GStreamer
 *
 * Copyright (C) 2009, Axis Communications AB, LUND, SWEDEN
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
#include <gst/app/gstappsink.h>

gint global_testdata;

static GstPad *mysrcpad;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-gst-check")
    );

static GstElement *
setup_appsink (void)
{
  GstElement *appsink;
  GstCaps *caps;

  GST_DEBUG ("setup_appsink");
  appsink = gst_check_setup_element ("appsink");
  mysrcpad = gst_check_setup_src_pad (appsink, &srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);

  caps = gst_caps_new_empty_simple ("application/x-gst-check");
  gst_check_setup_events (mysrcpad, appsink, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  return appsink;
}

static void
cleanup_appsink (GstElement * appsink)
{
  GST_DEBUG ("cleanup_appsink");

  gst_check_teardown_src_pad (appsink);
  gst_check_teardown_element (appsink);
}

/* This function does an operation to it's indata argument and returns it.
 * The exact operation performed doesn't matter. Currently it multiplies with
 * two, but it could do anything. The idea is to use the function to verify
 * that the code calling it gets run. */
static gint
operate_on_data (gint indata)
{
  return indata * 2;
}

static GstFlowReturn
callback_function (GstAppSink * appsink, gpointer callback_data)
{
  global_testdata = operate_on_data (*((gint *) callback_data));

  return GST_FLOW_OK;
}

static void
notify_function (gpointer callback_data)
{
  global_testdata = operate_on_data (*((gint *) callback_data));
}

GST_START_TEST (test_non_clients)
{
  GstElement *sink;
  GstBuffer *buffer;

  sink = setup_appsink ();

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  GST_DEBUG ("cleaning up appsink");
  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

/* Verifies that the handoff callback gets run one time when passing a buffer */
GST_START_TEST (test_handoff_callback)
{
  GstElement *sink;
  GstBuffer *buffer;
  gint testdata;
  GstAppSinkCallbacks callbacks = { NULL };

  sink = setup_appsink ();

  global_testdata = 0;
  testdata = 5;                 /* Arbitrary value */

  callbacks.new_sample = callback_function;

  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &callbacks, &testdata, NULL);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  buffer = gst_buffer_new_and_alloc (4);
  /* Pushing a buffer should run our callback */
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  testdata = operate_on_data (testdata);

  /* If both test_data & global_testdata have been operated on, we're happy. */
  fail_unless (testdata == global_testdata);

  GST_DEBUG ("cleaning up appsink");
  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

/* Verifies that the notify function gets executed when the sink is destroyed */
GST_START_TEST (test_notify0)
{
  GstElement *sink;
  gint testdata;
  GstAppSinkCallbacks callbacks = { NULL };

  sink = gst_element_factory_make ("appsink", NULL);

  global_testdata = 0;
  testdata = 17;                /* Arbitrary value */

  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &callbacks,
      &testdata, (*notify_function));

  GST_DEBUG ("cleaning up appsink");
  /* Destroying sink should call our notify_function */
  gst_object_unref (sink);

  testdata = operate_on_data (testdata);

  /* If both test_data & global_testdata have been operated on, we're happy. */
  fail_unless (testdata == global_testdata);
}

GST_END_TEST;


/* Verifies that the notify function gets executed when
 * gst_app_sink_set_callbacks () gets called */
GST_START_TEST (test_notify1)
{
  GstElement *sink;
  gint testdata;
  GstAppSinkCallbacks callbacks = { NULL };

  sink = gst_element_factory_make ("appsink", NULL);

  global_testdata = 0;
  testdata = 42;                /* Arbitrary value */

  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &callbacks,
      &testdata, (*notify_function));
  /* Setting new callbacks should trigger the destroy of the old data */
  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &callbacks, &testdata, NULL);

  testdata = operate_on_data (testdata);

  /* If both test_data & global_testdata have been operated on, we're happy. */
  fail_unless (testdata == global_testdata);

  GST_DEBUG ("cleaning up appsink");
  gst_object_unref (sink);
}

GST_END_TEST;

static const gint values[] = { 1, 2, 4 };

static GstBufferList *
create_buffer_list (void)
{
  guint len;
  GstBuffer *buffer;
  GstBufferList *mylist;

  mylist = gst_buffer_list_new ();
  fail_if (mylist == NULL);

  len = gst_buffer_list_length (mylist);
  fail_if (len != 0);

  buffer = gst_buffer_new_and_alloc (sizeof (gint));
  gst_buffer_fill (buffer, 0, &values[0], sizeof (gint));
  gst_buffer_list_add (mylist, buffer);

  buffer = gst_buffer_new_and_alloc (sizeof (gint));
  gst_buffer_fill (buffer, 0, &values[1], sizeof (gint));
  gst_buffer_list_add (mylist, buffer);

  buffer = gst_buffer_new_and_alloc (sizeof (gint));
  gst_buffer_fill (buffer, 0, &values[2], sizeof (gint));
  gst_buffer_list_add (mylist, buffer);

  return mylist;
}

static GstFlowReturn
callback_function_sample_fallback (GstAppSink * appsink, gpointer p_counter)
{
  GstSample *sample;
  GstBuffer *buf;
  gint *p_int_counter = p_counter;

  sample = gst_app_sink_pull_sample (appsink);
  buf = gst_sample_get_buffer (sample);
  fail_unless (GST_IS_BUFFER (buf));

  /* buffer list has 3 buffers in two groups */
  switch (*p_int_counter) {
    case 0:
      fail_unless_equals_int (gst_buffer_get_size (buf), sizeof (gint));
      gst_check_buffer_data (buf, &values[0], sizeof (gint));
      break;
    case 1:
      fail_unless_equals_int (gst_buffer_get_size (buf), sizeof (gint));
      gst_check_buffer_data (buf, &values[1], sizeof (gint));
      break;
    case 2:
      fail_unless_equals_int (gst_buffer_get_size (buf), sizeof (gint));
      gst_check_buffer_data (buf, &values[2], sizeof (gint));
      break;
    default:
      g_warn_if_reached ();
      break;
  }

  gst_sample_unref (sample);

  *p_int_counter += 1;

  return GST_FLOW_OK;
}

static GstFlowReturn
callback_function_sample (GstAppSink * appsink, gpointer p_counter)
{
  GstSample *sample;
  GstBufferList *list;
  gint *p_int_counter = p_counter;
  guint len;
  gint i;

  sample = gst_app_sink_pull_sample (appsink);
  list = gst_sample_get_buffer_list (sample);
  fail_unless (GST_IS_BUFFER_LIST (list));
  len = gst_buffer_list_length (list);
  fail_unless_equals_int (len, 3);

  for (i = 0; i < len; i++) {
    GstBuffer *buf = gst_buffer_list_get (list, i);
    fail_unless_equals_int (gst_buffer_get_size (buf), sizeof (gint));
    gst_check_buffer_data (buf, &values[i], sizeof (gint));
  }

  gst_sample_unref (sample);

  *p_int_counter += 1;

  return GST_FLOW_OK;
}

GST_START_TEST (test_buffer_list_fallback)
{
  GstElement *sink;
  GstBufferList *list;
  GstAppSinkCallbacks callbacks = { NULL };
  gint counter = 0;
  gboolean buffer_list_support;

  sink = setup_appsink ();

  /* verify that the buffer list support is disabled per default */
  g_object_get (sink, "buffer-list", &buffer_list_support, NULL);
  fail_unless (buffer_list_support == FALSE);


  callbacks.new_sample = callback_function_sample_fallback;

  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &callbacks, &counter, NULL);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  list = create_buffer_list ();
  fail_unless (gst_pad_push_list (mysrcpad, list) == GST_FLOW_OK);

  fail_unless_equals_int (counter, 3);

  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

GST_START_TEST (test_buffer_list_support)
{
  GstElement *sink;
  GstBufferList *list;
  GstAppSinkCallbacks callbacks = { NULL };
  gint counter = 0;

  sink = setup_appsink ();

  /* enable buffer list support */
  g_object_set (sink, "buffer-list", TRUE, NULL);

  callbacks.new_sample = callback_function_sample;

  gst_app_sink_set_callbacks (GST_APP_SINK (sink), &callbacks, &counter, NULL);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  list = create_buffer_list ();
  fail_unless (gst_pad_push_list (mysrcpad, list) == GST_FLOW_OK);

  fail_unless_equals_int (counter, 1);

  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

GST_START_TEST (test_buffer_list_fallback_signal)
{
  GstElement *sink;
  GstBufferList *list;
  gint counter = 0;

  sink = setup_appsink ();

  /* C calling convention to the rescue.. */
  g_signal_connect (sink, "new-sample",
      G_CALLBACK (callback_function_sample_fallback), &counter);

  g_object_set (sink, "emit-signals", TRUE, NULL);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  list = create_buffer_list ();
  fail_unless (gst_pad_push_list (mysrcpad, list) == GST_FLOW_OK);

  fail_unless_equals_int (counter, 3);

  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

GST_START_TEST (test_buffer_list_signal)
{
  GstElement *sink;
  GstBufferList *list;
  gint counter = 0;

  sink = setup_appsink ();

  /* enable buffer list support */
  g_object_set (sink, "buffer-list", TRUE, NULL);

  /* C calling convention to the rescue.. */
  g_signal_connect (sink, "new-sample", G_CALLBACK (callback_function_sample),
      &counter);

  g_object_set (sink, "emit-signals", TRUE, NULL);

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  list = create_buffer_list ();
  fail_unless (gst_pad_push_list (mysrcpad, list) == GST_FLOW_OK);

  fail_unless_equals_int (counter, 1);

  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

GST_START_TEST (test_segment)
{
  GstElement *sink;
  GstSegment segment;
  GstBuffer *buffer;
  GstSample *pulled_preroll;
  GstSample *pulled_sample;

  sink = setup_appsink ();

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 2 * GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  g_signal_emit_by_name (sink, "pull-preroll", &pulled_preroll);
  fail_unless (gst_segment_is_equal (&segment,
          gst_sample_get_segment (pulled_preroll)));
  gst_sample_unref (pulled_preroll);

  g_signal_emit_by_name (sink, "pull-sample", &pulled_sample);
  fail_unless (gst_segment_is_equal (&segment,
          gst_sample_get_segment (pulled_sample)));
  gst_sample_unref (pulled_sample);

  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

GST_START_TEST (test_pull_with_timeout)
{
  GstElement *sink;
  GstBuffer *buffer;
  GstSample *s;
  guint64 t1, tdiff;

  sink = setup_appsink ();

  ASSERT_SET_STATE (sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

  /* Check that it actually waits for a bit */
  t1 = gst_util_get_timestamp ();
  s = gst_app_sink_try_pull_preroll (GST_APP_SINK (sink), GST_SECOND / 20);
  tdiff = gst_util_get_timestamp () - t1;
  GST_LOG ("tdiff: %" GST_TIME_FORMAT, GST_TIME_ARGS (tdiff));
  fail_unless (s == NULL);
  fail_unless (tdiff > (GST_SECOND / (20 * 2)));

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  s = gst_app_sink_try_pull_preroll (GST_APP_SINK (sink), GST_SECOND / 20);
  fail_unless (s != NULL);
  gst_sample_unref (s);

  s = gst_app_sink_try_pull_sample (GST_APP_SINK (sink), 500 * GST_SECOND);
  fail_unless (s != NULL);
  gst_sample_unref (s);

  /* No waiting */
  s = gst_app_sink_try_pull_sample (GST_APP_SINK (sink), 0);
  fail_unless (s == NULL);

  /* Check that it actually waits for a bit */
  t1 = gst_util_get_timestamp ();
  s = gst_app_sink_try_pull_sample (GST_APP_SINK (sink), GST_SECOND / 20);
  tdiff = gst_util_get_timestamp () - t1;
  GST_LOG ("tdiff: %" GST_TIME_FORMAT, GST_TIME_ARGS (tdiff));
  fail_unless (s == NULL);
  fail_unless (tdiff > (GST_SECOND / (20 * 2)));

  /* No waiting, with buffer pending */
  buffer = gst_buffer_new_and_alloc (5);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  s = gst_app_sink_try_pull_sample (GST_APP_SINK (sink), 0);
  fail_unless (s != NULL);
  gst_sample_unref (s);

  /* With timeout, with buffer pending */
  buffer = gst_buffer_new_and_alloc (6);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  s = gst_app_sink_try_pull_sample (GST_APP_SINK (sink), GST_SECOND / 20);
  fail_unless (s != NULL);
  gst_sample_unref (s);

  ASSERT_SET_STATE (sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  cleanup_appsink (sink);
}

GST_END_TEST;

static Suite *
appsink_suite (void)
{
  Suite *s = suite_create ("appsink");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_non_clients);
  tcase_add_test (tc_chain, test_handoff_callback);
  tcase_add_test (tc_chain, test_notify0);
  tcase_add_test (tc_chain, test_notify1);
  tcase_add_test (tc_chain, test_buffer_list_fallback);
  tcase_add_test (tc_chain, test_buffer_list_support);
  tcase_add_test (tc_chain, test_buffer_list_fallback_signal);
  tcase_add_test (tc_chain, test_buffer_list_signal);
  tcase_add_test (tc_chain, test_segment);
  tcase_add_test (tc_chain, test_pull_with_timeout);

  return s;
}

GST_CHECK_MAIN (appsink);

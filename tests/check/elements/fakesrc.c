/* GStreamer
 *
 * unit test for fakesrc
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>

static gboolean have_eos = FALSE;

static GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    have_eos = TRUE;
  }
  gst_event_unref (event);
  return TRUE;
}

static GstElement *
setup_fakesrc (void)
{
  GstElement *fakesrc;

  GST_DEBUG ("setup_fakesrc");
  fakesrc = gst_check_setup_element ("fakesrc");
  mysinkpad = gst_check_setup_sink_pad (fakesrc, &sinktemplate);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);
  have_eos = FALSE;
  return fakesrc;
}

static void
cleanup_fakesrc (GstElement * fakesrc)
{
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (fakesrc);
  gst_check_teardown_element (fakesrc);
}

GST_START_TEST (test_num_buffers)
{
  GstElement *src;

  src = setup_fakesrc ();
  g_object_set (G_OBJECT (src), "num-buffers", 3, NULL);
  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless_equals_int (g_list_length (buffers), 3);
  gst_check_drop_buffers ();

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_empty)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 1, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless_equals_int (g_list_length (buffers), 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_unless (gst_buffer_get_size (buf) == 0);
    l = l->next;
  }
  gst_check_drop_buffers ();

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_fixed)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 2, NULL);
  g_object_set (G_OBJECT (src), "sizemax", 8192, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_unless (gst_buffer_get_size (buf) == 8192);
    l = l->next;
  }
  gst_check_drop_buffers ();

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_sizetype_random)
{
  GstElement *src;
  GList *l;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "sizetype", 3, NULL);
  g_object_set (G_OBJECT (src), "sizemin", 4096, NULL);
  g_object_set (G_OBJECT (src), "sizemax", 8192, NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 100, NULL);

  fail_unless (gst_element_set_state (src,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  while (!have_eos) {
    g_usleep (1000);
  }

  fail_unless (g_list_length (buffers) == 100);
  l = buffers;
  while (l) {
    GstBuffer *buf = l->data;

    fail_if (gst_buffer_get_size (buf) > 8192);
    fail_if (gst_buffer_get_size (buf) < 4096);
    l = l->next;
  }
  gst_check_drop_buffers ();

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

GST_START_TEST (test_no_preroll)
{
  GstElement *src;
  GstStateChangeReturn ret;

  src = setup_fakesrc ();

  g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);

  ret = gst_element_set_state (src, GST_STATE_PAUSED);

  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "error going to paused the first time");

  ret = gst_element_set_state (src, GST_STATE_PAUSED);

  fail_unless (ret == GST_STATE_CHANGE_NO_PREROLL,
      "error going to paused the second time");

  fail_unless (gst_element_set_state (src,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  /* cleanup */
  cleanup_fakesrc (src);
}

GST_END_TEST;

static void
handoff_cb (GstElement * element, GstBuffer * buf, GstPad * pad,
    gint * p_counter)
{
  *p_counter += 1;
  GST_LOG ("counter = %d", *p_counter);
}

GST_START_TEST (test_reuse_push)
{
  GstElement *src, *sep, *sink, *pipeline;
  GstBus *bus;
  gint counter, repeat = 3, num_buffers = 10;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL, "Failed to create pipeline!");

  bus = gst_element_get_bus (pipeline);

  src = gst_element_factory_make ("fakesrc", "fakesrc");
  fail_unless (src != NULL, "Failed to create 'fakesrc' element!");

  sep = gst_element_factory_make ("queue", "queue");
  fail_unless (sep != NULL, "Failed to create 'queue' element");

  sink = gst_element_factory_make ("fakesink", "fakesink");
  fail_unless (sink != NULL, "Failed to create 'fakesink' element!");

  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (handoff_cb), &counter);

  gst_bin_add_many (GST_BIN (pipeline), src, sep, sink, NULL);

  fail_unless (gst_element_link (src, sep));
  fail_unless (gst_element_link (sep, sink));

  g_object_set (src, "num-buffers", num_buffers, NULL);

  do {
    GstStateChangeReturn state_ret;
    GstMessage *msg;

    GST_INFO ("====================== round %d ======================", repeat);

    counter = 0;

    state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
    fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

    if (state_ret == GST_STATE_CHANGE_ASYNC) {
      GST_LOG ("waiting for pipeline to reach PAUSED state");
      state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
      fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
    }

    GST_LOG ("PAUSED, let's read all of it");

    state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
    fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

    msg = gst_bus_poll (bus, GST_MESSAGE_EOS, -1);
    fail_unless (msg != NULL, "Expected EOS message on bus!");

    gst_message_unref (msg);

    if (num_buffers >= 0) {
      fail_unless_equals_int (counter, num_buffers);
    }

    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
        GST_STATE_CHANGE_SUCCESS);

    --repeat;
  } while (repeat > 0);

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
fakesrc_suite (void)
{
  Suite *s = suite_create ("fakesrc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_num_buffers);
  tcase_add_test (tc_chain, test_sizetype_empty);
  tcase_add_test (tc_chain, test_sizetype_fixed);
  tcase_add_test (tc_chain, test_sizetype_random);
  tcase_add_test (tc_chain, test_no_preroll);
  tcase_add_test (tc_chain, test_reuse_push);

  return s;
}

GST_CHECK_MAIN (fakesrc);

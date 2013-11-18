/* GStreamer
 *
 * Copyright (C) 2010, Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#define SAMPLE_CAPS "application/x-gst-check-test"

static GstPad *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElement *
setup_appsrc (void)
{
  GstElement *appsrc;

  GST_DEBUG ("setup_appsrc");
  appsrc = gst_check_setup_element ("appsrc");
  mysinkpad = gst_check_setup_sink_pad (appsrc, &sinktemplate);

  gst_pad_set_active (mysinkpad, TRUE);

  return appsrc;
}

static void
cleanup_appsrc (GstElement * appsrc)
{
  GST_DEBUG ("cleanup_appsrc");

  gst_check_teardown_sink_pad (appsrc);
  gst_check_teardown_element (appsrc);
}

/*
 * Pushes 4 buffers into appsrc and checks the caps on them on the output.
 *
 * Appsrc is configured with caps=SAMPLE_CAPS, so the buffers should have the
 * same caps that they were pushed with.
 *
 * The 4 buffers have NULL, SAMPLE_CAPS, NULL, SAMPLE_CAPS caps,
 * respectively.
 */
GST_START_TEST (test_appsrc_non_null_caps)
{
  GstElement *src;
  GstBuffer *buffer;
  GstCaps *caps, *ccaps;

  src = setup_appsrc ();

  caps = gst_caps_from_string (SAMPLE_CAPS);
  g_object_set (src, "caps", caps, NULL);

  ASSERT_SET_STATE (src, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  buffer = gst_buffer_new_and_alloc (4);
  fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
          buffer) == GST_FLOW_OK);

  fail_unless (gst_app_src_end_of_stream (GST_APP_SRC (src)) == GST_FLOW_OK);

  /* Give some time to the appsrc loop to push the buffers */
  g_usleep (G_USEC_PER_SEC * 3);

  /* Check the output caps */
  fail_unless (g_list_length (buffers) == 4);

  ccaps = gst_pad_get_current_caps (mysinkpad);
  fail_unless (gst_caps_is_equal (ccaps, caps));
  gst_caps_unref (ccaps);

  ASSERT_SET_STATE (src, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
  gst_caps_unref (caps);
  cleanup_appsrc (src);
}

GST_END_TEST;

static GstAppSinkCallbacks app_callbacks;

typedef struct
{
  GMainLoop *loop;
  GstElement *source;
  GstElement *sink;
} ProgramData;

static GstFlowReturn
on_new_sample_from_source (GstAppSink * elt, gpointer user_data)
{
  ProgramData *data = (ProgramData *) user_data;
  GstSample *sample;
  GstBuffer *buffer;
  GstElement *source;

  sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
  buffer = gst_sample_get_buffer (sample);
  source = gst_bin_get_by_name (GST_BIN (data->sink), "testsource");
  gst_app_src_push_buffer (GST_APP_SRC (source), gst_buffer_ref (buffer));
  gst_sample_unref (sample);
  g_object_unref (source);
  return GST_FLOW_OK;
}

/* called when we get a GstMessage from the source pipeline when we get EOS, we
 * notify the appsrc of it. */
static gboolean
on_source_message (GstBus * bus, GstMessage * message, ProgramData * data)
{
  GstElement *source;
  gboolean ret = TRUE;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      source = gst_bin_get_by_name (GST_BIN (data->sink), "testsource");
      fail_unless (gst_app_src_end_of_stream (GST_APP_SRC (source)) ==
          GST_FLOW_OK);
      break;
    case GST_MESSAGE_ERROR:
      g_main_loop_quit (data->loop);
      ret = FALSE;
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
on_sink_message (GstBus * bus, GstMessage * message, ProgramData * data)
{
  gboolean ret = TRUE;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (data->loop);
      ret = FALSE;
      break;
    case GST_MESSAGE_ERROR:
      ASSERT_SET_STATE (data->sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
      ASSERT_SET_STATE (data->source, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
      g_main_loop_quit (data->loop);
      ret = FALSE;
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
error_timeout (ProgramData * data)
{
  GstBus *bus;
  bus = gst_element_get_bus (data->sink);
  gst_bus_post (bus, gst_message_new_error (GST_OBJECT (data->sink), NULL,
          "test error"));
  gst_object_unref (bus);
  return FALSE;
}

/*
 * appsink => appsrc pipelines executed 100 times: 
 * - appsink pipeline has sync=false
 * - appsrc pipeline has sync=true
 * - appsrc has block=true
 * after 1 second an error message is posted on appsink pipeline bus
 * when the error is received the appsrc pipeline is set to NULL
 * and then the appsink pipeline is
 * set to NULL too, this must not deadlock 
 */

GST_START_TEST (test_appsrc_block_deadlock)
{
  int i = 0;
  int num_iteration = 100;
  while (i < num_iteration) {
    ProgramData *data = NULL;
    GstBus *bus = NULL;
    GstElement *testsink = NULL;

    data = g_new0 (ProgramData, 1);

    data->loop = g_main_loop_new (NULL, FALSE);

    data->source =
        gst_parse_launch ("videotestsrc ! appsink sync=false name=testsink",
        NULL);

    fail_unless (data->source != NULL);

    bus = gst_element_get_bus (data->source);
    gst_bus_add_watch (bus, (GstBusFunc) on_source_message, data);
    gst_object_unref (bus);

    app_callbacks.new_sample = on_new_sample_from_source;
    testsink = gst_bin_get_by_name (GST_BIN (data->source), "testsink");
    gst_app_sink_set_callbacks (GST_APP_SINK_CAST (testsink), &app_callbacks,
        data, NULL);

    gst_object_unref (testsink);

    data->sink =
        gst_parse_launch
        ("appsrc name=testsource block=1 max-bytes=1000 is-live=true ! fakesink sync=true",
        NULL);

    fail_unless (data->sink != NULL);

    bus = gst_element_get_bus (data->sink);
    gst_bus_add_watch (bus, (GstBusFunc) on_sink_message, data);
    gst_object_unref (bus);

    g_timeout_add (150, (GSourceFunc) error_timeout, data);

    ASSERT_SET_STATE (data->sink, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);
    ASSERT_SET_STATE (data->source, GST_STATE_PLAYING, GST_STATE_CHANGE_ASYNC);

    g_main_loop_run (data->loop);

    ASSERT_SET_STATE (data->sink, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);
    ASSERT_SET_STATE (data->source, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS);

    gst_object_unref (data->source);
    gst_object_unref (data->sink);
    g_main_loop_unref (data->loop);
    g_free (data);
    i++;
    GST_INFO ("appsrc deadlock test iteration number %d/%d", i, num_iteration);
  }
}

GST_END_TEST;

static Suite *
appsrc_suite (void)
{
  Suite *s = suite_create ("appsrc");
  TCase *tc_chain = tcase_create ("general");

  tcase_add_test (tc_chain, test_appsrc_non_null_caps);
  tcase_add_test (tc_chain, test_appsrc_block_deadlock);

  tcase_set_timeout (tc_chain, 20);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (appsrc);

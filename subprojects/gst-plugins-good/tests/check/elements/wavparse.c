/* GStreamer WavParse unit tests
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#define CORRUPT_HEADER_WAV_PATH GST_TEST_FILES_PATH G_DIR_SEPARATOR_S \
    "corruptheadertestsrc.wav"
#define SIMPLE_WAV_PATH GST_TEST_FILES_PATH G_DIR_SEPARATOR_S "audiotestsrc.wav"

static GstElement *
create_file_pipeline (const char *path, GstPadMode mode)
{
  GstElement *pipeline;
  GstElement *src, *q = NULL;
  GstElement *wavparse;
  GstElement *fakesink;

  pipeline = gst_pipeline_new ("testpipe");
  src = gst_element_factory_make ("filesrc", "filesrc");
  fail_if (src == NULL);
  if (mode == GST_PAD_MODE_PUSH)
    q = gst_element_factory_make ("queue", "queue");
  wavparse = gst_element_factory_make ("wavparse", "wavparse");
  fail_if (wavparse == NULL);
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  fail_if (fakesink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, wavparse, fakesink, q, NULL);

  g_object_set (src, "location", path, NULL);

  if (mode == GST_PAD_MODE_PUSH)
    fail_unless (gst_element_link_many (src, q, wavparse, fakesink, NULL));
  else
    fail_unless (gst_element_link_many (src, wavparse, fakesink, NULL));

  return pipeline;
}

static void
do_test_simple_file (GstPadMode mode)
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstMessage *msg;

  pipeline = create_file_pipeline (SIMPLE_WAV_PATH, mode);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "eos");

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_simple_file_pull)
{
  do_test_simple_file (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_simple_file_push)
{
  do_test_simple_file (FALSE);
}

GST_END_TEST;

static void
do_test_corrupt_header_file (GstPadMode mode)
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstMessage *msg;

  pipeline = create_file_pipeline (CORRUPT_HEADER_WAV_PATH, mode);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ERROR);

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_corrupt_header_file_push)
{
  do_test_corrupt_header_file (GST_PAD_MODE_PUSH);
}

GST_END_TEST;

static void
do_test_empty_file (gboolean can_activate_pull)
{
  GstStateChangeReturn ret1, ret2;
  GstElement *pipeline;
  GstElement *src;
  GstElement *wavparse;
  GstElement *fakesink;

  /* Pull mode */
  pipeline = gst_pipeline_new ("testpipe");
  src = gst_element_factory_make ("fakesrc", NULL);
  fail_if (src == NULL);
  wavparse = gst_element_factory_make ("wavparse", NULL);
  fail_if (wavparse == NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink == NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, wavparse, fakesink, NULL);
  g_object_set (src, "num-buffers", 0, "can-activate-pull", can_activate_pull,
      NULL);

  fail_unless (gst_element_link_many (src, wavparse, fakesink, NULL));

  ret1 = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret1 == GST_STATE_CHANGE_ASYNC)
    ret2 = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  else
    ret2 = ret1;

  /* should have gotten an error on the bus, no output to fakesink */
  fail_unless_equals_int (ret2, GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_empty_file_pull)
{
  do_test_empty_file (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_empty_file_push)
{
  do_test_empty_file (FALSE);
}

GST_END_TEST;

static GstPadProbeReturn
fakesink_buffer_cb (GstPad * sink, GstPadProbeInfo * info, gpointer user_data)
{
  GstClockTime *ts = user_data;
  GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER (info);

  fail_unless (buf);
  if (!GST_CLOCK_TIME_IS_VALID (*ts))
    *ts = GST_BUFFER_PTS (buf);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_seek)
{
  GstStateChangeReturn ret;
  GstElement *pipeline;
  GstMessage *msg;
  GstElement *wavparse, *fakesink;
  GstPad *pad;
  GstClockTime seek_position = (20 * GST_MSECOND);
  GstClockTime first_ts = GST_CLOCK_TIME_NONE;

  pipeline = create_file_pipeline (SIMPLE_WAV_PATH, GST_PAD_MODE_PULL);
  wavparse = gst_bin_get_by_name (GST_BIN (pipeline), "wavparse");
  fail_unless (wavparse);
  fakesink = gst_bin_get_by_name (GST_BIN (pipeline), "fakesink");
  fail_unless (fakesink);

  pad = gst_element_get_static_pad (fakesink, "sink");
  fail_unless (pad);
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, fakesink_buffer_cb,
      &first_ts, NULL);
  gst_object_unref (pad);

  /* wavparse is able to seek in the READY state */
  ret = gst_element_set_state (pipeline, GST_STATE_READY);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  fail_unless (gst_element_seek_simple (wavparse, GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH, seek_position));

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_ASYNC);

  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (ret, GST_STATE_CHANGE_SUCCESS);

  msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipeline),
      GST_CLOCK_TIME_NONE, GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

  /* check that the first buffer produced by wavparse matches the seek
     position we requested */
  fail_unless_equals_clocktime (first_ts, seek_position);

  fail_unless_equals_string (GST_MESSAGE_TYPE_NAME (msg), "eos");

  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (wavparse);
  gst_object_unref (fakesink);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_query_uri)
{
  GstElement *pipeline, *filesrc, *wavparse, *fakesink;
  GstQuery *query;
  gchar *uri;
  fail_unless ((pipeline = gst_pipeline_new (NULL)) != NULL,
      "Could not create pipeline");
  fail_unless ((filesrc = gst_element_factory_make ("filesrc", NULL)) != NULL,
      "Could not create filesrc");
  fail_unless ((wavparse = gst_element_factory_make ("wavparse", NULL)) != NULL,
      "Could not create wavparse");
  fail_unless ((fakesink = gst_element_factory_make ("fakesink", NULL)) != NULL,
      "Could not create fakesink");
  gst_bin_add_many (GST_BIN (pipeline), filesrc, wavparse, fakesink, NULL);
  gst_element_link_many (filesrc, wavparse, fakesink, NULL);
  g_object_set (G_OBJECT (filesrc), "location", "my_test_file", NULL);
  fail_unless ((query = gst_query_new_uri ()) != NULL,
      "Could not prepare uri query");
  fail_unless (gst_element_query (GST_ELEMENT (wavparse), query),
      "Could not query uri");
  gst_query_parse_uri (query, &uri);
  fail_unless (uri != NULL);

  g_free (uri);
  gst_query_unref (query);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
wavparse_suite (void)
{
  Suite *s = suite_create ("wavparse");
  TCase *tc_chain = tcase_create ("wavparse");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_empty_file_pull);
  tcase_add_test (tc_chain, test_empty_file_push);
  tcase_add_test (tc_chain, test_corrupt_header_file_push);
  tcase_add_test (tc_chain, test_simple_file_pull);
  tcase_add_test (tc_chain, test_simple_file_push);
  tcase_add_test (tc_chain, test_seek);
  tcase_add_test (tc_chain, test_query_uri);
  return s;
}

GST_CHECK_MAIN (wavparse)

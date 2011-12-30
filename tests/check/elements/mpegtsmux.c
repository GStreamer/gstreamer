/* GStreamer
 *
 * Copyright (C) 2011 Alessandro Decina <alessandro.d@gmail.com>
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
#include <string.h>
#include <gst/video/video.h>

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate video_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264")
    );

static GstStaticPadTemplate audio_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg")
    );

typedef struct _TestData
{
  GstEvent *sink_event;
  GstEvent *src_event1;
  GstEvent *src_event2;
  gint src_events;
} TestData;

typedef struct _ThreadData
{
  GstPad *pad;
  GstBuffer *buffer;
  GstFlowReturn flow_return;
  GThread *thread;
} ThreadData;

static gboolean
src_event (GstPad * pad, GstEvent * event)
{
  TestData *data = (TestData *) gst_pad_get_element_private (pad);

  if (event->type == GST_EVENT_CUSTOM_UPSTREAM) {
    data->src_events += 1;
    if (data->src_event1 != NULL)
      data->src_event2 = event;
    else
      data->src_event1 = event;
  }

  return TRUE;
}

static gboolean
sink_event (GstPad * pad, GstEvent * event)
{
  TestData *data = (TestData *) gst_pad_get_element_private (pad);

  if (event->type == GST_EVENT_CUSTOM_DOWNSTREAM)
    data->sink_event = event;

  return TRUE;
}

static void
link_sinks (GstElement * mpegtsmux,
    GstPad ** src1, GstPad ** src2, GstPad ** src3, TestData * test_data)
{
  GstPad *mux_sink1, *mux_sink2, *mux_sink3;
  GstCaps *caps;

  /* link 3 sink pads, 2 video 1 audio */
  *src1 = gst_pad_new_from_static_template (&video_src_template, "src1");
  gst_pad_set_active (*src1, TRUE);
  gst_pad_set_element_private (*src1, test_data);
  gst_pad_set_event_function (*src1, src_event);
  mux_sink1 = gst_element_get_request_pad (mpegtsmux, "sink_1");
  fail_unless (gst_pad_link (*src1, mux_sink1) == GST_PAD_LINK_OK);

  *src2 = gst_pad_new_from_static_template (&video_src_template, "src2");
  gst_pad_set_active (*src2, TRUE);
  gst_pad_set_element_private (*src2, test_data);
  gst_pad_set_event_function (*src2, src_event);
  mux_sink2 = gst_element_get_request_pad (mpegtsmux, "sink_2");
  fail_unless (gst_pad_link (*src2, mux_sink2) == GST_PAD_LINK_OK);

  *src3 = gst_pad_new_from_static_template (&audio_src_template, "src3");
  gst_pad_set_active (*src3, TRUE);
  gst_pad_set_element_private (*src3, test_data);
  gst_pad_set_event_function (*src3, src_event);
  mux_sink3 = gst_element_get_request_pad (mpegtsmux, "sink_3");
  fail_unless (gst_pad_link (*src3, mux_sink3) == GST_PAD_LINK_OK);

  caps = gst_caps_new_simple ("video/x-h264", NULL);
  gst_pad_set_caps (mux_sink1, caps);
  gst_pad_set_caps (mux_sink2, caps);
  gst_caps_unref (caps);
  caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4, NULL);
  gst_pad_set_caps (mux_sink3, caps);
  gst_caps_unref (caps);

  gst_object_unref (mux_sink1);
  gst_object_unref (mux_sink2);
  gst_object_unref (mux_sink3);
}

static void
link_src (GstElement * mpegtsmux, GstPad ** sink, TestData * test_data)
{
  GstPad *mux_src;

  mux_src = gst_element_get_static_pad (mpegtsmux, "src");
  *sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_active (*sink, TRUE);
  gst_pad_set_event_function (*sink, sink_event);
  gst_pad_set_element_private (*sink, test_data);
  fail_unless (gst_pad_link (mux_src, *sink) == GST_PAD_LINK_OK);

  gst_object_unref (mux_src);
}

static gpointer
pad_push_thread (gpointer user_data)
{
  ThreadData *data = (ThreadData *) user_data;

  data->flow_return = gst_pad_push (data->pad, data->buffer);

  return NULL;
}

static ThreadData *
pad_push (GstPad * pad, GstBuffer * buffer, GstClockTime timestamp)
{
  ThreadData *data;

  data = g_new0 (ThreadData, 1);
  data->pad = pad;
  data->buffer = buffer;
  GST_BUFFER_TIMESTAMP (buffer) = timestamp;
  data->thread = g_thread_create (pad_push_thread, data, TRUE, NULL);

  return data;
}

GST_START_TEST (test_force_key_unit_event_downstream)
{
  GstElement *mpegtsmux;
  GstPad *sink;
  GstPad *src1;
  GstPad *src2;
  GstPad *src3;
  GstEvent *sink_event;
  GstClockTime timestamp, stream_time, running_time;
  gboolean all_headers = TRUE;
  gint count = 0;
  ThreadData *thread_data_1, *thread_data_2, *thread_data_3, *thread_data_4;
  TestData test_data = { 0, };

  mpegtsmux = gst_check_setup_element ("mpegtsmux");
  gst_element_set_state (mpegtsmux, GST_STATE_PLAYING);

  link_src (mpegtsmux, &sink, &test_data);
  link_sinks (mpegtsmux, &src1, &src2, &src3, &test_data);

  /* hack: make sure collectpads builds collect->data */
  gst_pad_push_event (src1, gst_event_new_flush_start ());
  gst_pad_push_event (src1, gst_event_new_flush_stop ());

  /* send a force-key-unit event with running_time=2s */
  timestamp = stream_time = running_time = 2 * GST_SECOND;
  sink_event = gst_video_event_new_downstream_force_key_unit (timestamp,
      stream_time, running_time, all_headers, count);

  fail_unless (gst_pad_push_event (src1, sink_event));
  fail_unless (test_data.sink_event == NULL);

  /* push 4 buffers, make sure mpegtsmux handles the force-key-unit event when
   * the buffer with the requested running time is collected */
  thread_data_1 = pad_push (src1, gst_buffer_new (), 1 * GST_SECOND);
  thread_data_2 = pad_push (src2, gst_buffer_new (), 2 * GST_SECOND);
  thread_data_3 = pad_push (src3, gst_buffer_new (), 3 * GST_SECOND);

  g_thread_join (thread_data_1->thread);
  fail_unless (test_data.sink_event == NULL);

  /* push again on src1 so that the buffer on src2 is collected */
  thread_data_4 = pad_push (src1, gst_buffer_new (), 4 * GST_SECOND);

  g_thread_join (thread_data_2->thread);
  fail_unless (test_data.sink_event != NULL);

  gst_element_set_state (mpegtsmux, GST_STATE_NULL);

  g_thread_join (thread_data_3->thread);
  g_thread_join (thread_data_4->thread);

  g_free (thread_data_1);
  g_free (thread_data_2);
  g_free (thread_data_3);
  g_free (thread_data_4);
  gst_object_unref (src1);
  gst_object_unref (src2);
  gst_object_unref (src3);
  gst_object_unref (sink);
  gst_object_unref (mpegtsmux);
}

GST_END_TEST;

GST_START_TEST (test_force_key_unit_event_upstream)
{
  GstElement *mpegtsmux;
  GstPad *sink;
  GstPad *src1;
  GstPad *src2;
  GstPad *src3;
  GstEvent *event;
  GstClockTime timestamp, stream_time, running_time;
  gboolean all_headers = TRUE;
  gint count = 0;
  TestData test_data = { 0, };
  ThreadData *thread_data_1, *thread_data_2, *thread_data_3, *thread_data_4;

  mpegtsmux = gst_check_setup_element ("mpegtsmux");
  gst_element_set_state (mpegtsmux, GST_STATE_PLAYING);

  link_src (mpegtsmux, &sink, &test_data);
  link_sinks (mpegtsmux, &src1, &src2, &src3, &test_data);

  /* hack: make sure collectpads builds collect->data */
  gst_pad_push_event (src1, gst_event_new_flush_start ());
  gst_pad_push_event (src1, gst_event_new_flush_stop ());

  /* send an upstream force-key-unit event with running_time=2s */
  timestamp = stream_time = running_time = 2 * GST_SECOND;
  event =
      gst_video_event_new_upstream_force_key_unit (running_time, TRUE, count);
  fail_unless (gst_pad_push_event (sink, event));

  fail_unless (test_data.sink_event == NULL);
  fail_unless_equals_int (test_data.src_events, 3);

  /* send downstream events with unrelated seqnums */
  event = gst_video_event_new_downstream_force_key_unit (timestamp,
      stream_time, running_time, all_headers, count);
  fail_unless (gst_pad_push_event (src1, event));
  event = gst_video_event_new_downstream_force_key_unit (timestamp,
      stream_time, running_time, all_headers, count);
  fail_unless (gst_pad_push_event (src2, event));

  /* events should be skipped */
  fail_unless (test_data.sink_event == NULL);

  /* push 4 buffers, make sure mpegtsmux handles the force-key-unit event when
   * the buffer with the requested running time is collected */
  thread_data_1 = pad_push (src1, gst_buffer_new (), 1 * GST_SECOND);
  thread_data_2 = pad_push (src2, gst_buffer_new (), 2 * GST_SECOND);
  thread_data_3 = pad_push (src3, gst_buffer_new (), 3 * GST_SECOND);

  g_thread_join (thread_data_1->thread);
  fail_unless (test_data.sink_event == NULL);

  /* push again on src1 so that the buffer on src2 is collected */
  thread_data_4 = pad_push (src1, gst_buffer_new (), 4 * GST_SECOND);

  g_thread_join (thread_data_2->thread);
  fail_unless (test_data.sink_event != NULL);

  gst_element_set_state (mpegtsmux, GST_STATE_NULL);

  g_thread_join (thread_data_3->thread);
  g_thread_join (thread_data_4->thread);

  g_free (thread_data_1);
  g_free (thread_data_2);
  g_free (thread_data_3);
  g_free (thread_data_4);

  gst_object_unref (src1);
  gst_object_unref (src2);
  gst_object_unref (src3);
  gst_object_unref (sink);
  gst_object_unref (mpegtsmux);
}

GST_END_TEST;

static Suite *
mpegtsmux_suite (void)
{
  Suite *s = suite_create ("mpegtsmux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_force_key_unit_event_downstream);
  tcase_add_test (tc_chain, test_force_key_unit_event_upstream);

  return s;
}

GST_CHECK_MAIN (mpegtsmux);

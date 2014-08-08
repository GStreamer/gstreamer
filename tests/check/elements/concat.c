/* GStreamer unit tests for concat
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>

#define N_BUFFERS 10
static gboolean got_eos;
static guint buffer_count;
static GstSegment current_segment;
static guint64 current_bytes;

static GstFlowReturn
output_chain_time (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstClockTime timestamp;
  guint8 b;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  fail_unless_equals_int64 (timestamp,
      (buffer_count % N_BUFFERS) * 25 * GST_MSECOND);
  timestamp =
      gst_segment_to_stream_time (&current_segment, GST_FORMAT_TIME, timestamp);
  fail_unless_equals_int64 (timestamp,
      (buffer_count % N_BUFFERS) * 25 * GST_MSECOND);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  timestamp =
      gst_segment_to_running_time (&current_segment, GST_FORMAT_TIME,
      timestamp);
  fail_unless_equals_int64 (timestamp, buffer_count * 25 * GST_MSECOND);

  gst_buffer_extract (buffer, 0, &b, 1);
  fail_unless_equals_int (b, buffer_count % N_BUFFERS);

  buffer_count++;
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static gboolean
output_event_time (GstPad * pad, GstObject * parent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&current_segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &current_segment);
      break;
    case GST_EVENT_EOS:
      got_eos = TRUE;
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static gpointer
push_buffers_time (gpointer data)
{
  GstSegment segment;
  GstPad *pad = data;
  gint i;
  GstClockTime timestamp = 0;

  gst_pad_send_event (pad, gst_event_new_stream_start ("test"));
  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_send_event (pad, gst_event_new_segment (&segment));

  for (i = 0; i < N_BUFFERS; i++) {
    GstBuffer *buf = gst_buffer_new_and_alloc (1000);

    gst_buffer_memset (buf, 0, i, 1);

    GST_BUFFER_TIMESTAMP (buf) = timestamp;
    timestamp += 25 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = timestamp - GST_BUFFER_TIMESTAMP (buf);

    fail_unless (gst_pad_chain (pad, buf) == GST_FLOW_OK);
  }
  gst_pad_send_event (pad, gst_event_new_eos ());

  return NULL;
}

GST_START_TEST (test_concat_simple_time)
{
  GstElement *concat;
  GstPad *sink1, *sink2, *sink3, *src, *output_sink;
  GThread *thread1, *thread2, *thread3;

  got_eos = FALSE;
  buffer_count = 0;
  gst_segment_init (&current_segment, GST_FORMAT_UNDEFINED);

  concat = gst_element_factory_make ("concat", NULL);
  fail_unless (concat != NULL);

  sink1 = gst_element_get_request_pad (concat, "sink_%u");
  fail_unless (sink1 != NULL);

  sink2 = gst_element_get_request_pad (concat, "sink_%u");
  fail_unless (sink2 != NULL);

  sink3 = gst_element_get_request_pad (concat, "sink_%u");
  fail_unless (sink3 != NULL);

  src = gst_element_get_static_pad (concat, "src");
  output_sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (output_sink != NULL);
  fail_unless (gst_pad_link (src, output_sink) == GST_PAD_LINK_OK);

  gst_pad_set_chain_function (output_sink, output_chain_time);
  gst_pad_set_event_function (output_sink, output_event_time);

  gst_pad_set_active (output_sink, TRUE);
  fail_unless (gst_element_set_state (concat,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  thread1 = g_thread_new ("thread1", (GThreadFunc) push_buffers_time, sink1);
  thread2 = g_thread_new ("thread2", (GThreadFunc) push_buffers_time, sink2);
  thread3 = g_thread_new ("thread3", (GThreadFunc) push_buffers_time, sink3);

  g_thread_join (thread1);
  g_thread_join (thread2);
  g_thread_join (thread3);

  fail_unless (got_eos);
  fail_unless_equals_int (buffer_count, 3 * N_BUFFERS);

  gst_element_set_state (concat, GST_STATE_NULL);
  gst_pad_unlink (src, output_sink);
  gst_object_unref (src);
  gst_element_release_request_pad (concat, sink1);
  gst_object_unref (sink1);
  gst_element_release_request_pad (concat, sink2);
  gst_object_unref (sink2);
  gst_element_release_request_pad (concat, sink3);
  gst_object_unref (sink3);
  gst_pad_set_active (output_sink, FALSE);
  gst_object_unref (output_sink);
  gst_object_unref (concat);
}

GST_END_TEST;

static GstFlowReturn
output_chain_bytes (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  guint8 b;

  fail_unless (current_bytes >= current_segment.start);
  fail_unless_equals_int64 (current_segment.start,
      (buffer_count / N_BUFFERS) * 1000 * N_BUFFERS);

  gst_buffer_extract (buffer, 0, &b, 1);
  fail_unless_equals_int (b, buffer_count % N_BUFFERS);

  current_bytes += gst_buffer_get_size (buffer), buffer_count++;
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static gboolean
output_event_bytes (GstPad * pad, GstObject * parent, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&current_segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_SEGMENT:
      gst_event_copy_segment (event, &current_segment);
      break;
    case GST_EVENT_EOS:
      got_eos = TRUE;
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static gpointer
push_buffers_bytes (gpointer data)
{
  GstSegment segment;
  GstPad *pad = data;
  gint i;

  gst_pad_send_event (pad, gst_event_new_stream_start ("test"));
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_send_event (pad, gst_event_new_segment (&segment));

  for (i = 0; i < N_BUFFERS; i++) {
    GstBuffer *buf = gst_buffer_new_and_alloc (1000);
    gst_buffer_memset (buf, 0, i, 1);

    fail_unless (gst_pad_chain (pad, buf) == GST_FLOW_OK);
  }
  gst_pad_send_event (pad, gst_event_new_eos ());

  return NULL;
}

GST_START_TEST (test_concat_simple_bytes)
{
  GstElement *concat;
  GstPad *sink1, *sink2, *sink3, *src, *output_sink;
  GThread *thread1, *thread2, *thread3;

  got_eos = FALSE;
  buffer_count = 0;
  current_bytes = 0;
  gst_segment_init (&current_segment, GST_FORMAT_UNDEFINED);

  concat = gst_element_factory_make ("concat", NULL);
  fail_unless (concat != NULL);

  sink1 = gst_element_get_request_pad (concat, "sink_%u");
  fail_unless (sink1 != NULL);

  sink2 = gst_element_get_request_pad (concat, "sink_%u");
  fail_unless (sink2 != NULL);

  sink3 = gst_element_get_request_pad (concat, "sink_%u");
  fail_unless (sink3 != NULL);

  src = gst_element_get_static_pad (concat, "src");
  output_sink = gst_pad_new ("sink", GST_PAD_SINK);
  fail_unless (output_sink != NULL);
  fail_unless (gst_pad_link (src, output_sink) == GST_PAD_LINK_OK);

  gst_pad_set_chain_function (output_sink, output_chain_bytes);
  gst_pad_set_event_function (output_sink, output_event_bytes);

  gst_pad_set_active (output_sink, TRUE);
  fail_unless (gst_element_set_state (concat,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  thread1 = g_thread_new ("thread1", (GThreadFunc) push_buffers_bytes, sink1);
  thread2 = g_thread_new ("thread2", (GThreadFunc) push_buffers_bytes, sink2);
  thread3 = g_thread_new ("thread3", (GThreadFunc) push_buffers_bytes, sink3);

  g_thread_join (thread1);
  g_thread_join (thread2);
  g_thread_join (thread3);

  fail_unless (got_eos);
  fail_unless_equals_int (buffer_count, 3 * N_BUFFERS);
  fail_unless_equals_int64 (current_bytes, 3 * N_BUFFERS * 1000);

  gst_element_set_state (concat, GST_STATE_NULL);
  gst_pad_unlink (src, output_sink);
  gst_object_unref (src);
  gst_element_release_request_pad (concat, sink1);
  gst_object_unref (sink1);
  gst_element_release_request_pad (concat, sink2);
  gst_object_unref (sink2);
  gst_element_release_request_pad (concat, sink3);
  gst_object_unref (sink3);
  gst_pad_set_active (output_sink, FALSE);
  gst_object_unref (output_sink);
  gst_object_unref (concat);
}

GST_END_TEST;

static Suite *
concat_suite (void)
{
  Suite *s = suite_create ("concat");
  TCase *tc_chain;

  tc_chain = tcase_create ("concat");
  tcase_add_test (tc_chain, test_concat_simple_time);
  tcase_add_test (tc_chain, test_concat_simple_bytes);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (concat);

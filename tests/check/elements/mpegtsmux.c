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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

#define AUDIO_CAPS_STRING "audio/mpeg, " \
                        "channels = (int) 1, " \
                        "rate = (int) 8000, " \
                        "mpegversion = (int) 1, "\
                        "parsed = (boolean) true "
#define VIDEO_CAPS_STRING "video/x-h264, " \
                          "stream-format = (string) byte-stream, " \
                          "alignment = (string) nal, " \
                          "parsed = (boolean) true "

#define KEYFRAME_DISTANCE 10

typedef void (CheckOutputBuffersFunc) (GList * buffers);

/* setup and teardown needs some special handling for muxer */
static GstPad *
setup_src_pad (GstElement * element,
    GstStaticPadTemplate * template, const gchar * sinkname, gchar ** padname)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up sending pad");
  /* sending pad */
  srcpad = gst_pad_new_from_static_template (template, "src");
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);

  if (!(sinkpad = gst_element_get_static_pad (element, sinkname)))
    sinkpad = gst_element_get_request_pad (element, sinkname);
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  /* references are owned by: 1) us, 2) tsmux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (sinkpad);   /* because we got it higher up */

  /* references are owned by: 1) tsmux, 2) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);

  if (padname)
    *padname = g_strdup (GST_PAD_NAME (sinkpad));

  return srcpad;
}

static void
teardown_src_pad (GstElement * element, const gchar * sinkname)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating src pad */
  if (!(sinkpad = gst_element_get_static_pad (element, sinkname)))
    sinkpad = gst_element_get_request_pad (element, sinkname);
  /* pad refs held by 1) tsmux 2) collectpads and 3) us (through _get) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  srcpad = gst_pad_get_peer (sinkpad);

  gst_pad_unlink (srcpad, sinkpad);
  GST_DEBUG ("src %p", srcpad);

  /* after unlinking, pad refs still held by
   * 1) tsmux and 2) collectpads and 3) us (through _get) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  gst_object_unref (sinkpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  gst_object_unref (srcpad);

}

static GstElement *
setup_tsmux (GstStaticPadTemplate * srctemplate, const gchar * sinkname,
    gchar ** padname)
{
  GstElement *mux;

  GST_DEBUG ("setup_tsmux");
  mux = gst_check_setup_element ("mpegtsmux");
  mysrcpad = setup_src_pad (mux, srctemplate, sinkname, padname);
  mysinkpad = gst_check_setup_sink_pad (mux, &sink_template);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return mux;
}

static void
cleanup_tsmux (GstElement * mux, const gchar * sinkname)
{
  GST_DEBUG ("cleanup_mux");
  gst_element_set_state (mux, GST_STATE_NULL);

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  teardown_src_pad (mux, sinkname);
  gst_check_teardown_sink_pad (mux);
  gst_check_teardown_element (mux);
}

static void
check_tsmux_pad (GstStaticPadTemplate * srctemplate,
    const gchar * src_caps_string, gint pes_id, gint pmt_id,
    const gchar * sinkname, CheckOutputBuffersFunc check_func, guint n_bufs,
    gssize input_buf_size, guint alignment)
{
  GstClockTime ts;
  GstElement *mux;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  gint num_buffers;
  gint i;
  gint pmt_pid = -1, el_pid = -1, pcr_pid = -1, packets = 0;
  gchar *padname;

  mux = setup_tsmux (srctemplate, sinkname, &padname);

  if (alignment != 0)
    g_object_set (mux, "alignment", alignment, NULL);

  fail_unless (gst_element_set_state (mux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (src_caps_string);
  gst_check_setup_events (mysrcpad, mux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  ts = 0;
  for (i = 0; i < n_bufs; ++i) {
    GstFlowReturn flow;

    if (input_buf_size >= 0)
      inbuffer = gst_buffer_new_and_alloc (input_buf_size);
    else
      inbuffer = gst_buffer_new_and_alloc (g_random_int_range (0, 49141));

    GST_BUFFER_TIMESTAMP (inbuffer) = ts;
    ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

    if (i % KEYFRAME_DISTANCE == 0 && pes_id == 0xe0) {
      GST_TRACE ("input keyframe");
      GST_BUFFER_FLAG_UNSET (inbuffer, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      GST_TRACE ("input delta");
      GST_BUFFER_FLAG_SET (inbuffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }
    flow = gst_pad_push (mysrcpad, inbuffer);
    if (flow != GST_FLOW_OK)
      fail ("Got %s flow instead of OK", gst_flow_get_name (flow));
    ts += 40 * GST_MSECOND;
  }

  if (check_func)
    check_func (buffers);

  num_buffers = g_list_length (buffers);
  /* all output might get aggregated */
  fail_unless (num_buffers >= 1);

  /* collect buffers in adapter for convenience */
  for (i = 0; i < num_buffers; ++i) {
    guint8 *odata;
    gint size;
    GstMapInfo map;

    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);
    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);

    gst_buffer_map (outbuffer, &map, GST_MAP_READ);
    odata = map.data;
    size = map.size;
    fail_unless (size % 188 == 0);

    for (; size; odata += 188, size -= 188) {
      guint pid, y;
      guint8 *data = odata;

      /* need sync_byte */
      fail_unless (*data == 0x47);
      data++;

      y = GST_READ_UINT16_BE (data);
      pid = y & (0x1FFF);
      data += 2;
      GST_TRACE ("pid: %d", pid);

      y = (y >> 14) & 0x1;
      /* only check packets with payload_start_indicator == 1 */
      if (!y) {
        GST_TRACE ("not at start");
        continue;
      }

      y = *data;
      data++;

      if (y & 0x20) {
        /* adaptation field */
        y = *data;
        data++;
        data += y;
        GST_TRACE ("adaptation %d", y);
      }

      if (pid == 0) {
        /* look for PAT */
        /* pointer field */
        y = *data;
        data++;
        data += y;
        /* table_id */
        y = *data;
        data++;
        fail_unless (y == 0x0);
        /* skip */
        data += 5;
        /* section_number */
        y = *data;
        fail_unless (y == 0);
        data++;
        /* last_section_number */
        y = *data;
        fail_unless (y == 0);
        data++;
        /* program_number */
        y = GST_READ_UINT16_BE (data);
        fail_unless (y != 0);
        data += 2;
        /* program_map_PID */
        y = GST_READ_UINT16_BE (data);
        pmt_pid = y & 0x1FFF;
        fail_unless (pmt_pid > 0x10 && pmt_pid != 0x1FF);
      } else if (pid == pmt_pid) {
        /* look for PMT */
        /* pointer field */
        y = *data;
        data++;
        data += y;
        /* table_id */
        y = *data;
        data++;
        fail_unless (y == 0x2);
        /* skip */
        data += 5;
        /* section_number */
        y = *data;
        fail_unless (y == 0);
        data++;
        /* last_section_number */
        y = *data;
        fail_unless (y == 0);
        data++;
        /* PCR_PID */
        y = GST_READ_UINT16_BE (data);
        data += 2;
        pcr_pid = y & 0x1FFF;
        /* program_info_length */
        y = GST_READ_UINT16_BE (data);
        data += 2;
        y = y & 0x0FFF;
        data += y;
        /* parsing only ES stream */
        /* stream_type */
        y = *data;
        data++;
        fail_unless (y == pmt_id);
        /* elementary_PID */
        y = GST_READ_UINT16_BE (data);
        data += 2;
        el_pid = y & 0x1FFF;
        fail_unless (el_pid > 0x10 && el_pid != 0x1FF);
      } else if (pid == el_pid) {
        packets++;
        /* expect to see a PES packet start */
        y = GST_READ_UINT32_BE (data);
        fail_unless (y >> 8 == 0x1);
        /* stream_id */
        y = y & 0xFF;
        fail_unless ((pes_id & 0xF0) == (y & 0xF0));
      }
    }
    gst_buffer_unmap (outbuffer, &map);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  fail_unless (pmt_pid > 0);
  fail_unless (el_pid > 0);
  fail_unless (pcr_pid == el_pid);
  fail_unless (packets > 0);

  g_list_free (buffers);
  buffers = NULL;

  cleanup_tsmux (mux, padname);
  g_free (padname);
}


GST_START_TEST (test_video)
{
  check_tsmux_pad (&video_src_template, VIDEO_CAPS_STRING, 0xE0, 0x1b,
      "sink_%d", NULL, 1, 1, 0);
}

GST_END_TEST;


GST_START_TEST (test_audio)
{
  check_tsmux_pad (&audio_src_template, AUDIO_CAPS_STRING, 0xC0, 0x03,
      "sink_%d", NULL, 1, 1, 0);
}

GST_END_TEST;


typedef struct _TestData
{
  GstEvent *sink_event;
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
src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  TestData *data = (TestData *) gst_pad_get_element_private (pad);

  if (event->type == GST_EVENT_CUSTOM_UPSTREAM)
    data->src_events += 1;

  gst_event_unref (event);
  return TRUE;
}

static gboolean
sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  TestData *data = (TestData *) gst_pad_get_element_private (pad);

  if (event->type == GST_EVENT_CUSTOM_DOWNSTREAM)
    data->sink_event = event;

  gst_event_unref (event);
  return TRUE;
}

static void
link_sinks (GstElement * mpegtsmux,
    GstPad ** src1, GstPad ** src2, GstPad ** src3, TestData * test_data)
{
  GstPad *mux_sink1, *mux_sink2, *mux_sink3;

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

static void
setup_caps (GstElement * mpegtsmux, GstPad * src1, GstPad * src2, GstPad * src3)
{
  GstSegment segment;
  GstCaps *caps;

  gst_segment_init (&segment, GST_FORMAT_TIME);

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "nal", NULL);
  gst_pad_push_event (src1, gst_event_new_stream_start ("1"));
  gst_pad_push_event (src1, gst_event_new_caps (caps));
  gst_pad_push_event (src1, gst_event_new_segment (&segment));
  gst_pad_push_event (src2, gst_event_new_stream_start ("2"));
  gst_pad_push_event (src2, gst_event_new_caps (caps));
  gst_pad_push_event (src2, gst_event_new_segment (&segment));
  gst_caps_unref (caps);
  caps = gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 4,
      "stream-format", G_TYPE_STRING, "raw", "framed", G_TYPE_BOOLEAN, TRUE,
      NULL);
  gst_pad_push_event (src3, gst_event_new_stream_start ("3"));
  gst_pad_push_event (src3, gst_event_new_caps (caps));
  gst_pad_push_event (src3, gst_event_new_segment (&segment));
  gst_caps_unref (caps);
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
  data->thread = g_thread_try_new ("gst-check", pad_push_thread, data, NULL);

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

  link_src (mpegtsmux, &sink, &test_data);
  link_sinks (mpegtsmux, &src1, &src2, &src3, &test_data);
  gst_element_set_state (mpegtsmux, GST_STATE_PLAYING);
  setup_caps (mpegtsmux, src1, src2, src3);

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
  GstClockTime timestamp, stream_time, running_time;
  gboolean all_headers = TRUE;
  gint count = 0;
  TestData test_data = { 0, };
  ThreadData *thread_data_1, *thread_data_2, *thread_data_3, *thread_data_4;
  GstEvent *event;

  mpegtsmux = gst_check_setup_element ("mpegtsmux");

  link_src (mpegtsmux, &sink, &test_data);
  link_sinks (mpegtsmux, &src1, &src2, &src3, &test_data);
  gst_element_set_state (mpegtsmux, GST_STATE_PLAYING);
  setup_caps (mpegtsmux, src1, src2, src3);

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

static GstFlowReturn expected_flow;

static GstFlowReturn
flow_test_stat_chain_func (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);

  GST_INFO ("returning flow %s (%d)", gst_flow_get_name (expected_flow),
      expected_flow);
  return expected_flow;
}

GST_START_TEST (test_propagate_flow_status)
{
  GstElement *mux;
  gchar *padname;
  GstBuffer *inbuffer;
  GstCaps *caps;
  guint i;

  GstFlowReturn expected[] = { GST_FLOW_OK, GST_FLOW_FLUSHING, GST_FLOW_EOS,
    GST_FLOW_NOT_NEGOTIATED, GST_FLOW_ERROR, GST_FLOW_NOT_SUPPORTED
  };

  mux = setup_tsmux (&video_src_template, "sink_%d", &padname);
  gst_pad_set_chain_function (mysinkpad, flow_test_stat_chain_func);

  fail_unless (gst_element_set_state (mux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, mux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  for (i = 0; i < G_N_ELEMENTS (expected); ++i) {
    GstFlowReturn res;

    inbuffer = gst_buffer_new_and_alloc (1);
    ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

    expected_flow = expected[i];
    GST_INFO ("expecting flow %s (%d)", gst_flow_get_name (expected_flow),
        expected_flow);

    GST_BUFFER_TIMESTAMP (inbuffer) = i * GST_SECOND;

    res = gst_pad_push (mysrcpad, inbuffer);

    fail_unless_equals_int (res, expected[i]);
  }

  cleanup_tsmux (mux, padname);
  g_free (padname);
}

GST_END_TEST;

GST_START_TEST (test_multiple_state_change)
{
  GstElement *mux;
  gchar *padname;
  GstSegment segment;
  GstCaps *caps;
  size_t i;

  /* it's just a sample of all possible permutations of all states and their
   * transitions */
  GstState states[] = { GST_STATE_PLAYING, GST_STATE_PAUSED, GST_STATE_PLAYING,
    GST_STATE_READY, GST_STATE_PAUSED, GST_STATE_PLAYING, GST_STATE_NULL
  };

  size_t num_transitions_to_test = 10;

  mux = setup_tsmux (&video_src_template, "sink_%d", &padname);
  gst_pad_set_chain_function (mysinkpad, flow_test_stat_chain_func);
  gst_segment_init (&segment, GST_FORMAT_TIME);

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, mux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  for (i = 0; i < num_transitions_to_test; ++i) {
    GstState next_state = states[i % G_N_ELEMENTS (states)];
    fail_unless (gst_element_set_state (mux,
            next_state) == GST_STATE_CHANGE_SUCCESS,
        "could not set to %s", gst_element_state_get_name (next_state));

    /* push some buffers when playing - this triggers a lot of activity */
    if (GST_STATE_PLAYING == next_state) {
      GstBuffer *inbuffer;

      fail_unless (gst_pad_push_event (mysrcpad,
              gst_event_new_segment (&segment)));

      inbuffer = gst_buffer_new_and_alloc (1);
      ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

      expected_flow = GST_FLOW_OK;
      GST_BUFFER_PTS (inbuffer) = 0;
      fail_unless (GST_FLOW_OK == gst_pad_push (mysrcpad, inbuffer));
    }
  }

  cleanup_tsmux (mux, padname);
  g_free (padname);
}

GST_END_TEST;

static void
test_align_check_output (GList * bufs)
{
  GST_LOG ("%u buffers", g_list_length (bufs));
  while (bufs != NULL) {
    GstBuffer *buf = bufs->data;
    gsize size;

    size = gst_buffer_get_size (buf);
    GST_LOG ("buffer, size = %5u", (guint) size);
    fail_unless_equals_int (size, 7 * 188);
    bufs = bufs->next;
  }
}

GST_START_TEST (test_align)
{
  check_tsmux_pad (&video_src_template, VIDEO_CAPS_STRING, 0xE0, 0x1b,
      "sink_%d", test_align_check_output, 817, -1, 7);
}

GST_END_TEST;

static void
test_keyframe_propagation_check_output (GList * bufs)
{
  guint keyframe_count = 0;

  GST_LOG ("%u buffers", g_list_length (bufs));
  while (bufs != NULL) {
    GstBuffer *buf = bufs->data;
    gboolean keyunit;

    keyunit = !GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

    if (keyunit)
      ++keyframe_count;

    GST_LOG ("buffer, keyframe=%d", keyunit);
    bufs = bufs->next;
  }
  fail_unless_equals_int (keyframe_count, 50 / KEYFRAME_DISTANCE);
}

GST_START_TEST (test_keyframe_flag_propagation)
{
  check_tsmux_pad (&video_src_template, VIDEO_CAPS_STRING, 0xE0, 0x1b,
      "sink_%d", test_keyframe_propagation_check_output, 50, -1, 0);
}

GST_END_TEST;

static Suite *
mpegtsmux_suite (void)
{
  Suite *s = suite_create ("mpegtsmux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_audio);
  tcase_add_test (tc_chain, test_video);
  tcase_add_test (tc_chain, test_force_key_unit_event_downstream);
  tcase_add_test (tc_chain, test_force_key_unit_event_upstream);
  tcase_add_test (tc_chain, test_propagate_flow_status);
  tcase_add_test (tc_chain, test_multiple_state_change);
  tcase_add_test (tc_chain, test_align);
  tcase_add_test (tc_chain, test_keyframe_flag_propagation);

  return s;
}

GST_CHECK_MAIN (mpegtsmux);

/* GStreamer
 *
 * Copyright (C) 2009 Nokia Corporation and its subsidary(-ies)
 *               contact: <stefan.kost@nokia.com>
 * Copyright (C) 2012 Cisco Systems, Inc
 *               Authors: Kelley Rogers <kelro@cisco.com>
 *               Havard Graff <hgraff@cisco.com>
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
#include <gst/check/gsttestclock.h>

#include <gst/rtp/gstrtpbuffer.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;
/* we also have a list of src buffers */
static GList *inbuffers = NULL;
static gint num_dropped = 0;

#define RTP_CAPS_STRING    \
    "application/x-rtp, "               \
    "media = (string)audio, "           \
    "payload = (int) 0, "               \
    "clock-rate = (int) 8000, "         \
    "encoding-name = (string)PCMU"

#define RTP_FRAME_SIZE 20

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "clock-rate = (int) [ 1, 2147483647 ]")
    );

static void
buffer_dropped (gpointer data, GstMiniObject * obj)
{
  GST_DEBUG ("dropping buffer %p", obj);
  num_dropped++;
}

static GstElement *
setup_jitterbuffer (gint num_buffers)
{
  GstElement *jitterbuffer;
  GstClock *clock;
  GstBuffer *buffer;
  GstCaps *caps;
  /* a 20 sample audio block (2,5 ms) generated with
   * gst-launch audiotestsrc wave=silence blocksize=40 num-buffers=3 !
   *    "audio/x-raw,channels=1,rate=8000" ! mulawenc ! rtppcmupay !
   *     fakesink dump=1
   */
  guint8 in[] = {               /* first 4 bytes are rtp-header, next 4 bytes are timestamp */
    0x80, 0x80, 0x1c, 0x24, 0x46, 0xcd, 0xb7, 0x11, 0x3c, 0x3a, 0x7c, 0x5b,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };
  GstClockTime ts = G_GUINT64_CONSTANT (0);
  GstClockTime tso = gst_util_uint64_scale (RTP_FRAME_SIZE, GST_SECOND, 8000);
  /*guint latency = GST_TIME_AS_MSECONDS (num_buffers * tso); */
  gint i;

  GST_DEBUG ("setup_jitterbuffer");
  jitterbuffer = gst_check_setup_element ("rtpjitterbuffer");
  /* we need a clock here */
  clock = gst_system_clock_obtain ();
  gst_element_set_clock (jitterbuffer, clock);
  gst_object_unref (clock);
  /* setup latency */
  /* latency would be 7 for 3 buffers here, default is 200
     g_object_set (G_OBJECT (jitterbuffer), "latency", latency, NULL);
     GST_INFO_OBJECT (jitterbuffer, "set latency to %u ms", latency);
   */

  mysrcpad = gst_check_setup_src_pad (jitterbuffer, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (jitterbuffer, &sinktemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  /* create n buffers */
  caps = gst_caps_from_string (RTP_CAPS_STRING);
  gst_check_setup_events (mysrcpad, jitterbuffer, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  for (i = 0; i < num_buffers; i++) {
    buffer = gst_buffer_new_and_alloc (sizeof (in));
    gst_buffer_fill (buffer, 0, in, sizeof (in));
    GST_BUFFER_DTS (buffer) = ts;
    GST_BUFFER_PTS (buffer) = ts;
    GST_BUFFER_DURATION (buffer) = tso;
    gst_mini_object_weak_ref (GST_MINI_OBJECT (buffer), buffer_dropped, NULL);
    GST_DEBUG ("created buffer: %p", buffer);

    if (!i)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);

    inbuffers = g_list_append (inbuffers, buffer);

    /* hackish way to update the rtp header */
    in[1] = 0x00;
    in[3]++;                    /* seqnumber */
    in[7] += RTP_FRAME_SIZE;    /* inc. timestamp with framesize */
    ts += tso;
  }
  num_dropped = 0;

  return jitterbuffer;
}

static GstStateChangeReturn
start_jitterbuffer (GstElement * jitterbuffer)
{
  GstStateChangeReturn ret;
  GstClockTime now;
  GstClock *clock;

  clock = gst_element_get_clock (jitterbuffer);
  now = gst_clock_get_time (clock);
  gst_object_unref (clock);

  gst_element_set_base_time (jitterbuffer, now);
  ret = gst_element_set_state (jitterbuffer, GST_STATE_PLAYING);

  return ret;
}

static void
cleanup_jitterbuffer (GstElement * jitterbuffer)
{
  GST_DEBUG ("cleanup_jitterbuffer");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  g_list_free (inbuffers);
  inbuffers = NULL;

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (jitterbuffer);
  gst_check_teardown_sink_pad (jitterbuffer);
  gst_check_teardown_element (jitterbuffer);
}

static void
check_jitterbuffer_results (GstElement * jitterbuffer, gint num_buffers)
{
  GstBuffer *buffer;
  GList *node;
  GstClockTime ts = G_GUINT64_CONSTANT (0);
  GstClockTime tso = gst_util_uint64_scale (RTP_FRAME_SIZE, GST_SECOND, 8000);
  GstMapInfo map;
  guint16 prev_sn = 0, cur_sn;
  guint32 prev_ts = 0, cur_ts;

  /* sleep for twice the latency */
  g_usleep (400 * 1000);

  GST_INFO ("of %d buffer %d/%d received/dropped", num_buffers,
      g_list_length (buffers), num_dropped);
  /* if this fails, not all buffers have been processed */
  fail_unless_equals_int ((g_list_length (buffers) + num_dropped), num_buffers);

  /* check the buffer list */
  fail_unless_equals_int (g_list_length (buffers), num_buffers);
  for (node = buffers; node; node = g_list_next (node)) {
    fail_if ((buffer = (GstBuffer *) node->data) == NULL);
    fail_if (GST_BUFFER_PTS (buffer) != ts);
    fail_if (GST_BUFFER_DTS (buffer) != ts);
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    cur_sn = ((guint16) map.data[2] << 8) | map.data[3];
    cur_ts = ((guint32) map.data[4] << 24) | ((guint32) map.data[5] << 16) |
        ((guint32) map.data[6] << 8) | map.data[7];
    gst_buffer_unmap (buffer, &map);

    if (node != buffers) {
      fail_unless (cur_sn > prev_sn);
      fail_unless (cur_ts > prev_ts);

      prev_sn = cur_sn;
      prev_ts = cur_ts;
    }
    ts += tso;
  }
}

GST_START_TEST (test_push_forward_seq)
{
  GstElement *jitterbuffer;
  const guint num_buffers = 3;
  GstBuffer *buffer;
  GList *node;

  jitterbuffer = setup_jitterbuffer (num_buffers);
  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  /* push buffers: 0,1,2, */
  for (node = inbuffers; node; node = g_list_next (node)) {
    buffer = (GstBuffer *) node->data;
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  /* check the buffer list */
  check_jitterbuffer_results (jitterbuffer, num_buffers);

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

GST_START_TEST (test_push_backward_seq)
{
  GstElement *jitterbuffer;
  const guint num_buffers = 4;
  GstBuffer *buffer;
  GList *node;

  jitterbuffer = setup_jitterbuffer (num_buffers);
  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  /* push buffers: 0,3,2,1 */
  buffer = (GstBuffer *) inbuffers->data;
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  for (node = g_list_last (inbuffers); node != inbuffers;
      node = g_list_previous (node)) {
    buffer = (GstBuffer *) node->data;
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  /* check the buffer list */
  check_jitterbuffer_results (jitterbuffer, num_buffers);

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

GST_START_TEST (test_push_unordered)
{
  GstElement *jitterbuffer;
  const guint num_buffers = 4;
  GstBuffer *buffer;

  jitterbuffer = setup_jitterbuffer (num_buffers);
  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  /* push buffers; 0,2,1,3 */
  buffer = (GstBuffer *) inbuffers->data;
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  buffer = g_list_nth_data (inbuffers, 2);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  buffer = g_list_nth_data (inbuffers, 1);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  buffer = g_list_nth_data (inbuffers, 3);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  /* check the buffer list */
  check_jitterbuffer_results (jitterbuffer, num_buffers);

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

GST_START_TEST (test_basetime)
{
  GstElement *jitterbuffer;
  const guint num_buffers = 3;
  GstBuffer *buffer;
  GList *node;
  GstClockTime tso = gst_util_uint64_scale (RTP_FRAME_SIZE, GST_SECOND, 8000);

  jitterbuffer = setup_jitterbuffer (num_buffers);
  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  /* push buffers: 2,1,0 */
  for (node = g_list_last (inbuffers); node; node = g_list_previous (node)) {
    buffer = (GstBuffer *) node->data;
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  /* sleep for twice the latency */
  g_usleep (400 * 1000);

  /* if this fails, not all buffers have been processed */
  fail_unless_equals_int ((g_list_length (buffers) + num_dropped), num_buffers);

  buffer = (GstBuffer *) buffers->data;
  fail_unless (GST_BUFFER_DTS (buffer) != (num_buffers * tso));
  fail_unless (GST_BUFFER_PTS (buffer) != (num_buffers * tso));

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

static GstCaps *
request_pt_map (GstElement * jitterbuffer, guint pt)
{
  fail_unless (pt == 0);

  return gst_caps_from_string (RTP_CAPS_STRING);
}

GST_START_TEST (test_clear_pt_map)
{
  GstElement *jitterbuffer;
  const guint num_buffers = 10;
  gint i;
  GstBuffer *buffer;
  GList *node;

  jitterbuffer = setup_jitterbuffer (num_buffers);
  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  g_signal_connect (jitterbuffer, "request-pt-map", (GCallback)
      request_pt_map, NULL);

  /* push buffers: 0,1,2, */
  for (node = inbuffers, i = 0; node && i < 3; node = g_list_next (node), i++) {
    buffer = (GstBuffer *) node->data;
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  g_usleep (400 * 1000);

  g_signal_emit_by_name (jitterbuffer, "clear-pt-map", NULL);

  for (; node && i < 10; node = g_list_next (node), i++) {
    buffer = (GstBuffer *) node->data;
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  /* check the buffer list */
  check_jitterbuffer_results (jitterbuffer, num_buffers);

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;
static const guint payload_size = 160;
static const guint clock_rate = 8000;
static const guint pcmu_payload_type = 0;
static const guint test_ssrc = 0x01BADBAD;

typedef struct
{
  GstElement *jitter_buffer;
  GstPad *test_sink_pad, *test_src_pad;
  GstClock *clock;
  GAsyncQueue *buf_queue;
  GAsyncQueue *sink_event_queue;
  GAsyncQueue *src_event_queue;
  gint lost_event_count;
  gint rtx_event_count;
} TestData;

static GstCaps *
generate_caps (void)
{
  return gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "audio",
      "clock-rate", G_TYPE_INT, clock_rate,
      "encoding-name", G_TYPE_STRING, "PCMU",
      "payload", G_TYPE_INT, pcmu_payload_type,
      "ssrc", G_TYPE_UINT, test_ssrc, NULL);
}

static GstBuffer *
generate_test_buffer (GstClockTime gst_ts,
    gboolean marker_bit, guint seq_num, guint32 rtp_ts)
{
  GstBuffer *buf;
  guint8 *payload;
  guint i;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  buf = gst_rtp_buffer_new_allocate (payload_size, 0, 0);
  GST_BUFFER_DTS (buf) = gst_ts;
  GST_BUFFER_PTS (buf) = gst_ts;

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type (&rtp, pcmu_payload_type);
  gst_rtp_buffer_set_marker (&rtp, marker_bit);
  gst_rtp_buffer_set_seq (&rtp, seq_num);
  gst_rtp_buffer_set_timestamp (&rtp, rtp_ts);
  gst_rtp_buffer_set_ssrc (&rtp, test_ssrc);

  payload = gst_rtp_buffer_get_payload (&rtp);
  for (i = 0; i < payload_size; i++)
    payload[i] = 0xff;

  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

static GstFlowReturn
test_sink_pad_chain_cb (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  TestData *data = gst_pad_get_element_private (pad);
  g_async_queue_push (data->buf_queue, buffer);
  return GST_FLOW_OK;
}

static gboolean
test_sink_pad_event_cb (GstPad * pad, GstObject * parent, GstEvent * event)
{
  TestData *data = gst_pad_get_element_private (pad);
  const GstStructure *structure = gst_event_get_structure (event);

  GST_DEBUG ("got event %" GST_PTR_FORMAT, event);

  if (strcmp (gst_structure_get_name (structure), "GstRTPPacketLost") == 0) {
    data->lost_event_count++;
    GST_DEBUG ("lost event count %d", data->lost_event_count);
  }

  g_async_queue_push (data->sink_event_queue, event);
  return TRUE;
}

static gboolean
test_src_pad_event_cb (GstPad * pad, GstObject * parent, GstEvent * event)
{
  TestData *data = gst_pad_get_element_private (pad);
  const GstStructure *structure = gst_event_get_structure (event);

  GST_DEBUG ("got event %" GST_PTR_FORMAT, event);

  if (structure
      && strcmp (gst_structure_get_name (structure),
          "GstRTPRetransmissionRequest") == 0) {
    data->rtx_event_count++;
    GST_DEBUG ("rtx event count %d", data->rtx_event_count);
  }

  g_async_queue_push (data->src_event_queue, event);
  return TRUE;
}

static void
setup_testharness (TestData * data)
{
  GstPad *jb_sink_pad, *jb_src_pad;
  GstSegment seg;
  GstMiniObject *obj;
  GstCaps *caps;

  /* create the testclock */
  data->clock = gst_test_clock_new ();
  g_assert (data->clock);
  gst_test_clock_set_time (GST_TEST_CLOCK (data->clock), 0);

  /* rig up the jitter buffer */
  data->jitter_buffer = gst_element_factory_make ("rtpjitterbuffer", NULL);
  g_assert (data->jitter_buffer);
  gst_element_set_clock (data->jitter_buffer, data->clock);
  g_object_set (data->jitter_buffer, "do-lost", TRUE, NULL);
  g_assert_cmpint (gst_element_set_state (data->jitter_buffer,
          GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);

  /* set up the buf and event queues */
  data->buf_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
  data->sink_event_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
  data->src_event_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);

  data->lost_event_count = 0;
  data->rtx_event_count = 0;

  /* link in the test source-pad */
  data->test_src_pad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_element_private (data->test_src_pad, data);
  gst_pad_set_event_function (data->test_src_pad, test_src_pad_event_cb);
  jb_sink_pad = gst_element_get_static_pad (data->jitter_buffer, "sink");
  g_assert_cmpint (gst_pad_link (data->test_src_pad, jb_sink_pad), ==,
      GST_PAD_LINK_OK);
  gst_object_unref (jb_sink_pad);

  /* link in the test sink-pad */
  data->test_sink_pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_element_private (data->test_sink_pad, data);
  caps = generate_caps ();
  gst_pad_set_caps (data->test_sink_pad, caps);
  gst_pad_set_chain_function (data->test_sink_pad, test_sink_pad_chain_cb);
  gst_pad_set_event_function (data->test_sink_pad, test_sink_pad_event_cb);
  jb_src_pad = gst_element_get_static_pad (data->jitter_buffer, "src");
  g_assert_cmpint (gst_pad_link (jb_src_pad, data->test_sink_pad), ==,
      GST_PAD_LINK_OK);
  gst_object_unref (jb_src_pad);

  g_assert (gst_pad_set_active (data->test_src_pad, TRUE));
  g_assert (gst_pad_set_active (data->test_sink_pad, TRUE));

  gst_segment_init (&seg, GST_FORMAT_TIME);

  gst_pad_push_event (data->test_src_pad,
      gst_event_new_stream_start ("stream0"));
  gst_pad_set_caps (data->test_src_pad, caps);
  gst_pad_push_event (data->test_src_pad, gst_event_new_segment (&seg));
  gst_caps_unref (caps);

  obj = g_async_queue_pop (data->sink_event_queue);
  gst_mini_object_unref (obj);
  obj = g_async_queue_pop (data->sink_event_queue);
  gst_mini_object_unref (obj);
  obj = g_async_queue_pop (data->sink_event_queue);
  gst_mini_object_unref (obj);
}

static void
destroy_testharness (TestData * data)
{
  /* clean up */
  g_assert_cmpint (gst_element_set_state (data->jitter_buffer, GST_STATE_NULL),
      ==, GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (data->jitter_buffer);
  data->jitter_buffer = NULL;

  gst_object_unref (data->test_src_pad);
  data->test_src_pad = NULL;

  gst_object_unref (data->test_sink_pad);
  data->test_sink_pad = NULL;

  gst_object_unref (data->clock);
  data->clock = NULL;

  g_async_queue_unref (data->buf_queue);
  data->buf_queue = NULL;

  g_async_queue_unref (data->sink_event_queue);
  data->sink_event_queue = NULL;
  g_async_queue_unref (data->src_event_queue);
  data->src_event_queue = NULL;

  data->lost_event_count = 0;
}

static void
verify_lost_event (GstEvent * event, guint32 expected_seqnum,
    GstClockTime expected_timestamp, GstClockTime expected_duration,
    gboolean expected_late)
{
  const GstStructure *s = gst_event_get_structure (event);
  const GValue *value;
  guint32 seqnum;
  GstClockTime timestamp;
  GstClockTime duration;
  gboolean late;

  g_assert (gst_structure_get_uint (s, "seqnum", &seqnum));

  value = gst_structure_get_value (s, "timestamp");
  g_assert (value && G_VALUE_HOLDS_UINT64 (value));
  timestamp = g_value_get_uint64 (value);

  value = gst_structure_get_value (s, "duration");
  g_assert (value && G_VALUE_HOLDS_UINT64 (value));
  duration = g_value_get_uint64 (value);

  g_assert (gst_structure_get_boolean (s, "late", &late));

  g_assert_cmpint (seqnum, ==, expected_seqnum);
  g_assert_cmpint (timestamp, ==, expected_timestamp);
  g_assert_cmpint (duration, ==, expected_duration);
  g_assert (late == expected_late);

  gst_event_unref (event);
}

static void
verify_rtx_event (GstEvent * event, guint32 expected_seqnum,
    GstClockTime expected_timestamp, guint expected_delay,
    GstClockTime expected_spacing)
{
  const GstStructure *s = gst_event_get_structure (event);
  const GValue *value;
  guint32 seqnum;
  GstClockTime timestamp, spacing;
  guint delay;

  g_assert (gst_structure_get_uint (s, "seqnum", &seqnum));

  value = gst_structure_get_value (s, "running-time");
  g_assert (value && G_VALUE_HOLDS_UINT64 (value));
  timestamp = g_value_get_uint64 (value);

  value = gst_structure_get_value (s, "delay");
  g_assert (value && G_VALUE_HOLDS_UINT (value));
  delay = g_value_get_uint (value);

  value = gst_structure_get_value (s, "packet-spacing");
  g_assert (value && G_VALUE_HOLDS_UINT64 (value));
  spacing = g_value_get_uint64 (value);

  g_assert_cmpint (seqnum, ==, expected_seqnum);
  g_assert_cmpint (timestamp, ==, expected_timestamp);
  g_assert_cmpint (delay, ==, expected_delay);
  g_assert_cmpint (spacing, ==, expected_spacing);

  gst_event_unref (event);
}

GST_START_TEST (test_only_one_lost_event_on_large_gaps)
{
  TestData data;
  GstClockID id, test_id;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 200;
  guint buffer_size_ms = (payload_size * 1000) / clock_rate;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  setup_testharness (&data);

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);
  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* wait for the first buffer to be synced to timestamp + latency */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);

  /* increase the time to timestamp + latency and release the wait */
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
      jb_latency_ms * GST_MSECOND);
  test_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (test_id == id);
  gst_clock_id_unref (test_id);
  gst_clock_id_unref (id);

  /* check for the buffer coming out that was pushed in */
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  g_assert_cmpint (GST_BUFFER_DTS (out_buf), ==, 0);
  g_assert_cmpint (GST_BUFFER_PTS (out_buf), ==, 0);
  gst_buffer_unref (out_buf);

  /* move time ahead 10 seconds */
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);

  /* wait a bit */
  g_usleep (G_USEC_PER_SEC / 10);

  /* check that no buffers have been pushed out and no pending waits */
  g_assert_cmpint (g_async_queue_length (data.buf_queue), ==, 0);
  g_assert (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock),
          &id) == FALSE);

  /* a buffer now arrives perfectly on time */
  in_buf = generate_test_buffer (10 * GST_SECOND, FALSE, 500, 500 * 160);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* release the wait */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_advance_time (GST_TEST_CLOCK (data.clock), GST_MSECOND * 20);
  test_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == test_id);
  gst_clock_id_unref (test_id);
  gst_clock_id_unref (id);

  /* we should now receive a packet-lost-event for buffers 1 through 489 */
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 1, 1 * GST_MSECOND * 20, GST_MSECOND * 20 * 490,
      TRUE);

  /* churn through sync_times until the new buffer gets pushed out */
  while (g_async_queue_length (data.buf_queue) < 1) {
    if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock), &id)) {
      GstClockTime t = gst_clock_id_get_time (id);
      if (t > gst_clock_get_time (data.clock)) {
        gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), t);
      }
      test_id =
          gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
      gst_clock_id_unref (test_id);
      gst_clock_id_unref (id);
    }
  }

  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
  g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, 500);
  gst_rtp_buffer_unmap (&rtp);
  g_assert_cmpint (GST_BUFFER_DTS (out_buf), ==, (10 * GST_SECOND));
  g_assert_cmpint (GST_BUFFER_PTS (out_buf), ==, (10 * GST_SECOND));
  gst_buffer_unref (out_buf);

  /* we get as many lost events as the the number of buffers the jitterbuffer
   * is able to wait for (+ the one we already got) */
  g_assert_cmpint (data.lost_event_count, ==, jb_latency_ms / buffer_size_ms);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_two_lost_one_arrives_in_time)
{
  TestData data;
  GstClockID id, test_id;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 100;
  GstClockTime buffer_time, now;
  gint b;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  setup_testharness (&data);

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  now = jb_latency_ms * GST_MSECOND;
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), now);
  test_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (test_id == id);
  gst_clock_id_unref (test_id);
  gst_clock_id_unref (id);
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);

  /* push some buffers arriving in perfect time! */
  for (b = 1; b < 3; b++) {
    buffer_time = b * GST_MSECOND * 20;
    in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
    gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), now + buffer_time);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
    gst_buffer_unref (out_buf);

    /* check for the buffer coming out that was pushed in */
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    g_assert_cmpint (GST_BUFFER_DTS (out_buf), ==, buffer_time);
    g_assert_cmpint (GST_BUFFER_PTS (out_buf), ==, buffer_time);
  }
  gst_buffer_unref (out_buf);

  /* hop over 2 packets and make another one (gap of 2) */
  b = 5;
  buffer_time = b * GST_MSECOND * 20;
  in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* verify that the jitterbuffer now wait for the latest moment it can push */
  /* the first lost buffer (buffer 3) out on (buffer-timestamp (60) + latency (10) = 70) */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  g_assert_cmpint (gst_clock_id_get_time (id), ==,
      (3 * GST_MSECOND * 20) + (jb_latency_ms * GST_MSECOND));

  /* let the time expire... */
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
      gst_clock_id_get_time (id));
  test_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (test_id == id);
  gst_clock_id_unref (test_id);
  gst_clock_id_unref (id);

  /* we should now receive a packet-lost-event for buffer 3 */
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 3, 3 * GST_MSECOND * 20, GST_MSECOND * 20,
      FALSE);

  /* buffer 4 now arrives just in time (time is 70, buffer 4 expires at 90) */
  b = 4;
  buffer_time = b * GST_MSECOND * 20;
  in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* verify that buffer 4 made it through! */
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
  g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, 4);
  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (out_buf);

  /* and see that buffer 5 now arrives in a normal fashion */
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  g_assert (!GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
  g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, 5);
  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (out_buf);

  /* should still have only seen 1 packet lost event */
  g_assert_cmpint (data.lost_event_count, ==, 1);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_late_packets_still_makes_lost_events)
{
  TestData data;
  GstClockID id, test_id;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 10;
  GstClockTime buffer_time;
  gint b;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  setup_testharness (&data);

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);

  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  test_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (test_id == id);
  gst_clock_id_unref (id);
  gst_clock_id_unref (test_id);
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);

  /* push some buffers in! */
  for (b = 1; b < 3; b++) {
    buffer_time = b * GST_MSECOND * 20;
    in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
    gst_buffer_unref (out_buf);

    /* check for the buffer coming out that was pushed in */
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    g_assert_cmpint (GST_BUFFER_DTS (out_buf), ==, buffer_time);
    g_assert_cmpint (GST_BUFFER_PTS (out_buf), ==, buffer_time);
  }
  gst_buffer_unref (out_buf);

  /* hop over 2 packets and make another one (gap of 2) */
  b = 5;
  buffer_time = b * GST_MSECOND * 20;
  in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* we should now receive a packet-lost-event for buffer 3 and 4 */
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 3, 3 * GST_MSECOND * 20, GST_MSECOND * 20 * 2,
      TRUE);

  /* verify that buffer 5 made it through! */
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
  g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, 5);
  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (out_buf);

  /* should still have only seen 1 packet lost event */
  g_assert_cmpint (data.lost_event_count, ==, 1);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_all_packets_are_timestamped_zero)
{
  TestData data;
  GstClockID id, test_id;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 10;
  gint b;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  setup_testharness (&data);

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);

  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  test_id = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (test_id == id);
  gst_clock_id_unref (test_id);
  gst_clock_id_unref (id);
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);

  /* push some buffers in! */
  for (b = 1; b < 3; b++) {
    in_buf = generate_test_buffer (0, TRUE, b, 0);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
    gst_buffer_unref (out_buf);

    /* check for the buffer coming out that was pushed in */
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    g_assert_cmpint (GST_BUFFER_DTS (out_buf), ==, 0);
    g_assert_cmpint (GST_BUFFER_PTS (out_buf), ==, 0);
  }
  gst_buffer_unref (out_buf);

  /* hop over 2 packets and make another one (gap of 2) */
  b = 5;
  in_buf = generate_test_buffer (0, TRUE, b, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* we should now receive a packet-lost-event for buffer 3 and 4 */
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  verify_lost_event (out_event, 3, 0, 0, FALSE);

  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  verify_lost_event (out_event, 4, 0, 0, FALSE);

  g_assert_cmpint (data.lost_event_count, ==, 2);

  /* verify that buffer 5 made it through! */
  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
  g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, 5);
  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (out_buf);

  /* should still have only seen 2 packet lost events */
  g_assert_cmpint (data.lost_event_count, ==, 2);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_rtx_expected_next)
{
  TestData data;
  GstClockID id, tid;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 200;

  setup_testharness (&data);
  g_object_set (data.jitter_buffer, "do-retransmission", TRUE, NULL);
  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);
  g_object_set (data.jitter_buffer, "rtx-retry-period", 120, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);

  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 20 * GST_MSECOND);

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_clock_id_unref (id);

  /* put second buffer, the jitterbuffer should now know that the packet spacing
   * is 20ms and should ask for retransmission of seqnum 2 in 20ms */
  in_buf = generate_test_buffer (20 * GST_MSECOND, TRUE, 1, 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 60 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (tid == id);
  gst_clock_id_unref (tid);
  gst_clock_id_unref (id);

  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 40 * GST_MSECOND, 20, 20 * GST_MSECOND);

  /* now we wait for the next timeout */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 100 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (tid);
  gst_clock_id_unref (id);

  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 40 * GST_MSECOND, 60, 20 * GST_MSECOND);

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 140 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (tid);
  gst_clock_id_unref (id);

  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 40 * GST_MSECOND, 100, 20 * GST_MSECOND);

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 200 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (tid);
  gst_clock_id_unref (id);

  out_buf = g_async_queue_pop (data.buf_queue);
  g_assert (out_buf != NULL);
  gst_buffer_unref (out_buf);


  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 260 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (tid == id);
  gst_clock_id_unref (tid);
  gst_clock_id_unref (id);

  /* we should now receive a packet-lost-event for buffer 2 */
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  verify_lost_event (out_event, 2, 40 * GST_MSECOND, 20 * GST_MSECOND, FALSE);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_rtx_two_missing)
{
  TestData data;
  GstClockID id, tid;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 200;
  gint i;
  GstStructure *rtx_stats;
  const GValue *rtx_stat;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  setup_testharness (&data);
  g_object_set (data.jitter_buffer, "do-retransmission", TRUE, NULL);
  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);
  g_object_set (data.jitter_buffer, "rtx-retry-period", 120, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);

  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 20 * GST_MSECOND);

  /* put second buffer, the jitterbuffer should now know that the packet spacing
   * is 20ms and should ask for retransmission of seqnum 2 at 60ms */
  in_buf = generate_test_buffer (20 * GST_MSECOND, TRUE, 1, 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* push buffer 4, 2 and 3 are missing now, we should get retransmission events
   * for 3 at 100ms*/
  in_buf = generate_test_buffer (80 * GST_MSECOND, TRUE, 4, 4 * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* wait for first retransmission request */
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 60 * GST_MSECOND);
  do {
    gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
    tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
    gst_clock_id_unref (id);
    gst_clock_id_unref (tid);
  } while (id != tid);

  /* we should have 2 events now, one for 2 and another for 3 */
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 40 * GST_MSECOND, 20, 20 * GST_MSECOND);
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 3, 60 * GST_MSECOND, 0, 20 * GST_MSECOND);

  /* now we wait for the next timeout */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 100 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  /* we should have 2 events now, one for 2 and another for 3 */
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 40 * GST_MSECOND, 60, 20 * GST_MSECOND);
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 3, 60 * GST_MSECOND, 40, 20 * GST_MSECOND);

  /* make buffer 3 */
  in_buf = generate_test_buffer (60 * GST_MSECOND, TRUE, 3, 3 * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* make more buffers */
  for (i = 5; i < 15; i++) {
    in_buf = generate_test_buffer (i * 20 * GST_MSECOND, TRUE, i, i * 160);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
  }

  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 140 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  /* now we only get requests for 2 */
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 40 * GST_MSECOND, 100, 20 * GST_MSECOND);

  /* this is when buffer 0 deadline expires */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 200 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  for (i = 0; i < 2; i++) {
    GST_DEBUG ("popping %d", i);
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
    g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, i);
    gst_rtp_buffer_unmap (&rtp);
    gst_buffer_unref (out_buf);
  }

  /* this is when 2 is lost */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 240 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  /* we should now receive a packet-lost-event for buffer 2 */
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  verify_lost_event (out_event, 2, 40 * GST_MSECOND, 20 * GST_MSECOND, FALSE);

  /* verify that buffers made it through! */
  for (i = 3; i < 15; i++) {
    GST_DEBUG ("popping %d", i);
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
    g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, i);
    gst_rtp_buffer_unmap (&rtp);
    gst_buffer_unref (out_buf);
  }
  /* should still have only seen 1 packet lost events */
  g_assert_cmpint (data.lost_event_count, ==, 1);

  g_object_get (data.jitter_buffer, "stats", &rtx_stats, NULL);

  rtx_stat = gst_structure_get_value (rtx_stats, "rtx-count");
  g_assert_cmpuint (g_value_get_uint64 (rtx_stat), ==, 5);

  rtx_stat = gst_structure_get_value (rtx_stats, "rtx-success-count");
  g_assert_cmpuint (g_value_get_uint64 (rtx_stat), ==, 1);

  rtx_stat = gst_structure_get_value (rtx_stats, "rtx-rtt");
  g_assert_cmpuint (g_value_get_uint64 (rtx_stat), ==, 0);
  gst_structure_free (rtx_stats);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_rtx_packet_delay)
{
  TestData data;
  GstClockID id, tid;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 200;
  gint i;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  setup_testharness (&data);
  g_object_set (data.jitter_buffer, "do-retransmission", TRUE, NULL);
  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);
  g_object_set (data.jitter_buffer, "rtx-retry-period", 120, NULL);

  /* push the first buffer in */
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  GST_BUFFER_FLAG_SET (in_buf, GST_BUFFER_FLAG_DISCONT);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 20 * GST_MSECOND);

  /* put second buffer, the jitterbuffer should now know that the packet spacing
   * is 20ms and should ask for retransmission of seqnum 2 at 60ms */
  in_buf = generate_test_buffer (20 * GST_MSECOND, TRUE, 1, 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* push buffer 8, 2 -> 7 are missing now. note that the rtp time is the same
   * as packet 1 because it was part of a fragmented payload. This means that
   * the estimate for 2 could be refined now to 20ms. also packet 2, 3 and 4 are
   * exceeding the max allowed reorder distance and should request a
   * retransmission right away */
  in_buf = generate_test_buffer (20 * GST_MSECOND, TRUE, 8, 8 * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* we should now receive retransmission requests for 2 -> 5 */
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 2, 20 * GST_MSECOND, 40, 20 * GST_MSECOND);

  for (i = 3; i < 5; i++) {
    GST_DEBUG ("popping %d", i);
    out_event = g_async_queue_pop (data.src_event_queue);
    g_assert (out_event != NULL);
    verify_rtx_event (out_event, i, 20 * GST_MSECOND, 0, 20 * GST_MSECOND);
  }
  g_assert_cmpint (data.rtx_event_count, ==, 3);

  /* push 9, this should immediately request retransmission of 5 */
  in_buf = generate_test_buffer (20 * GST_MSECOND, TRUE, 9, 9 * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* we should now receive retransmission requests for 5 */
  out_event = g_async_queue_pop (data.src_event_queue);
  g_assert (out_event != NULL);
  verify_rtx_event (out_event, 5, 20 * GST_MSECOND, 0, 20 * GST_MSECOND);

  /* wait for timeout for rtx 6 -> 7 */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  for (i = 6; i < 8; i++) {
    GST_DEBUG ("popping %d", i);
    out_event = g_async_queue_pop (data.src_event_queue);
    g_assert (out_event != NULL);
    verify_rtx_event (out_event, i, 20 * GST_MSECOND, 0, 20 * GST_MSECOND);
  }

  /* churn through sync_times until the new buffer gets pushed out */
  while (g_async_queue_length (data.buf_queue) < 1) {
    if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock), &id)) {
      GstClockTime t = gst_clock_id_get_time (id);
      if (t >= 240 * GST_MSECOND) {
        gst_clock_id_unref (id);
        break;
      }
      if (t > gst_clock_get_time (data.clock)) {
        gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), t);
      }
      tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
      gst_clock_id_unref (id);
      gst_clock_id_unref (tid);
    }
  }

  /* verify that buffer 0 and 1 made it through! */
  for (i = 0; i < 2; i++) {
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    if (i == 0)
      g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
    gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
    g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, i);
    gst_rtp_buffer_unmap (&rtp);
    gst_buffer_unref (out_buf);
  }

  /* churn through sync_times until the next buffer gets pushed out */
  while (g_async_queue_length (data.buf_queue) < 1) {
    if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock), &id)) {
      GstClockTime t = gst_clock_id_get_time (id);
      if (t >= 240 * GST_MSECOND) {
        gst_clock_id_unref (id);
        break;
      }
      if (t > gst_clock_get_time (data.clock)) {
        gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), t);
      }
      tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
      gst_clock_id_unref (id);
      gst_clock_id_unref (tid);
    }
  }

  for (i = 2; i < 8; i++) {
    GST_DEBUG ("popping lost event %d", i);
    out_event = g_async_queue_pop (data.sink_event_queue);
    g_assert (out_event != NULL);
    verify_lost_event (out_event, i, 20 * GST_MSECOND, 0, FALSE);
  }

  /* verify that buffer 8 made it through! */
  for (i = 8; i < 10; i++) {
    GST_DEBUG ("popping buffer %d", i);
    out_buf = g_async_queue_pop (data.buf_queue);
    g_assert (out_buf != NULL);
    if (i == 8)
      g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
    gst_rtp_buffer_map (out_buf, GST_MAP_READ, &rtp);
    g_assert_cmpint (gst_rtp_buffer_get_seq (&rtp), ==, i);
    gst_rtp_buffer_unmap (&rtp);
    gst_buffer_unref (out_buf);
  }

  GST_DEBUG ("waiting for 240ms");
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 240 * GST_MSECOND);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  g_assert (id == tid);
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  GST_DEBUG ("popping lost event 10");
  out_event = g_async_queue_pop (data.sink_event_queue);
  g_assert (out_event != NULL);
  verify_lost_event (out_event, 10, 40 * GST_MSECOND, 20 * GST_MSECOND, FALSE);

  /* should have seen 6 packet lost events */
  g_assert_cmpint (data.lost_event_count, ==, 7);
  g_assert_cmpint (data.rtx_event_count, ==, 26);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_gap_exceeds_latency)
{
  TestData data;
  GstBuffer *in_buf, *out_buf;
  GstClockID id, tid;
  GstEvent *out_event;
  guint32 timestamp_ms = 0;
  guint32 last_ts = 0;
  gint jb_latency_ms = 200;
  guint32 rtp_ts = 0;
  guint32 last_rtp = 0;
  const GstStructure *s = NULL;
  guint32 seqnum = 0;
  gint i;

  setup_testharness (&data);
  g_object_set (data.jitter_buffer, "do-retransmission", TRUE, NULL);
  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);
  g_object_set (data.jitter_buffer, "rtx-retry-period", 120, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);
  in_buf = generate_test_buffer (timestamp_ms * GST_MSECOND, TRUE, 0, rtp_ts);
  GST_BUFFER_FLAG_SET (in_buf, GST_BUFFER_FLAG_DISCONT);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  timestamp_ms += 20;
  rtp_ts += 160;
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
      timestamp_ms * GST_MSECOND);

  in_buf = generate_test_buffer (timestamp_ms * GST_MSECOND, TRUE, 1, rtp_ts);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
  last_rtp = rtp_ts;
  last_ts = timestamp_ms;

  /*  Allow seqnum 2 to be declared lost */
  do {
    out_event = g_async_queue_try_pop (data.sink_event_queue);
    if (!out_event) {
      if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock),
              &id)) {

        GstClockTime t = gst_clock_id_get_time (id);
        if (t > gst_clock_get_time (data.clock)) {
          gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), t);
        }
        tid =
            gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
        gst_clock_id_unref (id);
        gst_clock_id_unref (tid);
      }
    }
  } while (!out_event);

  out_buf = g_async_queue_pop (data.buf_queue);
  gst_buffer_unref (out_buf);

  out_buf = g_async_queue_pop (data.buf_queue);
  gst_buffer_unref (out_buf);

  timestamp_ms += (20 * 15);
  s = gst_event_get_structure (out_event);
  g_assert (gst_structure_get_uint (s, "seqnum", &seqnum));
  g_assert_cmpint (seqnum, ==, 2);
  gst_event_unref (out_event);

  /*  Now data comes in again, a "bulk" lost packet is created for 3 -> 6 */
  rtp_ts += (160 * 15);
  in_buf = generate_test_buffer (timestamp_ms * GST_MSECOND, TRUE, 16, rtp_ts);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 60;
  last_rtp += 480;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 8, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 9, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 10, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 11, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 12, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 13, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 14, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  last_ts += 20;
  last_rtp += 160;
  in_buf = generate_test_buffer (last_ts * GST_MSECOND, TRUE, 15, last_rtp);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  /* Wait for data to be pushed. */
  while (g_async_queue_length (data.buf_queue) < 1) {
    if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock), &id)) {
      GstClockTime t = gst_clock_id_get_time (id);
      if (t > gst_clock_get_time (data.clock)) {
        gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), t);
      }
      tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
      gst_clock_id_unref (id);
      gst_clock_id_unref (tid);
    }
  }

  out_event = g_async_queue_pop (data.sink_event_queue);
  s = gst_event_get_structure (out_event);
  g_assert (gst_structure_get_uint (s, "seqnum", &seqnum));
  g_assert_cmpint (seqnum, ==, 3);
  gst_event_unref (out_event);

  out_event = g_async_queue_pop (data.sink_event_queue);
  s = gst_event_get_structure (out_event);
  g_assert (gst_structure_get_uint (s, "seqnum", &seqnum));
  g_assert_cmpint (seqnum, ==, 7);
  gst_event_unref (out_event);

  /* 8 */
  for (i = 8; i <= 16; i++) {
    out_buf = g_async_queue_pop (data.buf_queue);
    GST_DEBUG ("pop %d", i);
    gst_buffer_unref (out_buf);
  }

  do {
    out_event = g_async_queue_try_pop (data.sink_event_queue);
    if (!out_event) {
      if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock),
              &id)) {

        GstClockTime t = gst_clock_id_get_time (id);
        if (t > gst_clock_get_time (data.clock)) {
          gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), t);
        }
        tid =
            gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
        gst_clock_id_unref (id);
        gst_clock_id_unref (tid);
      }
    }
  } while (!out_event);

  /* and lost of 17 */
  s = gst_event_get_structure (out_event);
  g_assert (gst_structure_get_uint (s, "seqnum", &seqnum));
  g_assert_cmpint (seqnum, ==, 17);
  gst_event_unref (out_event);

  destroy_testharness (&data);
}

GST_END_TEST;


static Suite *
rtpjitterbuffer_suite (void)
{
  Suite *s = suite_create ("rtpjitterbuffer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_push_forward_seq);
  tcase_add_test (tc_chain, test_push_backward_seq);
  tcase_add_test (tc_chain, test_push_unordered);
  tcase_add_test (tc_chain, test_basetime);
  tcase_add_test (tc_chain, test_clear_pt_map);
  tcase_add_test (tc_chain, test_only_one_lost_event_on_large_gaps);
  tcase_add_test (tc_chain, test_two_lost_one_arrives_in_time);
  tcase_add_test (tc_chain, test_late_packets_still_makes_lost_events);
  tcase_add_test (tc_chain, test_all_packets_are_timestamped_zero);
  tcase_add_test (tc_chain, test_rtx_expected_next);
  tcase_add_test (tc_chain, test_rtx_two_missing);
  tcase_add_test (tc_chain, test_rtx_packet_delay);
  tcase_add_test (tc_chain, test_gap_exceeds_latency);

  return s;
}

GST_CHECK_MAIN (rtpjitterbuffer);

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
    GST_BUFFER_TIMESTAMP (buffer) = ts;
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
    fail_if (GST_BUFFER_TIMESTAMP (buffer) != ts);
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
  fail_unless (GST_BUFFER_TIMESTAMP (buffer) != (num_buffers * tso));

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

#if 0
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
  GAsyncQueue *event_queue;
  gint lost_event_count;
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

  buf = gst_rtp_buffer_new_allocate (payload_size, 0, 0);
  GST_BUFFER_TIMESTAMP (buf) = gst_ts;
  GST_BUFFER_CAPS (buf) = generate_caps ();
  gst_rtp_buffer_set_payload_type (buf, pcmu_payload_type);
  gst_rtp_buffer_set_marker (buf, marker_bit);
  gst_rtp_buffer_set_seq (buf, seq_num);
  gst_rtp_buffer_set_timestamp (buf, rtp_ts);
  gst_rtp_buffer_set_ssrc (buf, test_ssrc);

  payload = gst_rtp_buffer_get_payload (buf);
  for (i = 0; i < payload_size; i++)
    payload[i] = 0xff;

  return buf;
}

static GstFlowReturn
test_sink_pad_chain_cb (GstPad * pad, GstBuffer * buffer)
{
  TestData *data = gst_pad_get_element_private (pad);
  g_async_queue_push (data->buf_queue, buffer);
  return GST_FLOW_OK;
}

static gboolean
test_sink_pad_event_cb (GstPad * pad, GstEvent * event)
{
  TestData *data = gst_pad_get_element_private (pad);
  const GstStructure *structure = gst_event_get_structure (event);
  if (strcmp (gst_structure_get_name (structure), "GstRTPPacketLost") == 0)
    data->lost_event_count++;

  g_async_queue_push (data->event_queue, event);
  return TRUE;
}

static void
setup_testharness (TestData * data)
{
  GstPad *jb_sink_pad, *jb_src_pad;

  // create the testclock
  data->clock = gst_test_clock_new ();
  g_assert (data->clock);
  gst_test_clock_set_time (GST_TEST_CLOCK (data->clock), 0);

  // rig up the jitter buffer
  data->jitter_buffer = gst_element_factory_make ("gstrtpjitterbuffer", NULL);
  g_assert (data->jitter_buffer);
  gst_element_set_clock (data->jitter_buffer, data->clock);
  g_object_set (data->jitter_buffer, "do-lost", TRUE, NULL);
  g_assert_cmpint (gst_element_set_state (data->jitter_buffer,
          GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);

  // link in the test source-pad
  data->test_src_pad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_caps (data->test_src_pad, generate_caps ());
  jb_sink_pad = gst_element_get_pad (data->jitter_buffer, "sink");
  g_assert_cmpint (gst_pad_link (data->test_src_pad, jb_sink_pad), ==,
      GST_PAD_LINK_OK);
  g_assert (gst_pad_set_active (data->test_src_pad, TRUE));
  gst_object_unref (jb_sink_pad);

  // link in the test sink-pad
  data->test_sink_pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_pad_set_caps (data->test_sink_pad, generate_caps ());
  gst_pad_set_chain_function (data->test_sink_pad, test_sink_pad_chain_cb);
  gst_pad_set_event_function (data->test_sink_pad, test_sink_pad_event_cb);
  jb_src_pad = gst_element_get_pad (data->jitter_buffer, "src");
  g_assert_cmpint (gst_pad_link (jb_src_pad, data->test_sink_pad), ==,
      GST_PAD_LINK_OK);
  g_assert (gst_pad_set_active (data->test_sink_pad, TRUE));
  gst_object_unref (jb_src_pad);

  // set up the buf and event queues
  data->buf_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);
  data->event_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);

  data->lost_event_count = 0;
  gst_pad_set_element_private (data->test_sink_pad, data);
}

static void
destroy_testharness (TestData * data)
{
  // clean up
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

  g_async_queue_unref (data->event_queue);
  data->event_queue = NULL;

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
}

GST_START_TEST (test_only_one_lost_event_on_large_gaps)
{
  TestData data;
  GstTestClockPendingID id;
  guint64 timeout;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 200;
  guint buffer_size_ms = (payload_size * 1000) / clock_rate;

  setup_testharness (&data);
  timeout = 20 * G_USEC_PER_SEC;

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  // push the first buffer in
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  // wait for the first buffer to be synced to timestamp + latency
  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));

  // increase the time to timestamp + latency and release the wait
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
      jb_latency_ms * GST_MSECOND);
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);

  // check for the buffer coming out that was pushed in
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);
  g_assert_cmpint (GST_BUFFER_TIMESTAMP (out_buf), ==, 0);

  // move time ahead 10 seconds
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);

  // wait a bit
  g_usleep (G_USEC_PER_SEC / 10);

  // check that no buffers have been pushed out and no pending waits
  g_assert_cmpint (g_async_queue_length (data.buf_queue), ==, 0);
  g_assert (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock),
          &id) == FALSE);

  // a buffer now arrives perfectly on time
  in_buf = generate_test_buffer (10 * GST_SECOND, FALSE, 500, 500 * 160);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  // release the wait
  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);

  // we should now receive a packet-lost-event for buffers 1 through 489
  out_event = g_async_queue_timeout_pop (data.event_queue, timeout);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 1, 1 * GST_MSECOND * 20, GST_MSECOND * 20 * 489,
      TRUE);

  // churn through sync_times until the new buffer gets pushed out
  while (g_async_queue_length (data.buf_queue) < 1) {
    if (gst_test_clock_peek_next_pending_id (GST_TEST_CLOCK (data.clock), &id)) {
      if (id.time > gst_clock_get_time (data.clock)) {
        gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), id.time);
        g_print ("setting time to %" GST_TIME_FORMAT "\n",
            GST_TIME_ARGS (id.time));
      }
      gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
    }
  }

  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  g_assert_cmpint (gst_rtp_buffer_get_seq (out_buf), ==, 500);
  g_assert_cmpint (GST_BUFFER_TIMESTAMP (out_buf), ==, (10 * GST_SECOND));

  // we get as many lost events as the the number of buffers the jitterbuffer
  // is able to wait for (+ the one we already got)
  g_assert_cmpint (data.lost_event_count, ==,
      jb_latency_ms / buffer_size_ms + 1);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_two_lost_one_arrives_in_time)
{
  TestData data;
  GstTestClockPendingID id;
  guint64 timeout;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 10;
  GstClockTime buffer_time;
  gint b;

  setup_testharness (&data);
  timeout = 20 * G_USEC_PER_SEC;

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  // push the first buffer in
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);
  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
      jb_latency_ms * GST_MSECOND);
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);

  // push some buffers arriving in perfect time!
  for (b = 1; b < 3; b++) {
    buffer_time = b * GST_MSECOND * 20;
    in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
    gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), buffer_time);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

    // check for the buffer coming out that was pushed in
    out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
    g_assert (out_buf != NULL);
    g_assert_cmpint (GST_BUFFER_TIMESTAMP (out_buf), ==, buffer_time);
  }

  // hop over 2 packets and make another one (gap of 2)
  b = 5;
  buffer_time = b * GST_MSECOND * 20;
  in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  // verify that the jitterbuffer now wait for the latest moment it can push
  // the first lost buffer (buffer 3) out on (buffer-timestamp (60) + latency (10) = 70)
  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  g_assert_cmpint (id.time, ==,
      (3 * GST_MSECOND * 20) + (jb_latency_ms * GST_MSECOND));

  // let the time expire...
  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), id.time);
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);

  // we should now receive a packet-lost-event for buffer 3
  out_event = g_async_queue_timeout_pop (data.event_queue, timeout);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 3, 3 * GST_MSECOND * 20, GST_MSECOND * 20,
      FALSE);

  // buffer 4 now arrives just in time (time is 70, buffer 4 expires at 90)
  b = 4;
  buffer_time = b * GST_MSECOND * 20;
  in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  // verify that buffer 4 made it through!
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  g_assert_cmpint (gst_rtp_buffer_get_seq (out_buf), ==, 4);

  // and see that buffer 5 now arrives in a normal fashion
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);
  g_assert (!GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  g_assert_cmpint (gst_rtp_buffer_get_seq (out_buf), ==, 5);

  // should still have only seen 1 packet lost event
  g_assert_cmpint (data.lost_event_count, ==, 1);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_late_packets_still_makes_lost_events)
{
  TestData data;
  GstTestClockPendingID id;
  guint64 timeout;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 10;
  GstClockTime buffer_time;
  gint b;

  setup_testharness (&data);
  timeout = 20 * G_USEC_PER_SEC;

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);

  // push the first buffer in
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);

  // push some buffers in!
  for (b = 1; b < 3; b++) {
    buffer_time = b * GST_MSECOND * 20;
    in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

    // check for the buffer coming out that was pushed in
    out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
    g_assert (out_buf != NULL);
    g_assert_cmpint (GST_BUFFER_TIMESTAMP (out_buf), ==, buffer_time);
  }

  // hop over 2 packets and make another one (gap of 2)
  b = 5;
  buffer_time = b * GST_MSECOND * 20;
  in_buf = generate_test_buffer (buffer_time, TRUE, b, b * 160);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  // release the wait
  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);

  // we should now receive a packet-lost-event for buffer 3 and 4
  out_event = g_async_queue_timeout_pop (data.event_queue, timeout);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 3, 3 * GST_MSECOND * 20, GST_MSECOND * 20 * 2,
      TRUE);

  // verify that buffer 5 made it through!
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  g_assert_cmpint (gst_rtp_buffer_get_seq (out_buf), ==, 5);

  // should still have only seen 1 packet lost event
  g_assert_cmpint (data.lost_event_count, ==, 1);

  destroy_testharness (&data);
}

GST_END_TEST;

GST_START_TEST (test_all_packets_are_timestamped_zero)
{
  TestData data;
  GstTestClockPendingID id;
  guint64 timeout;
  GstBuffer *in_buf, *out_buf;
  GstEvent *out_event;
  gint jb_latency_ms = 10;
  GstClockTime buffer_time;
  gint b;

  setup_testharness (&data);
  timeout = 20 * G_USEC_PER_SEC;

  g_object_set (data.jitter_buffer, "latency", jb_latency_ms, NULL);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_SECOND);

  // push the first buffer in
  in_buf = generate_test_buffer (0 * GST_MSECOND, TRUE, 0, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);

  // push some buffers in!
  for (b = 1; b < 3; b++) {
    in_buf = generate_test_buffer (0, TRUE, b, 0);
    g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

    // check for the buffer coming out that was pushed in
    out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
    g_assert (out_buf != NULL);
    g_assert_cmpint (GST_BUFFER_TIMESTAMP (out_buf), ==, 0);
  }

  // hop over 2 packets and make another one (gap of 2)
  b = 5;
  in_buf = generate_test_buffer (0, TRUE, b, 0);
  g_assert_cmpint (gst_pad_push (data.test_src_pad, in_buf), ==, GST_FLOW_OK);

  // release the wait
  g_assert (gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK
          (data.clock), &id));
  g_assert (gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock))
      == id.clock_id);

  // we should now receive a packet-lost-event for buffer 3 and 4
  out_event = g_async_queue_timeout_pop (data.event_queue, timeout);
  g_assert (out_event != NULL);
  g_assert_cmpint (data.lost_event_count, ==, 1);
  verify_lost_event (out_event, 3, 0, 0, TRUE);

  // verify that buffer 5 made it through!
  out_buf = g_async_queue_timeout_pop (data.buf_queue, timeout);
  g_assert (out_buf != NULL);
  g_assert (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  g_assert_cmpint (gst_rtp_buffer_get_seq (out_buf), ==, 5);

  // should still have only seen 1 packet lost event
  g_assert_cmpint (data.lost_event_count, ==, 1);

  destroy_testharness (&data);
}

GST_END_TEST;
#endif

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
#if 0
  tcase_add_test (tc_chain, test_only_one_lost_event_on_large_gaps);
  tcase_add_test (tc_chain, test_two_lost_one_arrives_in_time);
  tcase_add_test (tc_chain, test_late_packets_still_makes_lost_events);
  tcase_add_test (tc_chain, test_all_packets_are_timestamped_zero);
#endif

  /* FIXME: test buffer lists */

  return s;
}

GST_CHECK_MAIN (rtpjitterbuffer);

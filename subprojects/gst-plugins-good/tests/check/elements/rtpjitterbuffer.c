/* GStreamer
 *
 * Copyright (C) 2009 Nokia Corporation and its subsidiary(-ies)
 *               contact: <stefan.kost@nokia.com>
 * Copyright (C) 2012 Cisco Systems, Inc
 *               Authors: Kelley Rogers <kelro@cisco.com>
 *               Havard Graff <hgraff@cisco.com>
 * Copyright (C) 2013-2016 Pexip AS
 *               Stian Selnes <stian@pexip>
 *               Havard Graff <havard@pexip>
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
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

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
static GstStaticPadTemplate rtcpsrctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static void
buffer_dropped (G_GNUC_UNUSED gpointer data, GstMiniObject * obj)
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
  guint8 in[] = {
    /* first 4 bytes are rtp-header, next 4 bytes are timestamp */
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
  gst_element_set_state (jitterbuffer, GST_STATE_NULL);
  gst_check_teardown_src_pad (jitterbuffer);
  gst_check_teardown_sink_pad (jitterbuffer);
  gst_check_teardown_element (jitterbuffer);
}

static void
check_jitterbuffer_results (gint num_buffers)
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
  check_jitterbuffer_results (num_buffers);

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
  check_jitterbuffer_results (num_buffers);

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
  check_jitterbuffer_results (num_buffers);

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

gboolean is_eos;

static gboolean
eos_event_function (G_GNUC_UNUSED GstPad * pad,
    G_GNUC_UNUSED GstObject * parent, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    g_mutex_lock (&check_mutex);
    is_eos = TRUE;
    g_cond_signal (&check_cond);
    g_mutex_unlock (&check_mutex);
  }
  gst_event_unref (event);
  return TRUE;
}

GST_START_TEST (test_push_eos)
{
  GstElement *jitterbuffer;
  const guint num_buffers = 5;
  GList *node;
  GstStructure *stats;
  guint64 pushed, lost, late, duplicates;
  int n = 0;

  is_eos = FALSE;

  jitterbuffer = setup_jitterbuffer (num_buffers);
  gst_pad_set_event_function (mysinkpad, eos_event_function);

  g_object_set (jitterbuffer, "latency", 1, NULL);

  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  /* push buffers: 0,1,2, */
  for (node = inbuffers; node; node = g_list_next (node)) {
    GstBuffer *buffer;

    /* steal buffer from list */
    buffer = node->data;
    node->data = NULL;

    n++;
    /* Skip 1 */
    if (n == 2) {
      gst_buffer_unref (buffer);
      continue;
    }
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  gst_pad_push_event (mysrcpad, gst_event_new_eos ());

  g_mutex_lock (&check_mutex);
  while (!is_eos)
    g_cond_wait (&check_cond, &check_mutex);
  g_mutex_unlock (&check_mutex);

  fail_unless_equals_int (g_list_length (buffers), num_buffers - 1);

  /* Verify statistics */
  g_object_get (jitterbuffer, "stats", &stats, NULL);
  gst_structure_get (stats, "num-pushed", G_TYPE_UINT64, &pushed,
      "num-lost", G_TYPE_UINT64, &lost,
      "num-late", G_TYPE_UINT64, &late,
      "num-duplicates", G_TYPE_UINT64, &duplicates, NULL);
  fail_unless_equals_int (pushed, g_list_length (inbuffers) - 1);
  fail_unless_equals_int (lost, 1);
  fail_unless_equals_int (late, 0);
  fail_unless_equals_int (duplicates, 0);
  gst_structure_free (stats);

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
request_pt_map (G_GNUC_UNUSED GstElement * jitterbuffer, guint pt)
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
  check_jitterbuffer_results (num_buffers);

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
}

GST_END_TEST;

#define TEST_BUF_CLOCK_RATE 8000
#define AS_TEST_BUF_RTP_TIME(gst_time) gst_util_uint64_scale_int (TEST_BUF_CLOCK_RATE, gst_time, GST_SECOND)
#define TEST_BUF_PT 0
#define TEST_BUF_SSRC 0x01BADBAD
#define TEST_BUF_MS  20
#define TEST_BUF_DURATION (TEST_BUF_MS * GST_MSECOND)
#define TEST_BUF_SIZE (64000 * TEST_BUF_MS / 1000)
#define TEST_RTP_TS_DURATION AS_TEST_BUF_RTP_TIME (TEST_BUF_DURATION)

static GstCaps *
generate_caps (void)
{
  return gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "audio",
      "clock-rate", G_TYPE_INT, TEST_BUF_CLOCK_RATE,
      "encoding-name", G_TYPE_STRING, "TEST",
      "payload", G_TYPE_INT, TEST_BUF_PT,
      "ssrc", G_TYPE_UINT, TEST_BUF_SSRC, NULL);
}

static GstBuffer *
generate_test_buffer_full (GstClockTime dts, guint seq_num, guint32 rtp_ts)
{
  GstBuffer *buf;
  guint8 *payload;
  guint i;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  buf = gst_rtp_buffer_new_allocate (TEST_BUF_SIZE, 0, 0);
  GST_BUFFER_DTS (buf) = dts;

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type (&rtp, TEST_BUF_PT);
  gst_rtp_buffer_set_seq (&rtp, seq_num);
  gst_rtp_buffer_set_timestamp (&rtp, rtp_ts);
  gst_rtp_buffer_set_ssrc (&rtp, TEST_BUF_SSRC);

  payload = gst_rtp_buffer_get_payload (&rtp);
  for (i = 0; i < TEST_BUF_SIZE; i++)
    payload[i] = 0xff;

  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

static GstBuffer *
generate_test_buffer (guint seq_num)
{
  return generate_test_buffer_full (seq_num * TEST_BUF_DURATION,
      seq_num, seq_num * TEST_RTP_TS_DURATION);
}

static GstBuffer *
generate_test_buffer_rtx (GstClockTime dts, guint seq_num)
{
  GstBuffer *buffer = generate_test_buffer_full (dts, seq_num,
      seq_num * TEST_RTP_TS_DURATION);
  GST_BUFFER_FLAG_SET (buffer, GST_RTP_BUFFER_FLAG_RETRANSMISSION);
  return buffer;
}

static void
push_test_buffer (GstHarness * h, guint seq_num)
{
  gst_harness_set_time (h, seq_num * TEST_BUF_DURATION);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer (seq_num)));
}

static void
push_test_buffer_now (GstHarness * h, guint seqnum, guint32 rtptime,
    gboolean rtx)
{
  GstClockTime now =
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)) -
      h->element->base_time;
  GstBuffer *buf = generate_test_buffer_full (now, seqnum, rtptime);
  if (rtx)
    GST_BUFFER_FLAG_SET (buf, GST_RTP_BUFFER_FLAG_RETRANSMISSION);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
}

static gint
get_rtp_seq_num (GstBuffer * buf)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gint seq;
  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  seq = gst_rtp_buffer_get_seq (&rtp);
  gst_rtp_buffer_unmap (&rtp);
  return seq;
}

static void
verify_lost_event (GstHarness * h, guint exp_seq, GstClockTime exp_ts,
    GstClockTime exp_dur)
{
  GstEvent *event;
  const GstStructure *s;
  const GValue *value;
  guint seq;
  GstClockTime ts;
  GstClockTime dur;

  event = gst_harness_pull_event (h);
  fail_unless (event != NULL);

  s = gst_event_get_structure (event);
  fail_unless (s != NULL);
  fail_unless (gst_structure_get_uint (s, "seqnum", &seq));

  value = gst_structure_get_value (s, "timestamp");
  fail_unless (value && G_VALUE_HOLDS_UINT64 (value));

  ts = g_value_get_uint64 (value);
  value = gst_structure_get_value (s, "duration");
  fail_unless (value && G_VALUE_HOLDS_UINT64 (value));

  dur = g_value_get_uint64 (value);
  fail_unless_equals_int ((guint16) exp_seq, seq);
  fail_unless_equals_uint64 (exp_ts, ts);
  fail_unless_equals_uint64 (exp_dur, dur);

  gst_event_unref (event);
}

static void
verify_rtx_event (GstHarness * h, guint exp_seq, GstClockTime exp_ts,
    gint exp_delay, GstClockTime exp_spacing)
{
  GstEvent *event;
  const GstStructure *s;
  const GValue *value;
  guint seq;
  GstClockTime ts;
  guint delay;
  GstClockTime spacing;

  event = gst_harness_pull_upstream_event (h);
  fail_unless (event != NULL);

  s = gst_event_get_structure (event);
  fail_unless (s != NULL);
  fail_unless (gst_structure_get_uint (s, "seqnum", &seq));

  value = gst_structure_get_value (s, "running-time");
  fail_unless (value && G_VALUE_HOLDS_UINT64 (value));

  ts = g_value_get_uint64 (value);
  fail_unless (gst_structure_get_uint (s, "delay", &delay));
  value = gst_structure_get_value (s, "packet-spacing");
  fail_unless (value && G_VALUE_HOLDS_UINT64 (value));
  spacing = g_value_get_uint64 (value);
  fail_unless_equals_int ((guint16) exp_seq, seq);
  fail_unless_equals_uint64 (exp_ts, ts);
  fail_unless_equals_int (exp_delay, delay);
  fail_unless_equals_uint64 (exp_spacing, spacing);

  gst_event_unref (event);
}

static gboolean
verify_jb_stats (GstElement * jb, GstStructure * expected)
{
  gboolean ret;
  GstStructure *actual;
  g_object_get (jb, "stats", &actual, NULL);

  ret = gst_structure_is_subset (actual, expected);

  if (!ret) {
    gchar *e_str = gst_structure_to_string (expected);
    gchar *a_str = gst_structure_to_string (actual);
    fail_unless (ret, "%s is not a subset of %s", e_str, a_str);
    g_free (e_str);
    g_free (a_str);
  }

  gst_structure_free (expected);
  gst_structure_free (actual);

  return ret;
}

static guint
construct_deterministic_initial_state (GstHarness * h, gint latency_ms)
{
  guint next_seqnum = latency_ms / TEST_BUF_MS + 1;
  guint seqnum;
  gint i;

  g_assert (latency_ms % TEST_BUF_MS == 0);

  gst_harness_set_src_caps (h, generate_caps ());
  g_object_set (h->element, "latency", latency_ms, NULL);

  /* When the first packet arrives in the jitterbuffer, it will create a
   * timeout for this packet equal to the latency of the jitterbuffer.
   * This is known as DEADLINE internally, and is meant to allow the stream
   * to buffer a bit before starting to push it out, to get some ideas about
   * the nature of the stream. (packetspacing, jitter etc.)
   *
   * When writing tests using the test-clock, it it hence important to know
   * that by simply advancing the clock to this timeout, you are basically
   * describing a stream that had one initial packet, and then nothing at all
   * for the duration of the latency (100ms in this test), which is not a very
   * usual scenario.
   *
   * Instead, a pattern used throughout this test-suite, is to keep the buffers
   * arriving at their optimal time, until the DEADLINE is reached, and that
   * then becomes the "starting-point" for the test, because at this time
   * there should now be no waiting timers (unless using rtx) and we have
   * a "clean" state to craft the test from.
   */

  /* Packet 0 arrives at time 0ms, Packet 5 arrives at time 100ms */
  for (seqnum = 0; seqnum < next_seqnum; seqnum++) {
    push_test_buffer (h, seqnum);
    gst_harness_wait_for_clock_id_waits (h, 1, 60);
  }

  /* We release the DEADLINE timer for packet 0, verify the time is indeed
   * @latency_ms (100ms) and pull out all the buffers that have been released,
   * and verify their PTS and sequence numbers.
   */
  gst_harness_crank_single_clock_wait (h);
  fail_unless_equals_int64 (latency_ms * GST_MSECOND,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  for (seqnum = 0; seqnum < next_seqnum; seqnum++) {
    GstBuffer *buf = gst_harness_pull (h);
    fail_unless_equals_uint64 (seqnum * TEST_BUF_DURATION,
        GST_BUFFER_PTS (buf));
    fail_unless_equals_int (seqnum, get_rtp_seq_num (buf));
    gst_buffer_unref (buf);
  }

  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));

  /* drop reconfigure event */
  gst_event_unref (gst_harness_pull_upstream_event (h));

  /* Verify that at this point our queues are empty */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  return next_seqnum;
}

static GstBuffer *
setup_rtcp_sender_report (GstElement * jitterbuffer,
    guint64 ntp_time_seconds, guint32 rtp_time)
{
  GstRTCPBuffer rtcp_buf = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  GstBuffer *srep_buf;

  srep_buf = gst_rtcp_buffer_new (1000);

  if (gst_rtcp_buffer_map (srep_buf, GST_MAP_READWRITE, &rtcp_buf)) {
    if (gst_rtcp_buffer_add_packet (&rtcp_buf, GST_RTCP_TYPE_SR, &packet)) {
      gst_rtcp_packet_sr_set_sender_info (&packet, TEST_BUF_SSRC,       /* SSRC */
          /* ntp_time_seconds is the test time in seconds since Jan 1 1900.
             Here it is converted to NTP format  */
          (guint64) ntp_time_seconds << 32,     /* NTP timestamp */
          rtp_time,             /* RTP timestamp */
          1,                    /* sender's packet count */
          100);                 /* sender's octet count */
    }

    gst_rtcp_buffer_unmap (&rtcp_buf);
  }

  return srep_buf;
}

static GstPad *
setup_rtcp_pads (GstElement * jitterbuffer)
{
  GstPad *rtcp_fxsrc_pad;
  GstPad *rtcp_sink_pad;
  GstPadTemplate *pad_tmp;
  GstCaps *rtcp_caps;

  pad_tmp = gst_static_pad_template_get (&rtcpsrctemplate);

  rtcp_fxsrc_pad = gst_pad_new_from_template (pad_tmp, "src");
  fail_if (rtcp_fxsrc_pad == NULL, "Could not create a srcpad");

  rtcp_sink_pad = gst_element_request_pad_simple (jitterbuffer, "sink_rtcp");
  fail_if (rtcp_sink_pad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (jitterbuffer));

  fail_unless (gst_pad_link (rtcp_fxsrc_pad, rtcp_sink_pad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads",
      GST_ELEMENT_NAME (jitterbuffer));

  gst_pad_set_active (rtcp_sink_pad, TRUE);
  gst_pad_set_active (rtcp_fxsrc_pad, TRUE);


  rtcp_caps = gst_caps_new_simple ("application/x-rtcp",
      "clock-rate", G_TYPE_INT, TEST_BUF_CLOCK_RATE, NULL);

  gst_check_setup_events_with_stream_id (rtcp_fxsrc_pad, jitterbuffer,
      rtcp_caps, GST_FORMAT_TIME, "/test/jitbuf/rtcp");

  gst_object_unref (pad_tmp);
  gst_caps_unref (rtcp_caps);
  gst_object_unref (rtcp_sink_pad);

  return rtcp_fxsrc_pad;
}

GST_START_TEST (test_lost_event)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstBuffer *buf;
  gint latency_ms = 100;
  guint next_seqnum;
  guint missing_seqnum;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* We will now create a gap in the stream, by skipping one sequence-number,
   * and push the following packet.
   */
  missing_seqnum = next_seqnum;
  next_seqnum += 1;
  push_test_buffer (h, next_seqnum);

  /* This packet (@next_seqnum) will now be held back, awaiting the missing one,
   * verify that this is the case:
   */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  /* The lost-timeout for the missing packet will now be its pts + latency, so
   * now we will simply crank the clock to advance to this point in time, and
   * check that we get a lost-event, as well as the last packet we pushed in.
   */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, missing_seqnum,
      missing_seqnum * TEST_BUF_DURATION, TEST_BUF_DURATION);

  buf = gst_harness_pull (h);
  fail_unless_equals_uint64 (next_seqnum * TEST_BUF_DURATION,
      GST_BUFFER_PTS (buf));
  fail_unless_equals_int (next_seqnum, get_rtp_seq_num (buf));
  gst_buffer_unref (buf);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 1, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_only_one_lost_event_on_large_gaps)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  GstBuffer *out_buf;
  guint next_seqnum;
  gint latency_ms = 200;
  gint num_lost_events = latency_ms / TEST_BUF_MS;
  gint i;

  testclock = gst_harness_get_testclock (h);
  /* Need to set max-misorder-time and max-dropout-time to 0 so the
   * jitterbuffer does not base them on packet rate calculations.
   * If it does, out gap is big enough to be considered a new stream and
   * we wait for a few consecutive packets just to be sure
   */
  g_object_set (h->element, "do-lost", TRUE,
      "max-misorder-time", 0, "max-dropout-time", 0, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* move time ahead to just before 10 seconds */
  gst_harness_set_time (h, 10 * GST_SECOND - 1);

  /* check that we have no pending waits */
  fail_unless_equals_int (0, gst_test_clock_peek_id_count (testclock));

  /* a buffer now arrives perfectly on time */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (500)));

  /* release the wait, advancing the clock to 10 sec */
  fail_unless (gst_harness_crank_single_clock_wait (h));

  /* we should now receive a packet-lost-event for buffers 11 through 489 ... */
  verify_lost_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, TEST_BUF_DURATION * (490 - next_seqnum));

  /* ... as well as 490 (since at 10 sec 490 is too late) */
  verify_lost_event (h, 490, 490 * TEST_BUF_DURATION, TEST_BUF_DURATION);

  /* we get as many lost events as the the number of *
   * buffers the jitterbuffer is able to wait for */
  for (i = 1; i < num_lost_events; i++) {
    fail_unless (gst_harness_crank_single_clock_wait (h));
    verify_lost_event (h, 490 + i, (490 + i) * TEST_BUF_DURATION,
        TEST_BUF_DURATION);
  }

  /* and then the buffer is released */
  out_buf = gst_harness_pull (h);
  fail_unless (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  fail_unless_equals_int (500, get_rtp_seq_num (out_buf));
  fail_unless_equals_uint64 (10 * GST_SECOND, GST_BUFFER_PTS (out_buf));
  gst_buffer_unref (out_buf);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-lost", G_TYPE_UINT64, (guint64) 489, NULL)));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_two_lost_one_arrives_in_time)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  GstClockID id;
  GstBuffer *buf;
  gint latency_ms = 100;
  guint next_seqnum;
  guint first_missing;
  guint second_missing;
  guint current_arrived;

  testclock = gst_harness_get_testclock (h);
  g_object_set (h->element, "do-lost", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* hop over 2 packets and make another one (gap of 2) */
  first_missing = next_seqnum;
  second_missing = next_seqnum + 1;
  current_arrived = next_seqnum + 2;
  push_test_buffer (h, current_arrived);

  /* verify that the jitterbuffer now wait for the latest moment it can push the
   * @first_missing packet out.
   */
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  fail_unless_equals_uint64 (first_missing * TEST_BUF_DURATION +
      latency_ms * GST_MSECOND, gst_clock_id_get_time (id));
  gst_clock_id_unref (id);

  /* let the time expire... */
  fail_unless (gst_harness_crank_single_clock_wait (h));

  /* we should now receive a packet-lost-event */
  verify_lost_event (h, first_missing,
      first_missing * TEST_BUF_DURATION, TEST_BUF_DURATION);

  /* @second_missing now arrives just in time */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (second_missing)));

  /* verify that @second_missing made it through! */
  buf = gst_harness_pull (h);
  fail_unless (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
  fail_unless_equals_int (second_missing, get_rtp_seq_num (buf));
  gst_buffer_unref (buf);

  /* and see that @current_arrived now also is pushed */
  buf = gst_harness_pull (h);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
  fail_unless_equals_int (current_arrived, get_rtp_seq_num (buf));
  gst_buffer_unref (buf);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 2,
              "num-lost", G_TYPE_UINT64, (guint64) 1, NULL)));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_out_of_order_loss_not_reported)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  GstClockID id;
  GstBuffer *buf;
  gint latency_ms = 100;
  guint next_seqnum;
  guint first_packet;
  guint late_packet;
  guint third_packet;

  testclock = gst_harness_get_testclock (h);
  g_object_set (h->element, "do-lost", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* hop over 2 packets and make another one (gap of 2) */
  first_packet = next_seqnum;
  late_packet = next_seqnum + 1;
  third_packet = next_seqnum + 2;
  push_test_buffer (h, first_packet);

  /* push the third packet without moving the time */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (third_packet)));

  /* verify that the jitterbuffer waits for the latest moment it can push the
   * @late_packet packet out.
   */
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  fail_unless_equals_uint64 (late_packet * TEST_BUF_DURATION +
      latency_ms * GST_MSECOND, gst_clock_id_get_time (id));
  gst_clock_id_unref (id);

  /* @late_packet now arrives just in time for the latency */
  gst_harness_set_time (h, late_packet * TEST_BUF_DURATION +
      latency_ms * GST_MSECOND);
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (late_packet)));
  fail_unless (gst_harness_crank_single_clock_wait (h));

  /* verify that @first_packet made it through */
  buf = gst_harness_pull (h);
  fail_unless_equals_int (get_rtp_seq_num (buf), first_packet);
  gst_buffer_unref (buf);

  /* verify that @late_packet made it through */
  buf = gst_harness_pull (h);
  fail_unless_equals_int (get_rtp_seq_num (buf), late_packet);
  gst_buffer_unref (buf);

  /* verify that @third_packet made it through */
  buf = gst_harness_pull (h);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT));
  fail_unless_equals_int (get_rtp_seq_num (buf), third_packet);
  gst_buffer_unref (buf);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) third_packet + 1,
              "num-lost", G_TYPE_UINT64, (guint64) 0, NULL)));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_late_packets_still_makes_lost_events)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstBuffer *out_buf;
  gint latency_ms = 100;
  guint next_seqnum;
  guint seqnum;
  GstClockTime now;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* jump 10 seconds forward in time */
  now = 10 * GST_SECOND;
  gst_harness_set_time (h, now);

  /* push a packet with a gap of 2, that now is very late */
  seqnum = next_seqnum + 2;
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (now,
              seqnum, seqnum * TEST_RTP_TS_DURATION)));

  /* We get one "huge" lost-event accounting for all the missing time */
  verify_lost_event (h, next_seqnum, 120 * GST_MSECOND, 9860 * GST_MSECOND);

  /* and the next packet is optimistically expected to be the one
     just prior to our current packet, so we time that out with a crank */
  gst_harness_crank_single_clock_wait (h);

  /* and we verify that indeed this lost event was thought to should
     have arrived 20ms prior to the packet that actually arrived */
  verify_lost_event (h, next_seqnum + 1, 9980 * GST_MSECOND, 20 * GST_MSECOND);

  /* and finally verify that the super-late packet made it through! */
  out_buf = gst_harness_pull (h);
  fail_unless (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  fail_unless_equals_int (seqnum, get_rtp_seq_num (out_buf));
  gst_buffer_unref (out_buf);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 1,
              "num-lost", G_TYPE_UINT64, (guint64) 2, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;


GST_START_TEST (test_num_late_when_considered_lost_arrives)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gboolean do_lost = __i__ != 0;
  gint latency_ms = 100;
  guint next_seqnum;

  g_object_set (h->element, "do-lost", do_lost, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* gap of 1 */
  push_test_buffer (h, next_seqnum + 1);

  /* crank to trigger lost-event */
  gst_harness_crank_single_clock_wait (h);

  if (do_lost) {
    /* we should now receive packet-lost-events for the missing packet */
    verify_lost_event (h, next_seqnum,
        next_seqnum * TEST_BUF_DURATION, TEST_BUF_DURATION);
  }

  /* pull out the pushed packet */
  gst_buffer_unref (gst_harness_pull (h));

  /* we have one lost packet in the stats */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 1,
              "num-lost", G_TYPE_UINT64, (guint64) 1,
              "num-late", G_TYPE_UINT64, (guint64) 0, NULL)));

  /* the missing packet now arrives (too late) */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (next_seqnum)));

  /* and this increments num-late */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 1,
              "num-lost", G_TYPE_UINT64, (guint64) 1,
              "num-late", G_TYPE_UINT64, (guint64) 1, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_lost_event_uses_pts)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstClockTime now;
  gint latency_ms = 100;
  guint next_seqnum;
  guint lost_seqnum;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* hop over 1 packets and make another one (gap of 1), but due to
     network delays, this packets is also grossly late */
  lost_seqnum = next_seqnum;
  next_seqnum += 1;

  /* advance the clock to the latest time packet @next_seqnum could arrive */
  now = next_seqnum * TEST_BUF_DURATION + latency_ms * GST_MSECOND;
  gst_harness_set_time (h, now);
  gst_harness_push (h, generate_test_buffer_full (now, next_seqnum,
          next_seqnum * TEST_RTP_TS_DURATION));

  /* we should now have received a packet-lost-event for buffer 3 */
  verify_lost_event (h, lost_seqnum,
      lost_seqnum * TEST_BUF_DURATION, TEST_BUF_DURATION);

  /* and pull out packet 4 */
  gst_buffer_unref (gst_harness_pull (h));

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 1, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_lost_event_with_backwards_rtptime)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 40;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  construct_deterministic_initial_state (h, latency_ms);

  /*
   * For video using B-frames, an expected sequence
   * could be like this:
   * (I = I-frame, P = P-frame, B = B-frame)
   *               ___   ___   ___   ___   ___
   *          ... | 3 | | 4 | | 5 | | 6 | | 7 |
   *               –––   –––   –––   –––   –––
   * rtptime:       3(I)  5(P)  5(P)  4(B)  6(P)
   * arrival(dts):  3     5     5     5     6
   *
   * Notice here that packet 6 (the B frame) make
   * the rtptime go backwards.
   *
   * But we get this:
   *               ___   ___   _ _   ___   ___
   *          ... | 3 | | 4 | |   | | 6 | | 7 |
   *               –––   –––   - -   –––   –––
   * rtptime:       3(I)  5(P)        4(B)  6(P)
   * arrival(dts):  3     5           5     6
   *
   */

  /* seqnum 3 */
  push_test_buffer (h, 3);
  gst_buffer_unref (gst_harness_pull (h));

  /* seqnum 4, arriving at time 5 with rtptime 5 */
  gst_harness_push (h,
      generate_test_buffer_full (5 * TEST_BUF_DURATION,
          4, 5 * TEST_RTP_TS_DURATION));
  gst_buffer_unref (gst_harness_pull (h));

  /* seqnum 6, arriving at time 5 with rtptime 4,
     making a gap for missing seqnum 5 */
  gst_harness_push (h,
      generate_test_buffer_full (5 * TEST_BUF_DURATION,
          6, 4 * TEST_RTP_TS_DURATION));

  /* seqnum 7, arriving at time 6 with rtptime 6 */
  gst_harness_push (h,
      generate_test_buffer_full (6 * TEST_BUF_DURATION,
          7, 6 * TEST_RTP_TS_DURATION));

  /* we should now have received a packet-lost-event for seqnum 5,
     with time 5 and 0 duration */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, 5, 5 * TEST_BUF_DURATION, 0);

  /* and pull out 6 and 7 */
  gst_buffer_unref (gst_harness_pull (h));
  gst_buffer_unref (gst_harness_pull (h));

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) 7,
              "num-lost", G_TYPE_UINT64, (guint64) 1, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_all_packets_are_timestamped_zero)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstBuffer *out_buf;
  gint jb_latency_ms = 100;
  gint i, b;

  gst_harness_set_src_caps (h, generate_caps ());
  g_object_set (h->element, "do-lost", TRUE, "latency", jb_latency_ms, NULL);

  /* advance the clock with 10 seconds */
  gst_harness_set_time (h, 10 * GST_SECOND);

  /* push the first buffer through */
  gst_buffer_unref (gst_harness_push_and_pull (h, generate_test_buffer (0)));

  /* push some buffers in, all timestamped 0 */
  for (b = 1; b < 3; b++) {
    fail_unless_equals_int (GST_FLOW_OK,
        gst_harness_push (h,
            generate_test_buffer_full (0 * GST_MSECOND, b, 0)));

    /* check for the buffer coming out that was pushed in */
    out_buf = gst_harness_pull (h);
    fail_unless_equals_uint64 (0, GST_BUFFER_PTS (out_buf));
    gst_buffer_unref (out_buf);
  }

  /* hop over 2 packets and make another one (gap of 2) */
  b = 5;
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer_full (0 * GST_MSECOND, b, 0)));

  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));

  /* we should now receive packet-lost-events for buffer 3 and 4 */
  verify_lost_event (h, 3, 0, 0);
  verify_lost_event (h, 4, 0, 0);

  /* verify that buffer 5 made it through! */
  out_buf = gst_harness_pull (h);
  fail_unless (GST_BUFFER_FLAG_IS_SET (out_buf, GST_BUFFER_FLAG_DISCONT));
  fail_unless_equals_int (5, get_rtp_seq_num (out_buf));
  gst_buffer_unref (out_buf);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) 4,
              "num-lost", G_TYPE_UINT64, (guint64) 2, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_reorder_of_non_equidistant_packets)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  gint latency_ms = 5;
  GstClockID pending_id;
  GstClockTime time;
  gint seq, frame;
  gint num_init_frames = 1;
  const GstClockTime frame_dur = TEST_BUF_DURATION;
  const guint32 frame_rtp_ts_dur = TEST_RTP_TS_DURATION;

  gst_harness_set_src_caps (h, generate_caps ());
  testclock = gst_harness_get_testclock (h);
  g_object_set (h->element, "do-lost", TRUE, "latency", latency_ms, NULL);

  for (frame = 0, seq = 0; frame < num_init_frames; frame++, seq += 2) {
    /* Push a couple of packets with identical timestamp, typical for a video
     * stream where one frame generates multiple packets. */
    gst_harness_set_time (h, frame * frame_dur);
    gst_harness_push (h, generate_test_buffer_full (frame * frame_dur,
            seq, frame * frame_rtp_ts_dur));
    gst_harness_push (h, generate_test_buffer_full (frame * frame_dur,
            seq + 1, frame * frame_rtp_ts_dur));

    if (frame == 0)
      /* deadline for buffer 0 expires */
      gst_harness_crank_single_clock_wait (h);

    gst_buffer_unref (gst_harness_pull (h));
    gst_buffer_unref (gst_harness_pull (h));
  }

  /* Finally push the last frame reordered */
  gst_harness_set_time (h, frame * frame_dur);
  gst_harness_push (h, generate_test_buffer_full (frame * frame_dur,
          seq + 1, frame * frame_rtp_ts_dur));

  /* Check the scheduled lost timer. The expected arrival of this packet
   * should be assumed to be the same as the last packet received since we
   * don't know wether the missing packet belonged to this or previous
   * frame. */
  gst_test_clock_wait_for_next_pending_id (testclock, &pending_id);
  time = gst_clock_id_get_time (pending_id);
  fail_unless_equals_int64 (time, frame * frame_dur + latency_ms * GST_MSECOND);
  gst_clock_id_unref (pending_id);

  /* And then missing packet arrives just in time */
  gst_harness_set_time (h, time - 1);
  gst_harness_push (h, generate_test_buffer_full (time - 1, seq,
          frame * frame_rtp_ts_dur));

  gst_buffer_unref (gst_harness_pull (h));
  gst_buffer_unref (gst_harness_pull (h));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_loss_equidistant_spacing_with_parameter_packets)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 5;
  gint seq, frame;
  gint num_init_frames = 10;
  gint i;

  gst_harness_set_src_caps (h, generate_caps ());
  g_object_set (h->element, "do-lost", TRUE, "latency", latency_ms, NULL);

  /* drop stream-start, caps, segment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));

  for (frame = 0, seq = 0; frame < num_init_frames; frame++, seq++) {
    gst_harness_set_time (h, frame * TEST_BUF_DURATION);
    gst_harness_push (h, generate_test_buffer_full (frame * TEST_BUF_DURATION,
            seq, frame * TEST_RTP_TS_DURATION));

    if (frame == 0)
      /* deadline for buffer 0 expires */
      gst_harness_crank_single_clock_wait (h);

    gst_buffer_unref (gst_harness_pull (h));
  }

  /* Push three packets with same rtptime, simulating parameter packets +
   * frame. This should not disable equidistant mode as it is common for
   * certain audio codecs. */
  for (i = 0; i < 3; i++) {
    gst_harness_set_time (h, frame * TEST_BUF_DURATION);
    gst_harness_push (h, generate_test_buffer_full (frame * TEST_BUF_DURATION,
            seq++, frame * TEST_RTP_TS_DURATION));
    gst_buffer_unref (gst_harness_pull (h));
  }
  frame++;

  /* Finally push the last packet introducing a gap */
  gst_harness_set_time (h, frame * TEST_BUF_DURATION);
  gst_harness_push (h, generate_test_buffer_full (frame * TEST_BUF_DURATION,
          seq + 1, frame * TEST_RTP_TS_DURATION));

  /* given that the last known PTS (pkt#12) was 200ms and this last PTS (pkt#14) was 220ms,
     and our current packet-spacing is 20ms, the lost-event gets a problem here:
     If we use our packet-spacing, the last pushed packet (#12) should have
     a duration of 20ms, meaning we would expect the missing packet (#13) to
     have a PTS of 220ms. However, packet #14 comes in at 220ms, so what is
     the best estimation for the missing packet here?

     Given that we want to estimate the most optimistic PTS in order to give
     the packet as many chances as possible to arrive, we end up with a PTS
     of 220ms and a duration of 0, since that will be the most optimistic
     placement given that it has to be before pkt #14.
   */

  /* timeout the lost-event */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, seq, frame * TEST_BUF_DURATION, 0);

  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST;


typedef struct
{
  guint gap;
  GstClockTime duration[3];
} ThreeLostPackets;

ThreeLostPackets no_fractional_lost_event_durations_input[] = {
  {5, {60 * GST_MSECOND, 20 * GST_MSECOND, 20 * GST_MSECOND}},
  {4, {40 * GST_MSECOND, 20 * GST_MSECOND, 20 * GST_MSECOND}},
  {3, {20 * GST_MSECOND, 20 * GST_MSECOND, 20 * GST_MSECOND}},
  {2, {20 * GST_MSECOND, 20 * GST_MSECOND, 0 * GST_MSECOND}},
  {1, {20 * GST_MSECOND, 0 * GST_MSECOND, 0 * GST_MSECOND}},
  {0, {0 * GST_MSECOND, 0 * GST_MSECOND, 0 * GST_MSECOND}},
};

/* This test looks after that fact that when we have equidistant
   packetspacing, we try and keep that spacing for the lost events,
   so we operate in "whole" packets.
*/
GST_START_TEST (test_no_fractional_lost_event_durations)
{
  ThreeLostPackets *ctx = &no_fractional_lost_event_durations_input[__i__];

  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstClockTime now;
  guint latency_ms = 100;
  guint16 seqnum, gap_seqnum;
  GstClockTime pts;
  GstClockTime duration;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  seqnum = construct_deterministic_initial_state (h, latency_ms);
  gap_seqnum = seqnum + ctx->gap;

  now = gap_seqnum * TEST_BUF_DURATION;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (now,
              seqnum + 3, gap_seqnum * TEST_RTP_TS_DURATION)));

  pts = seqnum * TEST_BUF_DURATION;
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  /* check if the lost-event has expired, if not
     crank to move the time ahead */
  if (pts + latency_ms * GST_MSECOND > now)
    gst_harness_crank_single_clock_wait (h);
  duration = ctx->duration[0];
  verify_lost_event (h, seqnum, pts, duration);

  seqnum++;
  pts += duration;
  duration = ctx->duration[1];
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  if (pts + latency_ms * GST_MSECOND > now)
    gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, seqnum, pts, duration);

  seqnum++;
  pts += duration;
  duration = ctx->duration[2];
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  if (pts + latency_ms * GST_MSECOND > now)
    gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, seqnum, pts, duration);

  /* followed by the buffer */
  gst_buffer_unref (gst_harness_pull (h));
  /* verify that we have pulled out all waiting buffers and events */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_late_lost_with_same_pts)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstClockTime dts, now;
  guint latency_ms = 40;
  guint16 seqnum;
  guint rtp_ts;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  seqnum = construct_deterministic_initial_state (h, latency_ms);

  dts = seqnum * TEST_BUF_DURATION;
  rtp_ts = seqnum * TEST_RTP_TS_DURATION;

  /* set the time on the clock one buffer-length after the
     length of the jitterbuffer */
  now = dts + latency_ms * GST_MSECOND + TEST_BUF_DURATION;
  gst_test_clock_set_time (GST_TEST_CLOCK (GST_ELEMENT_CLOCK (h->element)),
      now);

  /* now two buffers arrive, same arrival time (in the past, must
     have spent a lot of time from udpsrc to jitterbuffer!),
     with the same rtptimestamp (typical of videobuffers),
     with a gap in between them */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (dts, seqnum, rtp_ts)));

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (dts, seqnum + 2, rtp_ts)));

  /* the lost event is generated immediately since we are already
     too late to wait for anything */
  verify_lost_event (h, seqnum + 1, dts, 0);
  gst_buffer_unref (gst_harness_pull (h));
  gst_buffer_unref (gst_harness_pull (h));

  /* verify that we have pulled out all waiting buffers and events */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

static void
gst_test_clock_set_time_and_process (GstTestClock * testclock,
    GstClockTime time)
{
  GstClockID id, tid;
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  gst_test_clock_set_time (testclock, time);
  tid = gst_test_clock_process_next_clock_id (testclock);
  g_assert (tid == id);
  gst_clock_id_unref (tid);
  gst_clock_id_unref (id);
}

GST_START_TEST (test_rtx_expected_next)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 200;
  guint next_seqnum;
  GstClockTime timeout;
  gint rtx_delay_ms;
  const GstClockTime rtx_retry_timeout_ms = 40;

  g_object_set (h->element, "do-lost", TRUE, NULL);
  g_object_set (h->element, "do-retransmission", TRUE, NULL);
  g_object_set (h->element, "rtx-retry-period", 120, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* At this point there is already existing a rtx-timer for @next_seqnum,
   * that will have a timeout of the expected arrival-time for that seqnum,
   * and a delay equal to 2*jitter==0 and 0.5*packet_spacing==10ms */
  timeout = next_seqnum * TEST_BUF_DURATION;
  rtx_delay_ms = TEST_BUF_MS / 2;

  /* We crank the clock to time-out the next scheduled timer */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum, timeout, rtx_delay_ms, TEST_BUF_DURATION);

  /* now we wait for the next timeout, all following timeouts 40ms in the
   * future because this is rtx-retry-timeout */
  rtx_delay_ms += rtx_retry_timeout_ms;
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum, timeout, rtx_delay_ms, TEST_BUF_DURATION);

  /* And a third time... */
  rtx_delay_ms += rtx_retry_timeout_ms;
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum, timeout, rtx_delay_ms, TEST_BUF_DURATION);

  /* we should now receive a packet-lost-event for packet @next_seqnum */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, next_seqnum, timeout, TEST_BUF_DURATION);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_not_bursting_requests)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 200;
  guint next_seqnum;
  guint missing_seqnum;
  GstClockTime now;

  g_object_set (h->element,
      "do-lost", TRUE,
      "do-retransmission", TRUE,
      "rtx-next-seqnum", FALSE, "rtx-max-retries", 3, NULL);

  next_seqnum = construct_deterministic_initial_state (h, latency_ms);
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));

  /* skip a packet and move the time for the next one
     quite a bit forward */
  missing_seqnum = next_seqnum;
  next_seqnum++;
  now += 150 * GST_MSECOND;
  gst_harness_set_time (h, now);

  push_test_buffer_now (h, next_seqnum, next_seqnum * TEST_RTP_TS_DURATION,
      FALSE);

  /* note the delay here is 130. This is because we advanced the clock 150,
     and 20 of those were the duration of the missing seqnum, so this
     RTX event is in effect 130ms "late" compared to its ideal time */
  verify_rtx_event (h, missing_seqnum,
      missing_seqnum * TEST_BUF_DURATION, 130, TEST_BUF_DURATION);

  /* verify we have not sent any other rtx events */
  fail_unless_equals_int (0, gst_harness_upstream_events_in_queue (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_next_seqnum_disabled)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 200;
  guint next_seqnum, missing_seqnum;
  GstTestClock *testclock;
  GstClockTime timeout, last_rtx_request;
  gint rtx_delay_ms;
  const GstClockTime rtx_retry_timeout_ms = 40;

  testclock = gst_harness_get_testclock (h);

  g_object_set (h->element, "do-lost", TRUE, NULL);
  g_object_set (h->element, "do-retransmission", TRUE, NULL);
  g_object_set (h->element, "rtx-retry-period", 120, NULL);
  g_object_set (h->element, "rtx-next-seqnum", FALSE, NULL);

  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* When rtx-next-seqnum is disabled there is no existing rtx-timer for
   * @next_seqnum until there is a gap and it's missing. */

  /* Check that we have no pending waits */
  fail_unless_equals_int (0, gst_test_clock_peek_id_count (testclock));

  /* Push next packet to create a gap and trigger rtx-timers */
  missing_seqnum = next_seqnum;
  next_seqnum += 1;
  push_test_buffer (h, next_seqnum);

  /* Now there should exist a rtx-timer for @next_seqnum, that will have a
   * timeout of the expected arrival-time for that seqnum, and a delay equal
   * to the elapsed time since the timeout and until now (which is the
   * duration of one buffer, 20 ms). */
  timeout = missing_seqnum * TEST_BUF_DURATION;
  rtx_delay_ms = TEST_BUF_MS;

  /* The first rtx-event is triggered immediately since the timeout + delay is
   * less than "now" */
  verify_rtx_event (h, missing_seqnum, timeout, rtx_delay_ms,
      TEST_BUF_DURATION);
  last_rtx_request = gst_clock_get_time (GST_CLOCK (testclock));
  fail_unless_equals_int64 (last_rtx_request,
      missing_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* now we wait for the next timeout, all following timers timeout in 40ms
   * increments because this is rtx-retry-timeout */
  rtx_delay_ms += rtx_retry_timeout_ms;
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, missing_seqnum, timeout, rtx_delay_ms,
      TEST_BUF_DURATION);
  last_rtx_request = gst_clock_get_time (GST_CLOCK (testclock));
  fail_unless_equals_int64 (last_rtx_request,
      missing_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* And a third time... */
  rtx_delay_ms += rtx_retry_timeout_ms;
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, missing_seqnum, timeout, rtx_delay_ms,
      TEST_BUF_DURATION);
  last_rtx_request = gst_clock_get_time (GST_CLOCK (testclock));
  fail_unless_equals_int64 (last_rtx_request,
      missing_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* we should now receive a packet-lost-event for packet @missing_seqnum */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, missing_seqnum, timeout, TEST_BUF_DURATION);

  /* Finally pull out the next packet */
  gst_buffer_unref (gst_harness_pull (h));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_two_missing)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 200;
  guint next_seqnum;
  GstClockTime last_rtx_request, now;
  gint rtx_delay_ms_0 = TEST_BUF_MS / 2;
  gint rtx_delay_ms_1 = TEST_BUF_MS;

  g_object_set (h->element, "do-retransmission", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);
  fail_unless_equals_int (11, next_seqnum);

  /*
   * The expected sequence of buffers is this:
   *      ____   ____   ____   ____
   * ... | 10 | | 11 | | 12 | | 13 |
   *      ––––   ––––   ––––   ––––
   *      200ms  220ms  240ms  260ms
   *
   * But instead we get this:
   *      ____    _ _    _ _   ____
   * ... | 10 |  |   |  |   | | 13 |
   *      ––––    - -    - -   ––––
   *      200ms                260ms
   *
   * Now it is important to note that the next thing that happens is that
   * the RTX timeout for packet 11 will happen at time 230ms, so we crank
   * the timer thread to advance the time to this:
   */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, 11, 11 * TEST_BUF_DURATION,
      rtx_delay_ms_0, TEST_BUF_DURATION);
  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      11 * TEST_BUF_DURATION + rtx_delay_ms_0 * GST_MSECOND);
  gst_harness_wait_for_clock_id_waits (h, 1, 60);

  /* The next scheduled RTX for packet 11 is now at 230 + 40 = 270ms,
     so the next thing that happens is that buffer 13 arrives in perfect time: */
  now = 13 * TEST_BUF_DURATION;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h,
          generate_test_buffer_full (now, 13, 13 * TEST_RTP_TS_DURATION)));

  /*
   *
   * This will estimate the dts on the two missing packets to:
   *      ____   ____
   * ... | 11 | | 12 | ...
   *      ––––   ––––
   *      220ms  240ms
   *
   * And given their regular interspacing of 20ms, it will schedule two RTX
   * timers for them like so:
   *
   *      ____   ____
   * ... | 11 | | 12 | ...
   *      ––––   ––––
   *      230ms  250ms
   *
   * There are however two problems, packet 11 we have already sent one RTX for
   * and its timeout is currently at 270ms, so we should not tamper with that,
   * and as for packet 12, 250ms has already expired, so we now expect to see
   * an rtx-event being sent for packet 12 immediately.
   *
   * Since the current time is 260 ms and packet 12 was expected at 240 ms,
   * the delay of the rtx-event is 20 ms.
   */
  verify_rtx_event (h, 12, 12 * TEST_BUF_DURATION,
      rtx_delay_ms_1, TEST_BUF_DURATION);
  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      12 * TEST_BUF_DURATION + rtx_delay_ms_1 * GST_MSECOND);

  /* and another crank will see the second RTX event being sent for packet 11 */
  gst_harness_crank_single_clock_wait (h);
  rtx_delay_ms_0 += 40;
  verify_rtx_event (h, 11, 11 * TEST_BUF_DURATION,
      rtx_delay_ms_0, TEST_BUF_DURATION);
  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      11 * TEST_BUF_DURATION + rtx_delay_ms_0 * GST_MSECOND);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_buffer_arrives_just_in_time)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 5 * TEST_BUF_MS;
  gint next_seqnum;
  GstBuffer *buffer;
  GstClockTime now, last_rtx_request;
  gint rtx_delay_ms = TEST_BUF_MS / 2;

  g_object_set (h->element, "do-retransmission", TRUE,
      "rtx-max-retries", 1, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Crank clock to send retransmission events requesting seqnum 6 which has
   * not arrived yet. */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);

  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* seqnum 6 arrives just before it times out and is considered lost */
  now = 200 * GST_MSECOND;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, next_seqnum)));
  buffer = gst_harness_pull (h);
  fail_unless_equals_int (next_seqnum, get_rtp_seq_num (buffer));
  gst_buffer_unref (buffer);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 1,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-per-packet", G_TYPE_DOUBLE, 1.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) (now - last_rtx_request),
              NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_buffer_arrives_too_late)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 5 * TEST_BUF_MS;
  gint next_seqnum;
  GstClockTime now, last_rtx_request;
  gint rtx_delay_ms = TEST_BUF_MS / 2;

  g_object_set (h->element, "do-retransmission", TRUE,
      "do-lost", TRUE, "rtx-max-retries", 1, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Crank clock to send retransmission events requesting seqnum 6 which has
   * not arrived yet. */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);

  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* packet @next_seqnum is considered lost */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, TEST_BUF_DURATION);

  /* packet @next_seqnum arrives too late */
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, next_seqnum)));

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 1,
              "num-late", G_TYPE_UINT64, (guint64) 1,
              "num-duplicates", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 0,
              "rtx-per-packet", G_TYPE_DOUBLE, 1.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) (now - last_rtx_request),
              NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_original_buffer_does_not_update_rtx_stats)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 100;
  gint next_seqnum;
  GstBuffer *buffer;
  GstClockTime now, last_rtx_request;
  gint rtx_delay_ms = TEST_BUF_MS / 2;

  g_object_set (h->element, "do-retransmission", TRUE,
      "rtx-max-retries", 1, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);
  fail_unless_equals_int (6, next_seqnum);

  /* Crank clock to send retransmission events requesting @next_seqnum which has
   * not arrived yet. */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);

  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* make sure the wait has settled before moving on */
  gst_harness_wait_for_clock_id_waits (h, 1, 1);

  /* ORIGINAL seqnum 6 arrives just before it times out and is considered
   * lost. */
  now = 200 * GST_MSECOND;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (now,
              next_seqnum, next_seqnum * TEST_RTP_TS_DURATION)));
  buffer = gst_harness_pull (h);
  fail_unless_equals_int (next_seqnum, get_rtp_seq_num (buffer));
  gst_buffer_unref (buffer);

  /* due to the advance in time, we will now also have sent
     an rtx-request for 7 */
  next_seqnum++;
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, 60, TEST_BUF_DURATION);

  /* The original buffer does not count in the RTX stats. */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 0,
              "rtx-per-packet", G_TYPE_DOUBLE, 0.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) 0, NULL)));

  /* Now the retransmitted packet arrives and stats should be updated. Note
   * that the buffer arrives in time and should not be considered late, but
   * a duplicate. */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, 6)));

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 1,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 0,
              "rtx-per-packet", G_TYPE_DOUBLE, 1.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) (now - last_rtx_request),
              NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_duplicate_packet_updates_rtx_stats)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 100;
  gint next_seqnum;
  GstClockTime now, rtx_request_6, rtx_request_7;
  gint rtx_delay_ms = TEST_BUF_MS / 2;
  gint i;

  g_object_set (h->element, "do-retransmission", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);
  fail_unless_equals_int (6, next_seqnum);

  /* Push packet 8 so that 6 and 7 is missing */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (8)));

  /* Wait for NACKs on 6 and 7 */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, 6, 6 * TEST_BUF_DURATION,
      rtx_delay_ms, TEST_BUF_DURATION);
  rtx_request_6 = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (rtx_request_6,
      6 * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h,
      7, 7 * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);
  rtx_request_7 = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (rtx_request_7,
      7 * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* Original packet 7 arrives */
  now = 161 * GST_MSECOND;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (now, 7, 7 * TEST_RTP_TS_DURATION)));

  /* We're still waiting for packet 6, so 7 should not be pushed */
  gst_harness_wait_for_clock_id_waits (h, 1, 60);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  /* The original buffer does not count in the RTX stats. */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 0,
              "rtx-per-packet", G_TYPE_DOUBLE, 0.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) 0, NULL)));

  /* Push RTX packet 7. Should be dropped as duplicate but update RTX stats. */
  now = 162 * GST_MSECOND;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, 7)));
  gst_harness_wait_for_clock_id_waits (h, 1, 60);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  /* Check RTX stats with updated num-duplicates and rtx-rtt fields */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 1,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 0,
              "rtx-per-packet", G_TYPE_DOUBLE, 1.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) (now - rtx_request_7),
              NULL)));

  /* RTX packet 6 arrives, both 6, 7 and 8 is ready to be pulled */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, 6)));

  for (i = 6; i <= 8; i++) {
    GstBuffer *buf = gst_harness_pull (h);
    fail_unless_equals_int (i, get_rtp_seq_num (buf));
    gst_buffer_unref (buf);
  }

  /* RTX stats is updated with success count increased. */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 3,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 1,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-per-packet", G_TYPE_DOUBLE, 1.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64)
              /* Use the rtx-rtt formula. Can be subject to change though. */
              ((now - rtx_request_6) + 47 * (now - rtx_request_7)) / 48,
              NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_buffer_arrives_after_lost_updates_rtx_stats)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 100;
  gint next_seqnum;
  GstClockTime now, last_rtx_request;
  gint rtx_delay_ms = TEST_BUF_MS / 2;

  g_object_set (h->element, "do-retransmission", TRUE,
      "do-lost", TRUE, "rtx-max-retries", 1, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Crank clock to send retransmission events requesting seqnum 6 which has
   * not arrived yet. */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);

  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* seqnum 6 is considered lost */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, TEST_BUF_DURATION);

  /* seqnum 6 arrives too late */
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, next_seqnum)));

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 1,
              "num-late", G_TYPE_UINT64, (guint64) 1,
              "num-duplicates", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 0,
              "rtx-per-packet", G_TYPE_DOUBLE, 1.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) (now - last_rtx_request),
              NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_rtt_larger_than_retry_timeout)
{
  /* When RTT is larger than retry period we will send two or more requests
   * before receiving any retransmission packets */
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 100;
  gint next_seqnum;
  gint rtx_retry_timeout_ms = 20;
  gint rtx_delay_ms = TEST_BUF_MS / 2;
  gint rtt = rtx_retry_timeout_ms * GST_MSECOND + 1;
  GstClockTime now, first_request, second_request;

  g_object_set (h->element, "do-retransmission", TRUE,
      "rtx-retry-timeout", rtx_retry_timeout_ms, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Wait for first NACK on 6 */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);
  first_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (first_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* Packet @next_seqnum + 1 arrives in time (so that we avoid its EXPECTED
   * timers to interfer with our test) */
  push_test_buffer (h, next_seqnum + 1);

  /* Simulating RTT > rtx-retry-timeout, we send a new NACK before receiving
   * the RTX packet. Wait for second NACK on @next_seqnum */
  gst_harness_crank_single_clock_wait (h);
  rtx_delay_ms += rtx_retry_timeout_ms;
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);
  second_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (second_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* The first retransmitted packet arrives */
  now = first_request + rtt;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, next_seqnum)));

  /* Pull packets @next_seqnum and @next_seqnum + 1 */
  gst_buffer_unref (gst_harness_pull (h));
  gst_buffer_unref (gst_harness_pull (h));

  /* Stats should be updated. Note that RTT is not updated since we cannot be
   * sure whether the RTX packet is in response to the first or second NACK. */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 2,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-per-packet", G_TYPE_DOUBLE, 2.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) 0, NULL)));

  /* Packet @next_seqnum + 2 arrives in time */
  push_test_buffer (h, next_seqnum + 2);
  gst_buffer_unref (gst_harness_pull (h));

  /* Now the second retransmitted packet arrives */
  now = second_request + rtt;
  gst_harness_set_time (h, now);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_rtx (now, next_seqnum)));

  /* The stats is updated with the correct RTT. */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum + 3,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "num-late", G_TYPE_UINT64, (guint64) 0,
              "num-duplicates", G_TYPE_UINT64, (guint64) 1,
              "rtx-count", G_TYPE_UINT64, (guint64) 2,
              "rtx-success-count", G_TYPE_UINT64, (guint64) 1,
              "rtx-per-packet", G_TYPE_DOUBLE, 2.0,
              "rtx-rtt", G_TYPE_UINT64, (guint64) rtt, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_no_request_if_time_past_retry_period)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  const gint latency_ms = 200;
  const gint retry_period_ms = 120;
  GstTestClock *testclock;
  GstClockID pending_id;
  GstClockTime time;
  gint i;

  gst_harness_set_src_caps (h, generate_caps ());
  testclock = gst_harness_get_testclock (h);

  g_object_set (h->element, "do-lost", TRUE, NULL);
  g_object_set (h->element, "do-retransmission", TRUE, NULL);
  g_object_set (h->element, "latency", latency_ms, NULL);
  g_object_set (h->element, "rtx-retry-period", retry_period_ms, NULL);

  /* push the first couple of buffers */
  push_test_buffer (h, 0);
  push_test_buffer (h, 1);

  /* drop reconfigure event */
  gst_event_unref (gst_harness_pull_upstream_event (h));
  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));

  /* Wait for the first EXPECTED timer to be scheduled */
  gst_test_clock_wait_for_next_pending_id (testclock, &pending_id);
  time = gst_clock_id_get_time (pending_id);
  gst_clock_id_unref (pending_id);
  fail_unless_equals_int64 (time, 2 * TEST_BUF_DURATION + 10 * GST_MSECOND);

  /* Let the first EXPECTED timer time out and be sent. However, set the 'now'
   * time to be past the retry-period simulating that the jitterbuffer has too
   * much to do and is not able to process all timers in real-time. In this
   * case the jitterbuffer should not schedule a new EXPECTED timer as that
   * would just make matters worse (more unnecessary processing of a request
   * that is already too late to be valuable). In practice this typically
   * happens for high loss networks with low RTT. */
  gst_test_clock_set_time_and_process (testclock,
      2 * TEST_BUF_DURATION + retry_period_ms * GST_MSECOND + 1);

  /* Verify the event. It could be argued that this request is already too
   * late and unnecessary. However, in order to keep things simple (for now)
   * we just keep the already scehduled EXPECTED timer, but refrain from
   * scheduled another EXPECTED timer */
  verify_rtx_event (h, 2, 2 * TEST_BUF_DURATION, 120, TEST_BUF_DURATION);

  /* "crank" to reach the DEADLINE for packet 0 */
  gst_harness_crank_single_clock_wait (h);
  gst_buffer_unref (gst_harness_pull (h));
  gst_buffer_unref (gst_harness_pull (h));

  fail_unless_equals_int (0, gst_harness_upstream_events_in_queue (h));
  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  /* "crank" to time out the LOST event */
  gst_harness_crank_single_clock_wait (h);
  verify_lost_event (h, 2, 2 * TEST_BUF_DURATION, TEST_BUF_DURATION);

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_same_delay_and_retry_timeout)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 5 * TEST_BUF_MS;
  gint next_seqnum;
  gint rtx_delay_ms = 20;
  GstClockTime last_rtx_request;

  g_object_set (h->element, "do-retransmission", TRUE,
      "rtx-max-retries", 3, "rtx-delay", rtx_delay_ms,
      "rtx-retry-timeout", rtx_delay_ms, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Crank clock to send retransmission events requesting seqnum 6 which has
   * not arrived yet. */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);
  /* first rtx for packet @next_seqnum should arrive at the right time */
  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * GST_MSECOND);

  /* verify we have pulled out all rtx-events */
  fail_unless_equals_int (0, gst_harness_upstream_events_in_queue (h));

  /* now crank to get the second attempt at packet @next_seqnum */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms * 2, TEST_BUF_DURATION);

  /* second rtx for seqnum 6 should arrive at 140 + 20ms */
  last_rtx_request = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  fail_unless_equals_int64 (last_rtx_request,
      next_seqnum * TEST_BUF_DURATION + rtx_delay_ms * 2 * GST_MSECOND);

  /* verify we have pulled out all rtx-events */
  fail_unless_equals_int (0, gst_harness_upstream_events_in_queue (h));

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) next_seqnum,
              "num-lost", G_TYPE_UINT64, (guint64) 0,
              "rtx-count", G_TYPE_UINT64, (guint64) 2, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_with_backwards_rtptime)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 40;

  g_object_set (h->element, "do-retransmission", TRUE, NULL);
  construct_deterministic_initial_state (h, latency_ms);

  /*
   * For video using B-frames, an expected sequence
   * could be like this:
   * (I = I-frame, P = P-frame, B = B-frame)
   *               ___   ___   ___
   *          ... | 3 | | 4 | | 5 |
   *               –––   –––   –––
   * rtptime:       3(I)  5(P)  4(B)
   * arrival(dts):  3     5     5
   *
   * Notice here that packet 5 (the B frame) make
   * the rtptime go backwards.
   */

  /* seqnum 3, arriving at time 3 with rtptime 3 */
  push_test_buffer (h, 3);
  gst_buffer_unref (gst_harness_pull (h));

  /* seqnum 4, arriving at time 5 with rtptime 5 */
  gst_harness_push (h, generate_test_buffer_full (5 * TEST_BUF_DURATION,
          4, 5 * TEST_RTP_TS_DURATION));
  gst_buffer_unref (gst_harness_pull (h));

  /* seqnum 5, arriving at time 5 with rtptime 4 */
  gst_harness_push (h, generate_test_buffer_full (5 * TEST_BUF_DURATION,
          5, 4 * TEST_RTP_TS_DURATION));
  gst_buffer_unref (gst_harness_pull (h));

  /* crank to time-out the rtx-request for seqnum 6, the point here
   * being that the backwards rtptime did not mess up the timeout for
   * the rtx event.
   *
   * Note: the jitterbuffer no longer update early timers, as a result
   * we need to advance the clock to the expected point
   */
  gst_harness_wait_for_clock_id_waits (h, 1, 1);
  gst_harness_set_time (h, 6 * TEST_BUF_DURATION + 15 * GST_MSECOND);
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, 6, 5 * TEST_BUF_DURATION + 15 * GST_MSECOND,
      20, 35 * GST_MSECOND);

  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) 6,
              "rtx-count", G_TYPE_UINT64, (guint64) 1,
              "num-lost", G_TYPE_UINT64, (guint64) 0, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_timer_reuse)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 5 * TEST_BUF_MS;
  gint rtx_delay_ms = TEST_BUF_MS / 2;
  guint next_seqnum;

  g_object_set (h->element, "do-retransmission", TRUE,
      "do-lost", TRUE, "rtx-max-retries", 1, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* crank to timeout the only rtx-request, and the timer will
   * now reschedule as a lost-timer internally */
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);

  /* but now buffer 6 arrives, and this should now reuse the lost-timer
   * for 6, as an expected-timer for 7 */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (next_seqnum)));

  /* now crank to timeout the expected-timer for 7 and verify */
  next_seqnum++;
  gst_harness_crank_single_clock_wait (h);
  verify_rtx_event (h, next_seqnum,
      next_seqnum * TEST_BUF_DURATION, rtx_delay_ms, TEST_BUF_DURATION);

  gst_harness_teardown (h);
}

GST_END_TEST;


static void
start_test_rtx_large_packet_spacing (GstHarness * h,
    gint latency_ms, gint frame_dur_ms, gint rtx_rtt_ms,
    guint16 * dst_lost_seq, GstClockTime * dst_now)
{
  gint i, seq, frame;
  GstBuffer *buffer;
  GstClockTime now, lost_packet_time;
  GstClockTime frame_dur = frame_dur_ms * GST_MSECOND;

  gst_harness_set_src_caps (h, generate_caps ());
  g_object_set (h->element,
      "do-lost", TRUE, "latency", latency_ms, "do-retransmission", TRUE, NULL);

  /* Pushing 2 frames @frame_dur_ms ms apart from each other to initialize
   * packet_spacing and avg jitter */
  for (frame = 0, seq = 0, now = 0; frame < 2;
      frame++, seq += 2, now += frame_dur) {
    gst_harness_set_time (h, now);
    gst_harness_push (h, generate_test_buffer_full (now, seq,
            AS_TEST_BUF_RTP_TIME (now)));
    gst_harness_push (h, generate_test_buffer_full (now, seq + 1,
            AS_TEST_BUF_RTP_TIME (now)));

    if (frame == 0)
      /* deadline for buffer 0 expires */
      gst_harness_crank_single_clock_wait (h);

    gst_buffer_unref (gst_harness_pull (h));
    gst_buffer_unref (gst_harness_pull (h));
  }

  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));
  /* drop reconfigure event */
  gst_event_unref (gst_harness_pull_upstream_event (h));

  /* The first packet (#@seq) of the 3rd frame is lost */
  lost_packet_time = now;
  gst_harness_set_time (h, now);
  gst_harness_push (h, generate_test_buffer_full (now, seq + 1,
          AS_TEST_BUF_RTP_TIME (now)));

  /* RTX delay calculated as:
   *     MIN(rtx_delay_max, MAX(2*avg_jitter, 0.5 * packet_spacing)).
   * Where rtx_delay_max:
   *     rtx_delay_max = latency - rtx_rtt.
   * We have not used RTX yet, so rtx_rtt = 0, rtx_delay_max = latency.
   * Thus we expect the first RTX event to be sent in @latency_ms ms */
  gst_harness_crank_single_clock_wait (h);
  fail_unless_equals_int64 (now + latency_ms * GST_MSECOND,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  verify_rtx_event (h, seq, now, latency_ms, frame_dur);
  verify_lost_event (h, seq, now, 0);
  gst_buffer_unref (gst_harness_pull (h));
  now += latency_ms * GST_MSECOND;

  /* Sending lost packet as RTX to initialize rtx_rtt */
  now += rtx_rtt_ms * GST_MSECOND;
  gst_harness_set_time (h, now);
  buffer =
      generate_test_buffer_full (now, seq,
      AS_TEST_BUF_RTP_TIME (lost_packet_time));
  GST_BUFFER_FLAG_SET (buffer, GST_RTP_BUFFER_FLAG_RETRANSMISSION);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer));

  /* No buffers should be pushed through, as lost packet arrived too late */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  seq += 2;
  frame += 1;
  now = frame * frame_dur;
  gst_harness_set_time (h, now);

  /* The first packet (#@seq) of the 4th frame is lost */
  gst_harness_push (h, generate_test_buffer_full (now, seq + 1,
          AS_TEST_BUF_RTP_TIME (now)));
  *dst_lost_seq = seq;
  *dst_now = now;
}

GST_START_TEST (test_rtx_large_packet_spacing_and_small_rtt)
{
  GstClockTime now;
  guint16 lost_seq;
  gint latency_ms = 20;
  gint frame_dur_ms = 50;
  gint rtx_rtt_ms = 5;
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");

  start_test_rtx_large_packet_spacing (h, latency_ms, frame_dur_ms, rtx_rtt_ms,
      &lost_seq, &now);

  /* With small rtx_rtt, RTX event expected to be sent in
     (@latency_ms - @rtx_rtt_ms) ms */
  gst_harness_crank_single_clock_wait (h);
  fail_unless_equals_int64 (now + (latency_ms - rtx_rtt_ms) * GST_MSECOND,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  verify_rtx_event (h, lost_seq, now, (latency_ms - rtx_rtt_ms),
      frame_dur_ms * GST_MSECOND);

  /* After @latency ms the packet should be considered lost */
  gst_harness_crank_single_clock_wait (h);
  fail_unless_equals_int64 (now + latency_ms * GST_MSECOND,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  verify_lost_event (h, lost_seq, now, 0);
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_large_packet_spacing_and_large_rtt)
{
  GstClockTime now;
  guint16 lost_seq;
  gint latency_ms = 20;
  gint frame_dur_ms = 50;
  gint rtx_rtt_ms = 30;
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");

  start_test_rtx_large_packet_spacing (h, latency_ms, frame_dur_ms, rtx_rtt_ms,
      &lost_seq, &now);

  /* With large rtx_rtt, RTX event expected to be sent in @latency_ms ms.
     The buffer considered lost. */
  gst_harness_crank_single_clock_wait (h);
  fail_unless_equals_int64 (now + latency_ms * GST_MSECOND,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  verify_rtx_event (h, lost_seq, now, latency_ms, frame_dur_ms * GST_MSECOND);
  verify_lost_event (h, lost_seq, now, 0);
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtx_large_packet_spacing_does_not_reset_jitterbuffer)
{
  gint latency_ms = 20;
  gint frame_dur_ms = 50;
  gint rtx_rtt_ms = 5;
  gint i, seq;
  GstBuffer *buffer;
  GstClockTime now, lost_packet_time;
  GstClockTime frame_dur = frame_dur_ms * GST_MSECOND;
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");

  gst_harness_set_src_caps (h, generate_caps ());
  g_object_set (h->element,
      "do-lost", TRUE, "latency", latency_ms, "do-retransmission", TRUE, NULL);

  /* Pushing 2 frames @frame_dur_ms ms apart from each other to initialize
   * packet_spacing and avg jitter */
  for (seq = 0, now = 0; seq < 2; ++seq, now += frame_dur) {
    gst_harness_set_time (h, now);
    gst_harness_push (h, generate_test_buffer_full (now, seq,
            AS_TEST_BUF_RTP_TIME (now)));
    if (seq == 0)
      gst_harness_crank_single_clock_wait (h);
    buffer = gst_harness_pull (h);
    fail_unless_equals_int64 (now, GST_BUFFER_PTS (buffer));
    gst_buffer_unref (buffer);
  }

  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));
  /* drop reconfigure event */
  gst_event_unref (gst_harness_pull_upstream_event (h));

  /* Waiting for the RTX timer of packet #2 to timeout */
  lost_packet_time = now;
  gst_harness_crank_single_clock_wait (h);
  fail_unless_equals_int64 (now + latency_ms * GST_MSECOND,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  verify_rtx_event (h, seq, now, latency_ms, frame_dur);
  verify_lost_event (h, seq, now, frame_dur);
  now += latency_ms * GST_MSECOND;

  /* Pushing packet #2 as RTX */
  now += rtx_rtt_ms * GST_MSECOND;
  gst_harness_set_time (h, now);
  buffer =
      generate_test_buffer_full (now, seq,
      AS_TEST_BUF_RTP_TIME (lost_packet_time));
  GST_BUFFER_FLAG_SET (buffer, GST_RTP_BUFFER_FLAG_RETRANSMISSION);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buffer));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  /* Packet #3 should have PTS not affected by clock skew logic */
  seq += 1;
  now = seq * frame_dur;
  gst_harness_set_time (h, now);
  gst_harness_push (h, generate_test_buffer_full (now, seq,
          AS_TEST_BUF_RTP_TIME (now)));
  buffer = gst_harness_pull (h);
  fail_unless_equals_int64 (now, GST_BUFFER_PTS (buffer));
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_minor_reorder_does_not_skew)
{
  gint latency_ms = 20;
  gint frame_dur_ms = 50;
  guint rtx_min_delay_ms = 110;
  gint hickup_ms = 2;
  gint i, seq;
  GstBuffer *buffer;
  GstClockTime now;
  GstClockTime frame_dur = frame_dur_ms * GST_MSECOND;
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");

  gst_harness_set_src_caps (h, generate_caps ());
  g_object_set (h->element,
      "do-lost", TRUE, "latency", latency_ms, "do-retransmission", TRUE,
      "rtx-min-delay", rtx_min_delay_ms, NULL);

  /* Pushing 2 frames @frame_dur_ms ms apart from each other to initialize
   * packet_spacing and avg jitter */
  for (seq = 0, now = 0; seq < 2; ++seq, now += frame_dur) {
    gst_harness_set_time (h, now);
    gst_harness_push (h, generate_test_buffer_full (now, seq,
            AS_TEST_BUF_RTP_TIME (now)));
    if (seq == 0)
      gst_harness_crank_single_clock_wait (h);
    buffer = gst_harness_pull (h);
    fail_unless_equals_int64 (now, GST_BUFFER_PTS (buffer));
    gst_buffer_unref (buffer);
  }

  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));
  /* drop reconfigure event */
  gst_event_unref (gst_harness_pull_upstream_event (h));

  /* Pushing packet #4 before #3, shortly after #3 would have arrived normally */
  gst_harness_set_time (h, now + hickup_ms * GST_MSECOND);
  gst_harness_push (h, generate_test_buffer_full (now + hickup_ms * GST_MSECOND,
          seq + 1, AS_TEST_BUF_RTP_TIME (now + frame_dur)));

  /* Pushing packet #3 after #4 when #4 would have normally arrived */
  gst_harness_set_time (h, now + frame_dur);
  gst_harness_push (h, generate_test_buffer_full (now + frame_dur, seq,
          AS_TEST_BUF_RTP_TIME (now)));

  /* Pulling should be retrieving #3 first */
  buffer = gst_harness_pull (h);
  fail_unless_equals_int64 (now, GST_BUFFER_PTS (buffer));
  gst_buffer_unref (buffer);

  /* Pulling should be retrieving #4 second */
  buffer = gst_harness_pull (h);
  fail_unless_equals_int64 (now + frame_dur, GST_BUFFER_PTS (buffer));
  gst_buffer_unref (buffer);

  now += 2 * frame_dur;
  seq += 2;

  /* Pushing packet #5 normal again */
  gst_harness_set_time (h, now);
  gst_harness_push (h, generate_test_buffer_full (now, seq,
          AS_TEST_BUF_RTP_TIME (now)));
  buffer = gst_harness_pull (h);
  fail_unless_equals_int64 (now, GST_BUFFER_PTS (buffer));
  gst_buffer_unref (buffer);

  seq++;
  now += frame_dur;

  /* Pushing packet #6 half a frame early to trigger clock skew */
  gst_harness_set_time (h, now);
  gst_harness_push (h, generate_test_buffer_full (now, seq,
          AS_TEST_BUF_RTP_TIME (now + frame_dur / 2)));
  buffer = gst_harness_pull (h);
  fail_unless (now + frame_dur / 2 > GST_BUFFER_PTS (buffer),
      "pts should have been adjusted due to clock skew");
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_deadline_ts_offset)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  GstClockID id;
  const gint jb_latency_ms = 10;

  gst_harness_set_src_caps (h, generate_caps ());
  testclock = gst_harness_get_testclock (h);

  g_object_set (h->element, "latency", jb_latency_ms, NULL);

  /* push the first buffer in */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (0)));

  /* wait_next_timeout() syncs on the deadline timer */
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  fail_unless_equals_uint64 (jb_latency_ms * GST_MSECOND,
      gst_clock_id_get_time (id));
  gst_clock_id_unref (id);

  /* add ts-offset while waiting */
  g_object_set (h->element, "ts-offset", 20 * GST_MSECOND, NULL);

  gst_test_clock_set_time_and_process (testclock, jb_latency_ms * GST_MSECOND);

  /* wait_next_timeout() syncs on the new deadline timer */
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  fail_unless_equals_uint64 ((20 + jb_latency_ms) * GST_MSECOND,
      gst_clock_id_get_time (id));
  gst_clock_id_unref (id);

  /* now make deadline timer timeout */
  gst_test_clock_set_time_and_process (testclock,
      (20 + jb_latency_ms) * GST_MSECOND);

  gst_buffer_unref (gst_harness_pull (h));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_deadline_ts_offset_overflow)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  GstClockID id;
  const gint jb_latency_ms = 10;

  gst_harness_set_src_caps (h, generate_caps ());
  testclock = gst_harness_get_testclock (h);

  g_object_set (h->element, "latency", jb_latency_ms, NULL);

  /* push the first buffer in */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer (0)));

  /* wait_next_timeout() syncs on the deadline timer */
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  fail_unless_equals_uint64 (jb_latency_ms * GST_MSECOND,
      gst_clock_id_get_time (id));
  gst_clock_id_unref (id);

  /* add ts-offset while waiting, this reschedules the deadline to a negative time */
  g_object_set (h->element, "ts-offset", -20 * GST_MSECOND, NULL);

  /* wait_next_timeout() syncs on the new deadline timer, which is scheduled
   * to the jitterbuffer latency due to being in the past */
  gst_test_clock_wait_for_next_pending_id (testclock, &id);
  fail_unless_equals_uint64 (jb_latency_ms * GST_MSECOND,
      gst_clock_id_get_time (id));
  gst_clock_id_unref (id);

  gst_test_clock_set_time_and_process (testclock, jb_latency_ms * GST_MSECOND);

  gst_buffer_unref (gst_harness_pull (h));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_big_gap_seqnum)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  const gint num_consecutive = 5;
  const guint gap = 20000;
  gint i;
  guint seqnum_org;
  GstClockTime dts_base;
  guint seqnum_base;
  guint32 rtpts_base;
  GstClockTime expected_ts;

  g_object_set (h->element, "do-lost", TRUE, "do-retransmission", TRUE, NULL);
  seqnum_org = construct_deterministic_initial_state (h, 100);

  /* a sudden jump in sequence-numbers (and rtptime), but packets keep arriving
     at the same pace */
  dts_base = seqnum_org * TEST_BUF_DURATION;
  seqnum_base = seqnum_org + gap;
  rtpts_base = seqnum_base * TEST_RTP_TS_DURATION;

  for (i = 0; i < num_consecutive; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            generate_test_buffer_full (dts_base + i * TEST_BUF_DURATION,
                seqnum_base + i, rtpts_base + i * TEST_RTP_TS_DURATION)));
  }

  for (i = 0; i < num_consecutive; i++) {
    GstBuffer *buf = gst_harness_pull (h);
    guint expected_seqnum = seqnum_base + i;
    fail_unless_equals_int (expected_seqnum, get_rtp_seq_num (buf));

    expected_ts = dts_base + i * TEST_BUF_DURATION;
    fail_unless_equals_int (expected_ts, GST_BUFFER_PTS (buf));
    gst_buffer_unref (buf);
  }

  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_big_gap_arrival_time)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  const gint num_consecutive = 5;
  const guint gap = 20000;
  gint i;
  guint seqnum_org;
  GstClockTime dts_base;
  guint seqnum_base;
  guint32 rtpts_base;
  GstClockTime expected_ts;

  g_object_set (h->element, "do-lost", TRUE, "do-retransmission", TRUE, NULL);
  seqnum_org = construct_deterministic_initial_state (h, 100);

  /* packets are being held back on the wire, then continues */
  dts_base = (seqnum_org + gap) * TEST_BUF_DURATION;
  seqnum_base = seqnum_org;
  rtpts_base = seqnum_base * TEST_RTP_TS_DURATION;

  for (i = 0; i < num_consecutive; i++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            generate_test_buffer_full (dts_base + i * TEST_BUF_DURATION,
                seqnum_base + i, rtpts_base + i * TEST_RTP_TS_DURATION)));
  }

  for (i = 0; i < num_consecutive; i++) {
    GstBuffer *buf = gst_harness_pull (h);
    guint expected_seqnum = seqnum_base + i;
    fail_unless_equals_int (expected_seqnum, get_rtp_seq_num (buf));

    expected_ts = dts_base + i * TEST_BUF_DURATION;
    fail_unless_equals_int (expected_ts, GST_BUFFER_PTS (buf));
    gst_buffer_unref (buf);
  }

  fail_unless_equals_int (0, gst_harness_events_in_queue (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

typedef struct
{
  guint seqnum_offset;
  guint late_buffer;
} TestLateArrivalInput;

static const TestLateArrivalInput
    test_considered_lost_packet_in_large_gap_arrives_input[] = {
  {0, 1}, {0, 2}, {65535, 1}, {65535, 2}, {65534, 1}, {65534, 2}
};

GST_START_TEST (test_considered_lost_packet_in_large_gap_arrives)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstTestClock *testclock;
  GstBuffer *buffer;
  gint jb_latency_ms = 20;
  const TestLateArrivalInput *test_input =
      &test_considered_lost_packet_in_large_gap_arrives_input[__i__];
  guint seq_offset = test_input->seqnum_offset;
  guint late_buffer = test_input->late_buffer;
  gint i;

  gst_harness_set_src_caps (h, generate_caps ());
  testclock = gst_harness_get_testclock (h);
  g_object_set (h->element, "do-lost", TRUE, "latency", jb_latency_ms, NULL);

  /* first push buffer 0 */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer_full (0 * TEST_BUF_DURATION,
              0 + seq_offset, 0 * TEST_RTP_TS_DURATION)));
  fail_unless (gst_harness_crank_single_clock_wait (h));
  gst_buffer_unref (gst_harness_pull (h));

  /* drop GstEventStreamStart & GstEventCaps & GstEventSegment */
  for (i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));

  /* hop over 3 packets, and push buffer 4 (gap of 3) */
  gst_harness_set_time (h, 4 * TEST_BUF_DURATION);
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, generate_test_buffer_full (4 * TEST_BUF_DURATION,
              4 + seq_offset, 4 * TEST_RTP_TS_DURATION)));

  /* we get a "bundled" lost-event for the 2 packets now already too late */
  verify_lost_event (h, 1 + seq_offset, 1 * TEST_BUF_DURATION,
      2 * TEST_BUF_DURATION);

  /* and another one for buffer 3 */
  verify_lost_event (h, 3 + seq_offset, 3 * TEST_BUF_DURATION,
      1 * TEST_BUF_DURATION);

  /* A late buffer arrives */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h,
          generate_test_buffer_full (late_buffer * TEST_BUF_DURATION,
              late_buffer + seq_offset, late_buffer * TEST_RTP_TS_DURATION)));

  /* buffer 4 is pushed as normal */
  buffer = gst_harness_pull (h);
  fail_unless_equals_int ((4 + seq_offset) & 0xffff, get_rtp_seq_num (buffer));
  gst_buffer_unref (buffer);

  /* we have lost 3, and one of them arrived eventually, but too late */
  fail_unless (verify_jb_stats (h->element,
          gst_structure_new ("application/x-rtp-jitterbuffer-stats",
              "num-pushed", G_TYPE_UINT64, (guint64) 2,
              "num-lost", G_TYPE_UINT64, (guint64) 3,
              "num-late", G_TYPE_UINT64, (guint64) 1, NULL)));

  gst_object_unref (testclock);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_performance)
{
  GstHarness *h =
      gst_harness_new_parse
      ("rtpjitterbuffer do-lost=1 do-retransmission=1 latency=1000");
  GTimer *timer = g_timer_new ();
  const gdouble test_duration = 2.0;
  guint buffers_pushed = 0;
  guint buffers_received;

  gst_harness_set_src_caps (h, generate_caps ());
  gst_harness_use_systemclock (h);

  while (g_timer_elapsed (timer, NULL) < test_duration) {
    /* Simulate 1ms packets */
    guint n = buffers_pushed * 2;       // every packet also produces a gap
    guint16 seqnum = n & 0xffff;
    guint32 rtp_ts = n * 8;
    GstClockTime dts = n * GST_MSECOND;
    gst_harness_push (h, generate_test_buffer_full (dts, seqnum, rtp_ts));
    buffers_pushed++;
    g_usleep (G_USEC_PER_SEC / 10000);
  }
  g_timer_destroy (timer);

  buffers_received = gst_harness_buffers_received (h);
  GST_INFO ("Pushed %d, received %d (%.1f%%)", buffers_pushed, buffers_received,
      100.0 * buffers_received / buffers_pushed);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_fill_queue)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  const gint num_consecutive = 40000;
  GstBuffer *buf;
  gint i;

  gst_harness_use_testclock (h);

  gst_harness_set_src_caps (h, generate_caps ());

  gst_harness_play (h);

  gst_harness_push (h, generate_test_buffer (1000));
  /* Skip 1001 */
  for (i = 2; i < num_consecutive; i++)
    gst_harness_push (h, generate_test_buffer (1000 + i));

  buf = gst_harness_pull (h);
  fail_unless_equals_int (1000, get_rtp_seq_num (buf));
  gst_buffer_unref (buf);
  /* 1001 is skipped */
  for (i = 2; i < num_consecutive; i++) {
    GstBuffer *buf = gst_harness_pull (h);
    fail_unless_equals_int (1000 + i, get_rtp_seq_num (buf));
    gst_buffer_unref (buf);
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

typedef struct
{
  gint64 dts_skew;
  gint16 seqnum_skew;
} RtxSkewCtx;

static const RtxSkewCtx rtx_does_not_affect_pts_calculation_input[] = {
  {0, 0},
  {20 * GST_MSECOND, -100},
  {20 * GST_MSECOND, 100},
  {-10 * GST_MSECOND, 1},
  {100 * GST_MSECOND, 0},
};

GST_START_TEST (test_rtx_does_not_affect_pts_calculation)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  GstBuffer *buffer;
  guint next_seqnum;
  guint rtx_seqnum;
  GstClockTime now;
  const RtxSkewCtx *ctx = &rtx_does_not_affect_pts_calculation_input[__i__];

  /* set up a deterministic state and take the time on the clock */
  g_object_set (h->element, "do-retransmission", TRUE, "do-lost", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, 3000);
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));

  /* push in a "bad" RTX buffer, arriving at various times / seqnums */
  rtx_seqnum = next_seqnum + ctx->seqnum_skew;
  buffer = generate_test_buffer_full (now + ctx->dts_skew, rtx_seqnum,
      rtx_seqnum * TEST_RTP_TS_DURATION);
  GST_BUFFER_FLAG_SET (buffer, GST_RTP_BUFFER_FLAG_RETRANSMISSION);
  gst_harness_push (h, buffer);

  /* now push in the next regular buffer at its ideal time, and verify the
     rogue RTX-buffer did not mess things up */
  push_test_buffer (h, next_seqnum);
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));
  buffer = gst_harness_pull (h);
  fail_unless_equals_int64 (now, GST_BUFFER_PTS (buffer));

  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_dont_drop_packet_based_on_skew)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  guint base_seqnum;
  GstClockTime now;
  guint i;

  /* set up a deterministic state and take the time on the clock */
  g_object_set (h->element, "do-retransmission", TRUE, "do-lost", TRUE, NULL);
  base_seqnum = construct_deterministic_initial_state (h, 20);
  now = gst_clock_get_time (GST_ELEMENT_CLOCK (h->element));

  /* and after a delay of 50ms... */
  now += GST_MSECOND * 50;
  gst_test_clock_set_time (GST_TEST_CLOCK (GST_ELEMENT_CLOCK (h->element)),
      now);

  /* ..two more buffers arrive in perfect order */
  for (i = 0; i < 2; i++) {
    gst_harness_push (h, generate_test_buffer_full (now + i * GST_MSECOND * 20,
            base_seqnum + i, (base_seqnum + i) * TEST_RTP_TS_DURATION));
  }

  /* verify we did not drop any of them */
  for (i = 0; i < 2; i++) {
    gst_buffer_unref (gst_harness_pull (h));
  }

  gst_harness_teardown (h);
}

GST_END_TEST;

static gboolean
check_drop_message (GstMessage * drop_msg, const char *reason_check,
    guint seqnum_check, guint num_msg)
{

  const GstStructure *s = gst_message_get_structure (drop_msg);
  const gchar *reason_str;
  GstClockTime timestamp;
  guint seqnum;
  guint num_too_late;
  guint num_drop_on_latency;

  guint num_too_late_check = 0;
  guint num_drop_on_latency_check = 0;

  /* Check that fields exist */
  fail_unless (gst_structure_get_uint (s, "seqnum", &seqnum));
  fail_unless (gst_structure_get_uint64 (s, "timestamp", &timestamp));
  fail_unless (gst_structure_get_uint (s, "num-too-late", &num_too_late));
  fail_unless (gst_structure_get_uint (s, "num-drop-on-latency",
          &num_drop_on_latency));
  fail_unless (reason_str = gst_structure_get_string (s, "reason"));

  /* Assing what to compare message fields to based on message reason */
  if (g_strcmp0 (reason_check, "too-late") == 0) {
    num_too_late_check += num_msg;
  } else if (g_strcmp0 (reason_check, "drop-on-latency") == 0) {
    num_drop_on_latency_check += num_msg;
  } else {
    return FALSE;
  }

  /* Check that fields have correct value */
  fail_unless (seqnum == seqnum_check);
  fail_unless (g_strcmp0 (reason_str, reason_check) == 0);
  fail_unless (num_too_late == num_too_late_check);
  fail_unless (num_drop_on_latency == num_drop_on_latency_check);

  return TRUE;
}

GST_START_TEST (test_drop_messages_too_late)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 100;
  guint next_seqnum;
  GstBus *bus;
  GstMessage *drop_msg;
  gboolean have_message = FALSE;

  g_object_set (h->element, "post-drop-messages", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Create a bus to get the drop message on */
  bus = gst_bus_new ();
  gst_element_set_bus (h->element, bus);

  /* Push test buffer resulting in gap of one */
  push_test_buffer (h, next_seqnum + 1);

  /* Advance time to trigger timeout of the missing buffer */
  gst_harness_crank_single_clock_wait (h);

  /* Pull out and unref pushed buffer */
  gst_buffer_unref (gst_harness_pull (h));

  /* Push missing buffer, now arriving "too-late" */
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer (next_seqnum)));

  /* Pop the resulting drop message and check its correctness */
  while (!have_message &&
      (drop_msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ELEMENT)) != NULL) {
    if (gst_message_has_name (drop_msg, "drop-msg")) {
      fail_unless (check_drop_message (drop_msg, "too-late", next_seqnum, 1));
      have_message = TRUE;
    }
    gst_message_unref (drop_msg);
  }
  fail_unless (have_message);

  /* Cleanup */
  gst_element_set_bus (h->element, NULL);
  gst_object_unref (bus);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_drop_messages_drop_on_latency)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  gint latency_ms = 20;
  guint next_seqnum;
  guint first_seqnum;
  guint final_seqnum;
  GstBus *bus;
  GstMessage *drop_msg;
  gboolean have_message = FALSE;

  g_object_set (h->element, "post-drop-messages", TRUE, NULL);
  g_object_set (h->element, "drop-on-latency", TRUE, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Create a bus to get the drop message on */
  bus = gst_bus_new ();
  gst_element_set_bus (h->element, bus);

  /* Push 3 buffers in correct seqnum order with initial gap of 1, with the buffers
   * arriving simultaneously in harness time. First buffer will wait for gap buffer,
   * and the third arriving buffer will trigger the first to be dropped due to
   * drop-on-latency.
   */
  first_seqnum = ++next_seqnum;
  final_seqnum = next_seqnum + 2;
  for (; next_seqnum <= final_seqnum; next_seqnum++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            generate_test_buffer_full (next_seqnum * TEST_BUF_DURATION,
                next_seqnum, next_seqnum * TEST_RTP_TS_DURATION)));
  }

  /* Pop the resulting drop message and check its correctness */
  while (!have_message &&
      (drop_msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ELEMENT)) != NULL) {
    if (gst_message_has_name (drop_msg, "drop-msg")) {
      fail_unless (check_drop_message (drop_msg, "drop-on-latency",
              first_seqnum, 1));
      have_message = TRUE;
    }
    gst_message_unref (drop_msg);
  }
  fail_unless (have_message);

  /* Cleanup */
  gst_element_set_bus (h->element, NULL);
  gst_object_unref (bus);
  gst_buffer_unref (gst_harness_take_all_data_as_buffer (h));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_drop_messages_interval)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  guint latency_ms = 100;
  GstClockTime interval = 10;
  guint next_seqnum;
  guint final_seqnum;
  GstBus *bus;
  GstMessage *drop_msg;
  GstClockTime now;
  guint num_late_not_sent = 0;
  guint num_sent_msg = 0;

  g_object_set (h->element, "post-drop-messages", TRUE, NULL);
  g_object_set (h->element, "drop-messages-interval", (guint) interval, NULL);
  next_seqnum = construct_deterministic_initial_state (h, latency_ms);

  /* Create a bus to get the drop message on */
  bus = gst_bus_new ();
  gst_element_set_bus (h->element, bus);

  /* Jump 1 second forward in time */
  now = 1 * GST_SECOND;
  gst_harness_set_time (h, now);

  /* Push a packet with a gap of 3, that now is very late */
  final_seqnum = next_seqnum + 3;

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          generate_test_buffer_full (now,
              final_seqnum, final_seqnum * TEST_RTP_TS_DURATION)));

  /* Pull and unref pushed buffer */
  gst_buffer_unref (gst_harness_pull (h));

  /* The 3 missing packets are now pushed with half the message "interval" between them.
   * When arriving they are considered as "too-late". Only the first and third should trigger
   * a drop_msg, as the second is dropped during the interval where no new messages will be sent.
   * The second should have num-too-late=2, as the "too-late" event that never sent a message
   * still increments the count of dropped "too-late" buffers.
   */
  for (; next_seqnum < final_seqnum; next_seqnum++) {
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
            generate_test_buffer (next_seqnum)));
    num_late_not_sent++;

    /* Pop a potential drop message and check its correctness */
    while ((drop_msg = gst_bus_pop (bus)) != NULL) {
      if (gst_message_has_name (drop_msg, "drop-msg")) {
        fail_unless (check_drop_message (drop_msg, "too-late", next_seqnum,
                num_late_not_sent));

        num_late_not_sent = 0;
        num_sent_msg++;
      }
      gst_message_unref (drop_msg);
    }
    /* Advance time half the minimum interval of sending drop messages */
    now += (interval * GST_MSECOND) / 2;
    gst_harness_set_time (h, now);
  }
  /* Exactly two drop messages should have been sent */
  fail_unless (num_sent_msg == 2);

  /* Cleanup */
  gst_element_set_bus (h->element, NULL);
  gst_object_unref (bus);
  gst_harness_teardown (h);
}

GST_END_TEST;

typedef struct
{
  gint seqnum_d;
  gint rtptime_d;
  gboolean rtx;
  gint sleep_us;
} BufferArrayCtx;

static void
buffer_array_push (GstHarness * h, GArray * array,
    guint16 seqnum_base, guint32 rtptime_base)
{
  guint16 seqnum = seqnum_base;
  guint32 rtptime = rtptime_base;
  guint i;

  for (i = 0; i < array->len; i++) {
    BufferArrayCtx *ctx = &g_array_index (array, BufferArrayCtx, i);
    seqnum += ctx->seqnum_d;
    rtptime += ctx->rtptime_d;
    push_test_buffer_now (h, seqnum, rtptime, ctx->rtx);
    g_usleep (ctx->sleep_us);
  }
}

static gint
buffer_array_get_max_seqnum_delta (GArray * array)
{
  gint delta = 0;
  gint max_delta = 0;
  guint i;

  for (i = 0; i < array->len; i++) {
    BufferArrayCtx *ctx = &g_array_index (array, BufferArrayCtx, i);
    delta += ctx->seqnum_d;
    if (delta > max_delta)
      max_delta = delta;
  }
  return max_delta;
}

static void
buffer_array_append_sequential (GArray * array, guint num_bufs)
{
  guint i;
  for (i = 0; i < num_bufs; i++) {
    BufferArrayCtx ctx;
    ctx.seqnum_d = 1;
    ctx.rtptime_d = TEST_RTP_TS_DURATION;       /* 20ms for 8KHz */
    ctx.rtx = FALSE;
    ctx.sleep_us = G_USEC_PER_SEC / 1000 * 20;  /* 20ms */
    g_array_append_val (array, ctx);
  }
}

static void
buffer_array_append_ctx (GArray * array, BufferArrayCtx * bufs, guint num_bufs)
{
  guint i;
  for (i = 0; i < num_bufs; i++) {
    g_array_append_val (array, bufs[i]);
  }
}

static gboolean
check_for_stall (GstHarness * h, BufferArrayCtx * bufs, guint num_bufs)
{
  guint latency_ms;
  guint initial_bufs;
  guint16 base_seqnum = 10000;
  guint32 base_rtptime = base_seqnum * TEST_RTP_TS_DURATION;
  guint16 max_seqnum;
  guint in_queue;
  GArray *array;

  gst_harness_use_systemclock (h);
  gst_element_set_base_time (h->element,
      gst_clock_get_time (GST_ELEMENT_CLOCK (h->element)));
  gst_harness_set_src_caps (h, generate_caps ());

  g_object_get (h->element, "latency", &latency_ms, NULL);
  initial_bufs = latency_ms / TEST_BUF_MS;

  array = g_array_new (FALSE, FALSE, sizeof (BufferArrayCtx));
  buffer_array_append_sequential (array, initial_bufs);
  buffer_array_append_ctx (array, bufs, num_bufs);
  max_seqnum = base_seqnum + buffer_array_get_max_seqnum_delta (array);
  buffer_array_push (h, array, base_seqnum, base_rtptime);
  g_array_set_size (array, 0);

  /* sleep a bit to settle things down, then find out
     how many buffers have been pushed out */
  g_usleep (G_USEC_PER_SEC);
  in_queue = gst_harness_buffers_in_queue (h);

  /* push another 50 buffers normally */
  buffer_array_append_sequential (array, 50);
  base_seqnum = max_seqnum + 1;
  base_rtptime = base_seqnum * TEST_RTP_TS_DURATION;
  buffer_array_push (h, array, base_seqnum, base_rtptime);
  g_array_unref (array);

  {
    gint64 start_time = g_get_monotonic_time ();
    gint64 timeout_s = 30;
    while (gst_harness_buffers_in_queue (h) <= in_queue) {

      gint64 duration_s =
          (g_get_monotonic_time () - start_time) / G_USEC_PER_SEC;
      if (duration_s > timeout_s)
        break;

      g_usleep (G_USEC_PER_SEC / 100);
    }
  }


  /* we expect at least some of those buffers to come through */
  return gst_harness_buffers_in_queue (h) > in_queue;
}

GST_START_TEST (test_reset_timers_does_not_stall)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  BufferArrayCtx bufs[] = {
    /* *INDENT-OFF* */
    { 1, 0, FALSE, 0},
    { 2, 0, FALSE, 0},
    { 3, 0, FALSE, 0},
    { 4, 0, FALSE, 0},
    { 5, 0, FALSE, 0},
    { 6, 0, FALSE, 0},
    { 7, 0, FALSE, 0},
    { 8, 0, FALSE, 0},
    { 9, 0, FALSE, 0},
    {10, 0, FALSE, 0},
    /* *INDENT-ON* */
  };

  g_object_set (h->element, "latency", 100,
      "do-retransmission", TRUE, "do-lost", TRUE, NULL);
  g_object_set (h->element, "max-dropout-time", 10, NULL);
  fail_unless (check_for_stall (h, bufs, G_N_ELEMENTS (bufs)));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_reset_timers_does_not_stall_2)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  BufferArrayCtx bufs[] = {
    /* *INDENT-OFF* */
    {278, 21920, FALSE,  31695},
    { 37,  5920, FALSE,  89911},
    {173, 13600, FALSE, 108078},
    { 30, 27200, FALSE, 190920},
    {-20, 43840,  TRUE, 150552},
    { 42,  4480, FALSE, 131498},
    /* *INDENT-ON* */
  };

  g_object_set (h->element, "latency", 200,
      "do-retransmission", TRUE, "do-lost", TRUE, NULL);
  fail_unless (check_for_stall (h, bufs, G_N_ELEMENTS (bufs)));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_multiple_lost_do_not_stall)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  BufferArrayCtx bufs[] = {
    /* *INDENT-OFF* */
    { 39,  4960, FALSE,   58},
    {-28, -5280, FALSE, 1000},
    /* *INDENT-ON* */
  };

  g_object_set (h->element, "latency", 200,
      "do-retransmission", TRUE, "do-lost", TRUE, NULL);
  fail_unless (check_for_stall (h, bufs, G_N_ELEMENTS (bufs)));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_reset_using_rtx_packets_does_not_stall)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");
  BufferArrayCtx bufs[] = {
    /* *INDENT-OFF* */
    {  1,    1 * TEST_RTP_TS_DURATION, FALSE, 2000000},
    {  62,  62 * TEST_RTP_TS_DURATION, FALSE, 0},
    { -13, -13 * TEST_RTP_TS_DURATION, TRUE, 10000},
    {   1,   1 * TEST_RTP_TS_DURATION, TRUE, 0},
    {   1,   1 * TEST_RTP_TS_DURATION, TRUE, 0},
    {   1,   1 * TEST_RTP_TS_DURATION, TRUE, 0},
    {   1,   1 * TEST_RTP_TS_DURATION, TRUE, 0},
    {   1,   1 * TEST_RTP_TS_DURATION, TRUE, 0},
    /* *INDENT-ON* */
  };

  g_object_set (h->element, "latency", 400,
      "do-retransmission", TRUE, "do-lost", TRUE, "max-misorder-time", 1, NULL);
  fail_unless (check_for_stall (h, bufs, G_N_ELEMENTS (bufs)));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_gap_using_rtx_does_not_stall)
{
  GstHarness *h = gst_harness_new ("rtpjitterbuffer");

  BufferArrayCtx bufs[] = {
    /* *INDENT-OFF* */
    { 201, -1440, FALSE, 185591 },
    { 265,     1, FALSE,      0 },
    /* *INDENT-ON* */
  };

  g_object_set (h->element, "do-lost", TRUE,
      "do-retransmission", TRUE,
      "rtx-next-seqnum", FALSE, "rtx-delay-reorder", 0, NULL);

  fail_unless (check_for_stall (h, bufs, G_N_ELEMENTS (bufs)));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_early_rtcp_sr_allows_meta)
{
  GstElement *jitterbuffer;
  GstPad *rtcp_fxsrc_pad;
  GstBuffer *srep_buf;
  GstBuffer *rtp_buffer;
  GstReferenceTimestampMeta *meta;
  GstCaps *ntp_caps;

  // No buffers since we want to control them later
  jitterbuffer = setup_jitterbuffer (0);

  g_object_set (G_OBJECT (jitterbuffer),
      "add-reference-timestamp-meta", TRUE, NULL);

  fail_unless (start_jitterbuffer (jitterbuffer)
      == GST_STATE_CHANGE_SUCCESS, "could not set to playing");

  srep_buf = setup_rtcp_sender_report (jitterbuffer, 3899471400, 1000);

  rtcp_fxsrc_pad = setup_rtcp_pads (jitterbuffer);

  /* rtcp sr is first */
  gst_pad_push (rtcp_fxsrc_pad, srep_buf);

  /* create rtp buf, with matching rtp timestamp */
  rtp_buffer = gst_rtp_buffer_new_allocate (0, 0, 0);

  if (rtp_buffer) {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (gst_rtp_buffer_map (rtp_buffer, GST_MAP_WRITE, &rtp)) {
      gst_rtp_buffer_set_ssrc (&rtp, TEST_BUF_SSRC);
      /* first rtp buffer, but second buffer overall, arrives 1 clock unit 
         after rtcp sr */
      gst_rtp_buffer_set_timestamp (&rtp, 1001);

      gst_rtp_buffer_unmap (&rtp);
    }
  }

  /* RTP buf is second */
  gst_pad_push (mysrcpad, rtp_buffer);

  ntp_caps = gst_caps_new_empty_simple ("timestamp/x-ntp");

  meta = gst_buffer_get_reference_timestamp_meta (rtp_buffer, ntp_caps);

  /* result should match the test time plus one clock unit. One
     clock unit is 125000 nanoseconds */
  fail_unless (meta->timestamp == (3899471400 * GST_SECOND + 125000));

  /* cleanup */
  cleanup_jitterbuffer (jitterbuffer);
  gst_object_unref (rtcp_fxsrc_pad);
  gst_caps_unref (ntp_caps);
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
  tcase_add_test (tc_chain, test_push_eos);
  tcase_add_test (tc_chain, test_basetime);
  tcase_add_test (tc_chain, test_clear_pt_map);

  tcase_add_test (tc_chain, test_lost_event);
  tcase_add_test (tc_chain, test_only_one_lost_event_on_large_gaps);
  tcase_add_test (tc_chain, test_two_lost_one_arrives_in_time);
  tcase_add_test (tc_chain, test_out_of_order_loss_not_reported);
  tcase_add_test (tc_chain, test_late_packets_still_makes_lost_events);
  tcase_add_test (tc_chain, test_lost_event_uses_pts);
  tcase_add_test (tc_chain, test_lost_event_with_backwards_rtptime);

  tcase_add_test (tc_chain, test_all_packets_are_timestamped_zero);
  tcase_add_loop_test (tc_chain, test_num_late_when_considered_lost_arrives, 0,
      2);
  tcase_add_test (tc_chain, test_reorder_of_non_equidistant_packets);
  tcase_add_test (tc_chain,
      test_loss_equidistant_spacing_with_parameter_packets);
  tcase_add_loop_test (tc_chain, test_no_fractional_lost_event_durations, 0,
      G_N_ELEMENTS (no_fractional_lost_event_durations_input));
  tcase_add_test (tc_chain, test_late_lost_with_same_pts);

  tcase_add_test (tc_chain, test_rtx_expected_next);
  tcase_add_test (tc_chain, test_rtx_not_bursting_requests);

  tcase_add_test (tc_chain, test_rtx_next_seqnum_disabled);
  tcase_add_test (tc_chain, test_rtx_two_missing);
  tcase_add_test (tc_chain, test_rtx_buffer_arrives_just_in_time);
  tcase_add_test (tc_chain, test_rtx_buffer_arrives_too_late);
  tcase_add_test (tc_chain, test_rtx_original_buffer_does_not_update_rtx_stats);
  tcase_add_test (tc_chain, test_rtx_duplicate_packet_updates_rtx_stats);
  tcase_add_test (tc_chain,
      test_rtx_buffer_arrives_after_lost_updates_rtx_stats);
  tcase_add_test (tc_chain, test_rtx_rtt_larger_than_retry_timeout);
  tcase_add_test (tc_chain, test_rtx_no_request_if_time_past_retry_period);
  tcase_add_test (tc_chain, test_rtx_same_delay_and_retry_timeout);
  tcase_add_test (tc_chain, test_rtx_with_backwards_rtptime);
  tcase_add_test (tc_chain, test_rtx_timer_reuse);
  tcase_add_test (tc_chain, test_rtx_large_packet_spacing_and_small_rtt);
  tcase_add_test (tc_chain, test_rtx_large_packet_spacing_and_large_rtt);
  tcase_add_test (tc_chain,
      test_rtx_large_packet_spacing_does_not_reset_jitterbuffer);
  tcase_add_test (tc_chain, test_minor_reorder_does_not_skew);
  tcase_add_loop_test (tc_chain, test_rtx_does_not_affect_pts_calculation, 0,
      G_N_ELEMENTS (rtx_does_not_affect_pts_calculation_input));
  tcase_add_test (tc_chain, test_dont_drop_packet_based_on_skew);

  tcase_add_test (tc_chain, test_deadline_ts_offset);
  tcase_add_test (tc_chain, test_deadline_ts_offset_overflow);
  tcase_add_test (tc_chain, test_big_gap_seqnum);
  tcase_add_test (tc_chain, test_big_gap_arrival_time);
  tcase_add_test (tc_chain, test_fill_queue);

  tcase_add_loop_test (tc_chain,
      test_considered_lost_packet_in_large_gap_arrives, 0,
      G_N_ELEMENTS (test_considered_lost_packet_in_large_gap_arrives_input));

  tcase_add_test (tc_chain, test_performance);

  tcase_add_test (tc_chain, test_drop_messages_too_late);
  tcase_add_test (tc_chain, test_drop_messages_drop_on_latency);
  tcase_add_test (tc_chain, test_drop_messages_interval);

  tcase_add_test (tc_chain, test_reset_timers_does_not_stall);
  tcase_add_test (tc_chain, test_reset_timers_does_not_stall_2);
  tcase_add_test (tc_chain, test_multiple_lost_do_not_stall);
  tcase_add_test (tc_chain, test_reset_using_rtx_packets_does_not_stall);
  tcase_add_test (tc_chain, test_gap_using_rtx_does_not_stall);
  tcase_add_test (tc_chain, test_early_rtcp_sr_allows_meta);


  return s;
}

GST_CHECK_MAIN (rtpjitterbuffer);

/* GStreamer
 *
 * unit test for gstrtpsession
 *
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2013 Collabora Ltd.
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
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/check/gstharness.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gsttestclock.h>
#include <gst/check/gstharness.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/net/gstnetaddressmeta.h>
#include <gst/video/video.h>

#define TEST_BUF_CLOCK_RATE 8000
#define TEST_BUF_PT 0
#define TEST_BUF_SSRC 0x01BADBAD
#define TEST_BUF_MS  20
#define TEST_BUF_DURATION (TEST_BUF_MS * GST_MSECOND)
#define TEST_BUF_BPS 512000
#define TEST_BUF_SIZE (TEST_BUF_BPS * TEST_BUF_MS / (1000 * 8))
#define TEST_RTP_TS_DURATION (TEST_BUF_CLOCK_RATE * TEST_BUF_MS / 1000)

#define TEST_TWCC_EXT_ID 5
#define TWCC_EXTMAP_STR "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"

static GstCaps *
generate_caps (void)
{
  return gst_caps_new_simple ("application/x-rtp",
      "clock-rate", G_TYPE_INT, TEST_BUF_CLOCK_RATE,
      "payload", G_TYPE_INT, TEST_BUF_PT, NULL);
}

static GstBuffer *
generate_test_buffer_full (GstClockTime ts,
    guint seqnum, guint32 rtp_ts, guint ssrc,
    gboolean marker_bit, guint8 payload_type, guint8 twcc_ext_id,
    guint16 twcc_seqnum)
{
  GstBuffer *buf;
  guint8 *payload;
  guint i;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  buf = gst_rtp_buffer_new_allocate (TEST_BUF_SIZE, 0, 0);
  GST_BUFFER_PTS (buf) = ts;
  GST_BUFFER_DTS (buf) = ts;

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type (&rtp, payload_type);
  gst_rtp_buffer_set_seq (&rtp, seqnum);
  gst_rtp_buffer_set_timestamp (&rtp, rtp_ts);
  gst_rtp_buffer_set_ssrc (&rtp, ssrc);
  gst_rtp_buffer_set_marker (&rtp, marker_bit);

  payload = gst_rtp_buffer_get_payload (&rtp);
  for (i = 0; i < TEST_BUF_SIZE; i++)
    payload[i] = 0xff;

  if (twcc_ext_id > 0) {
    guint8 twcc_seqnum_be[2];
    GST_WRITE_UINT16_BE (twcc_seqnum_be, twcc_seqnum);
    gst_rtp_buffer_add_extension_onebyte_header (&rtp, twcc_ext_id,
        twcc_seqnum_be, sizeof (twcc_seqnum_be));
  }

  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

static GstBuffer *
generate_test_buffer (guint seqnum, guint ssrc)
{
  return generate_test_buffer_full (seqnum * TEST_BUF_DURATION,
      seqnum, seqnum * TEST_RTP_TS_DURATION, ssrc, FALSE, TEST_BUF_PT, 0, 0);
}

static GstBuffer *
generate_twcc_recv_buffer (guint seqnum,
    GstClockTime arrival_time, gboolean marker_bit)
{
  return generate_test_buffer_full (arrival_time, seqnum,
      seqnum * TEST_RTP_TS_DURATION, TEST_BUF_SSRC, marker_bit, TEST_BUF_PT,
      TEST_TWCC_EXT_ID, seqnum);
}

static GstBuffer *
generate_twcc_send_buffer_full (guint seqnum, gboolean marker_bit,
    guint ssrc, guint8 payload_type)
{
  return generate_test_buffer_full (seqnum * TEST_BUF_DURATION,
      seqnum, seqnum * TEST_RTP_TS_DURATION, ssrc, marker_bit,
      payload_type, TEST_TWCC_EXT_ID, seqnum);
}

static GstBuffer *
generate_twcc_send_buffer (guint seqnum, gboolean marker_bit)
{
  return generate_twcc_send_buffer_full (seqnum, marker_bit, TEST_BUF_SSRC,
      TEST_BUF_PT);
}

typedef struct
{
  GstHarness *send_rtp_h;
  GstHarness *recv_rtp_h;
  GstHarness *rtcp_h;

  GstElement *session;
  GObject *internal_session;
  GstTestClock *testclock;
  GstCaps *caps;

  gboolean running;
  GMutex lock;
  GstStructure *last_twcc_stats;
} SessionHarness;

static GstCaps *
_pt_map_requested (GstElement * element, guint pt, gpointer data)
{
  SessionHarness *h = data;
  return gst_caps_copy (h->caps);
}

static void
_notify_twcc_stats (GParamSpec * spec G_GNUC_UNUSED,
    GObject * object G_GNUC_UNUSED, gpointer data)
{
  SessionHarness *h = data;
  GstStructure *stats;
  g_object_get (h->session, "twcc-stats", &stats, NULL);

  g_mutex_lock (&h->lock);
  if (h->last_twcc_stats)
    gst_structure_free (h->last_twcc_stats);
  h->last_twcc_stats = stats;
  g_mutex_unlock (&h->lock);
}

static GstStructure *
session_harness_get_last_twcc_stats (SessionHarness * h)
{
  GstStructure *ret = NULL;
  g_mutex_lock (&h->lock);
  if (h->last_twcc_stats)
    ret = gst_structure_copy (h->last_twcc_stats);
  g_mutex_unlock (&h->lock);
  return ret;
}

static SessionHarness *
session_harness_new (void)
{
  SessionHarness *h = g_new0 (SessionHarness, 1);
  h->caps = generate_caps ();
  g_mutex_init (&h->lock);

  h->testclock = GST_TEST_CLOCK_CAST (gst_test_clock_new ());
  gst_system_clock_set_default (GST_CLOCK_CAST (h->testclock));

  h->session = gst_element_factory_make ("rtpsession", NULL);
  gst_element_set_clock (h->session, GST_CLOCK_CAST (h->testclock));

  h->send_rtp_h = gst_harness_new_with_element (h->session,
      "send_rtp_sink", "send_rtp_src");
  gst_harness_set_src_caps (h->send_rtp_h, gst_caps_copy (h->caps));

  h->recv_rtp_h = gst_harness_new_with_element (h->session,
      "recv_rtp_sink", "recv_rtp_src");
  gst_harness_set_src_caps (h->recv_rtp_h, gst_caps_copy (h->caps));

  h->rtcp_h = gst_harness_new_with_element (h->session,
      "recv_rtcp_sink", "send_rtcp_src");
  gst_harness_set_src_caps_str (h->rtcp_h, "application/x-rtcp");

  g_signal_connect (h->session, "request-pt-map",
      (GCallback) _pt_map_requested, h);

  g_signal_connect (h->session, "notify::twcc-stats",
      (GCallback) _notify_twcc_stats, h);

  g_object_get (h->session, "internal-session", &h->internal_session, NULL);

  return h;
}

static void
session_harness_free (SessionHarness * h)
{
  gst_system_clock_set_default (NULL);

  gst_caps_unref (h->caps);
  gst_object_unref (h->testclock);

  gst_harness_teardown (h->rtcp_h);
  gst_harness_teardown (h->recv_rtp_h);
  gst_harness_teardown (h->send_rtp_h);

  g_mutex_clear (&h->lock);

  if (h->last_twcc_stats)
    gst_structure_free (h->last_twcc_stats);

  g_object_unref (h->internal_session);
  gst_object_unref (h->session);
  g_free (h);
}

static GstFlowReturn
session_harness_send_rtp (SessionHarness * h, GstBuffer * buf)
{
  return gst_harness_push (h->send_rtp_h, buf);
}

static GstBuffer *
session_harness_pull_send_rtp (SessionHarness * h)
{
  return gst_harness_pull (h->send_rtp_h);
}

static GstFlowReturn
session_harness_recv_rtp (SessionHarness * h, GstBuffer * buf)
{
  return gst_harness_push (h->recv_rtp_h, buf);
}

static GstFlowReturn
session_harness_recv_rtcp (SessionHarness * h, GstBuffer * buf)
{
  return gst_harness_push (h->rtcp_h, buf);
}

static GstBuffer *
session_harness_pull_rtcp (SessionHarness * h)
{
  return gst_harness_pull (h->rtcp_h);
}

static void
session_harness_crank_clock (SessionHarness * h)
{
  gst_test_clock_crank (h->testclock);
}

static gboolean
session_harness_advance_and_crank (SessionHarness * h, GstClockTime delta)
{
  GstClockID res, pending;
  gboolean result;
  gst_test_clock_wait_for_next_pending_id (h->testclock, &pending);
  gst_test_clock_advance_time (h->testclock, delta);
  res = gst_test_clock_process_next_clock_id (h->testclock);
  if (res == pending)
    result = TRUE;
  else
    result = FALSE;
  if (res)
    gst_clock_id_unref (res);
  gst_clock_id_unref (pending);
  return result;
}

static void
session_harness_produce_rtcp (SessionHarness * h, gint num_rtcp_packets)
{
  /* due to randomness in rescheduling of RTCP timeout, we need to
     keep cranking until we have the desired amount of packets */
  while (gst_harness_buffers_in_queue (h->rtcp_h) < num_rtcp_packets) {
    session_harness_crank_clock (h);
    /* allow the rtcp-thread to settle before checking the queue */
    gst_test_clock_wait_for_next_pending_id (h->testclock, NULL);
  }
}

static void
session_harness_force_key_unit (SessionHarness * h,
    guint count, guint ssrc, guint payload, gint * reqid, guint64 * sfr)
{
  GstClockTime running_time = GST_CLOCK_TIME_NONE;
  gboolean all_headers = TRUE;

  GstStructure *s = gst_structure_new ("GstForceKeyUnit",
      "running-time", GST_TYPE_CLOCK_TIME, running_time,
      "all-headers", G_TYPE_BOOLEAN, all_headers,
      "count", G_TYPE_UINT, count,
      "ssrc", G_TYPE_UINT, ssrc,
      "payload", G_TYPE_UINT, payload,
      NULL);

  if (reqid)
    gst_structure_set (s, "reqid", G_TYPE_INT, *reqid, NULL);
  if (sfr)
    gst_structure_set (s, "sfr", G_TYPE_UINT64, *sfr, NULL);

  gst_harness_push_upstream_event (h->recv_rtp_h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, s));
}

static void
session_harness_rtp_retransmission_request (SessionHarness * h,
    guint ssrc, guint seqnum, guint delay, guint deadline, guint avg_rtt)
{
  GstClockTime running_time = GST_CLOCK_TIME_NONE;

  GstStructure *s = gst_structure_new ("GstRTPRetransmissionRequest",
      "running-time", GST_TYPE_CLOCK_TIME, running_time,
      "ssrc", G_TYPE_UINT, ssrc,
      "seqnum", G_TYPE_UINT, seqnum,
      "delay", G_TYPE_UINT, delay,
      "deadline", G_TYPE_UINT, deadline,
      "avg-rtt", G_TYPE_UINT, avg_rtt,
      NULL);
  gst_harness_push_upstream_event (h->recv_rtp_h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, s));
}

static void
_add_twcc_field_to_caps (GstCaps * caps, guint8 ext_id)
{
  gchar *name = g_strdup_printf ("extmap-%u", ext_id);
  gst_caps_set_simple (caps, name, G_TYPE_STRING, TWCC_EXTMAP_STR, NULL);
  g_free (name);
}

static void
session_harness_set_twcc_recv_ext_id (SessionHarness * h, guint8 ext_id)
{
  _add_twcc_field_to_caps (h->caps, ext_id);
  g_signal_emit_by_name (h->session, "clear-pt-map");
}

static void
session_harness_set_twcc_send_ext_id (SessionHarness * h, guint8 ext_id)
{
  GstCaps *caps = gst_caps_copy (h->caps);
  _add_twcc_field_to_caps (caps, ext_id);
  gst_harness_set_src_caps (h->send_rtp_h, caps);
}

GST_START_TEST (test_multiple_ssrc_rr)
{
  SessionHarness *h = session_harness_new ();
  GstFlowReturn res;
  GstBuffer *in_buf, *out_buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  gint i, j;
  guint ssrc_match;

  guint ssrcs[] = {
    0x01BADBAD,
    0xDEADBEEF,
  };

  /* receive buffers with multiple ssrcs */
  for (i = 0; i < 2; i++) {
    for (j = 0; j < G_N_ELEMENTS (ssrcs); j++) {
      in_buf = generate_test_buffer (i, ssrcs[j]);
      res = session_harness_recv_rtp (h, in_buf);
      fail_unless_equals_int (GST_FLOW_OK, res);
    }
  }

  /* crank the rtcp-thread and pull out the rtcp-packet we have generated */
  session_harness_crank_clock (h);
  out_buf = session_harness_pull_rtcp (h);

  /* verify we have report blocks for both ssrcs */
  g_assert (out_buf != NULL);
  fail_unless (gst_rtcp_buffer_validate (out_buf));
  gst_rtcp_buffer_map (out_buf, GST_MAP_READ, &rtcp);
  g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));

  fail_unless_equals_int (G_N_ELEMENTS (ssrcs),
      gst_rtcp_packet_get_rb_count (&rtcp_packet));

  ssrc_match = 0;
  for (i = 0; i < G_N_ELEMENTS (ssrcs); i++) {
    guint32 ssrc;
    gst_rtcp_packet_get_rb (&rtcp_packet, i, &ssrc,
        NULL, NULL, NULL, NULL, NULL, NULL);
    for (j = 0; j < G_N_ELEMENTS (ssrcs); j++) {
      if (ssrcs[j] == ssrc)
        ssrc_match++;
    }
  }
  fail_unless_equals_int (G_N_ELEMENTS (ssrcs), ssrc_match);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (out_buf);

  session_harness_free (h);
}

GST_END_TEST;

/* This verifies that rtpsession will correctly place RBs round-robin
 * across multiple RRs when there are too many senders that their RBs
 * do not fit in one RR */
GST_START_TEST (test_multiple_senders_roundrobin_rbs)
{
  SessionHarness *h = session_harness_new ();
  GstFlowReturn res;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  gint i, j, k;
  guint32 ssrc;
  GHashTable *rb_ssrcs, *tmp_set;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* this is a hack to prevent the sources from timing out when cranking and
     hence messing with RTCP-generation, making the test fail 1/1000 times */
  g_object_set (h->session, "rtcp-min-interval", 20 * GST_SECOND, NULL);

  for (i = 0; i < 2; i++) {     /* cycles between RR reports */
    for (j = 0; j < 5; j++) {   /* packets per ssrc */
      gint seq = (i * 5) + j;
      for (k = 0; k < 35; k++) {        /* number of ssrcs */
        buf = generate_test_buffer (seq, 10000 + k);
        res = session_harness_recv_rtp (h, buf);
        fail_unless_equals_int (GST_FLOW_OK, res);
      }
    }
  }

  rb_ssrcs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_hash_table_unref);

  /* verify the rtcp packets */
  for (i = 0; i < 2; i++) {
    guint expected_rb_count = (i < 1) ? GST_RTCP_MAX_RB_COUNT :
        (35 - GST_RTCP_MAX_RB_COUNT);

    session_harness_produce_rtcp (h, 1);
    buf = session_harness_pull_rtcp (h);
    g_assert (buf != NULL);
    fail_unless (gst_rtcp_buffer_validate (buf));

    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    fail_unless_equals_int (GST_RTCP_TYPE_RR,
        gst_rtcp_packet_get_type (&rtcp_packet));

    ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
    fail_unless_equals_int (0xDEADBEEF, ssrc);

    /* inspect the RBs */
    fail_unless_equals_int (expected_rb_count,
        gst_rtcp_packet_get_rb_count (&rtcp_packet));

    if (i == 0) {
      tmp_set = g_hash_table_new (g_direct_hash, g_direct_equal);
      g_hash_table_insert (rb_ssrcs, GUINT_TO_POINTER (ssrc), tmp_set);
    } else {
      tmp_set = g_hash_table_lookup (rb_ssrcs, GUINT_TO_POINTER (ssrc));
      g_assert (tmp_set);
    }

    for (j = 0; j < expected_rb_count; j++) {
      gst_rtcp_packet_get_rb (&rtcp_packet, j, &ssrc, NULL, NULL,
          NULL, NULL, NULL, NULL);
      g_assert_cmpint (ssrc, >=, 10000);
      g_assert_cmpint (ssrc, <=, 10035);
      g_hash_table_add (tmp_set, GUINT_TO_POINTER (ssrc));
    }

    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }

  /* now verify all received ssrcs have been reported */
  fail_unless_equals_int (1, g_hash_table_size (rb_ssrcs));
  tmp_set = g_hash_table_lookup (rb_ssrcs, GUINT_TO_POINTER (0xDEADBEEF));
  g_assert (tmp_set);
  fail_unless_equals_int (35, g_hash_table_size (tmp_set));

  g_hash_table_unref (rb_ssrcs);
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_no_rbs_for_internal_senders)
{
  SessionHarness *h = session_harness_new ();
  GstFlowReturn res;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  gint i, j, k;
  guint32 ssrc;
  GHashTable *sr_ssrcs;
  GHashTable *rb_ssrcs, *tmp_set;

  /* push latency or the RTPSource won't be ready to produce RTCP */
  gst_harness_push_upstream_event (h->send_rtp_h, gst_event_new_latency (0));

  /* Push RTP from our send SSRCs */
  for (j = 0; j < 5; j++) {     /* packets per ssrc */
    for (k = 0; k < 2; k++) {   /* number of ssrcs */
      buf = generate_test_buffer (j, 10000 + k);
      res = session_harness_send_rtp (h, buf);
      fail_unless_equals_int (GST_FLOW_OK, res);
    }
  }

  /* crank the RTCP pad thread */
  session_harness_crank_clock (h);

  sr_ssrcs = g_hash_table_new (g_direct_hash, g_direct_equal);

  /* verify the rtcp packets */
  for (i = 0; i < 2; i++) {
    buf = session_harness_pull_rtcp (h);
    g_assert (buf != NULL);
    g_assert (gst_rtcp_buffer_validate (buf));

    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    fail_unless_equals_int (GST_RTCP_TYPE_SR,
        gst_rtcp_packet_get_type (&rtcp_packet));

    gst_rtcp_packet_sr_get_sender_info (&rtcp_packet, &ssrc, NULL, NULL,
        NULL, NULL);
    g_assert_cmpint (ssrc, >=, 10000);
    g_assert_cmpint (ssrc, <=, 10001);
    g_hash_table_add (sr_ssrcs, GUINT_TO_POINTER (ssrc));

    /* There should be no RBs as there are no remote senders */
    fail_unless_equals_int (0, gst_rtcp_packet_get_rb_count (&rtcp_packet));

    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }

  /* Ensure both internal senders generated RTCP */
  fail_unless_equals_int (2, g_hash_table_size (sr_ssrcs));
  g_hash_table_unref (sr_ssrcs);

  /* Generate RTP from remote side */
  for (j = 0; j < 5; j++) {     /* packets per ssrc */
    for (k = 0; k < 2; k++) {   /* number of ssrcs */
      buf = generate_test_buffer (j, 20000 + k);
      res = session_harness_recv_rtp (h, buf);
      fail_unless_equals_int (GST_FLOW_OK, res);
    }
  }

  sr_ssrcs = g_hash_table_new (g_direct_hash, g_direct_equal);
  rb_ssrcs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_hash_table_unref);

  /* verify the rtcp packets */
  for (i = 0; i < 2; i++) {
    session_harness_produce_rtcp (h, 1);
    buf = session_harness_pull_rtcp (h);
    g_assert (buf != NULL);
    g_assert (gst_rtcp_buffer_validate (buf));

    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    fail_unless_equals_int (GST_RTCP_TYPE_SR,
        gst_rtcp_packet_get_type (&rtcp_packet));

    gst_rtcp_packet_sr_get_sender_info (&rtcp_packet, &ssrc, NULL, NULL,
        NULL, NULL);
    g_assert_cmpint (ssrc, >=, 10000);
    g_assert_cmpint (ssrc, <=, 10001);
    g_hash_table_add (sr_ssrcs, GUINT_TO_POINTER (ssrc));

    /* There should be 2 RBs: one for each remote sender */
    fail_unless_equals_int (2, gst_rtcp_packet_get_rb_count (&rtcp_packet));

    tmp_set = g_hash_table_new (g_direct_hash, g_direct_equal);
    g_hash_table_insert (rb_ssrcs, GUINT_TO_POINTER (ssrc), tmp_set);

    for (j = 0; j < 2; j++) {
      gst_rtcp_packet_get_rb (&rtcp_packet, j, &ssrc, NULL, NULL,
          NULL, NULL, NULL, NULL);
      g_assert_cmpint (ssrc, >=, 20000);
      g_assert_cmpint (ssrc, <=, 20001);
      g_hash_table_add (tmp_set, GUINT_TO_POINTER (ssrc));
    }

    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }

  /* now verify all received ssrcs have been reported */
  fail_unless_equals_int (2, g_hash_table_size (sr_ssrcs));
  fail_unless_equals_int (2, g_hash_table_size (rb_ssrcs));
  for (i = 10000; i < 10002; i++) {
    tmp_set = g_hash_table_lookup (rb_ssrcs, GUINT_TO_POINTER (i));
    g_assert (tmp_set);
    fail_unless_equals_int (2, g_hash_table_size (tmp_set));
  }

  g_hash_table_unref (rb_ssrcs);
  g_hash_table_unref (sr_ssrcs);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_internal_sources_timeout)
{
  SessionHarness *h = session_harness_new ();
  guint internal_ssrc;
  guint32 ssrc;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  GstRTCPType rtcp_type;
  GstFlowReturn res;
  gint i, j;
  GstCaps *caps;
  gboolean seen_bye;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);
  g_object_get (h->internal_session, "internal-ssrc", &internal_ssrc, NULL);
  fail_unless_equals_int (0xDEADBEEF, internal_ssrc);

  /* this is a hack to prevent the sources from timing out when cranking and
     hence messing with RTCP-generation, making the test fail 1/100 times */
  g_object_set (h->session, "rtcp-min-interval", 20 * GST_SECOND, NULL);

  for (i = 1; i < 4; i++) {
    buf = generate_test_buffer (i, 0xBEEFDEAD);
    res = session_harness_recv_rtp (h, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);
  }

  /* push latency or the RTPSource won't be ready to produce RTCP */
  gst_harness_push_upstream_event (h->send_rtp_h, gst_event_new_latency (0));

  /* verify that rtpsession has sent RR for an internally-created
   * RTPSource that is using the internal-ssrc */
  session_harness_produce_rtcp (h, 1);
  buf = session_harness_pull_rtcp (h);

  fail_unless (buf != NULL);
  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
  fail_unless_equals_int (ssrc, internal_ssrc);
  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  /* ok, now let's push some RTP packets */
  caps = generate_caps ();
  gst_caps_set_simple (caps,
      "ssrc", G_TYPE_UINT, 0x01BADBAD,
      "rtx-ssrc", G_TYPE_UINT, 0x01020304, NULL);
  gst_harness_set_src_caps (h->send_rtp_h, caps);

  for (i = 1; i < 4; i++) {
    buf = generate_test_buffer (i, 0x01BADBAD);
    res = session_harness_send_rtp (h, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);
  }

  /* "rtx" packet */
  buf = generate_test_buffer (5, 0x01020304);
  res = session_harness_send_rtp (h, buf);
  fail_unless_equals_int (GST_FLOW_OK, res);

  /* internal ssrc must have changed already */
  g_object_get (h->internal_session, "internal-ssrc", &internal_ssrc, NULL);
  fail_unless (internal_ssrc != ssrc);
  fail_unless_equals_int (0x01BADBAD, internal_ssrc);

  /* verify SR and RR */
  j = 0;
  for (i = 0; i < 5; i++) {
    session_harness_produce_rtcp (h, 1);
    buf = session_harness_pull_rtcp (h);
    g_assert (buf != NULL);
    fail_unless (gst_rtcp_buffer_validate (buf));
    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    rtcp_type = gst_rtcp_packet_get_type (&rtcp_packet);

    if (rtcp_type == GST_RTCP_TYPE_SR) {
      gst_rtcp_packet_sr_get_sender_info (&rtcp_packet, &ssrc, NULL, NULL, NULL,
          NULL);
      if (ssrc == 0x01BADBAD) {
        g_assert_cmpint (ssrc, ==, internal_ssrc);
        j |= 0x1;
      } else {
        g_assert_cmpint (ssrc, !=, internal_ssrc);
        g_assert_cmpint (ssrc, ==, 0x01020304);
        j |= 0x4;
      }
    } else if (rtcp_type == GST_RTCP_TYPE_RR) {
      ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
      if (internal_ssrc != ssrc)
        j |= 0x2;
    }
    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }
  fail_unless_equals_int (0x7, j);      /* verify we got both SR and RR */

  /* go 30 seconds in the future and observe both sources timing out:
   * 0xDEADBEEF -> BYE,
   * 0x01BADBAD -> becomes receiver only,
   * 0x01020304 -> becomes receiver only */
  fail_unless (session_harness_advance_and_crank (h, 30 * GST_SECOND));

  /* verify BYE and RR */
  j = 0;
  seen_bye = FALSE;
  while (!seen_bye) {
    session_harness_produce_rtcp (h, 1);
    buf = session_harness_pull_rtcp (h);
    fail_unless (buf != NULL);
    fail_unless (gst_rtcp_buffer_validate (buf));
    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    rtcp_type = gst_rtcp_packet_get_type (&rtcp_packet);

    if (rtcp_type == GST_RTCP_TYPE_RR) {
      ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
      if (ssrc == 0x01BADBAD) {
        j |= 0x1;
        fail_unless_equals_int (internal_ssrc, ssrc);
        /* 2 => RR, SDES. There is no BYE here */
        fail_unless_equals_int (2, gst_rtcp_buffer_get_packet_count (&rtcp));
      } else if (ssrc == 0x01020304) {
        j |= 0x4;
        g_assert_cmpint (ssrc, !=, internal_ssrc);
        /* 2 => RR, SDES. There is no BYE here */
        g_assert_cmpint (gst_rtcp_buffer_get_packet_count (&rtcp), ==, 2);
      } else if (ssrc == 0xDEADBEEF) {
        j |= 0x2;
        g_assert_cmpint (ssrc, !=, internal_ssrc);
        /* 3 => RR, SDES, BYE */
        if (gst_rtcp_buffer_get_packet_count (&rtcp) == 3) {
          fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));
          fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));
          fail_unless_equals_int (GST_RTCP_TYPE_BYE,
              gst_rtcp_packet_get_type (&rtcp_packet));
          seen_bye = TRUE;
        }
      }
    }
    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }
  fail_unless_equals_int (0x7, j);      /* verify we got both all BYE and RR */

  session_harness_free (h);
}

GST_END_TEST;

typedef struct
{
  guint8 subtype;
  guint32 ssrc;
  gchar *name;
  GstBuffer *data;
} RTCPAppResult;

static void
on_app_rtcp_cb (GObject * session, guint subtype, guint ssrc,
    const gchar * name, GstBuffer * data, RTCPAppResult * result)
{
  result->subtype = subtype;
  result->ssrc = ssrc;
  result->name = g_strdup (name);
  result->data = data ? gst_buffer_ref (data) : NULL;
}

GST_START_TEST (test_receive_rtcp_app_packet)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  RTCPAppResult result = { 0 };
  guint8 data[] = { 0x11, 0x22, 0x33, 0x44 };

  g_signal_connect (h->internal_session, "on-app-rtcp",
      G_CALLBACK (on_app_rtcp_cb), &result);

  /* Push APP buffer with no data */
  buf = gst_rtcp_buffer_new (1000);
  fail_unless (gst_rtcp_buffer_map (buf, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_APP, &packet));
  gst_rtcp_packet_app_set_subtype (&packet, 21);
  gst_rtcp_packet_app_set_ssrc (&packet, 0x11111111);
  gst_rtcp_packet_app_set_name (&packet, "Test");
  gst_rtcp_buffer_unmap (&rtcp);

  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtcp (h, buf));

  fail_unless_equals_int (21, result.subtype);
  fail_unless_equals_int (0x11111111, result.ssrc);
  fail_unless_equals_string ("Test", result.name);
  fail_unless_equals_pointer (NULL, result.data);

  g_free (result.name);

  /* Push APP buffer with data */
  memset (&result, 0, sizeof (result));
  buf = gst_rtcp_buffer_new (1000);
  fail_unless (gst_rtcp_buffer_map (buf, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_APP, &packet));
  gst_rtcp_packet_app_set_subtype (&packet, 22);
  gst_rtcp_packet_app_set_ssrc (&packet, 0x22222222);
  gst_rtcp_packet_app_set_name (&packet, "Test");
  gst_rtcp_packet_app_set_data_length (&packet, sizeof (data) / 4);
  memcpy (gst_rtcp_packet_app_get_data (&packet), data, sizeof (data));
  gst_rtcp_buffer_unmap (&rtcp);

  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtcp (h, buf));

  fail_unless_equals_int (22, result.subtype);
  fail_unless_equals_int (0x22222222, result.ssrc);
  fail_unless_equals_string ("Test", result.name);
  fail_unless (gst_buffer_memcmp (result.data, 0, data, sizeof (data)) == 0);

  g_free (result.name);
  gst_buffer_unref (result.data);

  session_harness_free (h);
}

GST_END_TEST;

static void
stats_test_cb (GObject * object, GParamSpec * spec, gpointer data)
{
  guint num_sources = 0;
  gboolean *cb_called = data;
  g_assert (*cb_called == FALSE);

  /* We should be able to get a rtpsession property
     without introducing the deadlock */
  g_object_get (object, "num-sources", &num_sources, NULL);

  *cb_called = TRUE;
}

GST_START_TEST (test_dont_lock_on_stats)
{
  SessionHarness *h = session_harness_new ();
  gboolean cb_called = FALSE;

  /* connect to the stats-reporting */
  g_signal_connect (h->session, "notify::stats",
      G_CALLBACK (stats_test_cb), &cb_called);

  /* push latency or the RTPSource won't be ready to produce RTCP */
  gst_harness_push_upstream_event (h->send_rtp_h, gst_event_new_latency (0));

  /* Push RTP buffer to make sure RTCP-thread have started */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 0xDEADBEEF)));

  /* crank the RTCP-thread and pull out rtcp, generating a stats-callback */
  session_harness_crank_clock (h);
  gst_buffer_unref (session_harness_pull_rtcp (h));
  fail_unless (cb_called);

  session_harness_free (h);
}

GST_END_TEST;

static void
suspicious_bye_cb (GObject * object, GParamSpec * spec, gpointer data)
{
  GValueArray *stats_arr;
  GstStructure *stats, *internal_stats;
  gboolean *cb_called = data;
  gboolean internal = FALSE, sent_bye = TRUE;
  guint ssrc = 0;
  guint i;

  g_assert (*cb_called == FALSE);
  *cb_called = TRUE;

  g_object_get (object, "stats", &stats, NULL);
  stats_arr =
      g_value_get_boxed (gst_structure_get_value (stats, "source-stats"));
  g_assert (stats_arr != NULL);
  fail_unless (stats_arr->n_values >= 1);

  for (i = 0; i < stats_arr->n_values; i++) {
    internal_stats = g_value_get_boxed (g_value_array_get_nth (stats_arr, i));
    g_assert (internal_stats != NULL);

    gst_structure_get (internal_stats,
        "ssrc", G_TYPE_UINT, &ssrc,
        "internal", G_TYPE_BOOLEAN, &internal,
        "received-bye", G_TYPE_BOOLEAN, &sent_bye, NULL);

    if (ssrc == 0xDEADBEEF) {
      fail_unless (internal);
      fail_unless (!sent_bye);
      break;
    }
  }
  fail_unless_equals_int (ssrc, 0xDEADBEEF);

  gst_structure_free (stats);
}

static GstBuffer *
create_bye_rtcp (guint32 ssrc)
{
  GstRTCPPacket packet;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GSocketAddress *saddr;
  GstBuffer *buffer = gst_rtcp_buffer_new (1000);

  fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_BYE, &packet));
  gst_rtcp_packet_bye_add_ssrc (&packet, ssrc);
  gst_rtcp_buffer_unmap (&rtcp);

  /* Need to add meta to trigger collision detection */
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 3490);
  gst_buffer_add_net_address_meta (buffer, saddr);
  g_object_unref (saddr);
  return buffer;
}

GST_START_TEST (test_ignore_suspicious_bye)
{
  SessionHarness *h = session_harness_new ();
  gboolean cb_called = FALSE;

  /* connect to the stats-reporting */
  g_signal_connect (h->session, "notify::stats",
      G_CALLBACK (suspicious_bye_cb), &cb_called);

  /* push latency or the RTPSource won't be ready to produce RTCP */
  gst_harness_push_upstream_event (h->send_rtp_h, gst_event_new_latency (0));

  /* Push RTP buffer making our internal SSRC=0xDEADBEEF */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 0xDEADBEEF)));

  /* Receive BYE RTCP referencing our internal SSRC(!?!) (0xDEADBEEF) */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtcp (h, create_bye_rtcp (0xDEADBEEF)));

  /* "crank" and check the stats */
  session_harness_crank_clock (h);
  gst_buffer_unref (session_harness_pull_rtcp (h));
  fail_unless (cb_called);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_rr_stats_assignment)
{
  SessionHarness *h = session_harness_new ();
  GstFlowReturn res;
  GstBuffer *in_buf, *rtcp_buf;
  gint i, j;

  guint ssrcs[] = {
    0x01BADBAD,
    0xDEADBEEF,
  };

  /* receive buffers with multiple ssrcs */
  for (i = 0; i < 2; i++) {
    for (j = 0; j < G_N_ELEMENTS (ssrcs); j++) {
      in_buf = generate_test_buffer (i, ssrcs[j]);
      res = session_harness_recv_rtp (h, in_buf);
      fail_unless_equals_int (GST_FLOW_OK, res);
    }
  }

  /* crank the rtcp-thread and pull out the rtcp-packet we have generated */
  session_harness_crank_clock (h);
  rtcp_buf = session_harness_pull_rtcp (h);

  g_assert (rtcp_buf != NULL);
  fail_unless (gst_rtcp_buffer_validate (rtcp_buf));

  /* Now take this RTCP buffer to a second 'sender' session and check
   * that the RR info gets assigned to the correct internal senders */
  session_harness_free (h);
  h = session_harness_new ();

  /* Send some packets to create the sources */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 0x01BADBAD)));
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 0xDEADBEEF)));

  session_harness_recv_rtcp (h, rtcp_buf);

  for (i = 0; i < G_N_ELEMENTS (ssrcs); i++) {
    guint32 ssrc = ssrcs[i], rb_ssrc;
    GObject *source;
    GstStructure *stats;
    gboolean have_rb = FALSE;

    g_signal_emit_by_name (h->internal_session, "get-source-by-ssrc", ssrc,
        &source);

    g_object_get (source, "stats", &stats, NULL);

    GST_DEBUG ("Got stats from source %" GST_PTR_FORMAT " %" GST_PTR_FORMAT,
        source, stats);

    fail_unless (gst_structure_get_boolean (stats, "have-rb", &have_rb));
    fail_unless (gst_structure_get_uint (stats, "rb-ssrc", &rb_ssrc));
    fail_unless (rb_ssrc == ssrc);

    gst_structure_free (stats);
    g_object_unref (source);
  }
  session_harness_free (h);
}

GST_END_TEST;
static GstBuffer *
create_buffer (guint8 * data, gsize size)
{
  return gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      data, size, 0, size, NULL, NULL);
}

GST_START_TEST (test_receive_regular_pli)
{
  SessionHarness *h = session_harness_new ();
  GstEvent *ev;
  const GstStructure *s;

  /* PLI packet */
  guint8 rtcp_pkt[] = {
    0x81,                       /* PLI */
    0xce,                       /* Type 206 Application layer feedback */
    0x00, 0x02,                 /* Length */
    0x37, 0x56, 0x93, 0xed,     /* Sender SSRC */
    0x37, 0x56, 0x93, 0xed      /* Media SSRC */
  };

  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 928420845)));

  session_harness_recv_rtcp (h, create_buffer (rtcp_pkt, sizeof (rtcp_pkt)));
  fail_unless_equals_int (3,
      gst_harness_upstream_events_received (h->send_rtp_h));

  /* Remove the first 2 reconfigure events */
  fail_unless ((ev = gst_harness_pull_upstream_event (h->send_rtp_h)) != NULL);
  fail_unless_equals_int (GST_EVENT_RECONFIGURE, GST_EVENT_TYPE (ev));
  gst_event_unref (ev);
  fail_unless ((ev = gst_harness_pull_upstream_event (h->send_rtp_h)) != NULL);
  fail_unless_equals_int (GST_EVENT_RECONFIGURE, GST_EVENT_TYPE (ev));
  gst_event_unref (ev);

  /* Then pull and check the force key-unit event */
  fail_unless ((ev = gst_harness_pull_upstream_event (h->send_rtp_h)) != NULL);
  fail_unless_equals_int (GST_EVENT_CUSTOM_UPSTREAM, GST_EVENT_TYPE (ev));
  fail_unless (gst_video_event_is_force_key_unit (ev));
  s = gst_event_get_structure (ev);
  fail_unless (s);
  fail_unless (G_VALUE_HOLDS_UINT (gst_structure_get_value (s, "ssrc")));
  gst_event_unref (ev);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_receive_pli_no_sender_ssrc)
{
  SessionHarness *h = session_harness_new ();
  GstEvent *ev;
  const GstStructure *s;

  /* PLI packet */
  guint8 rtcp_pkt[] = {
    0x81,                       /* PLI */
    0xce,                       /* Type 206 Application layer feedback */
    0x00, 0x02,                 /* Length */
    0x00, 0x00, 0x00, 0x00,     /* Sender SSRC */
    0x37, 0x56, 0x93, 0xed      /* Media SSRC */
  };

  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 928420845)));

  session_harness_recv_rtcp (h, create_buffer (rtcp_pkt, sizeof (rtcp_pkt)));
  fail_unless_equals_int (3,
      gst_harness_upstream_events_received (h->send_rtp_h));

  /* Remove the first 2 reconfigure events */
  fail_unless ((ev = gst_harness_pull_upstream_event (h->send_rtp_h)) != NULL);
  fail_unless_equals_int (GST_EVENT_RECONFIGURE, GST_EVENT_TYPE (ev));
  gst_event_unref (ev);
  fail_unless ((ev = gst_harness_pull_upstream_event (h->send_rtp_h)) != NULL);
  fail_unless_equals_int (GST_EVENT_RECONFIGURE, GST_EVENT_TYPE (ev));
  gst_event_unref (ev);

  /* Then pull and check the force key-unit event */
  fail_unless ((ev = gst_harness_pull_upstream_event (h->send_rtp_h)) != NULL);
  fail_unless_equals_int (GST_EVENT_CUSTOM_UPSTREAM, GST_EVENT_TYPE (ev));
  fail_unless (gst_video_event_is_force_key_unit (ev));
  s = gst_event_get_structure (ev);
  fail_unless (s);
  fail_unless (G_VALUE_HOLDS_UINT (gst_structure_get_value (s, "ssrc")));
  gst_event_unref (ev);

  session_harness_free (h);
}

GST_END_TEST;

static void
add_rtcp_sdes_packet (GstBuffer * gstbuf, guint32 ssrc, const char *cname)
{
  GstRTCPPacket packet;
  GstRTCPBuffer buffer = GST_RTCP_BUFFER_INIT;

  gst_rtcp_buffer_map (gstbuf, GST_MAP_READWRITE, &buffer);

  fail_unless (gst_rtcp_buffer_add_packet (&buffer, GST_RTCP_TYPE_SDES,
          &packet) == TRUE);
  fail_unless (gst_rtcp_packet_sdes_add_item (&packet, ssrc) == TRUE);
  fail_unless (gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_CNAME,
          strlen (cname), (const guint8 *) cname));

  gst_rtcp_buffer_unmap (&buffer);
}


static void
on_ssrc_collision_cb (GstElement * rtpsession, guint ssrc, gpointer user_data)
{
  gboolean *had_collision = user_data;

  *had_collision = TRUE;
}

GST_START_TEST (test_ssrc_collision_when_sending)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstEvent *ev;
  GSocketAddress *saddr;
  gboolean had_collision = FALSE;

  g_signal_connect (h->internal_session, "on-ssrc-collision",
      G_CALLBACK (on_ssrc_collision_cb), &had_collision);


  /* Push SDES with identical SSRC as what we will use for sending RTP,
     establishing this as a non-internal SSRC */
  buf = gst_rtcp_buffer_new (1400);
  add_rtcp_sdes_packet (buf, 0x12345678, "test@foo.bar");
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  session_harness_recv_rtcp (h, buf);

  fail_unless (had_collision == FALSE);

  /* Push RTP buffer making our internal SSRC=0x12345678 */
  buf = generate_test_buffer (0, 0x12345678);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_send_rtp (h, buf));

  fail_unless (had_collision == TRUE);

  /* Verify the packet we just sent is not being boomeranged back to us
     as a received packet! */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  while ((ev = gst_harness_try_pull_upstream_event (h->send_rtp_h)) != NULL) {
    if (GST_EVENT_CUSTOM_UPSTREAM == GST_EVENT_TYPE (ev) &&
        gst_event_has_name (ev, "GstRTPCollision"))
      break;
    gst_event_unref (ev);
  }
  fail_unless (ev != NULL);
  gst_event_unref (ev);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_ssrc_collision_when_sending_loopback)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstEvent *ev;
  GSocketAddress *saddr;
  gboolean had_collision = FALSE;
  guint new_ssrc;
  const GstStructure *s;

  g_signal_connect (h->internal_session, "on-ssrc-collision",
      G_CALLBACK (on_ssrc_collision_cb), &had_collision);

  /* Push SDES with identical SSRC as what we will use for sending RTP,
     establishing this as a non-internal SSRC */
  buf = gst_rtcp_buffer_new (1400);
  add_rtcp_sdes_packet (buf, 0x12345678, "test@foo.bar");
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  session_harness_recv_rtcp (h, buf);

  fail_unless (had_collision == FALSE);

  /* Push RTP buffer making our internal SSRC=0x12345678 */
  buf = generate_test_buffer (0, 0x12345678);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_send_rtp (h, buf));

  fail_unless (had_collision == TRUE);

  /* Verify the packet we just sent is not being boomeranged back to us
     as a received packet! */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  while ((ev = gst_harness_try_pull_upstream_event (h->send_rtp_h)) != NULL) {
    if (GST_EVENT_CUSTOM_UPSTREAM == GST_EVENT_TYPE (ev) &&
        gst_event_has_name (ev, "GstRTPCollision"))
      break;
    gst_event_unref (ev);
  }
  fail_unless (ev != NULL);

  s = gst_event_get_structure (ev);
  fail_unless (gst_structure_get_uint (s, "ssrc", &new_ssrc));
  gst_event_unref (ev);

  /* reset collision detection */
  had_collision = FALSE;

  /* Push SDES from same address but with the new SSRC, as if someone
   * was looping back our packets to us */
  buf = gst_rtcp_buffer_new (1400);
  add_rtcp_sdes_packet (buf, new_ssrc, "test@foo.bar");
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  session_harness_recv_rtcp (h, buf);

  /* Make sure we didn't detect a collision */
  fail_unless (had_collision == FALSE);

  /* Make sure there is no collision event either */
  while ((ev = gst_harness_try_pull_upstream_event (h->send_rtp_h)) != NULL) {
    fail_if (GST_EVENT_CUSTOM_UPSTREAM == GST_EVENT_TYPE (ev) &&
        gst_event_has_name (ev, "GstRTPCollision"));
    gst_event_unref (ev);
  }

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_ssrc_collision_when_receiving)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstEvent *ev;
  GSocketAddress *saddr;
  gboolean had_collision = FALSE;

  g_signal_connect (h->internal_session, "on-ssrc-collision",
      G_CALLBACK (on_ssrc_collision_cb), &had_collision);

  /* Push RTP buffer making our internal SSRC=0x12345678 */
  buf = generate_test_buffer (0, 0x12345678);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_send_rtp (h, buf));

  fail_unless (had_collision == FALSE);

  /* Push SDES with identical SSRC as what we used to send RTP,
     to create a collision */
  buf = gst_rtcp_buffer_new (1400);
  add_rtcp_sdes_packet (buf, 0x12345678, "test@foo.bar");
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  session_harness_recv_rtcp (h, buf);

  fail_unless (had_collision == TRUE);

  /* Verify the packet we just sent is not being boomeranged back to us
     as a received packet! */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  while ((ev = gst_harness_try_pull_upstream_event (h->send_rtp_h)) != NULL) {
    if (GST_EVENT_CUSTOM_UPSTREAM == GST_EVENT_TYPE (ev))
      break;
    gst_event_unref (ev);
  }
  fail_unless (ev != NULL);
  gst_event_unref (ev);

  session_harness_free (h);
}

GST_END_TEST;


GST_START_TEST (test_ssrc_collision_third_party)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GSocketAddress *saddr;
  gboolean had_collision = FALSE;
  guint i;

  g_signal_connect (h->internal_session, "on-ssrc-collision",
      G_CALLBACK (on_ssrc_collision_cb), &had_collision);

  for (i = 0; i < 4; i++) {
    /* Receive 4 buffers SSRC=0x12345678 from 127.0.0.1 to pass probation */
    buf = generate_test_buffer (i, 0x12345678);
    saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
    gst_buffer_add_net_address_meta (buf, saddr);
    g_object_unref (saddr);
    fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtp (h, buf));
  }

  /* Check that we received the first 4 buffer */
  for (i = 0; i < 4; i++) {
    buf = gst_harness_pull (h->recv_rtp_h);
    fail_unless (buf);
    gst_buffer_unref (buf);
  }
  fail_unless (had_collision == FALSE);

  /* Receive buffer SSRC=0x12345678 from 127.0.0.2 */
  buf = generate_test_buffer (0, 0x12345678);
  saddr = g_inet_socket_address_new_from_string ("127.0.0.2", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtp (h, buf));

  /* Verify the packet we just sent has been dropped */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  /* Receive another buffer SSRC=0x12345678 from 127.0.0.1 */
  buf = generate_test_buffer (0, 0x12345678);
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtp (h, buf));


  /* Check that we received the other buffer */
  buf = gst_harness_pull (h->recv_rtp_h);
  fail_unless (buf);
  gst_buffer_unref (buf);
  fail_unless (had_collision == FALSE);

  session_harness_free (h);
}

GST_END_TEST;


GST_START_TEST (test_ssrc_collision_third_party_favor_new)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GSocketAddress *saddr;
  gboolean had_collision = FALSE;
  guint i;

  g_object_set (h->internal_session, "favor-new", TRUE, NULL);
  g_signal_connect (h->internal_session, "on-ssrc-collision",
      G_CALLBACK (on_ssrc_collision_cb), &had_collision);

  for (i = 0; i < 4; i++) {
    /* Receive 4 buffers SSRC=0x12345678 from 127.0.0.1 to pass probation */
    buf = generate_test_buffer (i, 0x12345678);
    saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
    gst_buffer_add_net_address_meta (buf, saddr);
    g_object_unref (saddr);
    fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtp (h, buf));
  }

  /* Check that we received the first 4 buffer */
  for (i = 0; i < 4; i++) {
    buf = gst_harness_pull (h->recv_rtp_h);
    fail_unless (buf);
    gst_buffer_unref (buf);
  }
  fail_unless (had_collision == FALSE);

  /* Receive buffer SSRC=0x12345678 from 127.0.0.2 */
  buf = generate_test_buffer (0, 0x12345678);
  saddr = g_inet_socket_address_new_from_string ("127.0.0.2", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  g_object_unref (saddr);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtp (h, buf));

  /* Check that we received the other buffer */
  buf = gst_harness_pull (h->recv_rtp_h);
  fail_unless (buf);
  gst_buffer_unref (buf);
  fail_unless (had_collision == FALSE);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_ssrc_collision_never_send_on_non_internal_source)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstEvent *ev;
  GSocketAddress *saddr;
  gboolean had_collision = FALSE;

  g_signal_connect (h->internal_session, "on-ssrc-collision",
      G_CALLBACK (on_ssrc_collision_cb), &had_collision);

  /* Push SDES with identical SSRC as what we will use for sending RTP,
     establishing this as a non-internal SSRC */
  buf = gst_rtcp_buffer_new (1400);
  add_rtcp_sdes_packet (buf, 0xdeadbeef, "test@foo.bar");
  saddr = g_inet_socket_address_new_from_string ("127.0.0.1", 8080);
  gst_buffer_add_net_address_meta (buf, saddr);
  session_harness_recv_rtcp (h, buf);
  g_object_unref (saddr);

  fail_unless (had_collision == FALSE);

  /* Push RTP buffer making our internal SSRC=0xdeadbeef */
  buf = generate_test_buffer (0, 0xdeadbeef);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_send_rtp (h, buf));

  fail_unless (had_collision == TRUE);

  /* verify we drop this packet because of SSRC collision */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->send_rtp_h));
  /* Verify the packet we just sent is not being boomeranged back to us
     as a received packet! */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  /* verify we get an upstream GstRTPCollision event */
  while ((ev = gst_harness_try_pull_upstream_event (h->send_rtp_h)) != NULL) {
    if (GST_EVENT_CUSTOM_UPSTREAM == GST_EVENT_TYPE (ev) &&
        gst_event_has_name (ev, "GstRTPCollision"))
      break;
    gst_event_unref (ev);
  }
  fail_unless (ev != NULL);
  gst_event_unref (ev);

  /* Push another RTP buffer and verify that one is not send or "received" as well */
  buf = generate_test_buffer (1, 0xdeadbeef);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_send_rtp (h, buf));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->send_rtp_h));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  /* now generate a BYE to the non-internal SSRC */
  session_harness_produce_rtcp (h, 1);

  /* and verify we can now send using that SSRC */
  buf = generate_test_buffer (2, 0xdeadbeef);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_send_rtp (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h->send_rtp_h));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_request_fir)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  guint8 *fci_data;

  /* add FIR-capabilites to our caps */
  gst_caps_set_simple (h->caps, "rtcp-fb-ccm-fir", G_TYPE_BOOLEAN, TRUE, NULL);
  /* clear pt-map to removed the cached caps without fir */
  g_signal_emit_by_name (h->session, "clear-pt-map");

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire from 2 different ssrcs */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x87654321)));

  /* fix to make the test deterministic: We need to wait for the RTCP-thread
     to have settled to ensure the key-unit will considered once released */
  gst_test_clock_wait_for_next_pending_id (h->testclock, NULL);

  /* request FIR for both SSRCs */
  session_harness_force_key_unit (h, 0, 0x12345678, TEST_BUF_PT, NULL, NULL);
  session_harness_force_key_unit (h, 0, 0x87654321, TEST_BUF_PT, NULL, NULL);

  session_harness_produce_rtcp (h, 1);
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our FIR */
  fail_unless_equals_int (GST_RTCP_TYPE_PSFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_PSFB_TYPE_FIR,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  /* FIR has sender-ssrc as normal, but media-ssrc set to 0, because
     it can have multiple media-ssrcs in its fci-data */
  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0, gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));
  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);

  fail_unless_equals_int (16,
      gst_rtcp_packet_fb_get_fci_length (&rtcp_packet) * sizeof (guint32));

  /* verify the FIR contains both SSRCs */
  fail_unless_equals_int (0x87654321, GST_READ_UINT32_BE (fci_data));
  fail_unless_equals_int (1, fci_data[4]);
  fail_unless_equals_int (0, fci_data[5]);
  fail_unless_equals_int (0, fci_data[6]);
  fail_unless_equals_int (0, fci_data[7]);
  fci_data += 8;

  fail_unless_equals_int (0x12345678, GST_READ_UINT32_BE (fci_data));
  fail_unless_equals_int (1, fci_data[4]);
  fail_unless_equals_int (0, fci_data[5]);
  fail_unless_equals_int (0, fci_data[6]);
  fail_unless_equals_int (0, fci_data[7]);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_request_pli)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;

  /* add PLI-capabilites to our caps */
  gst_caps_set_simple (h->caps, "rtcp-fb-nack-pli", G_TYPE_BOOLEAN, TRUE, NULL);
  /* clear pt-map to removed the cached caps without PLI */
  g_signal_emit_by_name (h->session, "clear-pt-map");

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Wait for first regular RTCP to be sent so that we are clear to send early RTCP */
  session_harness_produce_rtcp (h, 1);
  gst_buffer_unref (session_harness_pull_rtcp (h));

  /* request PLI */
  session_harness_force_key_unit (h, 0, 0x12345678, TEST_BUF_PT, NULL, NULL);

  /* PLI should be produced immediately as early RTCP is allowed. Pull buffer
     without advancing the clock to ensure this is the case */
  buf = session_harness_pull_rtcp (h);
  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our PLI */
  fail_unless_equals_int (GST_RTCP_TYPE_PSFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_PSFB_TYPE_PLI,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));
  fail_unless_equals_int (0, gst_rtcp_packet_fb_get_fci_length (&rtcp_packet));

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_request_fir_after_pli_in_caps)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  guint8 *fci_data;

  /* add PLI-capabilites to our caps */
  gst_caps_set_simple (h->caps, "rtcp-fb-nack-pli", G_TYPE_BOOLEAN, TRUE, NULL);
  /* clear pt-map to removed the cached caps without PLI */
  g_signal_emit_by_name (h->session, "clear-pt-map");

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Wait for first regular RTCP to be sent so that we are clear to send early RTCP */
  session_harness_produce_rtcp (h, 1);
  gst_buffer_unref (session_harness_pull_rtcp (h));

  /* request PLI */
  session_harness_force_key_unit (h, 0, 0x12345678, TEST_BUF_PT, NULL, NULL);

  /* PLI should be produced immediately as early RTCP is allowed. Pull buffer
     without advancing the clock to ensure this is the case */
  buf = session_harness_pull_rtcp (h);
  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our PLI */
  fail_unless_equals_int (GST_RTCP_TYPE_PSFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_PSFB_TYPE_PLI,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));
  fail_unless_equals_int (0, gst_rtcp_packet_fb_get_fci_length (&rtcp_packet));

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  /* Rebuild the caps */
  gst_caps_unref (h->caps);
  h->caps = generate_caps ();

  /* add FIR-capabilites to our caps */
  gst_caps_set_simple (h->caps, "rtcp-fb-ccm-fir", G_TYPE_BOOLEAN, TRUE, NULL);
  /* clear pt-map to removed the cached caps without fir */
  g_signal_emit_by_name (h->session, "clear-pt-map");

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* fix to make the test deterministic: We need to wait for the RTCP-thread
     to have settled to ensure the key-unit will considered once released */
  gst_test_clock_wait_for_next_pending_id (h->testclock, NULL);

  /* request FIR */
  session_harness_force_key_unit (h, 0, 0x12345678, TEST_BUF_PT, NULL, NULL);

  session_harness_produce_rtcp (h, 1);
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our FIR */
  fail_unless_equals_int (GST_RTCP_TYPE_PSFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_PSFB_TYPE_FIR,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  /* FIR has sender-ssrc as normal, but media-ssrc set to 0, because
     it can have multiple media-ssrcs in its fci-data */
  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0, gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));
  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);

  fail_unless_equals_int (8,
      gst_rtcp_packet_fb_get_fci_length (&rtcp_packet) * sizeof (guint32));

  /* verify the FIR contains the SSRC */
  fail_unless_equals_int (0x12345678, GST_READ_UINT32_BE (fci_data));
  fail_unless_equals_int (1, fci_data[4]);
  fail_unless_equals_int (0, fci_data[5]);
  fail_unless_equals_int (0, fci_data[6]);
  fail_unless_equals_int (0, fci_data[7]);
  fail_unless_equals_int (0, fci_data[7]);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_illegal_rtcp_fb_packet)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  /* Zero length RTCP feedback packet (reduced size) */
  const guint8 rtcp_zero_fb_pkt[] = { 0x8f, 0xce, 0x00, 0x00 };

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  buf = gst_buffer_new_and_alloc (sizeof (rtcp_zero_fb_pkt));
  gst_buffer_fill (buf, 0, rtcp_zero_fb_pkt, sizeof (rtcp_zero_fb_pkt));
  GST_BUFFER_DTS (buf) = GST_BUFFER_PTS (buf) = G_GUINT64_CONSTANT (0);

  /* Push the packet, this did previously crash because length of packet was
   * never validated. */
  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtcp (h, buf));

  session_harness_free (h);
}

GST_END_TEST;

typedef struct
{
  GCond *cond;
  GMutex *mutex;
  gboolean fired;
} FeedbackRTCPCallbackData;

static void
feedback_rtcp_cb (GstElement * element, guint fbtype, guint fmt,
    guint sender_ssrc, guint media_ssrc, GstBuffer * fci,
    FeedbackRTCPCallbackData * cb_data)
{
  g_mutex_lock (cb_data->mutex);
  cb_data->fired = TRUE;
  g_cond_wait (cb_data->cond, cb_data->mutex);
  g_mutex_unlock (cb_data->mutex);
}

static void *
send_feedback_rtcp (SessionHarness * h)
{
  GstRTCPPacket packet;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstBuffer *buffer = gst_rtcp_buffer_new (1000);

  fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_PSFB, &packet));
  gst_rtcp_packet_fb_set_type (&packet, GST_RTCP_PSFB_TYPE_PLI);
  gst_rtcp_packet_fb_set_fci_length (&packet, 0);
  gst_rtcp_packet_fb_set_media_ssrc (&packet, 0xABE2B0B);
  gst_rtcp_packet_fb_set_media_ssrc (&packet, 0xDEADBEEF);
  gst_rtcp_buffer_unmap (&rtcp);
  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtcp (h, buffer));

  return NULL;
}

GST_START_TEST (test_feedback_rtcp_race)
{
  SessionHarness *h = session_harness_new ();

  GCond cond;
  GMutex mutex;
  FeedbackRTCPCallbackData cb_data;
  GThread *send_rtcp_thread;

  g_cond_init (&cond);
  g_mutex_init (&mutex);
  cb_data.cond = &cond;
  cb_data.mutex = &mutex;
  cb_data.fired = FALSE;
  g_signal_connect (h->internal_session, "on-feedback-rtcp",
      G_CALLBACK (feedback_rtcp_cb), &cb_data);

  /* Push RTP buffer making external source with SSRC=0xDEADBEEF */
  fail_unless_equals_int (GST_FLOW_OK, session_harness_recv_rtp (h,
          generate_test_buffer (0, 0xDEADBEEF)));

  /* Push feedback RTCP with media SSRC=0xDEADBEEF */
  send_rtcp_thread = g_thread_new (NULL, (GThreadFunc) send_feedback_rtcp, h);

  /* Waiting for feedback RTCP callback to fire */
  while (!cb_data.fired)
    g_usleep (G_USEC_PER_SEC / 100);

  /* While send_rtcp_thread thread is waiting for our signal
     advance the clock by 30sec triggering removal of 0xDEADBEEF,
     as if the source was inactive for too long */
  session_harness_advance_and_crank (h, GST_SECOND * 30);
  gst_buffer_unref (session_harness_pull_rtcp (h));

  /* Let send_rtcp_thread finish */
  g_mutex_lock (&mutex);
  g_cond_signal (&cond);
  g_mutex_unlock (&mutex);
  g_thread_join (send_rtcp_thread);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_dont_send_rtcp_while_idle)
{
  SessionHarness *h = session_harness_new ();

  /* verify the RTCP thread has not started */
  fail_unless_equals_int (0, gst_test_clock_peek_id_count (h->testclock));
  /* and that no RTCP has been pushed */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->rtcp_h));

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_send_rtcp_when_signalled)
{
  SessionHarness *h = session_harness_new ();
  gboolean ret;

  /* verify the RTCP thread has not started */
  fail_unless_equals_int (0, gst_test_clock_peek_id_count (h->testclock));
  /* and that no RTCP has been pushed */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->rtcp_h));

  /* then ask explicitly to send RTCP */
  g_signal_emit_by_name (h->internal_session,
      "send-rtcp-full", GST_SECOND, &ret);
  /* this is FALSE due to no next RTCP check time */
  fail_unless (ret == FALSE);

  /* "crank" and verify RTCP now was sent */
  session_harness_crank_clock (h);
  gst_buffer_unref (session_harness_pull_rtcp (h));

  session_harness_free (h);
}

GST_END_TEST;

static void
validate_sdes_priv (GstBuffer * buf, const char *name_ref, const char *value)
{
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket pkt;

  fail_unless (gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp));

  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &pkt));

  do {
    if (gst_rtcp_packet_get_type (&pkt) == GST_RTCP_TYPE_SDES) {
      fail_unless (gst_rtcp_packet_sdes_first_entry (&pkt));

      do {
        GstRTCPSDESType type;
        guint8 len;
        guint8 *data;

        fail_unless (gst_rtcp_packet_sdes_get_entry (&pkt, &type, &len, &data));

        if (type == GST_RTCP_SDES_PRIV) {
          char *name = g_strndup ((const gchar *) &data[1], data[0]);
          len -= data[0] + 1;
          data += data[0] + 1;

          fail_unless_equals_int (len, strlen (value));
          fail_unless (!strncmp (value, (char *) data, len));
          fail_unless_equals_string (name, name_ref);
          g_free (name);
          goto sdes_done;
        }
      } while (gst_rtcp_packet_sdes_next_entry (&pkt));

      g_assert_not_reached ();
    }
  } while (gst_rtcp_packet_move_to_next (&pkt));

  g_assert_not_reached ();

sdes_done:

  fail_unless (gst_rtcp_buffer_unmap (&rtcp));

}

GST_START_TEST (test_change_sent_sdes)
{
  SessionHarness *h = session_harness_new ();
  GstStructure *s;
  GstBuffer *buf;
  gboolean ret;
  GstFlowReturn res;

  /* verify the RTCP thread has not started */
  fail_unless_equals_int (0, gst_test_clock_peek_id_count (h->testclock));
  /* and that no RTCP has been pushed */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->rtcp_h));

  s = gst_structure_new ("application/x-rtp-source-sdes",
      "other", G_TYPE_STRING, "first", NULL);
  g_object_set (h->internal_session, "sdes", s, NULL);
  gst_structure_free (s);

  /* then ask explicitly to send RTCP */
  g_signal_emit_by_name (h->internal_session,
      "send-rtcp-full", GST_SECOND, &ret);
  /* this is FALSE due to no next RTCP check time */
  fail_unless (ret == FALSE);

  /* "crank" and verify RTCP now was sent */
  session_harness_crank_clock (h);
  buf = session_harness_pull_rtcp (h);
  fail_unless (buf);
  validate_sdes_priv (buf, "other", "first");
  gst_buffer_unref (buf);

  /* Change the SDES */
  s = gst_structure_new ("application/x-rtp-source-sdes",
      "other", G_TYPE_STRING, "second", NULL);
  g_object_set (h->internal_session, "sdes", s, NULL);
  gst_structure_free (s);

  /* Send an RTP packet */
  buf = generate_test_buffer (22, 10000);
  res = session_harness_send_rtp (h, buf);
  fail_unless_equals_int (GST_FLOW_OK, res);

  /* "crank" enough to ensure a RTCP packet has been produced ! */
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);
  session_harness_crank_clock (h);

  /* and verify RTCP now was sent with new SDES */
  buf = session_harness_pull_rtcp (h);
  validate_sdes_priv (buf, "other", "second");
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_request_nack)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  guint8 *fci_data;
  guint32 fci_length;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Wait for first regular RTCP to be sent so that we are clear to send early RTCP */
  session_harness_produce_rtcp (h, 1);
  gst_buffer_unref (session_harness_pull_rtcp (h));

  /* request NACK immediately */
  session_harness_rtp_retransmission_request (h, 0x12345678, 1234, 0, 0, 0);

  /* NACK should be produced immediately as early RTCP is allowed. Pull buffer
     without advancing the clock to ensure this is the case */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_NACK,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));

  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);
  fci_length =
      gst_rtcp_packet_fb_get_fci_length (&rtcp_packet) * sizeof (guint32);
  fail_unless_equals_int (4, fci_length);
  fail_unless_equals_int (GST_READ_UINT32_BE (fci_data), 1234L << 16);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

typedef struct
{
  gulong id;
  GstPad *pad;
  GMutex mutex;
  GCond cond;
  gboolean blocked;
} BlockingProbeData;

static GstPadProbeReturn
on_rtcp_pad_blocked (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  BlockingProbeData *probe = user_data;

  g_mutex_lock (&probe->mutex);
  probe->blocked = TRUE;
  g_cond_signal (&probe->cond);
  g_mutex_unlock (&probe->mutex);

  return GST_PAD_PROBE_OK;
}

static void
session_harness_block_rtcp (SessionHarness * h, BlockingProbeData * probe)
{
  probe->pad = gst_element_get_static_pad (h->session, "send_rtcp_src");
  fail_unless (probe->pad);

  g_mutex_init (&probe->mutex);
  g_cond_init (&probe->cond);
  probe->blocked = FALSE;
  probe->id = gst_pad_add_probe (probe->pad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER |
      GST_PAD_PROBE_TYPE_BUFFER_LIST, on_rtcp_pad_blocked, probe, NULL);

  g_mutex_lock (&probe->mutex);
  while (!probe->blocked) {
    session_harness_crank_clock (h);
    g_cond_wait (&probe->cond, &probe->mutex);
  }
  g_mutex_unlock (&probe->mutex);
}

static void
session_harness_unblock_rtcp (SessionHarness * h, BlockingProbeData * probe)
{
  gst_pad_remove_probe (probe->pad, probe->id);
  gst_object_unref (probe->pad);
  g_mutex_clear (&probe->mutex);
}

GST_START_TEST (test_request_nack_surplus)
{
  SessionHarness *h = session_harness_new ();
  GstRTCPPacket rtcp_packet;
  BlockingProbeData probe;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  guint8 *fci_data;
  gint i;
  GstStructure *sdes;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* sdes cname has variable size, fix it */
  g_object_get (h->internal_session, "sdes", &sdes, NULL);
  gst_structure_set (sdes, "cname", G_TYPE_STRING, "user@test", NULL);
  g_object_set (h->internal_session, "sdes", sdes, NULL);
  gst_structure_free (sdes);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Block on first regular RTCP so we can fill the nack list */
  session_harness_block_rtcp (h, &probe);

  /* request 400 NACK with 17 seqnum distance to optain the worst possible
   * packing  */
  for (i = 0; i < 350; i++)
    session_harness_rtp_retransmission_request (h, 0x12345678, 1234 + i * 17,
        0, 0, 0);
  /* and the last 50 with a 2s deadline */
  for (i = 350; i < 400; i++)
    session_harness_rtp_retransmission_request (h, 0x12345678, 1234 + i * 17,
        0, 2000, 0);

  /* Unblock and wait for the regular and first early packet */
  session_harness_unblock_rtcp (h, &probe);
  session_harness_produce_rtcp (h, 2);

  /* Move time forward, so that only the remaining 50 are still up to date */
  session_harness_advance_and_crank (h, GST_SECOND);
  session_harness_produce_rtcp (h, 3);

  /* ignore the regular RTCP packet */
  buf = session_harness_pull_rtcp (h);
  gst_buffer_unref (buf);

  /* validate the first early RTCP which should hold 335 Nack */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_NACK,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));

  fail_unless_equals_int (340,
      gst_rtcp_packet_fb_get_fci_length (&rtcp_packet));
  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);
  fail_unless_equals_int (GST_READ_UINT32_BE (fci_data), 1234L << 16);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  /* validate the second early RTCP which should hold 50 Nack */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_NACK,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));

  fail_unless_equals_int (50, gst_rtcp_packet_fb_get_fci_length (&rtcp_packet));
  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);
  fail_unless_equals_int (GST_READ_UINT32_BE (fci_data),
      (guint16) (1234 + 350 * 17) << 16);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_request_nack_packing)
{
  SessionHarness *h = session_harness_new ();
  GstRTCPPacket rtcp_packet;
  BlockingProbeData probe;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  guint8 *fci_data;
  gint i;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Block on first regular RTCP so we can fill the nack list */
  session_harness_block_rtcp (h, &probe);

  /* append 16 consecutive seqnum */
  for (i = 1; i < 17; i++)
    session_harness_rtp_retransmission_request (h, 0x12345678, 1234 + i,
        0, 0, 0);
  /* prepend one, still consecutive */
  session_harness_rtp_retransmission_request (h, 0x12345678, 1234, 0, 0, 0);
  /* update it */
  session_harness_rtp_retransmission_request (h, 0x12345678, 1234, 0, 0, 0);

  /* Unblock and wait for the regular and first early packet */
  session_harness_unblock_rtcp (h, &probe);
  session_harness_produce_rtcp (h, 2);

  /* ignore the regular RTCP packet */
  buf = session_harness_pull_rtcp (h);
  gst_buffer_unref (buf);

  /* validate the early RTCP which should hold 1 Nack */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_NACK,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));

  fail_unless_equals_int (1, gst_rtcp_packet_fb_get_fci_length (&rtcp_packet));
  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);
  fail_unless_equals_int (GST_READ_UINT32_BE (fci_data), 1234L << 16 | 0xFFFF);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_disable_sr_timestamp)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  guint64 ntptime;
  guint32 rtptime;

  g_object_set (h->internal_session, "disable-sr-timestamp", TRUE, NULL);

  /* push latency or the RTPSource won't be ready to produce RTCP */
  gst_harness_push_upstream_event (h->send_rtp_h, gst_event_new_latency (0));

  /* Push RTP buffer to make sure RTCP-thread have started */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 0xDEADBEEF)));

  /* crank the RTCP-thread and pull out rtcp, generating a stats-callback */
  session_harness_crank_clock (h);
  buf = session_harness_pull_rtcp (h);

  gst_rtcp_buffer_map (buf, GST_MAP_READWRITE, &rtcp);

  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  fail_unless_equals_int (GST_RTCP_TYPE_SR,
      gst_rtcp_packet_get_type (&rtcp_packet));

  gst_rtcp_packet_sr_get_sender_info (&rtcp_packet, NULL, &ntptime, &rtptime,
      NULL, NULL);

  fail_unless_equals_uint64 (ntptime, 0);
  fail_unless (rtptime == 0);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

static guint
on_sending_nacks (GObject * internal_session, guint sender_ssrc,
    guint media_ssrc, GArray * nacks, GstBuffer * buffer)
{
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  guint16 seqnum = g_array_index (nacks, guint16, 0);
  guint8 *data;

  if (seqnum == 1235)
    return 0;

  fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_APP, &packet));

  gst_rtcp_packet_app_set_ssrc (&packet, media_ssrc);
  gst_rtcp_packet_app_set_name (&packet, "TEST");

  fail_unless (gst_rtcp_packet_app_set_data_length (&packet, 1));
  data = gst_rtcp_packet_app_get_data (&packet);
  GST_WRITE_UINT32_BE (data, seqnum);

  gst_rtcp_buffer_unmap (&rtcp);
  return 1;
}

GST_START_TEST (test_on_sending_nacks)
{
  SessionHarness *h = session_harness_new ();
  BlockingProbeData probe;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  guint8 *data;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Block on first regular RTCP so we can fill the nack list */
  session_harness_block_rtcp (h, &probe);
  g_signal_connect (h->internal_session, "on-sending-nacks",
      G_CALLBACK (on_sending_nacks), NULL);

  /* request NACK immediately */
  session_harness_rtp_retransmission_request (h, 0x12345678, 1234, 0, 0, 0);
  session_harness_rtp_retransmission_request (h, 0x12345678, 1235, 0, 0, 0);

  session_harness_unblock_rtcp (h, &probe);
  gst_buffer_unref (session_harness_pull_rtcp (h));
  session_harness_produce_rtcp (h, 2);

  /* first packet only includes seqnum 1234 in an APP FB */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_APP,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_string ("TEST",
      gst_rtcp_packet_app_get_name (&rtcp_packet));

  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_app_get_ssrc (&rtcp_packet));

  fail_unless_equals_int (1,
      gst_rtcp_packet_app_get_data_length (&rtcp_packet));
  data = gst_rtcp_packet_app_get_data (&rtcp_packet);
  fail_unless_equals_int (GST_READ_UINT32_BE (data), 1234L);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  /* second will contain seqnum 1235 in a generic nack packet */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_NACK,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));

  fail_unless_equals_int (1, gst_rtcp_packet_fb_get_fci_length (&rtcp_packet));
  data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);
  fail_unless_equals_int (GST_READ_UINT32_BE (data), 1235L << 16);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

static void
disable_probation_on_new_ssrc (GObject * session, GObject * source)
{
  g_object_set (source, "probation", 0, NULL);
}

GST_START_TEST (test_disable_probation)
{
  SessionHarness *h = session_harness_new ();

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);
  g_signal_connect (h->internal_session, "on-new-ssrc",
      G_CALLBACK (disable_probation_on_new_ssrc), NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* When probation is disabled, the packet should be produced immediately */
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h->recv_rtp_h));

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_request_late_nack)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  guint8 *fci_data;
  guint32 fci_length;

  g_object_set (h->internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* Receive a RTP buffer from the wire */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Wait for first regular RTCP to be sent so that we are clear to send early RTCP */
  session_harness_produce_rtcp (h, 1);
  gst_buffer_unref (session_harness_pull_rtcp (h));

  /* request NACK immediately, but also advance the clock, so the request is
   * now late, but it should be kept to avoid sending an early rtcp without
   * NACK. This would otherwise lead to a stall if the late packet was cause
   * by high RTT, we need to send some RTX in order to update that statistic. */
  session_harness_rtp_retransmission_request (h, 0x12345678, 1234, 0, 0, 0);
  gst_test_clock_advance_time (h->testclock, 100 * GST_USECOND);

  /* NACK should be produced immediately as early RTCP is allowed. Pull buffer
     without advancing the clock to ensure this is the case */
  buf = session_harness_pull_rtcp (h);

  fail_unless (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless_equals_int (3, gst_rtcp_buffer_get_packet_count (&rtcp));
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));

  /* first a Receiver Report */
  fail_unless_equals_int (GST_RTCP_TYPE_RR,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* then a SDES */
  fail_unless_equals_int (GST_RTCP_TYPE_SDES,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless (gst_rtcp_packet_move_to_next (&rtcp_packet));

  /* and then our NACK */
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,
      gst_rtcp_packet_get_type (&rtcp_packet));
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_NACK,
      gst_rtcp_packet_fb_get_type (&rtcp_packet));

  fail_unless_equals_int (0xDEADBEEF,
      gst_rtcp_packet_fb_get_sender_ssrc (&rtcp_packet));
  fail_unless_equals_int (0x12345678,
      gst_rtcp_packet_fb_get_media_ssrc (&rtcp_packet));

  fci_data = gst_rtcp_packet_fb_get_fci (&rtcp_packet);
  fci_length =
      gst_rtcp_packet_fb_get_fci_length (&rtcp_packet) * sizeof (guint32);
  fail_unless_equals_int (4, fci_length);
  fail_unless_equals_int (GST_READ_UINT32_BE (fci_data), 1234L << 16);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

static gpointer
_push_caps_events (gpointer user_data)
{
  SessionHarness *h = user_data;
  gint payload = 0;
  while (h->running) {

    GstCaps *caps = gst_caps_new_simple ("application/x-rtp",
        "payload", G_TYPE_INT, payload,
        NULL);
    gst_harness_set_src_caps (h->recv_rtp_h, caps);
    g_thread_yield ();
    payload++;
  }

  return NULL;
}

GST_START_TEST (test_clear_pt_map_stress)
{
  SessionHarness *h = session_harness_new ();
  GThread *thread;
  guint i;

  h->running = TRUE;
  thread = g_thread_new (NULL, _push_caps_events, h);

  for (i = 0; i < 1000; i++) {
    g_signal_emit_by_name (h->session, "clear-pt-map");
    g_thread_yield ();
  }

  h->running = FALSE;
  g_thread_join (thread);

  session_harness_free (h);
}

GST_END_TEST;

static GstBuffer *
generate_stepped_ts_buffer (guint i, gboolean stepped)
{
  GstBuffer *buf;
  guint ts = (TEST_BUF_CLOCK_RATE * i) / 1000;

  if (stepped) {
    const int TEST_BUF_CLOCK_STEP = TEST_BUF_CLOCK_RATE / 30;

    ts /= TEST_BUF_CLOCK_STEP;
    ts *= TEST_BUF_CLOCK_STEP;
  }
  GST_LOG ("ts: %" GST_TIME_FORMAT " rtp: %u (%" GST_TIME_FORMAT "), seq: %u\n",
      GST_TIME_ARGS (i * GST_MSECOND), ts,
      GST_TIME_ARGS (gst_util_uint64_scale_int (GST_SECOND, ts,
              TEST_BUF_CLOCK_RATE)), i);

  buf = generate_test_buffer_full (i * GST_MSECOND, i, ts, 0xAAAA, FALSE,
      TEST_BUF_PT, 0, 0);
  return buf;
}

static void
test_packet_rate_impl (gboolean stepped)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  guint i;
  const int PROBATION_CNT = 5;
  GstStructure *stats;
  GObject *source;
  guint pktrate;

  /* First do probation */
  for (i = 0; i < PROBATION_CNT; i++) {
    buf = generate_stepped_ts_buffer (i, stepped);
    fail_unless_equals_int (session_harness_recv_rtp (h, buf), GST_FLOW_OK);
  }
  for (i = 0; i < PROBATION_CNT; i++) {
    buf = gst_harness_pull (h->recv_rtp_h);
    fail_unless (buf);
    gst_buffer_unref (buf);
  }

  /* Now run the real test */
  for (i = PROBATION_CNT; i < 10000; i++) {
    buf = generate_stepped_ts_buffer (i, stepped);
    fail_unless_equals_int (session_harness_recv_rtp (h, buf), GST_FLOW_OK);

    buf = gst_harness_pull (h->recv_rtp_h);
    fail_unless (buf);
    gst_buffer_unref (buf);
  }

  g_signal_emit_by_name (h->internal_session, "get-source-by-ssrc", 0xAAAA,
      &source);

  g_object_get (source, "stats", &stats, NULL);

  fail_unless (gst_structure_get_uint (stats, "recv-packet-rate", &pktrate));
  fail_unless (pktrate > 900 && pktrate < 1100);        /* Allow 10% of error */

  gst_structure_free (stats);
  g_object_unref (source);

  session_harness_free (h);
}

GST_START_TEST (test_packet_rate)
{
  test_packet_rate_impl (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_stepped_packet_rate)
{
  test_packet_rate_impl (TRUE);
}

GST_END_TEST;


/********************* TWCC-tests *********************/

static GstRTCPFBType
_gst_buffer_get_rtcp_fbtype (GstBuffer * buf)
{
  GstRTCPFBType ret = GST_RTCP_FB_TYPE_INVALID;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;

  if (!gst_rtcp_buffer_validate_reduced (buf))
    return ret;

  if (!gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp))
    return ret;

  if (!gst_rtcp_buffer_get_first_packet (&rtcp, &packet))
    goto done;

  if (GST_RTCP_TYPE_RTPFB != gst_rtcp_packet_get_type (&packet))
    goto done;

  ret = gst_rtcp_packet_fb_get_type (&packet);

done:
  gst_rtcp_buffer_unmap (&rtcp);
  return ret;
}

static GstBuffer *
session_harness_produce_twcc (SessionHarness * h)
{
  GstBuffer *buf = NULL;
  while (TRUE) {
    session_harness_crank_clock (h);
    buf = session_harness_pull_rtcp (h);
    if (GST_RTCP_RTPFB_TYPE_TWCC == _gst_buffer_get_rtcp_fbtype (buf)) {
      break;
    } else {
      gst_buffer_unref (buf);
    }
    /* allow the rtcp-thread to settle before cranking again */
    gst_test_clock_wait_for_next_pending_id (h->testclock, NULL);
  }
  return buf;
}

typedef struct
{
  guint16 base_seqnum;
  guint16 num_packets;
  GstClockTime base_time;
  GstClockTime duration;
} TWCCTestData;

static TWCCTestData twcc_header_and_run_length_test_data[] = {
  {0, 10, 0, 33 * GST_MSECOND},
  {65530, 12, 37 * 64 * GST_MSECOND, 10 * GST_MSECOND}, /* seqnum wrap */
  {99, 200, 1024 * 64 * GST_MSECOND, 10 * GST_MSECOND}, /* many packets */
  {20000, 23, 0, 250 * GST_USECOND},    /* minimal duration */
  {56000, 15, 1000 * 64 * GST_MSECOND, 10 * GST_MSECOND},       /* timestamp offset */
};

GST_START_TEST (test_twcc_header_and_run_length)
{
  SessionHarness *h = session_harness_new ();
  gint i;
  GstFlowReturn res;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  guint8 *fci_data;
  guint16 run_length;

  TWCCTestData *td = &twcc_header_and_run_length_test_data[__i__];

  /* enable twcc */
  session_harness_set_twcc_recv_ext_id (h, TEST_TWCC_EXT_ID);

  /* receive some buffers */
  for (i = 0; i < td->num_packets; i++) {
    gboolean last_packet = i == (td->num_packets - 1);

    GstClockTime now = gst_clock_get_time (GST_CLOCK_CAST (h->testclock));
    GstClockTime ts = td->base_time + i * td->duration;
    if (ts > now)
      gst_test_clock_set_time (h->testclock, ts);

    buf = generate_twcc_recv_buffer (i + td->base_seqnum, ts, last_packet);
    res = session_harness_recv_rtp (h, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);
  }

  buf = session_harness_produce_twcc (h);
  fail_unless (buf);

  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &packet));

  fci_data = gst_rtcp_packet_fb_get_fci (&packet);

  /* base seqnum */
  fail_unless_equals_int (td->base_seqnum, GST_READ_UINT16_BE (&fci_data[0]));

  /*  packet count */
  fail_unless_equals_int (td->num_packets, GST_READ_UINT16_BE (&fci_data[2]));

  /* reference time (in 64ms units) */
  fail_unless_equals_int (td->base_time,
      GST_READ_UINT24_BE (&fci_data[4]) * 64 * GST_MSECOND);

  /* feedback packet number */
  fail_unless_equals_int (0, fci_data[7]);

  /* run-length coding */
  fail_unless_equals_int (0, fci_data[8] & 0x80);

  /* status: small-delta */
  fail_unless_equals_int (0x20, fci_data[8] & 0x60);

  /* packets in run_length */
  run_length = GST_READ_UINT16_BE (&fci_data[8]);
  run_length = run_length & ~0xE000;    /* mask out the upper 3 status bits */
  fail_unless_equals_int (td->num_packets, run_length);

  /* first recv-delta always 0 */
  fail_unless_equals_int (0, fci_data[10]);

  /* following recv-delta equal to duration (in 250us units) */
  fail_unless_equals_clocktime (td->duration, fci_data[11] * 250 * GST_USECOND);

  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

typedef struct
{
  guint16 seqnum;
  GstClockTime timestamp;
  gboolean marker;
} TWCCPacket;

#define TWCC_DELTA_UNIT (250 * GST_USECOND)
#define TWCC_REF_TIME_UNIT (64 * GST_MSECOND)
#define TWCC_REF_TIME_INITIAL_OFFSET ((1 << 24) * TWCC_REF_TIME_UNIT)

static void
fail_unless_equals_twcc_clocktime (GstClockTime twcc_packet_ts,
    GstClockTime pkt_ts)
{
  fail_unless_equals_clocktime (
      (twcc_packet_ts / TWCC_DELTA_UNIT) * TWCC_DELTA_UNIT +
      TWCC_REF_TIME_INITIAL_OFFSET, pkt_ts);
}

#define twcc_push_packets(h, packets)                                          \
G_STMT_START {                                                                 \
  guint i;                                                                     \
  session_harness_set_twcc_recv_ext_id ((h), TEST_TWCC_EXT_ID);                \
  for (i = 0; i < G_N_ELEMENTS ((packets)); i++) {                             \
    TWCCPacket *twcc_pkt = &(packets)[i];                                      \
    GstClockTime now = gst_clock_get_time (GST_CLOCK_CAST (h->testclock));     \
    if (twcc_pkt->timestamp > now)                                             \
      gst_test_clock_set_time ((h->testclock), twcc_pkt->timestamp);           \
    fail_unless_equals_int (GST_FLOW_OK,                                       \
        session_harness_recv_rtp ((h),                                         \
            generate_twcc_recv_buffer (twcc_pkt->seqnum,                       \
                twcc_pkt->timestamp, twcc_pkt->marker)));                      \
  }                                                                            \
} G_STMT_END

#define twcc_verify_fci(buf, exp_fci)                                          \
G_STMT_START {                                                                 \
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;                                   \
  GstRTCPPacket packet;                                                        \
  guint8 *fci_data;                                                            \
  guint16 fci_length;                                                          \
  fail_unless (gst_rtcp_buffer_validate_reduced (buf));                        \
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);                              \
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &packet));             \
  fail_unless_equals_int (GST_RTCP_TYPE_RTPFB,                                 \
      gst_rtcp_packet_get_type (&packet));                                     \
  fail_unless_equals_int (GST_RTCP_RTPFB_TYPE_TWCC,                            \
      gst_rtcp_packet_fb_get_type (&packet));                                  \
  fci_data = gst_rtcp_packet_fb_get_fci (&packet);                             \
  fci_length = gst_rtcp_packet_fb_get_fci_length (&packet) * sizeof (guint32); \
  GST_MEMDUMP ("fci:", fci_data, fci_length);                                  \
  fail_unless_equals_int (fci_length, sizeof (exp_fci));                       \
  fail_unless_equals_int (0, memcmp (fci_data, (exp_fci), fci_length));        \
  gst_rtcp_buffer_unmap (&rtcp);                                               \
} G_STMT_END

#define twcc_verify_packets_to_fci(h, packets, exp_fci)                        \
G_STMT_START {                                                                 \
  GstBuffer *buf;                                                              \
  twcc_push_packets (h, packets);                                              \
  buf = session_harness_produce_twcc ((h));                                    \
  twcc_verify_fci (buf, exp_fci);                                              \
  gst_buffer_unref (buf);                                                      \
} G_STMT_END

#define twcc_verify_packets_to_event(packets, event)                           \
G_STMT_START {                                                                 \
  guint i;                                                                     \
  guint j = 0;                                                                 \
  GValueArray *packets_array = g_value_get_boxed (                             \
      gst_structure_get_value (gst_event_get_structure ((event)), "packets")); \
  for (i = 0; i < packets_array->n_values; i++) {                              \
    TWCCPacket *twcc_pkt;                                                      \
    GstClockTime ts;                                                           \
    guint seqnum;                                                              \
    gboolean lost;                                                             \
    const GstStructure *pkt_s =                                                \
        gst_value_get_structure (g_value_array_get_nth (packets_array, i));    \
    fail_unless (gst_structure_get_boolean (pkt_s, "lost", &lost));            \
    if (lost)                                                                  \
      continue;                                                                \
    fail_unless (gst_structure_get_clock_time (pkt_s, "remote-ts", &ts));      \
    fail_unless (gst_structure_get_uint (pkt_s, "seqnum", &seqnum));           \
    twcc_pkt = &(packets)[j++];                                                \
    fail_unless_equals_int (twcc_pkt->seqnum, seqnum);                         \
    fail_unless_equals_twcc_clocktime (twcc_pkt->timestamp, ts);               \
  }                                                                            \
  gst_event_unref (event);                                                     \
} G_STMT_END

#define twcc_verify_packets_to_packets(send_h, recv_h, packets)                \
G_STMT_START {                                                                 \
  guint i;                                                                     \
  GstEvent *event;                                                             \
  twcc_push_packets ((recv_h), packets);                                       \
  session_harness_recv_rtcp ((send_h),                                         \
      session_harness_produce_twcc ((recv_h)));                                \
  for (i = 0; i < 2; i++)                                                      \
    gst_event_unref (gst_harness_pull_upstream_event ((send_h)->send_rtp_h));  \
  event = gst_harness_pull_upstream_event ((send_h)->send_rtp_h);              \
  twcc_verify_packets_to_event (packets, event);                               \
} G_STMT_END

#define twcc_verify_stats(h, bitrate_sent, bitrate_recv, pkts_sent, pkts_recv, loss_pct, avg_dod)  \
G_STMT_START {                                                                                     \
  GstStructure *twcc_stats;                                                                        \
  guint stats_bitrate_sent;                                                                        \
  guint stats_bitrate_recv;                                                                        \
  guint stats_packets_sent;                                                                        \
  guint stats_packets_recv;                                                                        \
  gdouble stats_loss_pct;                                                                          \
  GstClockTimeDiff stats_avg_dod;                                                                  \
  twcc_stats = session_harness_get_last_twcc_stats (h);                                            \
  fail_unless (gst_structure_get (twcc_stats,                                                      \
          "bitrate-sent", G_TYPE_UINT, &stats_bitrate_sent,                                        \
          "bitrate-recv", G_TYPE_UINT, &stats_bitrate_recv,                                        \
          "packets-sent", G_TYPE_UINT, &stats_packets_sent,                                        \
          "packets-recv", G_TYPE_UINT, &stats_packets_recv,                                        \
          "packet-loss-pct", G_TYPE_DOUBLE, &stats_loss_pct,                                       \
          "avg-delta-of-delta", G_TYPE_INT64, &stats_avg_dod, NULL));                              \
  fail_unless_equals_int (bitrate_sent, stats_bitrate_sent);                                       \
  fail_unless_equals_int (bitrate_recv, stats_bitrate_recv);                                       \
  fail_unless_equals_int (pkts_sent, stats_packets_sent);                                          \
  fail_unless_equals_int (pkts_recv, stats_packets_recv);                                          \
  fail_unless_equals_float (loss_pct, stats_loss_pct);                                             \
  fail_unless_equals_int64 (avg_dod, stats_avg_dod);                                               \
  gst_structure_free (twcc_stats);                                                                 \
} G_STMT_END

GST_START_TEST (test_twcc_1_bit_status_vector)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {10, 0 * GST_MSECOND, FALSE},
    {12, 12 * GST_MSECOND, FALSE},
    {14, 14 * GST_MSECOND, FALSE},
    {15, 15 * GST_MSECOND, FALSE},
    {17, 17 * GST_MSECOND, FALSE},
    {20, 20 * GST_MSECOND, FALSE},
    {21, 21 * GST_MSECOND, FALSE},
    {23, 23 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x0a,                 /* base sequence number: 10 */
    0x00, 0x0e,                 /* packet status count: 14 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0xab, 0x4d,                 /* packet chunk: 1 0 1 0 1 0 1 1 | 0 1 0 0 1 1 0 1 */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x30,                       /* recv delta: +0:00:00.012000000 */
    0x08,                       /* recv delta: +0:00:00.002000000 */
    0x04,                       /* recv delta: +0:00:00.001000000 */
    0x08,                       /* recv delta: +0:00:00.002000000 */
    0x0c,                       /* recv delta: +0:00:00.003000000 */
    0x04,                       /* recv delta: +0:00:00.001000000 */
    0x08,                       /* recv delta: +0:00:00.002000000 */
    0x00, 0x00,                 /* padding */
  };

  /* check we get the expected fci */
  twcc_verify_packets_to_fci (h0, packets, exp_fci);

  /* and check we can parse this back to the original packets */
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_status_vector_split_large_delta)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {1, 1 * 60 * GST_MSECOND, FALSE},
    {2, 2 * 60 * GST_MSECOND, FALSE},
    {3, 3 * 60 * GST_MSECOND, FALSE},
    {4, 4 * 60 * GST_MSECOND, FALSE},
    {5, 5 * 60 * GST_MSECOND, FALSE},
    {6, 6 * 60 * GST_MSECOND, FALSE},
    {7, 7 * 60 * GST_MSECOND, FALSE},
    {8, 8 * 60 * GST_MSECOND, FALSE},
    {9, 9 * 60 * GST_MSECOND, FALSE},
    {10, 10 * 60 * GST_MSECOND, FALSE},
    {11, 11 * 60 * GST_MSECOND, FALSE},
    {12, 12 * 60 * GST_MSECOND, FALSE},
    {13, 13 * 60 * GST_MSECOND, FALSE},
    {14, 14 * 60 * GST_MSECOND, FALSE},

    {15, 60 * 60 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x0f,                 /* packet status count: 15 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0x20, 0x0e,                 /* run-length chunk with small delta for #1 to #14:  0 0 1 0 0 0 0 0 | 0 0 0 0 1 1 1 0 */
    0x40, 0x01,                 /* rung-length with large delta for #15:             0 1 0 0 0 0 0 0 | 0 0 0 0 0 0 0 1 */

    /* recv deltas: */
    0xf0, 0xf0,                 /* 14 small deltas : +0:00:00.060000000 */
    0xf0, 0xf0,
    0xf0, 0xf0,
    0xf0, 0xf0,
    0xf0, 0xf0,
    0xf0, 0xf0,
    0xf0, 0xf0,
    0x2b, 0x20,                 /* large delta: +0:00:02.760000000 */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_2_bit_status_vector)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {5, 5 * 64 * GST_MSECOND, FALSE},
    {7, 7 * 64 * GST_MSECOND, FALSE},
    {8, 8 * 64 * GST_MSECOND, FALSE},
    {11, 12 * 64 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x05,                 /* base sequence number: 5 */
    0x00, 0x07,                 /* packet status count: 7 */
    0x00, 0x00, 0x05,           /* reference time: 5 */
    0x00,                       /* feedback packet count: 0 */
    0xd2, 0x82,                 /* packet chunk: 1 1 0 1 0 0 1 0 | 1 0 0 0 0 0 1 0 */
    /* normal, missing, large, large, missing, missing, large */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x02, 0x00,                 /* recv delta: +0:00:00.128000000 */
    0x01, 0x00,                 /* recv delta: +0:00:00.064000000 */
    0x04, 0x00,                 /* recv delta: +0:00:00.256000000 */
    0x00, 0x00, 0x00,           /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);

  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_2_bit_over_capacity)
{
  SessionHarness *h = session_harness_new ();

  TWCCPacket packets[] = {
    {0, 0 * GST_MSECOND, FALSE},
    {6, 250 * 250 + 250 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x00,                 /* base sequence number: 0 */
    0x00, 0x07,                 /* packet status count: 7 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0xd0, 0x02,                 /* packet chunk: 1 1 0 1 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x03, 0xe8,                 /* recv delta: +0:00:00.000000000 */
    0x00, 0x00, 0x00,           /* padding */
  };

  twcc_verify_packets_to_fci (h, packets, exp_fci);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_status_vector_split_with_gap)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {0, 0 * GST_MSECOND, FALSE},
    {7, (250 * 250) + 250 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x00,                 /* base sequence number: 0 */
    0x00, 0x08,                 /* packet status count: 8 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0xd0, 0x00,                 /* packet chunk: 1 1 0 1 0 0 0 0 | 0 0 0 0 0 0 0 0 */
    0xe0, 0x00,                 /* packet chunk: 1 1 1 0 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x03, 0xe8,                 /* recv delta: +0:00:00.250000000 */
    0x00,                       /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_status_vector_split_into_three)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    /* 7 packets with small deltas */
    {0, 0 * 250 * GST_USECOND, FALSE},
    {1, 1 * 250 * GST_USECOND, FALSE},
    {2, 2 * 250 * GST_USECOND, FALSE},
    {3, 3 * 250 * GST_USECOND, FALSE},
    {4, 4 * 250 * GST_USECOND, FALSE},
    {5, 5 * 250 * GST_USECOND, FALSE},
    {6, 6 * 250 * GST_USECOND, FALSE},

    /* 2 large delta, #8 will present a negative delta */
    {7, 7 * 250 * GST_MSECOND, FALSE},
    {8, 8 * 250 * GST_USECOND, FALSE},

    /* 13 packets with small deltas */
    {9, 9 * 250 * GST_USECOND, FALSE},
    {10, 10 * 250 * GST_USECOND, FALSE},
    {11, 11 * 250 * GST_USECOND, FALSE},
    {12, 12 * 250 * GST_USECOND, FALSE},
    {13, 13 * 250 * GST_USECOND, FALSE},
    {14, 14 * 250 * GST_USECOND, FALSE},
    {15, 15 * 250 * GST_USECOND, FALSE},
    {16, 16 * 250 * GST_USECOND, FALSE},
    {17, 17 * 250 * GST_USECOND, FALSE},
    {18, 18 * 250 * GST_USECOND, FALSE},
    {19, 19 * 250 * GST_USECOND, FALSE},
    {20, 20 * 250 * GST_USECOND, FALSE},
    {21, 21 * 250 * GST_USECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x00,                 /* base sequence number: 0 */
    0x00, 0x16,                 /* packet status count: 22 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0x20, 0x07,                 /* run-length chunk (small deltas) for #0 to #6:   0 0 1 0 0 0 0 0 | 0 0 0 0 0 1 1 1 */
    0x40, 0x02,                 /* run-length chunk (large deltas) for #7 and #8   0 1 0 0 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    0x20, 0x0d,                 /* run-length chunk (small deltas) for #9 and #21  0 1 0 0 0 0 0 0 | 0 0 0 0 1 1 0 1 */

    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */

    0x1b, 0x52,                 /* recv delta: +0:00:01.748500000 */
    0xe4, 0xb0,                 /* recv delta: -0:00:01.748000000 */

    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */

    0x00, 0x00,                 /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_2_bit_full_status_vector)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {1, 1 * 64 * GST_MSECOND, FALSE},
    {2, 2 * 64 * GST_MSECOND, FALSE},
    {6, 6 * 64 * GST_MSECOND, FALSE},
    {7, 7 * 64 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x07,                 /* packet status count: 7 */
    0x00, 0x00, 0x01,           /* reference time: 1 */
    0x00,                       /* feedback packet count: 0 */
    0xd8, 0x0a,                 /* packet chunk: 1 1 0 1 1 0 0 0 | 0 0 0 0 1 0 1 0 */
    0x00, 0x01,                 /* recv delta: +0:00:00.064000000 */
    0x00, 0x04,                 /* recv delta: +0:00:00.256000000 */
    0x00, 0x01,                 /* recv delta: +0:00:00.064000000 */
    0x00, 0x00, 0x00, 0x00,     /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_various_gaps)
{
  SessionHarness *h = session_harness_new ();
  guint16 seq = 1 + __i__;

  TWCCPacket packets[] = {
    {0, 0 * 250 * GST_USECOND, FALSE},
    {seq, seq * 250 * GST_USECOND, TRUE},
  };

  twcc_verify_packets_to_packets (h, h, packets);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_negative_delta)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {0, 0 * 250 * GST_USECOND, FALSE},
    {1, 2 * 250 * GST_USECOND, FALSE},
    {2, 1 * 250 * GST_USECOND, FALSE},
    {3, 3 * 250 * GST_USECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x00,                 /* base sequence number: 0 */
    0x00, 0x04,                 /* packet status count: 4 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0xd6, 0x40,                 /* packet chunk: 1 1 0 1 0 1 1 0 | 0 1 0 0 0 0 0 0 */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x02,                       /* recv delta: +0:00:00.000500000 */
    0xff, 0xff,                 /* recv delta: -0:00:00.000250000 */
    0x02,                       /* recv delta: +0:00:00.000500000 */
    0x00,                       /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);

  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_seqnum_wrap)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {65534, 0 * 250 * GST_USECOND, FALSE},
    {65535, 1 * 250 * GST_USECOND, FALSE},
    {0, 2 * 250 * GST_USECOND, FALSE},
    {1, 3 * 250 * GST_USECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0xff, 0xfe,                 /* base sequence number: 65534 */
    0x00, 0x04,                 /* packet status count: 4 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0x20, 0x04,                 /* packet chunk: 0 0 1 0 0 0 0 0 | 0 0 0 0 0 1 0 0 */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x01,                       /* recv delta: +0:00:00.000250000 */
    0x00, 0x00,                 /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_seqnum_wrap_with_loss)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;

  TWCCPacket packets[] = {
    {65534, 0 * 250 * GST_USECOND, TRUE},
    {1, 3 * 250 * GST_USECOND, TRUE},
  };

  guint8 exp_fci0[] = {
    0xff, 0xfe,                 /* base sequence number: 65534 */
    0x00, 0x01,                 /* packet status count: 1 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0x20, 0x01,                 /* packet chunk: 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 1  */
    0x00,                       /* recv delta: +0:00:00.000000000 */
    0x00,                       /* padding */
  };

  guint8 exp_fci1[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x01,                 /* packet status count: 1 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x01,                       /* feedback packet count: 1 */
    0x20, 0x01,                 /* packet chunk: 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 1  */
    0x03,                       /* recv delta: +0:00:00.000750000 */
    0x00,                       /* padding */
  };

  twcc_push_packets (h, packets);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci0);
  gst_buffer_unref (buf);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci1);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_double_packets)
{
  SessionHarness *h = session_harness_new ();

  TWCCPacket packets0[] = {
    {11, 11 * GST_MSECOND, FALSE},
    {12, 12 * GST_MSECOND, TRUE},
  };

  TWCCPacket packets1[] = {
    {13, 13 * GST_MSECOND, FALSE},
    {14, 14 * GST_MSECOND, FALSE},
    {15, 15 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci0[] = {
    0x00, 0x0b,                 /* base sequence number: 11 */
    0x00, 0x02,                 /* packet status count: 2 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    0x20, 0x02,                 /* packet chunk: 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    0x2c, 0x04,                 /* recv deltas */
  };

  guint8 exp_fci1[] = {
    0x00, 0x0d,                 /* base sequence number: 13 */
    0x00, 0x03,                 /* packet status count: 3 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x01,                       /* feedback packet count: 1 */
    0x20, 0x03,                 /* packet chunk: 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 1 1 */
    0x34, 0x04, 0x04,           /* recv deltas */
    0x00, 0x00, 0x00,           /* padding */
  };

  twcc_verify_packets_to_fci (h, packets0, exp_fci0);
  twcc_verify_packets_to_fci (h, packets1, exp_fci1);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_huge_seqnum_gap)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {9, 4 * 32 * GST_MSECOND, FALSE},
    {10, 5 * 32 * GST_MSECOND, FALSE},
    {30011, 6 * 32 * GST_MSECOND, FALSE},
    {30012, 7 * 32 * GST_MSECOND, FALSE},
    {30013, 8 * 32 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x09,                 /* base sequence number: 9 */
    0x75, 0x35,                 /* packet status count: 30005 */
    0x00, 0x00, 0x02,           /* reference time: 2 */
    0x00,                       /* feedback packet count: 0 */
    /* packet chunks: */
    0xb0, 0x00,                 /* run-length: 1 bit 2 there, 12 lost: 1 0 1 1 0 0 0 0 | 0 0 0 0 0 0 0 0 */
    0x1f, 0xff,                 /* run-length: 8191 lost:  0 0 0 1 1 1 1 1 | 1 1 1 1 1 1 1 1 */
    0x1f, 0xff,                 /* run-length: 8191 lost:  0 0 0 1 1 1 1 1 | 1 1 1 1 1 1 1 1 */
    0x1f, 0xff,                 /* run-length: 8191 lost:  0 0 0 1 1 1 1 1 | 1 1 1 1 1 1 1 1 */
    0x15, 0x27,                 /* run-length: 5415 lost:  0 0 0 1 0 1 0 1 | 0 0 1 0 0 1 1 1 */
    /* 12 + 8191 + 8191 + 8191 + 5415 = 30000 lost packets */
    0xb8, 0x00,                 /* 1 bit 3 there         : 1 0 1 1 1 0 0 0 | 0 0 0 0 0 0 0 0 */

    0x00, 0x80, 0x80, 0x80, 0x80,       /* recv deltas */
    0x00, 0x00, 0x00,           /* padding */
  };

  twcc_push_packets (h0, packets);

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_duplicate_seqnums)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;

  /* A duplicate seqnum can be interpreted as a gap of 65536 packets.
     Whatever the cause might be, we will follow the behavior of reordered
     packets, and drop it */
  TWCCPacket packets[] = {
    {1, 4 * 32 * GST_MSECOND, FALSE},
    {2, 5 * 32 * GST_MSECOND, FALSE},
    {1, 6 * 32 * GST_MSECOND, FALSE},
    {3, 7 * 32 * GST_MSECOND, TRUE},
  };

  guint8 exp_fci[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x03,                 /* packet status count: 2 */
    0x00, 0x00, 0x02,           /* reference time: 2 * 64ms */
    0x00,                       /* feedback packet count: 0 */
    /* packet chunks: */
    0xd6, 0x00,                 /* 1 1 0 1 0 1 1 0 | 0 0 0 0 0 0 0 0  */
    0x00, 0x80,                 /* recv deltas: +0, +32ms, + 64ms */
    0x01, 0x00,
    0x00, 0x00,                 /* padding */
  };

  twcc_push_packets (h, packets);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;


GST_START_TEST (test_twcc_multiple_markers)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;

  /* for this test, notice how the first recv-delta should relate back to
     the reference-time, which is 0 in this case. The packets are incrementing
     in timestamps equal to the smallest unit for TWCC (250 microseconds) */
  TWCCPacket packets[] = {
    {1, 1 * 250 * GST_USECOND, FALSE},
    {2, 2 * 250 * GST_USECOND, FALSE},
    {3, 3 * 250 * GST_USECOND, TRUE},
    {4, 4 * 250 * GST_USECOND, FALSE},
    {5, 5 * 250 * GST_USECOND, TRUE},
    {6, 6 * 250 * GST_USECOND, FALSE},
    {7, 7 * 250 * GST_USECOND, FALSE},
    {8, 8 * 250 * GST_USECOND, FALSE},
    {9, 9 * 250 * GST_USECOND, TRUE},
  };

  guint8 exp_fci0[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x03,                 /* packet status count: 3 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    /* packet chunks: */
    0x20, 0x03,                 /* 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 1 1  */
    0x01, 0x01, 0x01,           /* recv deltas, +1, +1, +1 */
    0x00, 0x00, 0x00,           /* padding */
  };

  guint8 exp_fci1[] = {
    0x00, 0x04,                 /* base sequence number: 4 */
    0x00, 0x02,                 /* packet status count: 2 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x01,                       /* feedback packet count: 1 */
    /* packet chunks: */
    0x20, 0x02,                 /* 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    0x04, 0x01,                 /* recv deltas, +4, +1, +1 */
  };

  guint8 exp_fci2[] = {
    0x00, 0x06,                 /* base sequence number: 6 */
    0x00, 0x04,                 /* packet status count: 4 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x02,                       /* feedback packet count: 2 */
    /* packet chunks: */
    0x20, 0x04,                 /* 0 0 1 0 0 0 0 0 | 0 0 0 0 0 1 0 0 */
    0x06, 0x01, 0x01, 0x01,     /* recv deltas, +6, +1, +1, +1 */
    0x00, 0x00,
  };

  twcc_push_packets (h, packets);

  /* we should get 1 SR/RR, and then 3x TWCC packets */
  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci0);
  gst_buffer_unref (buf);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci1);
  gst_buffer_unref (buf);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci2);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_no_marker_and_gaps)
{
  SessionHarness *h = session_harness_new ();
  guint i;

  g_object_set (h->internal_session, "probation", 1, NULL);

  /* Push packets with gaps and no marker bit. This should not prevent
     the feedback packets from being sent at all. */
  for (i = 0; i < 100; i += 10) {
    TWCCPacket packets[] = { {i, i * 250 * GST_USECOND, FALSE}
    };
    twcc_push_packets (h, packets);
  }

  /* verify we did receive some feedback for these packets */
  gst_buffer_unref (session_harness_produce_twcc (h));

  session_harness_free (h);
}

GST_END_TEST;

static GstBuffer *
generate_twcc_feedback_rtcp (guint8 * fci_data, guint16 fci_length)
{
  GstRTCPPacket packet;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstBuffer *buffer = gst_rtcp_buffer_new (1000);
  guint8 *fci;

  fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_RTPFB,
          &packet));
  gst_rtcp_packet_fb_set_type (&packet, GST_RTCP_RTPFB_TYPE_TWCC);
  gst_rtcp_packet_fb_set_fci_length (&packet, fci_length);
  fci = gst_rtcp_packet_fb_get_fci (&packet);
  memcpy (fci, fci_data, fci_length);
  gst_rtcp_packet_fb_set_sender_ssrc (&packet, TEST_BUF_SSRC);
  gst_rtcp_packet_fb_set_media_ssrc (&packet, 0);
  gst_rtcp_buffer_unmap (&rtcp);

  return buffer;
}

GST_START_TEST (test_twcc_bad_rtcp)
{
  SessionHarness *h = session_harness_new ();
  guint i;
  GstBuffer *buf;
  GstEvent *event;
  GValueArray *packets_array;

  guint8 fci[] = {
    0xff, 0xff,                 /* base sequence number: max */
    0xff, 0xff,                 /* packet status count: max */
    0xff, 0xff, 0xff,           /* reference time: max */
    0xff,                       /* feedback packet count: max */
    0x3f, 0xff,                 /* packet chunk: run-length, max */
    0x00,                       /* only 1 recv-delta */
  };

  buf = generate_twcc_feedback_rtcp (fci, sizeof (fci));
  session_harness_recv_rtcp (h, buf);

  /* two reconfigure events */
  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (h->send_rtp_h));

  event = gst_harness_pull_upstream_event (h->send_rtp_h);
  packets_array =
      g_value_get_boxed (gst_structure_get_value (gst_event_get_structure
          (event), "packets"));

  /* this ends up with 0 packets, due to completely invalid data */
  fail_unless_equals_int (packets_array->n_values, 0);

  gst_event_unref (event);
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_delta_ts_rounding)
{
  SessionHarness *h = session_harness_new ();
  guint i, j = 0;
  GstEvent *event;
  GstBuffer *buf;
  GValueArray *packets_array;

  TWCCPacket packets[] = {
    {2002, 9 * GST_SECOND + 366458177, FALSE}
    ,
    {2003, 9 * GST_SECOND + 366497068, FALSE}
    ,
    {2017, 9 * GST_SECOND + 366929482, FALSE}
    ,
    {2019, 9 * GST_SECOND + 391595309, FALSE}
    ,
    {2020, 9 * GST_SECOND + 426883507, FALSE}
    ,
    {2025, 9 * GST_SECOND + 427021638, TRUE}
    ,
  };

  TWCCPacket exp_packets[] = {
    {2002, TWCC_REF_TIME_INITIAL_OFFSET + 9 * GST_SECOND + 366250000, FALSE}
    ,
    {2003, TWCC_REF_TIME_INITIAL_OFFSET + 9 * GST_SECOND + 366250000, FALSE}
    ,
    {2017, TWCC_REF_TIME_INITIAL_OFFSET + 9 * GST_SECOND + 366750000, FALSE}
    ,
    {2019, TWCC_REF_TIME_INITIAL_OFFSET + 9 * GST_SECOND + 391500000, FALSE}
    ,
    {2020, TWCC_REF_TIME_INITIAL_OFFSET + 9 * GST_SECOND + 426750000, FALSE}
    ,
    {2025, TWCC_REF_TIME_INITIAL_OFFSET + 9 * GST_SECOND + 427000000, TRUE}
    ,
  };

  guint8 exp_fci[] = {
    0x07, 0xd2,                 /* base sequence number: 2002 */
    0x00, 0x18,                 /* packet status count: 24 */
    0x00, 0x00, 0x92,           /* reference time: 0:00:09.344000000 */
    0x00,                       /* feedback packet count: 0 */
    0xb0, 0x00,                 /* packet chunk: 1 0 1 1 0 0 0 0 | 0 0 0 0 0 0 0 0 */
    0x96, 0x10,                 /* packet chunk: 1 0 0 1 0 1 1 0 | 0 0 0 1 0 0 0 0 */
    0x59,                       /* recv delta: 0:00:00.022250000 abs: 0:00:09.366250000 */
    0x00,                       /* recv delta: 0:00:00.000000000 abs: 0:00:09.366250000 */
    0x02,                       /* recv delta: 0:00:00.000500000 abs: 0:00:09.366750000 */
    0x63,                       /* recv delta: 0:00:00.024750000 abs: 0:00:09.391500000 */
    0x8d,                       /* recv delta: 0:00:00.035250000 abs: 0:00:09.426750000 */
    0x01,                       /* recv delta: 0:00:00.000250000 abs: 0:00:09.427000000 */
    0x00, 0x00,                 /* padding */
  };

  twcc_push_packets (h, packets);
  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci);

  session_harness_recv_rtcp (h, buf);
  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (h->send_rtp_h));
  event = gst_harness_pull_upstream_event (h->send_rtp_h);

  packets_array =
      g_value_get_boxed (gst_structure_get_value (gst_event_get_structure
          (event), "packets"));
  for (i = 0; i < packets_array->n_values; i++) {
    TWCCPacket *twcc_pkt;
    const GstStructure *pkt_s =
        gst_value_get_structure (g_value_array_get_nth (packets_array, i));
    GstClockTime ts;
    guint seqnum;
    gboolean lost;
    fail_unless (gst_structure_get_boolean (pkt_s, "lost", &lost));
    if (lost)
      continue;
    twcc_pkt = &exp_packets[j++];

    fail_unless (gst_structure_get_clock_time (pkt_s, "remote-ts", &ts));
    fail_unless (gst_structure_get_uint (pkt_s, "seqnum", &seqnum));

    fail_unless_equals_int (twcc_pkt->seqnum, seqnum);
    fail_unless_equals_clocktime (twcc_pkt->timestamp, ts);
  }

  gst_event_unref (event);
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_double_gap)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    {1202, 5 * GST_SECOND + 717000000, FALSE}
    ,
    {1215, 5 * GST_SECOND + 760250000, FALSE}
    ,
    {1221, 5 * GST_SECOND + 775500000, TRUE}
    ,
  };

  guint8 exp_fci[] = {
    0x04, 0xb2,                 /* base sequence number: 1202 */
    0x00, 0x14,                 /* packet status count: 20 */
    0x00, 0x00, 0x59,           /* reference time: 0:00:05.696000000 */
    0x00,                       /* feedback packet count: 0 */
    0xa0, 0x01,                 /* packet chunk: 1 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 1 */
    0x81, 0x00,                 /* packet chunk: 1 0 0 0 0 0 0 1 | 0 0 0 0 0 0 0 0 */
    0x54,                       /* recv delta: +0:00:00.021000000 */
    0xad,                       /* recv delta: +0:00:00.043250000 */
    0x3d,                       /* recv delta: +0:00:00.015250000 */
    0x00,                       /* padding */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);

  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_recv_packets_reordered)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;

  /* a reordered seqence, with marker-bits for #3 and #4 */
  TWCCPacket packets[] = {
    {1, 1 * 250 * GST_USECOND, FALSE}
    ,
    {3, 2 * 250 * GST_USECOND, TRUE}
    ,
    {2, 3 * 250 * GST_USECOND, FALSE}
    ,
    {4, 4 * 250 * GST_USECOND, TRUE}
    ,
  };

  /* first we expect #2 to be reported lost */
  guint8 exp_fci0[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x03,                 /* packet status count: 3 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */
    /* packet chunks: */
    0xa8, 0x00,                 /* 1 0 1 0 1 0 0 0 | 0 0 0 0 0 0 0 0 */
    0x01, 0x01,                 /* recv deltas, +1, +1 */
  };

  /* and then when 2 actually arrives, it is already reported lost,
     so we will not re-report it, but drop it */
  guint8 exp_fci1[] = {
    0x00, 0x04,                 /* base sequence number: 4 */
    0x00, 0x01,                 /* packet status count: 1 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x01,                       /* feedback packet count: 1 */
    /* packet chunks: */
    0x20, 0x01,                 /* 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 1 */
    0x04,                       /* recv deltas, +4 */
    0x00,                       /* padding */
  };

  twcc_push_packets (h, packets);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci0);
  gst_buffer_unref (buf);

  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci1);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_recv_late_packet_fb_pkt_count_wrap)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  guint i;

  guint8 exp_fci0[] = {
    0x01, 0x00,                 /* base sequence number: 256 */
    0x00, 0x01,                 /* packet status count: 1 */
    0x00, 0x00, 0x01,           /* reference time: 1 */
    0x00,                       /* feedback packet count: 00 */
    /* packet chunks: */
    0x20, 0x01,                 /* 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 1 */
    0x00,                       /* 0 recv-delta */
    0x00,                       /* padding */
  };

  guint8 exp_fci1[] = {
    0x01, 0x01,                 /* base sequence number: 257 */
    0x00, 0x01,                 /* packet status count: 1 */
    0x00, 0x00, 0x01,           /* reference time: 1 */
    0x01,                       /* feedback packet count: 1 */
    /* packet chunks: */
    0x20, 0x01,                 /* 0 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 1 */
    0x01,                       /* 1 recv-delta */
    0x00,                       /* padding */
  };

  session_harness_set_twcc_recv_ext_id ((h), TEST_TWCC_EXT_ID);

  /* Push packets to get the feedback packet count wrap limit */
  for (i = 0; i < 255; i++) {
    GstClockTime ts = i * 250 * GST_USECOND;
    gst_test_clock_set_time (h->testclock, ts);
    fail_unless_equals_int (GST_FLOW_OK,
        session_harness_recv_rtp ((h),
            generate_twcc_recv_buffer (i, ts, TRUE)));
  }

  /* push pkt #256 to jump ahead and force the overflow */
  gst_test_clock_set_time (h->testclock, 256 * 250 * GST_USECOND);
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp ((h),
          generate_twcc_recv_buffer (256, 256 * 250 * GST_USECOND, TRUE)));

  /* pkt #255 is late and should be dropped */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp ((h),
          generate_twcc_recv_buffer (255, 255 * 250 * GST_USECOND, TRUE)));


  /* push pkt #257 to verify fci is correct */
  gst_test_clock_set_time (h->testclock, 257 * 250 * GST_USECOND);
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp ((h),
          generate_twcc_recv_buffer (257, 257 * 250 * GST_USECOND, TRUE)));

  /* ignore the twcc for the first 255 packets  */
  for (i = 0; i < 255; i++)
    gst_buffer_unref (session_harness_produce_twcc (h));

  /* we expect a fci for pkt #256 */
  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci0);
  gst_buffer_unref (buf);

  /* and one fci for pkt #257 */
  buf = session_harness_produce_twcc (h);
  twcc_verify_fci (buf, exp_fci1);
  gst_buffer_unref (buf);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_recv_rtcp_reordered)
{
  SessionHarness *send_h = session_harness_new ();
  SessionHarness *recv_h = session_harness_new ();
  GstBuffer *buf[4];
  GstEvent *event;
  guint i;

  /* three frames, two packets each */
  TWCCPacket packets[] = {
    {1, 1 * GST_SECOND, FALSE}
    ,
    {2, 2 * GST_SECOND, TRUE}
    ,
    {3, 3 * GST_SECOND, FALSE}
    ,
    {4, 4 * GST_SECOND, TRUE}
    ,
    {5, 5 * GST_SECOND, FALSE}
    ,
    {6, 6 * GST_SECOND, TRUE}
    ,
    {7, 7 * GST_SECOND, FALSE}
    ,
    {8, 8 * GST_SECOND, TRUE}
    ,
  };

/*
  TWCCPacket expected_packets0[] = {
    {1, 1 * 250 * GST_USECOND, FALSE},
    {2, 2 * 250 * GST_USECOND, TRUE},
  };
*/
  twcc_push_packets (recv_h, packets);

  buf[0] = session_harness_produce_twcc (recv_h);
  buf[1] = session_harness_produce_twcc (recv_h);
  buf[2] = session_harness_produce_twcc (recv_h);
  buf[3] = session_harness_produce_twcc (recv_h);

  /* reorder the twcc-feedback */
  session_harness_recv_rtcp (send_h, buf[0]);
  session_harness_recv_rtcp (send_h, buf[2]);
  session_harness_recv_rtcp (send_h, buf[1]);
  session_harness_recv_rtcp (send_h, buf[3]);

  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (send_h->send_rtp_h));

  event = gst_harness_pull_upstream_event (send_h->send_rtp_h);
  twcc_verify_packets_to_event (&packets[0 * 2], event);

  event = gst_harness_pull_upstream_event (send_h->send_rtp_h);
  twcc_verify_packets_to_event (&packets[2 * 2], event);

  event = gst_harness_pull_upstream_event (send_h->send_rtp_h);
  twcc_verify_packets_to_event (&packets[1 * 2], event);

  event = gst_harness_pull_upstream_event (send_h->send_rtp_h);
  twcc_verify_packets_to_event (&packets[3 * 2], event);

  session_harness_free (send_h);
  session_harness_free (recv_h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_no_exthdr_in_buffer)
{
  SessionHarness *h = session_harness_new ();

  session_harness_set_twcc_recv_ext_id (h, TEST_TWCC_EXT_ID);

  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_recv_rtp (h, generate_test_buffer (0, 1234)));
  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_send_and_recv)
{
  SessionHarness *h_send = session_harness_new ();
  SessionHarness *h_recv = session_harness_new ();
  guint frame;
  const guint num_frames = 2;
  const guint num_slices = 15;

  /* enable twcc */
  session_harness_set_twcc_recv_ext_id (h_recv, TEST_TWCC_EXT_ID);
  session_harness_set_twcc_send_ext_id (h_send, TEST_TWCC_EXT_ID);

  for (frame = 0; frame < num_frames; frame++) {
    GstBuffer *buf;
    guint slice;

    for (slice = 0; slice < num_slices; slice++) {
      GstFlowReturn res;
      guint seq = frame * num_slices + slice;

      /* from payloder to rtpbin */
      buf = generate_twcc_send_buffer (seq, slice == num_slices - 1);
      res = session_harness_send_rtp (h_send, buf);
      fail_unless_equals_int (GST_FLOW_OK, res);
      session_harness_advance_and_crank (h_send, TEST_BUF_DURATION);

      /* get the buffer ready for the network */
      buf = session_harness_pull_send_rtp (h_send);

      /* buffer arrives at the receiver */
      res = session_harness_recv_rtp (h_recv, buf);
      fail_unless_equals_int (GST_FLOW_OK, res);
    }

    /* receiver sends a TWCC packet to the sender */
    buf = session_harness_produce_twcc (h_recv);

    /* sender receives the TWCC packet */
    session_harness_recv_rtcp (h_send, buf);

    if (frame > 0)
      twcc_verify_stats (h_send, TEST_BUF_BPS, TEST_BUF_BPS, num_slices,
          num_slices, 0.0f, 0);
  }

  session_harness_free (h_send);
  session_harness_free (h_recv);
}

GST_END_TEST;

GST_START_TEST (test_twcc_multiple_payloads_below_window)
{
  SessionHarness *h_send = session_harness_new ();
  SessionHarness *h_recv = session_harness_new ();

  guint i;

  GstBuffer *buffers[] = {
    generate_twcc_send_buffer_full (0, FALSE, 0xabc, 98),
    generate_twcc_send_buffer_full (0, FALSE, 0xdef, 111),
    generate_twcc_send_buffer_full (1, FALSE, 0xdef, 111),
    generate_twcc_send_buffer_full (2, FALSE, 0xdef, 111),
    generate_twcc_send_buffer_full (1, TRUE, 0xabc, 98),
  };

  /* enable twcc */
  session_harness_set_twcc_recv_ext_id (h_recv, TEST_TWCC_EXT_ID);
  session_harness_set_twcc_send_ext_id (h_send, TEST_TWCC_EXT_ID);

  for (i = 0; i < G_N_ELEMENTS (buffers); i++) {
    GstBuffer *buf = buffers[i];
    GstFlowReturn res;

    /* from payloder to rtpbin */
    res = session_harness_send_rtp (h_send, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);

    buf = session_harness_pull_send_rtp (h_send);
    session_harness_advance_and_crank (h_send, TEST_BUF_DURATION);

    /* buffer arrives at the receiver */
    res = session_harness_recv_rtp (h_recv, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);
  }

  /* sender receives the TWCC packet from the receiver */
  session_harness_recv_rtcp (h_send, session_harness_produce_twcc (h_recv));
  twcc_verify_stats (h_send, 0, 0, 5, 5, 0.0f, GST_CLOCK_STIME_NONE);

  session_harness_free (h_send);
  session_harness_free (h_recv);
}

GST_END_TEST;

typedef struct
{
  GstClockTime interval;
  guint num_packets;
  GstClockTime ts_delta;
  guint num_feedback;
} TWCCFeedbackIntervalCtx;

static TWCCFeedbackIntervalCtx test_twcc_feedback_interval_ctx[] = {
  {50 * GST_MSECOND, 21, 10 * GST_MSECOND, 4},
  {50 * GST_MSECOND, 16, 7 * GST_MSECOND, 2},
  {50 * GST_MSECOND, 16, 66 * GST_MSECOND, 15},
  {50 * GST_MSECOND, 15, 33 * GST_MSECOND, 9},
};

GST_START_TEST (test_twcc_feedback_interval)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf;
  TWCCFeedbackIntervalCtx *ctx = &test_twcc_feedback_interval_ctx[__i__];

  session_harness_set_twcc_recv_ext_id (h, TEST_TWCC_EXT_ID);
  g_object_set (h->internal_session, "twcc-feedback-interval", ctx->interval,
      NULL);

  for (guint i = 0; i < ctx->num_packets; i++) {
    GstClockTime ts = i * ctx->ts_delta;
    gst_test_clock_set_time ((h->testclock), ts);
    fail_unless_equals_int (GST_FLOW_OK,
        session_harness_recv_rtp (h, generate_twcc_recv_buffer (i, ts, FALSE)));
  }

  for (guint i = 0; i < ctx->num_feedback; i++) {
    buf = session_harness_produce_twcc (h);
    gst_buffer_unref (buf);
  }

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_feedback_count_wrap)
{
  SessionHarness *h = session_harness_new ();
  guint i;
  GstBuffer *buf;
  GstEvent *event;
  GValueArray *packets_array;

  guint8 fci1[] = {
    0x05, 0xfd,                 /* base sequence number: 1533 */
    0x00, 0x00,                 /* packet status count: 0 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0xff,                       /* feedback packet count: 255 */
    0x00, 0x00,                 /* packet chunk: run-length, 0 */
    0x00,                       /* 0 recv-delta */
  };

  guint8 fci2[] = {
    0x05, 0xfe,                 /* base sequence number: 1534 */
    0x00, 0x00,                 /* packet status count: 0 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x01,                       /* feedback packet count: 1 */
    0x00, 0x00,                 /* packet chunk: run-length, 0 */
    0x00,                       /* 0 recv-delta */
  };

  buf = generate_twcc_feedback_rtcp (fci1, sizeof (fci1));
  session_harness_recv_rtcp (h, buf);

  buf = generate_twcc_feedback_rtcp (fci2, sizeof (fci2));
  session_harness_recv_rtcp (h, buf);

  /* two reconfigure events */
  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (h->send_rtp_h));


  for (i = 0; i < 2; i++) {
    event = gst_harness_pull_upstream_event (h->send_rtp_h);
    packets_array =
        g_value_get_boxed (gst_structure_get_value (gst_event_get_structure
            (event), "packets"));

    /* we expect zero packets due to feedback packet count jump ahead */
    fail_unless_equals_int (packets_array->n_values, 0);
    gst_event_unref (event);
  }

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_feedback_old_seqnum)
{
  SessionHarness *h = session_harness_new ();
  guint i;
  GstBuffer *buf;
  GstEvent *event;
  GValueArray *packets_array;

  guint8 fci1[] = {
    0x05, 0xfd,                 /* base sequence number: 1533 */
    0x00, 0x00,                 /* packet status count: 0 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 255 */
    0x00, 0x00,                 /* packet chunk: run-length, 0 */
    0x00,                       /* 0 recv-delta */
  };

  guint8 fci2[] = {
    0x05, 0xdc,                 /* base sequence number: 1500 */
    0x00, 0x00,                 /* packet status count: 0 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x01,                       /* feedback packet count: 1 */
    0x00, 0x00,                 /* packet chunk: run-length, 0 */
    0x00,                       /* 0 recv-delta */
  };

  buf = generate_twcc_feedback_rtcp (fci1, sizeof (fci1));
  session_harness_recv_rtcp (h, buf);

  buf = generate_twcc_feedback_rtcp (fci2, sizeof (fci2));
  session_harness_recv_rtcp (h, buf);

  /* two reconfigure events */
  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (h->send_rtp_h));

  for (i = 0; i < 2; i++) {
    event = gst_harness_pull_upstream_event (h->send_rtp_h);
    packets_array =
        g_value_get_boxed (gst_structure_get_value (gst_event_get_structure
            (event), "packets"));

    /* we expect zero packets due to old sequence number */
    fail_unless_equals_int (packets_array->n_values, 0);
    gst_event_unref (event);
  }

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_run_length_max)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    /* *INDENT-OFF* */
    {0, 1000 * GST_USECOND, FALSE},
    {8205, 2000 * GST_USECOND, TRUE},
    /* *INDENT-ON* */
  };

  guint8 exp_fci[] = {
    0x00, 0x00,                 /* base sequence number: 0 */
    0x20, 0x0e,                 /* packet status count: 8206 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */

    0xa0, 0x00,                 /* 1bit status for #0 received:                               1 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 0 */
    0x1f, 0xff,                 /* run-length with max length is reported as not received:    0 0 0 1 1 1 1 1 | 1 1 1 1 1 1 1 1 */
    0xa0, 0x00,                 /* 1bit status for #8205 received:                            1 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 0 */

    0x04,                       /* recv delta: +0:00:00.001000000 */
    0x04,                       /* recv delta: +0:00:00.001000000 */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_run_length_min)
{
  SessionHarness *h0 = session_harness_new ();
  SessionHarness *h1 = session_harness_new ();

  TWCCPacket packets[] = {
    /* *INDENT-OFF* */
    {0, 1000 * GST_USECOND, FALSE},
    {29, 2000 * GST_USECOND, TRUE},
    /* *INDENT-ON* */
  };

  guint8 exp_fci[] = {
    0x00, 0x00,                 /* base sequence number: 0 */
    0x00, 0x1e,                 /* packet status count: 30 */
    0x00, 0x00, 0x00,           /* reference time: 0 */
    0x00,                       /* feedback packet count: 0 */

    0xa0, 0x00,                 /* 1bit status for #0 received:                      1 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 0 */
    0x00, 0x0f,                 /* run-length with length of 15, all not received:   0 0 0 0 0 0 0 0 | 0 0 0 0 1 1 1 1 */
    0xa0, 0x00,                 /* 1bit status for #29 received:                     1 0 1 0 0 0 0 0 | 0 0 0 0 0 0 0 0 */

    0x04,                       /* recv delta: +0:00:00.001000000 */
    0x04,                       /* recv delta: +0:00:00.001000000 */
  };

  twcc_verify_packets_to_fci (h0, packets, exp_fci);
  twcc_verify_packets_to_packets (h1, h1, packets);

  session_harness_free (h0);
  session_harness_free (h1);
}

GST_END_TEST;

GST_START_TEST (test_twcc_reference_time_wrap)
{
  SessionHarness *h = session_harness_new ();
  guint i, j;
  GstBuffer *buf;
  GstEvent *event;
  GValueArray *packets_array;

  /* This fci packet is used as a template for generating fci packets in the
   * test. In these packets only the reference time is patched, which means we
   * get a completely unrealistic sequence of packets all claiming to report the
   * status of packets with sequence number 1 and 2. This is fine in this test
   * since we're only concerned with changes to the reference timestamp and want
   * to get rid of other noise. */
  guint8 fci[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x02,                 /* packet status count: 2 */
    0xcc, 0xcc, 0xcc,           /* reference time (will be replaced in each packet sent) */
    0x00,                       /* feedback packet count: 0 */
    0x40, 0x02,                 /* run-length with large delta for 2 packets: 0 1 0 0 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    /* recv deltas: */
    0x0f, 0xa0,                 /* large delta: +0:00:01.000000000 */
    0xe0, 0xc0,                 /* large delta: -0:00:02.000000000 */
    /* padding */
    0x00, 0x00
  };

  guint8 fci_base_times[][3] = {
    {0x7f, 0xff, 0xfe},         /* 0x7ffffe <=> +149:07:50.784 */
    /* increase over signed wrap */
    {0x80, 0x00, 0x03},         /* 0x800003 <=> +149:07:51.104 */
    /* decrease over signed wrap */
    {0x7f, 0xff, 0xf7},         /* 0x7ffff7 <=> +149:07:50.336 */
    /* increase over signed wrap again */
    {0xff, 0xff, 0xf1},         /* 0xfffff1 <=> +298:15:40.864 */
    /* increase over unsigned wrap */
    {0x00, 0x00, 0x05},         /* 0x000005 (+0x1000000) <=> +298:15:42.144 */
    /* decrease over unsigned wrap */
    {0xff, 0xff, 0xfe},         /* 0xfffffe <=> +298:15:41.696 */
    /* increase over unsigned wrap again */
    {0x55, 0x55, 0x55},         /* 0x555555 (+0x1000000) <=> +397:40:55.744 */
    /* increase over signed wrap again */
    {0xaa, 0xaa, 0xaa},         /* 0xaaaaaa (+0x1000000) <=> +497:06:09.664 */
    /* increase over unsigned wrap again */
    {0x00, 0x00, 0x42},         /* 0x000042 (+0x1000000) <=> +597:31:27.872 */
  };

  GstClockTime exp_ts[] = {
    TWCC_REF_TIME_INITIAL_OFFSET + 0x07ffffeLL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x07ffffeLL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x0800003LL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x0800003LL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x07ffff7LL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x07ffff7LL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x0fffff1LL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x0fffff1LL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x1000005LL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x1000005LL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x0fffffeLL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x0fffffeLL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x1555555LL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x1555555LL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x1aaaaaaLL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x1aaaaaaLL * 64 * GST_MSECOND -
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x2000042LL * 64 * GST_MSECOND +
        1 * GST_SECOND,
    TWCC_REF_TIME_INITIAL_OFFSET + 0x2000042LL * 64 * GST_MSECOND -
        1 * GST_SECOND,
  };

  for (i = 0; i < sizeof (fci_base_times) / 3; ++i) {
    /* patch our fci packet with a new reference time */
    fci[4] = fci_base_times[i][0];
    fci[5] = fci_base_times[i][1];
    fci[6] = fci_base_times[i][2];
    buf = generate_twcc_feedback_rtcp (fci, sizeof (fci));
    session_harness_recv_rtcp (h, buf);
  }

  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (h->send_rtp_h));

  for (i = 0; i < sizeof (fci_base_times) / 3; ++i) {
    event = gst_harness_pull_upstream_event (h->send_rtp_h);
    packets_array =
        g_value_get_boxed (gst_structure_get_value (gst_event_get_structure
            (event), "packets"));

    fail_unless_equals_int (packets_array->n_values, 2);

    /* iterate all values in the array and check that the remote-ts property is set to 0 */
    for (j = 0; j < packets_array->n_values; j++) {
      const GstStructure *pkt_s =
          gst_value_get_structure (g_value_array_get_nth (packets_array, j));
      GstClockTime ts = 1;      /* initialize to non-zero */
      fail_unless (gst_structure_get_clock_time (pkt_s, "remote-ts", &ts));
      fail_unless_equals_clocktime (ts, exp_ts[i * 2 + j]);
    }

    gst_event_unref (event);
  }

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_twcc_reference_time_wrap_start_negative)
{
  SessionHarness *h = session_harness_new ();
  guint i, j;
  GstBuffer *buf;
  GstEvent *event;
  GValueArray *packets_array;

  /* This fci packet is used as a template for generating fci packets in the
   * test. In these packets only the reference time is patched, which means we
   * get a completely unrealistic sequence of packets all claiming to report the
   * status of packets with sequence number 1 and 2. This is fine in this test
   * since we're only concerned with changes to the reference timestamp and want
   * to get rid of other noise. */
  guint8 fci[] = {
    0x00, 0x01,                 /* base sequence number: 1 */
    0x00, 0x02,                 /* packet status count: 2 */
    0xcc, 0xcc, 0xcc,           /* reference time (will be replaced in each packet sent) */
    0x00,                       /* feedback packet count: 0 */
    0x40, 0x02,                 /* run-length with large delta for 2 packets: 0 1 0 0 0 0 0 0 | 0 0 0 0 0 0 1 0 */
    /* recv deltas: */
    0x0f, 0xa0,                 /* large delta: +0:00:01.000000000 */
    0xe0, 0xc0,                 /* large delta: -0:00:02.000000000 */
    /* padding */
    0x00, 0x00
  };

  guint8 fci_base_times[][3] = {
    {0x80, 0x00, 0x03},         /* 0x800003 <=> +149:07:51.104 */
    /* decrease over signed wrap */
    {0x7f, 0xff, 0xf7},         /* 0x7ffff7 <=> +149:07:50.336 */
    /* increase over signed wrap again */
    {0xff, 0xff, 0xf1},         /* 0xfffff1 <=> +298:15:40.864 */
  };

  /* note that we should not add TWCC_REF_TIME_INITIAL_OFFSET here because we
   * subtract from that */
  GstClockTime exp_ts[] = {
    0x800003LL * 64 * GST_MSECOND + 1 * GST_SECOND,
    0x800003LL * 64 * GST_MSECOND - 1 * GST_SECOND,
    0x7ffff7LL * 64 * GST_MSECOND + 1 * GST_SECOND,
    0x7ffff7LL * 64 * GST_MSECOND - 1 * GST_SECOND,
    0xfffff1LL * 64 * GST_MSECOND + 1 * GST_SECOND,
    0xfffff1LL * 64 * GST_MSECOND - 1 * GST_SECOND,
  };

  for (i = 0; i < sizeof (fci_base_times) / 3; ++i) {
    /* patch our fci packet with a new reference time */
    fci[4] = fci_base_times[i][0];
    fci[5] = fci_base_times[i][1];
    fci[6] = fci_base_times[i][2];
    buf = generate_twcc_feedback_rtcp (fci, sizeof (fci));
    session_harness_recv_rtcp (h, buf);
  }

  for (i = 0; i < 2; i++)
    gst_event_unref (gst_harness_pull_upstream_event (h->send_rtp_h));

  for (i = 0; i < sizeof (fci_base_times) / 3; ++i) {
    event = gst_harness_pull_upstream_event (h->send_rtp_h);
    packets_array =
        g_value_get_boxed (gst_structure_get_value (gst_event_get_structure
            (event), "packets"));

    fail_unless_equals_int (packets_array->n_values, 2);

    /* iterate all values in the array and check that the remote-ts property is set to 0 */
    for (j = 0; j < packets_array->n_values; j++) {
      const GstStructure *pkt_s =
          gst_value_get_structure (g_value_array_get_nth (packets_array, j));
      GstClockTime ts = 1;      /* initialize to non-zero */
      fail_unless (gst_structure_get_clock_time (pkt_s, "remote-ts", &ts));
      fail_unless_equals_clocktime (ts, exp_ts[i * 2 + j]);
    }

    gst_event_unref (event);
  }

  session_harness_free (h);
}

GST_END_TEST;

static Suite *
rtpsession_suite (void)
{
  Suite *s = suite_create ("rtpsession");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_multiple_ssrc_rr);
  tcase_add_test (tc_chain, test_multiple_senders_roundrobin_rbs);
  tcase_add_test (tc_chain, test_no_rbs_for_internal_senders);
  tcase_add_test (tc_chain, test_internal_sources_timeout);
  tcase_add_test (tc_chain, test_receive_rtcp_app_packet);
  tcase_add_test (tc_chain, test_dont_lock_on_stats);
  tcase_add_test (tc_chain, test_ignore_suspicious_bye);
  tcase_add_test (tc_chain, test_rr_stats_assignment);

  tcase_add_test (tc_chain, test_ssrc_collision_when_sending);
  tcase_add_test (tc_chain, test_ssrc_collision_when_sending_loopback);
  tcase_add_test (tc_chain, test_ssrc_collision_when_receiving);
  tcase_add_test (tc_chain, test_ssrc_collision_third_party);
  tcase_add_test (tc_chain, test_ssrc_collision_third_party_favor_new);
  tcase_add_test (tc_chain,
      test_ssrc_collision_never_send_on_non_internal_source);

  tcase_add_test (tc_chain, test_request_fir);
  tcase_add_test (tc_chain, test_request_pli);
  tcase_add_test (tc_chain, test_request_fir_after_pli_in_caps);
  tcase_add_test (tc_chain, test_request_nack);
  tcase_add_test (tc_chain, test_request_nack_surplus);
  tcase_add_test (tc_chain, test_request_nack_packing);
  tcase_add_test (tc_chain, test_illegal_rtcp_fb_packet);
  tcase_add_test (tc_chain, test_feedback_rtcp_race);
  tcase_add_test (tc_chain, test_receive_regular_pli);
  tcase_add_test (tc_chain, test_receive_pli_no_sender_ssrc);
  tcase_add_test (tc_chain, test_dont_send_rtcp_while_idle);
  tcase_add_test (tc_chain, test_send_rtcp_when_signalled);
  tcase_add_test (tc_chain, test_change_sent_sdes);
  tcase_add_test (tc_chain, test_disable_sr_timestamp);
  tcase_add_test (tc_chain, test_on_sending_nacks);
  tcase_add_test (tc_chain, test_disable_probation);
  tcase_add_test (tc_chain, test_request_late_nack);
  tcase_add_test (tc_chain, test_clear_pt_map_stress);
  tcase_add_test (tc_chain, test_packet_rate);
  tcase_add_test (tc_chain, test_stepped_packet_rate);

  /* twcc */
  tcase_add_loop_test (tc_chain, test_twcc_header_and_run_length,
      0, G_N_ELEMENTS (twcc_header_and_run_length_test_data));
  tcase_add_test (tc_chain, test_twcc_run_length_max);
  tcase_add_test (tc_chain, test_twcc_run_length_min);
  tcase_add_test (tc_chain, test_twcc_1_bit_status_vector);
  tcase_add_test (tc_chain, test_twcc_2_bit_status_vector);
  tcase_add_test (tc_chain, test_twcc_2_bit_over_capacity);
  tcase_add_test (tc_chain, test_twcc_2_bit_full_status_vector);
  tcase_add_test (tc_chain, test_twcc_status_vector_split_large_delta);
  tcase_add_test (tc_chain, test_twcc_status_vector_split_with_gap);
  tcase_add_test (tc_chain, test_twcc_status_vector_split_into_three);
  tcase_add_loop_test (tc_chain, test_twcc_various_gaps, 0, 50);
  tcase_add_test (tc_chain, test_twcc_negative_delta);
  tcase_add_test (tc_chain, test_twcc_seqnum_wrap);
  tcase_add_test (tc_chain, test_twcc_seqnum_wrap_with_loss);
  tcase_add_test (tc_chain, test_twcc_huge_seqnum_gap);
  tcase_add_test (tc_chain, test_twcc_double_packets);
  tcase_add_test (tc_chain, test_twcc_duplicate_seqnums);
  tcase_add_test (tc_chain, test_twcc_multiple_markers);
  tcase_add_test (tc_chain, test_twcc_no_marker_and_gaps);
  tcase_add_test (tc_chain, test_twcc_bad_rtcp);
  tcase_add_test (tc_chain, test_twcc_delta_ts_rounding);
  tcase_add_test (tc_chain, test_twcc_double_gap);
  tcase_add_test (tc_chain, test_twcc_recv_packets_reordered);
  tcase_add_test (tc_chain, test_twcc_recv_late_packet_fb_pkt_count_wrap);
  tcase_add_test (tc_chain, test_twcc_recv_rtcp_reordered);
  tcase_add_test (tc_chain, test_twcc_no_exthdr_in_buffer);
  tcase_add_test (tc_chain, test_twcc_send_and_recv);
  tcase_add_test (tc_chain, test_twcc_multiple_payloads_below_window);
  tcase_add_loop_test (tc_chain, test_twcc_feedback_interval, 0,
      G_N_ELEMENTS (test_twcc_feedback_interval_ctx));
  tcase_add_test (tc_chain, test_twcc_feedback_count_wrap);
  tcase_add_test (tc_chain, test_twcc_feedback_old_seqnum);
  tcase_add_test (tc_chain, test_twcc_reference_time_wrap);
  tcase_add_test (tc_chain, test_twcc_reference_time_wrap_start_negative);

  return s;
}

GST_CHECK_MAIN (rtpsession);

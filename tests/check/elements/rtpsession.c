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
#define TEST_BUF_SIZE (64000 * TEST_BUF_MS / 1000)
#define TEST_RTP_TS_DURATION (TEST_BUF_CLOCK_RATE * TEST_BUF_MS / 1000)

static GstCaps *
generate_caps (void)
{
  return gst_caps_new_simple ("application/x-rtp",
      "clock-rate", G_TYPE_INT, TEST_BUF_CLOCK_RATE,
      "payload", G_TYPE_INT, TEST_BUF_PT, NULL);
}

static GstBuffer *
generate_test_buffer_full (GstClockTime dts,
    guint seq_num, guint32 rtp_ts, guint ssrc)
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
  gst_rtp_buffer_set_ssrc (&rtp, ssrc);

  payload = gst_rtp_buffer_get_payload (&rtp);
  for (i = 0; i < TEST_BUF_SIZE; i++)
    payload[i] = 0xff;

  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

static GstBuffer *
generate_test_buffer (guint seq_num, guint ssrc)
{
  return generate_test_buffer_full (seq_num * TEST_BUF_DURATION,
      seq_num, seq_num * TEST_RTP_TS_DURATION, ssrc);
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
} SessionHarness;

static GstCaps *
_pt_map_requested (GstElement * element, guint pt, gpointer data)
{
  SessionHarness *h = data;
  return gst_caps_copy (h->caps);
}

static SessionHarness *
session_harness_new (void)
{
  SessionHarness *h = g_new0 (SessionHarness, 1);
  h->caps = generate_caps ();

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

  g_object_unref (h->internal_session);
  gst_object_unref (h->session);
  g_free (h);
}

static GstFlowReturn
session_harness_send_rtp (SessionHarness * h, GstBuffer * buf)
{
  return gst_harness_push (h->send_rtp_h, buf);
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
  while (gst_harness_buffers_in_queue (h->rtcp_h) < num_rtcp_packets)
    session_harness_crank_clock (h);
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

  for (i = 1; i < 4; i++) {
    buf = generate_test_buffer (i, 0xBEEFDEAD);
    res = session_harness_recv_rtp (h, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);
  }

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
  caps = gst_caps_new_simple ("application/x-rtp",
      "ssrc", G_TYPE_UINT, 0x01BADBAD, NULL);
  gst_harness_set_src_caps (h->send_rtp_h, caps);

  for (i = 1; i < 4; i++) {
    buf = generate_test_buffer (i, 0x01BADBAD);
    res = session_harness_send_rtp (h, buf);
    fail_unless_equals_int (GST_FLOW_OK, res);
  }

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
      fail_unless_equals_int (internal_ssrc, ssrc);
      fail_unless_equals_int (0x01BADBAD, ssrc);
      j |= 0x1;
    } else if (rtcp_type == GST_RTCP_TYPE_RR) {
      ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
      if (internal_ssrc != ssrc)
        j |= 0x2;
    }
    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }
  fail_unless_equals_int (0x3, j);      /* verify we got both SR and RR */

  /* go 30 seconds in the future and observe both sources timing out:
   * 0xDEADBEEF -> BYE, 0x01BADBAD -> becomes receiver only */
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
  fail_unless_equals_int (0x3, j);      /* verify we got both BYE and RR */

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
  gst_event_unref (ev);

  session_harness_free (h);
}

GST_END_TEST;

GST_START_TEST (test_receive_pli_no_sender_ssrc)
{
  SessionHarness *h = session_harness_new ();
  GstEvent *ev;

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

GST_START_TEST (test_ssrc_collision_when_sending)
{
  SessionHarness *h = session_harness_new ();
  GstBuffer *buf = gst_rtcp_buffer_new (1400);

/* Push SDES with identical SSRC as what we will use for sending RTP,
   establishing this as a non-internal SSRC */
  add_rtcp_sdes_packet (buf, 0x12345678, "test@foo.bar");
  session_harness_recv_rtcp (h, buf);

  /* Push RTP buffer making our internal SSRC=0x12345678 */
  fail_unless_equals_int (GST_FLOW_OK,
      session_harness_send_rtp (h, generate_test_buffer (0, 0x12345678)));

  /* Verify the packet we just sent is not being boomeranged back to us
     as a received packet! */
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h->recv_rtp_h));

  /* FIXME: verify a Collision event coming upstream! */

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

  /* fix to make the test deterministic: We need to wait for the RTCP-thread
     to have settled to ensure the key-unit will considered once released */
  gst_test_clock_wait_for_next_pending_id (h->testclock, NULL);

  /* request PLI */
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
  tcase_add_test (tc_chain, test_ssrc_collision_when_sending);
  tcase_add_test (tc_chain, test_request_fir);
  tcase_add_test (tc_chain, test_request_pli);
  tcase_add_test (tc_chain, test_illegal_rtcp_fb_packet);
  tcase_add_test (tc_chain, test_feedback_rtcp_race);
  tcase_add_test (tc_chain, test_receive_regular_pli);
  tcase_add_test (tc_chain, test_receive_pli_no_sender_ssrc);
  tcase_add_test (tc_chain, test_dont_send_rtcp_while_idle);
  tcase_add_test (tc_chain, test_send_rtcp_when_signalled);
  return s;
}

GST_CHECK_MAIN (rtpsession);

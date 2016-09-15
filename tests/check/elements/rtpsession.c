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

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/net/gstnetaddressmeta.h>

static const guint payload_size = 160;
static const guint clock_rate = 8000;
static const guint payload_type = 0;

typedef struct
{
  GstElement *session;
  GstPad *src, *rtcp_sink, *rtpsrc;
  GstClock *clock;
  GAsyncQueue *rtcp_queue;
} TestData;

static GstCaps *
generate_caps (void)
{
  return gst_caps_new_simple ("application/x-rtp",
      "clock-rate", G_TYPE_INT, clock_rate,
      "payload-type", G_TYPE_INT, payload_type, NULL);
}

static GstBuffer *
generate_test_buffer (GstClockTime gst_ts,
    gboolean marker_bit, guint seq_num, guint32 rtp_ts, guint ssrc)
{
  GstBuffer *buf;
  guint8 *payload;
  guint i;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  buf = gst_rtp_buffer_new_allocate (payload_size, 0, 0);
  GST_BUFFER_DTS (buf) = gst_ts;
  GST_BUFFER_PTS (buf) = gst_ts;

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type (&rtp, payload_type);
  gst_rtp_buffer_set_marker (&rtp, marker_bit);
  gst_rtp_buffer_set_seq (&rtp, seq_num);
  gst_rtp_buffer_set_timestamp (&rtp, rtp_ts);
  gst_rtp_buffer_set_ssrc (&rtp, ssrc);

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
  g_async_queue_push (data->rtcp_queue, buffer);
  GST_DEBUG ("chained");
  return GST_FLOW_OK;
}

static GstCaps *
pt_map_requested (GstElement * elemen, guint pt, gpointer data)
{
  return generate_caps ();
}

static void
destroy_testharness (TestData * data)
{
  g_assert_cmpint (gst_element_set_state (data->session, GST_STATE_NULL),
      ==, GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (data->session);
  data->session = NULL;

  gst_object_unref (data->src);
  data->src = NULL;

  gst_object_unref (data->rtcp_sink);
  data->rtcp_sink = NULL;

  gst_object_unref (data->rtpsrc);
  data->rtpsrc = NULL;

  gst_object_unref (data->clock);
  data->clock = NULL;

  g_async_queue_unref (data->rtcp_queue);
  data->rtcp_queue = NULL;
}

static void
setup_testharness (TestData * data, gboolean session_as_sender)
{
  GstPad *rtp_sink_pad, *rtcp_src_pad, *rtp_src_pad;
  GstSegment seg;
  GstMiniObject *obj;
  GstCaps *caps;

  data->clock = gst_test_clock_new ();
  GST_DEBUG ("Setting default system clock to test clock");
  gst_system_clock_set_default (data->clock);
  g_assert (data->clock);
  gst_test_clock_set_time (GST_TEST_CLOCK (data->clock), 0);

  data->session = gst_element_factory_make ("rtpsession", NULL);
  g_signal_connect (data->session, "request-pt-map",
      (GCallback) pt_map_requested, data);
  g_assert (data->session);
  gst_element_set_clock (data->session, data->clock);
  g_assert_cmpint (gst_element_set_state (data->session,
          GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);

  data->rtcp_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_mini_object_unref);

  /* link in the test source-pad */
  data->src = gst_pad_new ("src", GST_PAD_SRC);
  g_assert (data->src);
  rtp_sink_pad = gst_element_get_request_pad (data->session,
      session_as_sender ? "send_rtp_sink" : "recv_rtp_sink");
  g_assert (rtp_sink_pad);
  g_assert_cmpint (gst_pad_link (data->src, rtp_sink_pad), ==, GST_PAD_LINK_OK);
  gst_object_unref (rtp_sink_pad);

  data->rtpsrc = gst_pad_new ("sink", GST_PAD_SINK);
  g_assert (data->rtpsrc);
  rtp_src_pad = gst_element_get_static_pad (data->session,
      session_as_sender ? "send_rtp_src" : "recv_rtp_src");
  g_assert (rtp_src_pad);
  g_assert_cmpint (gst_pad_link (rtp_src_pad, data->rtpsrc), ==,
      GST_PAD_LINK_OK);
  gst_object_unref (rtp_src_pad);

  /* link in the test sink-pad */
  data->rtcp_sink = gst_pad_new ("sink", GST_PAD_SINK);
  g_assert (data->rtcp_sink);
  gst_pad_set_element_private (data->rtcp_sink, data);
  caps = generate_caps ();
  gst_pad_set_caps (data->rtcp_sink, caps);
  gst_pad_set_chain_function (data->rtcp_sink, test_sink_pad_chain_cb);
  rtcp_src_pad = gst_element_get_request_pad (data->session, "send_rtcp_src");
  g_assert (rtcp_src_pad);
  g_assert_cmpint (gst_pad_link (rtcp_src_pad, data->rtcp_sink), ==,
      GST_PAD_LINK_OK);
  gst_object_unref (rtcp_src_pad);

  g_assert (gst_pad_set_active (data->src, TRUE));
  g_assert (gst_pad_set_active (data->rtcp_sink, TRUE));

  gst_segment_init (&seg, GST_FORMAT_TIME);
  gst_pad_push_event (data->src, gst_event_new_stream_start ("stream0"));
  gst_pad_set_caps (data->src, caps);
  gst_pad_push_event (data->src, gst_event_new_segment (&seg));
  gst_caps_unref (caps);

  while ((obj = g_async_queue_try_pop (data->rtcp_queue)))
    gst_mini_object_unref (obj);
}

GST_START_TEST (test_multiple_ssrc_rr)
{
  TestData data;
  GstFlowReturn res;
  GstClockID id, tid;
  GstBuffer *in_buf, *out_buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  int i;
  guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
  gint32 packetslost;
  guint8 fractionlost;

  setup_testharness (&data, FALSE);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), 10 * GST_MSECOND);

  for (i = 0; i < 5; i++) {
    GST_DEBUG ("Push %i", i);
    in_buf =
        generate_test_buffer (i * 20 * GST_MSECOND, FALSE, i, i * 20,
        0x01BADBAD);
    res = gst_pad_push (data.src, in_buf);
    fail_unless (res == GST_FLOW_OK || res == GST_FLOW_FLUSHING);


    gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
    tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
    gst_clock_id_unref (id);
    if (tid)
      gst_clock_id_unref (tid);

    in_buf =
        generate_test_buffer (i * 20 * GST_MSECOND, FALSE, i, i * 20,
        0xDEADBEEF);
    res = gst_pad_push (data.src, in_buf);
    fail_unless (res == GST_FLOW_OK || res == GST_FLOW_FLUSHING);

    gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
    tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
    GST_DEBUG ("pushed %i", i);
    gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
        gst_clock_id_get_time (id));
    gst_clock_id_unref (id);
    if (tid)
      gst_clock_id_unref (tid);
  }

  out_buf = g_async_queue_try_pop (data.rtcp_queue);
  if (out_buf)
    gst_buffer_unref (out_buf);

  gst_test_clock_set_time (GST_TEST_CLOCK (data.clock),
      gst_clock_id_get_time (id) + (5 * GST_SECOND));
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);
  tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
  gst_clock_id_unref (id);
  gst_clock_id_unref (tid);

  out_buf = g_async_queue_pop (data.rtcp_queue);
  g_assert (out_buf != NULL);
  g_assert (gst_rtcp_buffer_validate (out_buf));
  gst_rtcp_buffer_map (out_buf, GST_MAP_READ, &rtcp);
  g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
  g_assert (gst_rtcp_packet_get_type (&rtcp_packet) == GST_RTCP_TYPE_RR);
  g_assert_cmpint (gst_rtcp_packet_get_rb_count (&rtcp_packet), ==, 2);

  gst_rtcp_packet_get_rb (&rtcp_packet, 0, &ssrc, &fractionlost, &packetslost,
      &exthighestseq, &jitter, &lsr, &dlsr);

  g_assert_cmpint (ssrc, ==, 0x01BADBAD);

  gst_rtcp_packet_get_rb (&rtcp_packet, 1, &ssrc, &fractionlost, &packetslost,
      &exthighestseq, &jitter, &lsr, &dlsr);
  g_assert_cmpint (ssrc, ==, 0xDEADBEEF);
  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (out_buf);

  destroy_testharness (&data);
}

GST_END_TEST;

/* This verifies that rtpsession will correctly place RBs round-robin
 * across multiple SRs when there are too many senders that their RBs
 * do not fit in one SR */
GST_START_TEST (test_multiple_senders_roundrobin_rbs)
{
  TestData data;
  GstFlowReturn res;
  GstClockID id, tid;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  GstClockTime time;
  gint queue_length;
  gint i, j, k;
  guint32 ssrc;
  GHashTable *sr_ssrcs, *rb_ssrcs, *tmp_set;

  setup_testharness (&data, TRUE);

  /* only the RTCP thread waits on the clock */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);

  for (i = 0; i < 2; i++) {     /* cycles between SR reports */
    for (j = 0; j < 5; j++) {   /* packets per ssrc */
      gint seq = (i * 5) + j;
      GST_DEBUG ("Push %i", seq);

      gst_test_clock_advance_time (GST_TEST_CLOCK (data.clock),
          200 * GST_MSECOND);

      for (k = 0; k < 35; k++) {        /* number of ssrcs */
        buf =
            generate_test_buffer (seq * 200 * GST_MSECOND, FALSE, seq,
            seq * 200, 10000 + k);
        res = gst_pad_push (data.src, buf);
        fail_unless (res == GST_FLOW_OK || res == GST_FLOW_FLUSHING);
      }

      GST_DEBUG ("pushed %i", seq);
    }

    queue_length = g_async_queue_length (data.rtcp_queue);

    do {
      /* crank the RTCP pad thread */
      time = gst_clock_id_get_time (id);
      GST_DEBUG ("Advancing time to %" GST_TIME_FORMAT, GST_TIME_ARGS (time));
      gst_test_clock_set_time (GST_TEST_CLOCK (data.clock), time);
      tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data.clock));
      fail_unless_equals_pointer (tid, id);
      gst_clock_id_unref (id);
      gst_clock_id_unref (tid);

      /* wait for the RTCP pad thread to output its data
       * and start waiting on the next timeout */
      gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock),
          &id);

      /* and retry as long as there are no new RTCP packets out,
       * because the RTCP thread may randomly decide to reschedule
       * the RTCP timeout for later */
    } while (g_async_queue_length (data.rtcp_queue) == queue_length);

    GST_DEBUG ("RTCP timeout processed");
  }
  gst_clock_id_unref (id);

  sr_ssrcs = g_hash_table_new (g_direct_hash, g_direct_equal);
  rb_ssrcs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_hash_table_unref);

  /* verify the rtcp packets */
  for (i = 0; i < 2 * 35; i++) {
    guint expected_rb_count = (i < 35) ? GST_RTCP_MAX_RB_COUNT :
        (35 - GST_RTCP_MAX_RB_COUNT - 1);

    GST_DEBUG ("pop %d", i);

    buf = g_async_queue_pop (data.rtcp_queue);
    g_assert (buf != NULL);
    g_assert (gst_rtcp_buffer_validate (buf));

    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    g_assert_cmpint (gst_rtcp_packet_get_type (&rtcp_packet), ==,
        GST_RTCP_TYPE_SR);

    gst_rtcp_packet_sr_get_sender_info (&rtcp_packet, &ssrc, NULL, NULL, NULL,
        NULL);
    g_assert_cmpint (ssrc, >=, 10000);
    g_assert_cmpint (ssrc, <=, 10035);
    g_hash_table_add (sr_ssrcs, GUINT_TO_POINTER (ssrc));

    /* inspect the RBs */
    g_assert_cmpint (gst_rtcp_packet_get_rb_count (&rtcp_packet), ==,
        expected_rb_count);

    if (i < 35) {
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

    /* cycle done, verify all ssrcs have issued SR reports */
    if ((i + 1) == 35 || (i + 1) == (2 * 35)) {
      g_assert_cmpint (g_hash_table_size (sr_ssrcs), ==, 35);
      g_hash_table_remove_all (sr_ssrcs);
    }
  }

  /* now verify all other ssrcs have been reported on each ssrc's SR */
  g_assert_cmpint (g_hash_table_size (rb_ssrcs), ==, 35);
  for (i = 10000; i < 10035; i++) {
    tmp_set = g_hash_table_lookup (rb_ssrcs, GUINT_TO_POINTER (i));
    g_assert (tmp_set);
    /* SR contains RBs for each other ssrc except the ssrc of the SR */
    g_assert_cmpint (g_hash_table_size (tmp_set), ==, 34);
    g_assert (!g_hash_table_contains (tmp_set, GUINT_TO_POINTER (i)));
  }

  g_hash_table_unref (sr_ssrcs);
  g_hash_table_unref (rb_ssrcs);

  destroy_testharness (&data);
}

GST_END_TEST;

static void
crank_rtcp_thread (TestData * data, GstClockTime * time, GstClockID * id)
{
  gint queue_length;
  GstClockID *tid;

  queue_length = g_async_queue_length (data->rtcp_queue);
  do {
    *time = gst_clock_id_get_time (*id);
    GST_DEBUG ("Advancing time to %" GST_TIME_FORMAT, GST_TIME_ARGS (*time));
    if (*time > gst_clock_get_time (data->clock))
      gst_test_clock_set_time (GST_TEST_CLOCK (data->clock), *time);
    tid = gst_test_clock_process_next_clock_id (GST_TEST_CLOCK (data->clock));
    fail_unless_equals_pointer (tid, *id);

    gst_clock_id_unref (tid);
    gst_clock_id_unref (*id);
    *id = NULL;

    /* wait for the RTCP pad thread to output its data
     * and start waiting on the next timeout */
    gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data->clock), id);

    /* and retry as long as there are no new RTCP packets out,
     * because the RTCP thread may randomly decide to reschedule
     * the RTCP timeout for later */
  } while (g_async_queue_length (data->rtcp_queue) == queue_length);
}

GST_START_TEST (test_internal_sources_timeout)
{
  TestData data;
  GstClockID id;
  GstClockTime time;
  GObject *internal_session;
  guint internal_ssrc;
  guint32 ssrc;
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket rtcp_packet;
  GstFlowReturn res;
  gint i, j;
  GstCaps *caps;

  setup_testharness (&data, TRUE);
  g_object_get (data.session, "internal-session", &internal_session, NULL);
  g_object_set (internal_session, "internal-ssrc", 0xDEADBEEF, NULL);

  /* only the RTCP thread waits on the clock */
  gst_test_clock_wait_for_next_pending_id (GST_TEST_CLOCK (data.clock), &id);

  /* crank the RTCP pad thread until it creates a RR for its internal-ssrc
   * source, since we have not pushed any RTP packets and it doesn't have
   * any other source available */
  crank_rtcp_thread (&data, &time, &id);

  g_object_get (internal_session, "internal-ssrc", &internal_ssrc, NULL);
  g_assert_cmpint (internal_ssrc, ==, 0xDEADBEEF);

  /* verify that rtpsession has sent RR for an internally-created
   * RTPSource that is using the internal-ssrc */
  buf = g_async_queue_pop (data.rtcp_queue);
  g_assert (buf != NULL);
  g_assert (gst_rtcp_buffer_validate (buf));
  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
  g_assert_cmpint (gst_rtcp_packet_get_type (&rtcp_packet), ==,
      GST_RTCP_TYPE_RR);
  ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
  g_assert_cmpint (ssrc, ==, internal_ssrc);
  gst_rtcp_buffer_unmap (&rtcp);
  gst_buffer_unref (buf);

  /* ok, now let's push some RTP packets */
  caps =
      gst_caps_new_simple ("application/x-rtp", "ssrc", G_TYPE_UINT, 0x01BADBAD,
      NULL);
  gst_pad_set_caps (data.src, caps);
  gst_caps_unref (caps);

  for (i = 1; i < 4; i++) {
    gst_test_clock_advance_time (GST_TEST_CLOCK (data.clock),
        200 * GST_MSECOND);
    buf =
        generate_test_buffer (time + i * 200 * GST_MSECOND, FALSE, i, i * 200,
        0x01BADBAD);
    res = gst_pad_push (data.src, buf);
    fail_unless (res == GST_FLOW_OK || res == GST_FLOW_FLUSHING);
  }

  /* internal ssrc must have changed already */
  g_object_get (internal_session, "internal-ssrc", &internal_ssrc, NULL);
  g_assert_cmpint (ssrc, !=, internal_ssrc);
  g_assert_cmpint (internal_ssrc, ==, 0x01BADBAD);

  /* wait for SR */
  crank_rtcp_thread (&data, &time, &id);

  /* verify SR and RR */
  j = 0;
  for (i = 0; i < 2; i++) {
    buf = g_async_queue_pop (data.rtcp_queue);
    g_assert (buf != NULL);
    g_assert (gst_rtcp_buffer_validate (buf));
    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
    g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    if (gst_rtcp_packet_get_type (&rtcp_packet) == GST_RTCP_TYPE_SR) {
      gst_rtcp_packet_sr_get_sender_info (&rtcp_packet, &ssrc, NULL, NULL, NULL,
          NULL);
      g_assert_cmpint (ssrc, ==, internal_ssrc);
      g_assert_cmpint (ssrc, ==, 0x01BADBAD);
      j |= 0x1;
    } else if (gst_rtcp_packet_get_type (&rtcp_packet) == GST_RTCP_TYPE_RR) {
      ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
      g_assert_cmpint (ssrc, !=, internal_ssrc);
      g_assert_cmpint (ssrc, ==, 0xDEADBEEF);
      j |= 0x2;
    }
    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }
  g_assert_cmpint (j, ==, 0x3); /* verify we got both SR and RR */

  /* go 30 seconds in the future and observe both sources timing out:
   * 0xDEADBEEF -> BYE, 0x01BADBAD -> becomes receiver only */
  gst_test_clock_advance_time (GST_TEST_CLOCK (data.clock), 30 * GST_SECOND);
  crank_rtcp_thread (&data, &time, &id);

  /* verify BYE and RR */
  j = 0;
  for (i = 0; i < 2; i++) {
    buf = g_async_queue_pop (data.rtcp_queue);
    g_assert (buf != NULL);
    g_assert (gst_rtcp_buffer_validate (buf));
    gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);

    g_assert (gst_rtcp_buffer_get_first_packet (&rtcp, &rtcp_packet));
    g_assert_cmpint (gst_rtcp_packet_get_type (&rtcp_packet), ==,
        GST_RTCP_TYPE_RR);
    ssrc = gst_rtcp_packet_rr_get_ssrc (&rtcp_packet);
    if (ssrc == 0x01BADBAD) {
      j |= 0x1;
      g_assert_cmpint (ssrc, ==, internal_ssrc);
      /* 2 => RR, SDES. There is no BYE here */
      g_assert_cmpint (gst_rtcp_buffer_get_packet_count (&rtcp), ==, 2);
    } else if (ssrc == 0xDEADBEEF) {
      j |= 0x2;
      g_assert_cmpint (ssrc, !=, internal_ssrc);
      /* 3 => RR, SDES, BYE */
      g_assert_cmpint (gst_rtcp_buffer_get_packet_count (&rtcp), ==, 3);
      g_assert (gst_rtcp_packet_move_to_next (&rtcp_packet));
      g_assert (gst_rtcp_packet_move_to_next (&rtcp_packet));
      g_assert_cmpint (gst_rtcp_packet_get_type (&rtcp_packet), ==,
          GST_RTCP_TYPE_BYE);
    }

    gst_rtcp_buffer_unmap (&rtcp);
    gst_buffer_unref (buf);
  }
  g_assert_cmpint (j, ==, 0x3); /* verify we got both BYE and RR */
  gst_clock_id_unref (id);

  g_object_unref (internal_session);
  destroy_testharness (&data);
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
  GstHarness *h;
  GstBuffer *buffer;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  RTCPAppResult result = { 0 };
  GstElement *internal_session;
  guint8 data[] = { 0x11, 0x22, 0x33, 0x44 };

  h = gst_harness_new_with_padnames ("rtpsession", "recv_rtcp_sink", NULL);
  g_object_get (h->element, "internal-session", &internal_session, NULL);

  g_signal_connect (internal_session, "on-app-rtcp",
      G_CALLBACK (on_app_rtcp_cb), &result);

  /* Push APP buffer with no data */
  buffer = gst_rtcp_buffer_new (1000);
  fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_APP, &packet));
  gst_rtcp_packet_app_set_subtype (&packet, 21);
  gst_rtcp_packet_app_set_ssrc (&packet, 0x11111111);
  gst_rtcp_packet_app_set_name (&packet, "Test");
  gst_rtcp_buffer_unmap (&rtcp);

  gst_harness_set_src_caps_str (h, "application/x-rtcp");
  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  fail_unless_equals_int (result.subtype, 21);
  fail_unless_equals_int (result.ssrc, 0x11111111);
  fail_unless_equals_string (result.name, "Test");
  fail_unless_equals_pointer (result.data, NULL);

  g_free (result.name);

  /* Push APP buffer with data */
  memset (&result, 0, sizeof (result));
  buffer = gst_rtcp_buffer_new (1000);
  fail_unless (gst_rtcp_buffer_map (buffer, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_APP, &packet));
  gst_rtcp_packet_app_set_subtype (&packet, 22);
  gst_rtcp_packet_app_set_ssrc (&packet, 0x22222222);
  gst_rtcp_packet_app_set_name (&packet, "Test");
  gst_rtcp_packet_app_set_data_length (&packet, sizeof (data) / 4);
  memcpy (gst_rtcp_packet_app_get_data (&packet), data, sizeof (data));
  gst_rtcp_buffer_unmap (&rtcp);

  fail_unless_equals_int (gst_harness_push (h, buffer), GST_FLOW_OK);

  fail_unless_equals_int (result.subtype, 22);
  fail_unless_equals_int (result.ssrc, 0x22222222);
  fail_unless_equals_string (result.name, "Test");
  fail_unless (gst_buffer_memcmp (result.data, 0, data, sizeof (data)) == 0);

  g_free (result.name);
  gst_buffer_unref (result.data);

  gst_object_unref (internal_session);
  gst_harness_teardown (h);
}

GST_END_TEST;

static void
stats_test_cb (GObject * object, GParamSpec * spec, gpointer data)
{
  guint num_sources = 0;
  gboolean *cb_called = data;
  g_assert (*cb_called == FALSE);
  *cb_called = TRUE;

  /* We should be able to get a rtpsession property
     without introducing the deadlock */
  g_object_get (object, "num-sources", &num_sources, NULL);
}

GST_START_TEST (test_dont_lock_on_stats)
{
  GstHarness *h_rtcp;
  GstHarness *h_send;
  GstClock *clock = gst_test_clock_new ();
  GstTestClock *testclock = GST_TEST_CLOCK (clock);
  gboolean cb_called = FALSE;

  /* use testclock as the systemclock to capture the rtcp thread waits */
  gst_system_clock_set_default (GST_CLOCK (testclock));

  h_rtcp =
      gst_harness_new_with_padnames ("rtpsession", "recv_rtcp_sink",
      "send_rtcp_src");
  h_send =
      gst_harness_new_with_element (h_rtcp->element, "send_rtp_sink",
      "send_rtp_src");

  /* connect to the stats-reporting */
  g_signal_connect (h_rtcp->element, "notify::stats",
      G_CALLBACK (stats_test_cb), &cb_called);

  /* "crank" and check the stats */
  g_assert (gst_test_clock_crank (testclock));
  gst_buffer_unref (gst_harness_pull (h_rtcp));
  fail_unless (cb_called);

  gst_harness_teardown (h_send);
  gst_harness_teardown (h_rtcp);
  gst_object_unref (clock);
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
  GstHarness *h_rtcp = NULL;
  GstHarness *h_send = NULL;
  gboolean cb_called = FALSE;
  GstTestClock *testclock = GST_TEST_CLOCK (gst_test_clock_new ());

  /* use testclock as the systemclock to capture the rtcp thread waits */
  gst_system_clock_set_default (GST_CLOCK (testclock));

  h_rtcp =
      gst_harness_new_with_padnames ("rtpsession", "recv_rtcp_sink",
      "send_rtcp_src");
  h_send =
      gst_harness_new_with_element (h_rtcp->element, "send_rtp_sink",
      "send_rtp_src");

  /* connect to the stats-reporting */
  g_signal_connect (h_rtcp->element, "notify::stats",
      G_CALLBACK (suspicious_bye_cb), &cb_called);

  /* Push RTP buffer making our internal SSRC=0xDEADBEEF */
  gst_harness_set_src_caps_str (h_send,
      "application/x-rtp,ssrc=(uint)0xDEADBEEF,"
      "clock-rate=90000,seqnum-offset=(uint)12345");
  gst_harness_push (h_send,
      generate_test_buffer (0, FALSE, 12345, 0, 0xDEADBEEF));

  /* Push BYE RTCP with internal SSRC (0xDEADBEEF) */
  gst_harness_set_src_caps_str (h_rtcp, "application/x-rtcp");
  gst_harness_push (h_rtcp, create_bye_rtcp (0xDEADBEEF));

  /* "crank" and check the stats */
  g_assert (gst_test_clock_crank (testclock));
  gst_buffer_unref (gst_harness_pull (h_rtcp));
  fail_unless (cb_called);

  gst_harness_teardown (h_send);
  gst_harness_teardown (h_rtcp);
  gst_object_unref (testclock);
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
  tcase_add_test (tc_chain, test_internal_sources_timeout);
  tcase_add_test (tc_chain, test_receive_rtcp_app_packet);
  tcase_add_test (tc_chain, test_dont_lock_on_stats);
  tcase_add_test (tc_chain, test_ignore_suspicious_bye);

  return s;
}

GST_CHECK_MAIN (rtpsession);

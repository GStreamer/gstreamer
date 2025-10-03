/* GStreamer
 *
 * Copyright (C) 2018 Collabora Ltd.
 *               Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 * Copyright (C) 2019 Pexip
 *               Author: Havard Graff <havard@pexip.com>
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

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#else
# define RUNNING_ON_VALGRIND 0
#endif

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
      "media", G_TYPE_STRING, "audio",
      "clock-rate", G_TYPE_INT, TEST_BUF_CLOCK_RATE, NULL);
}

static GstBuffer *
create_buffer (guint seq_num, guint32 ssrc)
{
  GstBuffer *buf;
  guint8 *payload;
  guint i;
  GstClockTime dts = seq_num * TEST_BUF_DURATION;
  guint32 rtp_ts = seq_num * TEST_RTP_TS_DURATION;
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


typedef struct
{
  GstHarness *rtp_sink;
  GstHarness *rtcp_sink;
  GstHarness *rtp_src;
  GstHarness *rtcp_src;
} TestContext;

static void
rtpssrcdemux_pad_added (G_GNUC_UNUSED GstElement * demux, GstPad * src_pad,
    TestContext * ctx)
{
  GstHarness *h;

  h = gst_harness_new_with_element (ctx->rtp_sink->element, NULL,
      GST_PAD_NAME (src_pad));

  /* FIXME We should also check that pads have current caps, but this is not
   * currently the case as both pads are created when the first pad receive a
   * buffer. If the other pad is not linked, you'll get a pad without caps.
   * Changing this implies not having both pads on 'on-new-ssrc' which would
   * break rtpbin assumption. */

  if (g_str_has_prefix (GST_PAD_NAME (src_pad), "src_")) {
    g_assert_null (ctx->rtp_src);
    ctx->rtp_src = h;
  } else if (g_str_has_prefix (GST_PAD_NAME (src_pad), "rtcp_src_")) {
    g_assert_null (ctx->rtcp_src);
    ctx->rtcp_src = h;
  } else {
    g_assert_not_reached ();
  }
}

GST_START_TEST (test_event_forwarding)
{
  TestContext ctx = { NULL, NULL, NULL, NULL };
  GstHarness *h;
  GstEvent *event;
  GstCaps *caps;
  GstStructure *s;
  guint ssrc;

  ctx.rtp_sink = h = gst_harness_new_with_padnames ("rtpssrcdemux", "sink",
      NULL);
  g_signal_connect (h->element, "pad_added",
      G_CALLBACK (rtpssrcdemux_pad_added), &ctx);

  ctx.rtcp_sink = gst_harness_new_with_element (h->element, "rtcp_sink", NULL);

  gst_harness_set_src_caps (h, generate_caps ());
  gst_harness_push (h, create_buffer (0, TEST_BUF_SSRC));

  g_assert_nonnull (ctx.rtp_src);
  g_assert_nonnull (ctx.rtcp_src);

  gst_harness_push_event (h, gst_event_new_eos ());

  /* We expect stream-start/caps/segment/eos */
  g_assert_cmpint (gst_harness_events_in_queue (ctx.rtp_src), ==, 4);

  event = gst_harness_pull_event (ctx.rtp_src);
  g_assert_cmpint (event->type, ==, GST_EVENT_STREAM_START);
  gst_event_unref (event);

  event = gst_harness_pull_event (ctx.rtp_src);
  g_assert_cmpint (event->type, ==, GST_EVENT_CAPS);
  gst_event_parse_caps (event, &caps);
  s = gst_caps_get_structure (caps, 0);
  g_assert_true (gst_structure_has_field (s, "ssrc"));
  g_assert_true (gst_structure_get_uint (s, "ssrc", &ssrc));
  g_assert_cmpuint (ssrc, ==, TEST_BUF_SSRC);
  gst_event_unref (event);

  event = gst_harness_pull_event (ctx.rtp_src);
  g_assert_cmpint (event->type, ==, GST_EVENT_SEGMENT);
  gst_event_unref (event);

  event = gst_harness_pull_event (ctx.rtp_src);
  g_assert_cmpint (event->type, ==, GST_EVENT_EOS);
  gst_event_unref (event);

  /* We pushed on the RTP pad, no events should have reached the RTCP pad */
  g_assert_cmpint (gst_harness_events_in_queue (ctx.rtcp_src), ==, 0);

  /* push EOS on the rtcp sink pad, to make sure it EOS properly, the harness
   * will create the missing stream-start */
  gst_harness_push_event (ctx.rtcp_sink, gst_event_new_eos ());

  g_assert_cmpint (gst_harness_events_in_queue (ctx.rtp_src), ==, 0);
  g_assert_cmpint (gst_harness_events_in_queue (ctx.rtcp_src), ==, 1);

  event = gst_harness_pull_event (ctx.rtcp_src);
  g_assert_cmpint (event->type, ==, GST_EVENT_EOS);
  gst_event_unref (event);

  gst_harness_teardown (ctx.rtp_src);
  gst_harness_teardown (ctx.rtcp_src);
  gst_harness_teardown (ctx.rtcp_sink);
  gst_harness_teardown (ctx.rtp_sink);
}

GST_END_TEST;

typedef struct
{
  gint ready;
  GMutex mutex;
  GCond cond;
} LockTestContext;

static void
new_ssrc_pad_cb (G_GNUC_UNUSED GstElement * element, G_GNUC_UNUSED guint ssrc,
    G_GNUC_UNUSED GstPad * pad, LockTestContext * ctx)
{
  g_message ("Signalling ready");
  g_atomic_int_set (&ctx->ready, 1);

  g_message ("Waiting no more ready");
  while (g_atomic_int_get (&ctx->ready))
    g_usleep (G_USEC_PER_SEC / 100);

  g_mutex_lock (&ctx->mutex);
  g_mutex_unlock (&ctx->mutex);
}

static gpointer
push_buffer_func (gpointer user_data)
{
  GstHarness *h = user_data;
  gst_harness_push (h, create_buffer (0, 0xdeadbeef));
  return NULL;
}

GST_START_TEST (test_oob_event_locking)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpssrcdemux", "sink", NULL);
  LockTestContext ctx;
  GThread *thread;

  memset (&ctx, 0, sizeof (LockTestContext));
  g_mutex_init (&ctx.mutex);
  g_cond_init (&ctx.cond);

  gst_harness_set_src_caps_str (h, "application/x-rtp");
  g_signal_connect (h->element,
      "new-ssrc-pad", G_CALLBACK (new_ssrc_pad_cb), &ctx);

  thread = g_thread_new ("streaming-thread", push_buffer_func, h);

  g_mutex_lock (&ctx.mutex);

  g_message ("Waiting for ready");
  while (!g_atomic_int_get (&ctx.ready))
    g_usleep (G_USEC_PER_SEC / 100);
  g_message ("Signal no more ready");
  g_atomic_int_set (&ctx.ready, 0);

  gst_harness_push_event (h,
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_OOB, NULL));

  g_mutex_unlock (&ctx.mutex);

  g_thread_join (thread);
  g_mutex_clear (&ctx.mutex);
  g_cond_clear (&ctx.cond);
  gst_harness_teardown (h);
}

GST_END_TEST;


static void
new_ssrc_pad_found (GstElement * element, G_GNUC_UNUSED guint ssrc,
    GstPad * pad, GSList ** src_h)
{
  GstHarness *h = gst_harness_new_with_element (element, NULL, NULL);
  gst_harness_add_element_src_pad (h, pad);
  *src_h = g_slist_prepend (*src_h, h);
}

GST_START_TEST (test_rtpssrcdemux_max_streams)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpssrcdemux", "sink", NULL);
  GSList *src_h = NULL;
  gint i;

  g_object_set (h->element, "max-streams", 64, NULL);
  gst_harness_set_src_caps_str (h, "application/x-rtp");
  g_signal_connect (h->element,
      "new-ssrc-pad", (GCallback) new_ssrc_pad_found, &src_h);
  gst_harness_play (h);

  for (i = 0; i < 128; ++i) {
    fail_unless_equals_int (GST_FLOW_OK,
        gst_harness_push (h, create_buffer (0, i)));
  }

  fail_unless_equals_int (g_slist_length (src_h), 64);
  g_slist_free_full (src_h, (GDestroyNotify) gst_harness_teardown);
  gst_harness_teardown (h);
}

GST_END_TEST;

static void
new_rtcp_ssrc_pad_found (GstElement * element, guint ssrc,
    G_GNUC_UNUSED GstPad * rtp_pad, GSList ** src_h)
{
  GstHarness *h;
  gchar *name;

  name = g_strdup_printf ("rtcp_src_%u", ssrc);
  h = gst_harness_new_with_element (element, NULL, name);
  g_free (name);
  *src_h = g_slist_prepend (*src_h, h);
}

GST_START_TEST (test_rtpssrcdemux_rtcp_app)
{
  GstHarness *h =
      gst_harness_new_with_padnames ("rtpssrcdemux", "rtcp_sink", NULL);
  GSList *src_h = NULL;
  guint8 rtcp_app_pkt[] = { 0x81, 0xcc, 0x00, 0x05, 0x00, 0x00, 0x5d, 0xaf,
    0x20, 0x20, 0x20, 0x20, 0x21, 0x02, 0x00, 0x0a,
    0x00, 0x00, 0x5d, 0xaf, 0x00, 0x00, 0x16, 0x03
  };

  gst_harness_set_src_caps_str (h, "application/x-rtcp");
  g_signal_connect (h->element,
      "new-ssrc-pad", (GCallback) new_rtcp_ssrc_pad_found, &src_h);
  gst_harness_play (h);

  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, gst_buffer_new_wrapped_full (0, rtcp_app_pkt,
              sizeof rtcp_app_pkt, 0, sizeof rtcp_app_pkt, NULL, NULL)));

  fail_unless_equals_int (g_slist_length (src_h), 1);
  g_slist_free_full (src_h, (GDestroyNotify) gst_harness_teardown);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpssrcdemux_invalid_rtp)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpssrcdemux", "sink", NULL);
  guint8 bad_pkt[] = {
    0x01, 0x02, 0x03
  };

  gst_harness_set_src_caps_str (h, "application/x-rtp");
  gst_harness_play (h);

  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, gst_buffer_new_wrapped_full (0, bad_pkt,
              sizeof bad_pkt, 0, sizeof bad_pkt, NULL, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtpssrcdemux_invalid_rtcp)
{
  GstHarness *h =
      gst_harness_new_with_padnames ("rtpssrcdemux", "rtcp_sink", NULL);
  guint8 bad_pkt[] = {
    0x01, 0x02, 0x03
  };

  gst_harness_set_src_caps_str (h, "application/x-rtcp");
  gst_harness_play (h);

  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, gst_buffer_new_wrapped_full (0, bad_pkt,
              sizeof bad_pkt, 0, sizeof bad_pkt, NULL, NULL)));

  gst_harness_teardown (h);
}

GST_END_TEST;

static GstBuffer *
generate_rtcp_sr_buffer (guint ssrc)
{
  GstBuffer *buf;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;

  buf = gst_rtcp_buffer_new (1000);
  fail_unless (gst_rtcp_buffer_map (buf, GST_MAP_READWRITE, &rtcp));
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_SR, &packet));
  gst_rtcp_packet_sr_set_sender_info (&packet, ssrc, 0, 0, 1, 1);
  gst_rtcp_buffer_unmap (&rtcp);
  return buf;
}

typedef struct
{
  GstHarness *rtp_h;
  GstHarness *rtcp_h;
} SimulCtx;

static void
_simul_ctx_new_ssrc_pad_cb (GstElement * element, guint ssrc,
    GstPad * rtp_pad, SimulCtx * ctx)
{
  GstPad *rtcp_pad;
  gchar *name;

  gst_harness_add_element_src_pad (ctx->rtp_h, rtp_pad);

  name = g_strdup_printf ("rtcp_src_%u", ssrc);
  rtcp_pad = gst_element_get_static_pad (element, name);
  gst_harness_add_element_src_pad (ctx->rtcp_h, rtcp_pad);
  gst_object_unref (rtcp_pad);
  g_free (name);
}

static gpointer
_simul_ctx_push_rtp_buffers (gpointer user_data)
{
  SimulCtx *ctx = user_data;

  gst_harness_set_src_caps_str (ctx->rtp_h, "application/x-rtp");
  gst_harness_push (ctx->rtp_h, create_buffer (0, 1111));
  return NULL;
}

static gpointer
_simul_ctx_push_rtcp_buffers (gpointer user_data)
{
  SimulCtx *ctx = user_data;

  g_usleep (10);
  gst_harness_set_src_caps_str (ctx->rtcp_h, "application/x-rtcp");
  gst_harness_push (ctx->rtcp_h, generate_rtcp_sr_buffer (1111));
  return NULL;
}

GST_START_TEST (test_rtp_and_rtcp_arrives_simultaneously)
{
  guint r;
  guint repeats = 1000;
  if (RUNNING_ON_VALGRIND)
    repeats = 2;

  for (r = 0; r < repeats; r++) {
    SimulCtx ctx;
    GThread *t0, *t1;

    ctx.rtp_h = gst_harness_new_with_padnames ("rtpssrcdemux", "sink", NULL);
    ctx.rtcp_h =
        gst_harness_new_with_element (ctx.rtp_h->element, "rtcp_sink", NULL);

    g_signal_connect (ctx.rtp_h->element,
        "new-ssrc-pad", (GCallback) _simul_ctx_new_ssrc_pad_cb, &ctx);

    t0 = g_thread_new ("push rtp", _simul_ctx_push_rtp_buffers, &ctx);
    t1 = g_thread_new ("push rtcp", _simul_ctx_push_rtcp_buffers, &ctx);

    g_thread_join (t0);
    g_thread_join (t1);

    gst_harness_teardown (ctx.rtp_h);
    gst_harness_teardown (ctx.rtcp_h);
  }
}

GST_END_TEST;

static Suite *
rtpssrcdemux_suite (void)
{
  Suite *s = suite_create ("rtpssrcdemux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_event_forwarding);
  tcase_add_test (tc_chain, test_oob_event_locking);
  tcase_add_test (tc_chain, test_rtpssrcdemux_max_streams);
  tcase_add_test (tc_chain, test_rtpssrcdemux_rtcp_app);
  tcase_add_test (tc_chain, test_rtpssrcdemux_invalid_rtp);
  tcase_add_test (tc_chain, test_rtpssrcdemux_invalid_rtcp);
  tcase_add_test (tc_chain, test_rtp_and_rtcp_arrives_simultaneously);

  return s;
}

GST_CHECK_MAIN (rtpssrcdemux);

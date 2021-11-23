/* GStreamer
 *
 * unit test for rtpfunnel
 *
 * Copyright (C) <2017> Pexip.
 *   Contact: Havard Graff <havard@pexip.com>
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
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

GST_START_TEST (rtpfunnel_ssrc_demuxing)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpfunnel", NULL, "src");
  GstHarness *h0 = gst_harness_new_with_element (h->element, "sink_0", NULL);
  GstHarness *h1 = gst_harness_new_with_element (h->element, "sink_1", NULL);

  gst_harness_set_src_caps_str (h0, "application/x-rtp, ssrc=(uint)123");
  gst_harness_set_src_caps_str (h1, "application/x-rtp, ssrc=(uint)321");

  /* unref latency events */
  gst_event_unref (gst_harness_pull_upstream_event (h0));
  gst_event_unref (gst_harness_pull_upstream_event (h1));
  fail_unless_equals_int (1, gst_harness_upstream_events_received (h0));
  fail_unless_equals_int (1, gst_harness_upstream_events_received (h1));

  /* send to pad 0 */
  gst_harness_push_upstream_event (h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstForceKeyUnit",
              "ssrc", G_TYPE_UINT, 123, NULL)));
  fail_unless_equals_int (2, gst_harness_upstream_events_received (h0));
  fail_unless_equals_int (1, gst_harness_upstream_events_received (h1));

  /* send to pad 1 */
  gst_harness_push_upstream_event (h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstForceKeyUnit",
              "ssrc", G_TYPE_UINT, 321, NULL)));
  fail_unless_equals_int (2, gst_harness_upstream_events_received (h0));
  fail_unless_equals_int (2, gst_harness_upstream_events_received (h1));

  /* unknown ssrc, we drop it */
  gst_harness_push_upstream_event (h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstForceKeyUnit",
              "ssrc", G_TYPE_UINT, 666, NULL)));
  fail_unless_equals_int (2, gst_harness_upstream_events_received (h0));
  fail_unless_equals_int (2, gst_harness_upstream_events_received (h1));

  /* no ssrc, we send to all */
  gst_harness_push_upstream_event (h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new_empty ("GstForceKeyUnit")));
  fail_unless_equals_int (3, gst_harness_upstream_events_received (h0));
  fail_unless_equals_int (3, gst_harness_upstream_events_received (h1));

  /* remove pad 0, and send an event referencing the now dead ssrc */
  gst_harness_teardown (h0);
  gst_harness_push_upstream_event (h,
      gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("GstForceKeyUnit",
              "ssrc", G_TYPE_UINT, 123, NULL)));
  fail_unless_equals_int (3, gst_harness_upstream_events_received (h1));

  gst_harness_teardown (h);
  gst_harness_teardown (h1);
}

GST_END_TEST;

GST_START_TEST (rtpfunnel_ssrc_downstream_not_leaking_through)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpfunnel",
      "sink_0", "src");
  GstCaps *caps;
  const GstStructure *s;

  gst_harness_set_sink_caps_str (h, "application/x-rtp, ssrc=(uint)123");

  caps = gst_pad_peer_query_caps (h->srcpad, NULL);
  s = gst_caps_get_structure (caps, 0);

  fail_unless (!gst_structure_has_field (s, "ssrc"));

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (rtpfunnel_common_ts_offset)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpfunnel",
      "sink_0", "src");
  GstCaps *caps;
  const GstStructure *s;
  const guint expected_ts_offset = 12345;
  guint ts_offset;

  g_object_set (h->element, "common-ts-offset", expected_ts_offset, NULL);

  caps = gst_pad_peer_query_caps (h->srcpad, NULL);
  s = gst_caps_get_structure (caps, 0);

  fail_unless (gst_structure_get_uint (s, "timestamp-offset", &ts_offset));
  fail_unless_equals_int (expected_ts_offset, ts_offset);

  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_END_TEST;

static GstBuffer *
generate_test_buffer (guint seqnum, guint ssrc, guint8 twcc_ext_id)
{
  GstBuffer *buf;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  buf = gst_rtp_buffer_new_allocate (0, 0, 0);
  GST_BUFFER_PTS (buf) = seqnum * 20 * GST_MSECOND;
  GST_BUFFER_DTS (buf) = GST_BUFFER_PTS (buf);

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type (&rtp, 100);
  gst_rtp_buffer_set_seq (&rtp, seqnum);
  gst_rtp_buffer_set_timestamp (&rtp, seqnum * 160);
  gst_rtp_buffer_set_ssrc (&rtp, ssrc);

  if (twcc_ext_id > 0) {
    guint16 data;
    GST_WRITE_UINT16_BE (&data, seqnum);
    gst_rtp_buffer_add_extension_onebyte_header (&rtp, twcc_ext_id,
        &data, sizeof (guint16));
  }

  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

GST_START_TEST (rtpfunnel_custom_sticky)
{
  GstHarness *h, *h0, *h1;
  GstEvent *event;
  const GstStructure *s;
  const gchar *value = NULL;

  h = gst_harness_new_with_padnames ("rtpfunnel", NULL, "src");

  /* request a sinkpad, with some caps */
  h0 = gst_harness_new_with_element (h->element, "sink_0", NULL);
  gst_harness_set_src_caps_str (h0, "application/x-rtp, " "ssrc=(uint)123");

  /* request a second sinkpad, also with caps */
  h1 = gst_harness_new_with_element (h->element, "sink_1", NULL);
  gst_harness_set_src_caps_str (h1, "application/x-rtp, " "ssrc=(uint)456");

  while ((event = gst_harness_try_pull_event (h)))
    gst_event_unref (event);

  fail_unless (gst_harness_push_event (h0,
          gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_STICKY,
              gst_structure_new ("test", "key", G_TYPE_STRING, "value0",
                  NULL))));

  fail_unless (gst_harness_push_event (h1,
          gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_STICKY,
              gst_structure_new ("test", "key", G_TYPE_STRING, "value1",
                  NULL))));

  /* Send a buffer through first pad, expect the event to be the first one */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h0, generate_test_buffer (500, 123, 0)));
  for (;;) {
    event = gst_harness_pull_event (h);
    if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM_STICKY)
      break;
    gst_event_unref (event);
  }
  s = gst_event_get_structure (event);
  fail_unless (s);
  fail_unless (gst_structure_has_name (s, "test"));
  value = gst_structure_get_string (s, "key");
  fail_unless_equals_string (value, "value0");
  gst_event_unref (event);
  gst_buffer_unref (gst_harness_pull (h));

  /* Send a buffer through second pad, expect the event to be the second one
   */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h1, generate_test_buffer (500, 123, 0)));
  for (;;) {
    event = gst_harness_pull_event (h);
    if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM_STICKY)
      break;
    gst_event_unref (event);
  }
  s = gst_event_get_structure (event);
  fail_unless (s);
  fail_unless (gst_structure_has_name (s, "test"));
  value = gst_structure_get_string (s, "key");
  fail_unless_equals_string (value, "value1");
  gst_event_unref (event);
  gst_buffer_unref (gst_harness_pull (h));

  /* Send a buffer through first pad, expect the event to again be the first
   * one
   */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h0, generate_test_buffer (500, 123, 5)));
  for (;;) {
    event = gst_harness_pull_event (h);
    if (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM_STICKY)
      break;
    gst_event_unref (event);
  }
  s = gst_event_get_structure (event);
  fail_unless (s);
  fail_unless (gst_structure_has_name (s, "test"));
  value = gst_structure_get_string (s, "key");
  fail_unless_equals_string (value, "value0");
  gst_event_unref (event);
  gst_buffer_unref (gst_harness_pull (h));

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h1);
}

GST_END_TEST;

GST_START_TEST (rtpfunnel_stress)
{
  GstHarness *h = gst_harness_new_with_padnames ("rtpfunnel",
      "sink_0", "src");
  GstHarness *h1 = gst_harness_new_with_element (h->element, "sink_1", NULL);

  GstPadTemplate *templ =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (h->element),
      "sink_%u");
  GstCaps *caps = gst_caps_from_string ("application/x-rtp, ssrc=(uint)123");
  GstBuffer *buf = gst_buffer_new_allocate (NULL, 0, NULL);
  GstSegment segment;
  GstHarnessThread *statechange, *push, *req, *push1;

  gst_check_add_log_filter ("GStreamer", G_LOG_LEVEL_WARNING,
      g_regex_new ("Got data flow before (stream-start|segment) event",
          (GRegexCompileFlags) 0, (GRegexMatchFlags) 0, NULL),
      NULL, NULL, NULL);
  gst_check_add_log_filter ("GStreamer", G_LOG_LEVEL_WARNING,
      g_regex_new ("Sticky event misordering",
          (GRegexCompileFlags) 0, (GRegexMatchFlags) 0, NULL),
      NULL, NULL, NULL);


  gst_segment_init (&segment, GST_FORMAT_TIME);

  statechange = gst_harness_stress_statechange_start (h);
  push = gst_harness_stress_push_buffer_start (h, caps, &segment, buf);
  req = gst_harness_stress_requestpad_start (h, templ, NULL, NULL, TRUE);
  push1 = gst_harness_stress_push_buffer_start (h1, caps, &segment, buf);

  gst_caps_unref (caps);
  gst_buffer_unref (buf);

  /* test-length */
  g_usleep (G_USEC_PER_SEC * 1);

  gst_harness_stress_thread_stop (push1);
  gst_harness_stress_thread_stop (req);
  gst_harness_stress_thread_stop (push);
  gst_harness_stress_thread_stop (statechange);

  gst_harness_teardown (h1);
  gst_harness_teardown (h);

  gst_check_clear_log_filter ();
}

GST_END_TEST;

#define TWCC_EXTMAP_STR "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"

#define BOGUS_EXTMAP_STR "http://www.ietf.org/id/bogus"

GST_START_TEST (rtpfunnel_twcc_caps)
{
  GstHarness *h, *h0, *h1;
  GstCaps *caps, *expected_caps;

  h = gst_harness_new_with_padnames ("rtpfunnel", NULL, "src");

  /* request a sinkpad, set caps with twcc extmap */
  h0 = gst_harness_new_with_element (h->element, "sink_0", NULL);
  gst_harness_set_src_caps_str (h0, "application/x-rtp, "
      "ssrc=(uint)123, extmap-5=" TWCC_EXTMAP_STR "");

  /* request a second sinkpad, the extmap should not be
     present in the caps when doing a caps-query downstream,
     as we don't want to force upstream (typically a payloader)
     to use the extension */
  h1 = gst_harness_new_with_element (h->element, "sink_1", NULL);
  caps = gst_pad_query_caps (GST_PAD_PEER (h1->srcpad), NULL);
  expected_caps = gst_caps_new_empty_simple ("application/x-rtp");
  fail_unless (gst_caps_is_equal (expected_caps, caps));
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  /* now try and set a different extmap for the same id on the other
   * sinkpad, and verify this does not work */
  gst_harness_set_src_caps_str (h1, "application/x-rtp, "
      "ssrc=(uint)456, extmap-5=" BOGUS_EXTMAP_STR "");
  caps = gst_pad_get_current_caps (GST_PAD_PEER (h1->srcpad));
  fail_unless (caps == NULL);

  /* ...but setting the right extmap (5) will work just fine */
  expected_caps = gst_caps_from_string ("application/x-rtp, "
      "ssrc=(uint)456, extmap-5=" TWCC_EXTMAP_STR "");
  gst_harness_set_src_caps (h1, gst_caps_ref (expected_caps));
  caps = gst_pad_get_current_caps (GST_PAD_PEER (h1->srcpad));
  fail_unless (gst_caps_is_equal (expected_caps, caps));
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h1);
}

GST_END_TEST;

static gint32
get_twcc_seqnum (GstBuffer * buf, guint8 ext_id)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gint32 val = -1;
  gpointer data;

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  if (gst_rtp_buffer_get_extension_onebyte_header (&rtp, ext_id,
          0, &data, NULL)) {
    val = GST_READ_UINT16_BE (data);
  }
  gst_rtp_buffer_unmap (&rtp);

  return val;
}

static guint32
get_ssrc (GstBuffer * buf)
{
  guint32 ssrc;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  ssrc = gst_rtp_buffer_get_ssrc (&rtp);
  gst_rtp_buffer_unmap (&rtp);
  return ssrc;
}

GST_START_TEST (rtpfunnel_twcc_passthrough)
{
  GstHarness *h, *h0;
  GstBuffer *buf;
  guint16 offset = 65530;
  guint packets = 40;
  guint i;

  h = gst_harness_new_with_padnames ("rtpfunnel", NULL, "src");
  h0 = gst_harness_new_with_element (h->element, "sink_0", NULL);
  gst_harness_set_src_caps_str (h0, "application/x-rtp, "
      "ssrc=(uint)123, extmap-5=" TWCC_EXTMAP_STR "");

  /* push some packets with twcc seqnum */
  for (i = 0; i < packets; i++) {
    guint16 seqnum = i + offset;
    fail_unless_equals_int (GST_FLOW_OK,
        gst_harness_push (h0, generate_test_buffer (seqnum, 123, 5)));
  }

  /* and verify the seqnums stays unchanged through the funnel */
  for (i = 0; i < packets; i++) {
    guint16 seqnum = i + offset;
    buf = gst_harness_pull (h);
    fail_unless_equals_int (seqnum, get_twcc_seqnum (buf, 5));
    gst_buffer_unref (buf);
  }

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
}

GST_END_TEST;

GST_START_TEST (rtpfunnel_twcc_mux)
{
  GstHarness *h, *h0, *h1;
  GstBuffer *buf;

  h = gst_harness_new_with_padnames ("rtpfunnel", NULL, "src");
  h0 = gst_harness_new_with_element (h->element, "sink_0", NULL);
  h1 = gst_harness_new_with_element (h->element, "sink_1", NULL);
  gst_harness_set_src_caps_str (h0, "application/x-rtp, "
      "ssrc=(uint)123, extmap-5=" TWCC_EXTMAP_STR "");
  gst_harness_set_src_caps_str (h1, "application/x-rtp, "
      "ssrc=(uint)456, extmap-5=" TWCC_EXTMAP_STR "");

  /* push buffers on both pads with different twcc-seqnums on them (500 and 60000) */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h0, generate_test_buffer (500, 123, 5)));
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h1, generate_test_buffer (60000, 321, 5)));

  /* verify they are muxed continuously (0 -> 1) */
  buf = gst_harness_pull (h);
  fail_unless_equals_int (123, get_ssrc (buf));
  fail_unless_equals_int (0, get_twcc_seqnum (buf, 5));
  gst_buffer_unref (buf);

  buf = gst_harness_pull (h);
  fail_unless_equals_int (321, get_ssrc (buf));
  fail_unless_equals_int (1, get_twcc_seqnum (buf, 5));
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h1);
}

GST_END_TEST;


GST_START_TEST (rtpfunnel_twcc_passthrough_then_mux)
{
  GstHarness *h, *h0, *h1;
  GstBuffer *buf;
  guint offset0 = 500;
  guint offset1 = 45678;
  guint i;

  h = gst_harness_new_with_padnames ("rtpfunnel", NULL, "src");
  h0 = gst_harness_new_with_element (h->element, "sink_0", NULL);
  gst_harness_set_src_caps_str (h0, "application/x-rtp, "
      "ssrc=(uint)123, extmap-5=" TWCC_EXTMAP_STR "");

  /* push one packet with twcc seqnum 100 on pad0 */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h0, generate_test_buffer (offset0, 123, 5)));

  /* add pad1 to the funnel, also with twcc */
  h1 = gst_harness_new_with_element (h->element, "sink_1", NULL);
  gst_harness_set_src_caps_str (h1, "application/x-rtp, "
      "ssrc=(uint)456, extmap-5=" TWCC_EXTMAP_STR "");

  /* push one buffer on both pads, with pad1 starting at a different
     offset */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h0, generate_test_buffer (offset0 + 1, 123, 5)));
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h1, generate_test_buffer (offset1, 321, 5)));

  /* and verify the seqnums are continuous for all 3 packets, using
     the inital offset from pad0 */
  for (i = 0; i < 3; i++) {
    guint16 seqnum = i + offset0;
    buf = gst_harness_pull (h);
    fail_unless_equals_int (seqnum, get_twcc_seqnum (buf, 5));
    gst_buffer_unref (buf);
  }

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h1);
}

GST_END_TEST;

static Suite *
rtpfunnel_suite (void)
{
  Suite *s = suite_create ("rtpfunnel");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, rtpfunnel_ssrc_demuxing);
  tcase_add_test (tc_chain, rtpfunnel_ssrc_downstream_not_leaking_through);
  tcase_add_test (tc_chain, rtpfunnel_common_ts_offset);
  tcase_add_test (tc_chain, rtpfunnel_custom_sticky);

  tcase_add_test (tc_chain, rtpfunnel_stress);

  tcase_add_test (tc_chain, rtpfunnel_twcc_caps);
  tcase_add_test (tc_chain, rtpfunnel_twcc_passthrough);
  tcase_add_test (tc_chain, rtpfunnel_twcc_mux);
  tcase_add_test (tc_chain, rtpfunnel_twcc_passthrough_then_mux);

  return s;
}

GST_CHECK_MAIN (rtpfunnel)

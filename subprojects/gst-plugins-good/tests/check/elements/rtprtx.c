/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *   @author Julien Isorce <julien.isorce@collabora.co.uk>
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
#include <gst/rtp/rtp.h>

#define verify_buf(buf, is_rtx, expected_ssrc, expted_pt, expected_seqnum)       \
  G_STMT_START {                                                                 \
    GstRTPBuffer _rtp = GST_RTP_BUFFER_INIT;                                     \
    fail_unless (gst_rtp_buffer_map (buf, GST_MAP_READ, &_rtp));                 \
    fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&_rtp), expected_ssrc);     \
    fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&_rtp), expted_pt); \
    if (!(is_rtx)) {                                                             \
      fail_unless_equals_int (gst_rtp_buffer_get_seq (&_rtp), expected_seqnum);  \
    } else {                                                                     \
      fail_unless_equals_int (GST_READ_UINT16_BE (gst_rtp_buffer_get_payload     \
              (&_rtp)), expected_seqnum);                                        \
      fail_unless (GST_BUFFER_FLAG_IS_SET (buf,                                  \
          GST_RTP_BUFFER_FLAG_RETRANSMISSION));                                  \
    }                                                                            \
    gst_rtp_buffer_unmap (&_rtp);                                                \
  } G_STMT_END

#define pull_and_verify(h, is_rtx, expected_ssrc, expted_pt, expected_seqnum) \
  G_STMT_START {                                                              \
    GstBuffer *_buf = gst_harness_pull (h);                                   \
    verify_buf (_buf, is_rtx, expected_ssrc, expted_pt, expected_seqnum);     \
    gst_buffer_unref (_buf);                                                  \
  } G_STMT_END

#define push_pull_and_verify(h, buf, is_rtx, expected_ssrc, expted_pt, expected_seqnum) \
  G_STMT_START {                                                                        \
    gst_harness_push (h, buf);                                                          \
    pull_and_verify (h, is_rtx, expected_ssrc, expted_pt, expected_seqnum);             \
  } G_STMT_END

static GstEvent *
create_rtx_event (guint32 ssrc, guint8 payload_type, guint16 seqnum)
{
  return gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
      gst_structure_new ("GstRTPRetransmissionRequest",
          "seqnum", G_TYPE_UINT, seqnum,
          "ssrc", G_TYPE_UINT, ssrc,
          "payload-type", G_TYPE_UINT, payload_type, NULL));
}

static void
compare_rtp_packets (GstBuffer * a, GstBuffer * b)
{
  GstRTPBuffer rtp_a = GST_RTP_BUFFER_INIT;
  GstRTPBuffer rtp_b = GST_RTP_BUFFER_INIT;

  gst_rtp_buffer_map (a, GST_MAP_READ, &rtp_a);
  gst_rtp_buffer_map (b, GST_MAP_READ, &rtp_b);

  fail_unless_equals_int (gst_rtp_buffer_get_header_len (&rtp_a),
      gst_rtp_buffer_get_header_len (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_version (&rtp_a),
      gst_rtp_buffer_get_version (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp_a),
      gst_rtp_buffer_get_ssrc (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp_a),
      gst_rtp_buffer_get_seq (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (&rtp_a),
      gst_rtp_buffer_get_csrc_count (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_marker (&rtp_a),
      gst_rtp_buffer_get_marker (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&rtp_a),
      gst_rtp_buffer_get_payload_type (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&rtp_a),
      gst_rtp_buffer_get_timestamp (&rtp_b));
  fail_unless_equals_int (gst_rtp_buffer_get_extension (&rtp_a),
      gst_rtp_buffer_get_extension (&rtp_b));

  fail_unless_equals_int (gst_rtp_buffer_get_payload_len (&rtp_a),
      gst_rtp_buffer_get_payload_len (&rtp_b));
  fail_unless_equals_int (memcmp (gst_rtp_buffer_get_payload (&rtp_a),
          gst_rtp_buffer_get_payload (&rtp_b),
          gst_rtp_buffer_get_payload_len (&rtp_a)), 0);

  if (gst_rtp_buffer_get_extension (&rtp_a)) {
    guint16 ext_bits_a, ext_bits_b;
    guint8 *ext_data_a, *ext_data_b;
    guint wordlen_a, wordlen_b;

    fail_unless_equals_int (TRUE, gst_rtp_buffer_get_extension_data (&rtp_a,
            &ext_bits_a, (gpointer) & ext_data_a, &wordlen_a));
    fail_unless_equals_int (TRUE, gst_rtp_buffer_get_extension_data (&rtp_b,
            &ext_bits_b, (gpointer) & ext_data_b, &wordlen_b));
    fail_unless_equals_int (ext_bits_a, ext_bits_b);
    fail_unless_equals_int (wordlen_a, wordlen_b);
    fail_unless_equals_int (0, memcmp (ext_data_a, ext_data_b, wordlen_a * 4));
  }

  gst_rtp_buffer_unmap (&rtp_a);
  gst_rtp_buffer_unmap (&rtp_b);
}

static GstRTPBuffer *
create_rtp_buffer_ex (guint32 ssrc, guint8 payload_type, guint16 seqnum,
    guint32 timestamp, guint payload_size)
{
  GstRTPBuffer *ret = g_new0 (GstRTPBuffer, 1);
  GstBuffer *buf = gst_rtp_buffer_new_allocate (payload_size, 0, 0);

  gst_rtp_buffer_map (buf, GST_MAP_WRITE, ret);
  gst_rtp_buffer_set_ssrc (ret, ssrc);
  gst_rtp_buffer_set_payload_type (ret, payload_type);
  gst_rtp_buffer_set_seq (ret, seqnum);
  gst_rtp_buffer_set_timestamp (ret, (guint32) timestamp);
  memset (gst_rtp_buffer_get_payload (ret), 0, payload_size);
  return ret;
}

static GstBuffer *
create_rtp_buffer (guint32 ssrc, guint8 payload_type, guint16 seqnum)
{
  guint payload_size = 29;
  guint64 timestamp = gst_util_uint64_scale_int (seqnum, 90000, 30);
  GstRTPBuffer *rtpbuf = create_rtp_buffer_ex (ssrc, payload_type, seqnum,
      (guint32) timestamp, payload_size);
  GstBuffer *ret = rtpbuf->buffer;

  memset (gst_rtp_buffer_get_payload (rtpbuf), 0x29, payload_size);

  gst_rtp_buffer_unmap (rtpbuf);
  g_free (rtpbuf);
  return ret;
}

static GstBuffer *
create_rtp_buffer_with_timestamp (guint32 ssrc, guint8 payload_type,
    guint16 seqnum, guint32 timestamp, GstClockTime pts)
{
  guint payload_size = 29;
  GstRTPBuffer *rtpbuf = create_rtp_buffer_ex (ssrc, payload_type, seqnum,
      timestamp, payload_size);
  GstBuffer *ret = rtpbuf->buffer;

  memset (gst_rtp_buffer_get_payload (rtpbuf), 0x29, payload_size);

  gst_rtp_buffer_unmap (rtpbuf);
  g_free (rtpbuf);

  GST_BUFFER_PTS (ret) = pts;

  return ret;
}

static GstStructure *
create_rtx_map (const gchar * name, guint key, guint value)
{
  gchar *key_str = g_strdup_printf ("%u", key);
  GstStructure *s = gst_structure_new (name,
      key_str, G_TYPE_UINT, value, NULL);
  g_free (key_str);
  return s;
}

GST_START_TEST (test_rtxsend_basic)
{
  const guint32 main_ssrc = 1234567;
  const guint main_pt = 96;
  const guint32 rtx_ssrc = 7654321;
  const guint rtx_pt = 106;

  GstHarness *h = gst_harness_new ("rtprtxsend");
  GstStructure *ssrc_map =
      create_rtx_map ("application/x-rtp-ssrc-map", main_ssrc, rtx_ssrc);
  GstStructure *pt_map =
      create_rtx_map ("application/x-rtp-pt-map", main_pt, rtx_pt);

  g_object_set (h->element, "ssrc-map", ssrc_map, NULL);
  g_object_set (h->element, "payload-type-map", pt_map, NULL);

  gst_harness_set_src_caps_str (h, "application/x-rtp, "
      "clock-rate = (int)90000");

  /* push a packet */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (main_ssrc, main_pt, 0)));

  /* and check it came through */
  pull_and_verify (h, FALSE, main_ssrc, main_pt, 0);

  /* now request this packet as rtx */
  gst_harness_push_upstream_event (h, create_rtx_event (main_ssrc, main_pt, 0));

  /* and verify we got an rtx-packet for it */
  pull_and_verify (h, TRUE, rtx_ssrc, rtx_pt, 0);

  gst_structure_free (ssrc_map);
  gst_structure_free (pt_map);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtxsend_disabled_enabled_disabled)
{
  const guint32 main_ssrc = 1234567;
  const guint main_pt = 96;
  const guint32 rtx_ssrc = 7654321;
  const guint rtx_pt = 106;

  GstHarness *h = gst_harness_new ("rtprtxsend");
  GstStructure *ssrc_map =
      create_rtx_map ("application/x-rtp-ssrc-map", main_ssrc, rtx_ssrc);
  GstStructure *pt_map =
      create_rtx_map ("application/x-rtp-pt-map", main_pt, rtx_pt);
  GstStructure *empty_pt_map =
      gst_structure_new_empty ("application/x-rtp-pt-map");

  /* set ssrc-map, but not pt-map, making the element work in passthrough */
  g_object_set (h->element, "ssrc-map", ssrc_map, NULL);

  gst_harness_set_src_caps_str (h, "application/x-rtp, "
      "clock-rate = (int)90000");

  /* push, pull, request-rtx, verify nothing arrives */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (main_ssrc, main_pt, 0)));
  pull_and_verify (h, FALSE, main_ssrc, main_pt, 0);
  gst_harness_push_upstream_event (h, create_rtx_event (main_ssrc, main_pt, 0));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  /* verify there is no task on the rtxsend srcpad */
  fail_unless (GST_PAD_TASK (GST_PAD_PEER (h->sinkpad)) == NULL);

  /* now enable rtx by setting the pt-map */
  g_object_set (h->element, "payload-type-map", pt_map, NULL);

  /* push, pull, request rtx, pull rtx */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (main_ssrc, main_pt, 1)));
  pull_and_verify (h, FALSE, main_ssrc, main_pt, 1);
  gst_harness_push_upstream_event (h, create_rtx_event (main_ssrc, main_pt, 1));
  pull_and_verify (h, TRUE, rtx_ssrc, rtx_pt, 1);
  /* verify there is a task on the rtxsend srcpad */
  fail_unless (GST_PAD_TASK (GST_PAD_PEER (h->sinkpad)) != NULL);

  /* now enable disable rtx agian by setting an empty pt-map */
  g_object_set (h->element, "payload-type-map", empty_pt_map, NULL);

  /* push, pull, request-rtx, verify nothing arrives */
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (main_ssrc, main_pt, 2)));
  pull_and_verify (h, FALSE, main_ssrc, main_pt, 2);
  gst_harness_push_upstream_event (h, create_rtx_event (main_ssrc, main_pt, 2));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));
  /* verify the task is gone again */
  fail_unless (GST_PAD_TASK (GST_PAD_PEER (h->sinkpad)) == NULL);

  gst_structure_free (ssrc_map);
  gst_structure_free (pt_map);
  gst_structure_free (empty_pt_map);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtxsend_configured_not_playing_cleans_up)
{
  GstElement *rtxsend = gst_element_factory_make ("rtprtxsend", NULL);
  GstStructure *ssrc_map =
      create_rtx_map ("application/x-rtp-ssrc-map", 123, 96);
  GstStructure *pt_map = create_rtx_map ("application/x-rtp-pt-map", 321, 106);

  g_object_set (rtxsend, "ssrc-map", ssrc_map, NULL);
  g_object_set (rtxsend, "payload-type-map", pt_map, NULL);
  gst_structure_free (ssrc_map);
  gst_structure_free (pt_map);

  g_usleep (G_USEC_PER_SEC);

  gst_object_unref (rtxsend);
}

GST_END_TEST;


GST_START_TEST (test_rtxreceive_empty_rtx_packet)
{
  guint rtx_ssrc = 7654321;
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_pt = 99;
  GstStructure *pt_map;
  GstRTPBuffer *rtp;
  GstBuffer *rtp_buf;
  GstHarness *h = gst_harness_new ("rtprtxreceive");
  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  g_object_set (h->element, "payload-type-map", pt_map, NULL);
  gst_harness_set_src_caps_str (h, "application/x-rtp, "
      "clock-rate = (int)90000");

  /* Assosiating master stream & rtx stream */
  gst_harness_push_upstream_event (h,
      create_rtx_event (master_ssrc, master_pt, 100));
  /* RTX packet with seqnum=200 containing master stream buffer with seqnum=100 */
  rtp = create_rtp_buffer_ex (rtx_ssrc, rtx_pt, 200, 0, 2);
  rtp_buf = rtp->buffer;
  GST_WRITE_UINT16_BE (gst_rtp_buffer_get_payload (rtp), 100);
  gst_rtp_buffer_unmap (rtp);
  g_free (rtp);
  gst_buffer_unref (gst_harness_push_and_pull (h, rtp_buf));

  /* Creating empty RTX packet */
  rtp = create_rtp_buffer_ex (rtx_ssrc, rtx_pt, 201, 0, 0);
  rtp_buf = rtp->buffer;
  gst_rtp_buffer_unmap (rtp);
  g_free (rtp);
  gst_harness_push (h, rtp_buf);

  /* Empty RTX packet should be ignored */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);
  gst_structure_free (pt_map);
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_rtxsend_rtxreceive)
{
  const guint packets_num = 5;
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_pt = 99;
  GstStructure *pt_map;
  GstBuffer *inbufs[5];
  GstHarness *hrecv = gst_harness_new ("rtprtxreceive");
  GstHarness *hsend = gst_harness_new ("rtprtxsend");
  guint i;

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  g_object_set (hrecv->element, "payload-type-map", pt_map, NULL);
  g_object_set (hsend->element, "payload-type-map", pt_map, NULL);

  gst_harness_set_src_caps_str (hsend, "application/x-rtp, "
      "clock-rate = (int)90000");
  gst_harness_set_src_caps_str (hrecv, "application/x-rtp, "
      "clock-rate = (int)90000");

  /* Push 'packets_num' packets through rtxsend to rtxreceive */
  for (i = 0; i < packets_num; i++) {
    inbufs[i] = create_rtp_buffer (master_ssrc, master_pt, 100 + i);
    gst_harness_push (hsend, gst_buffer_ref (inbufs[i]));
    gst_harness_push (hrecv, gst_harness_pull (hsend));
    pull_and_verify (hrecv, FALSE, master_ssrc, master_pt, 100 + i);
  }

  /* Getting rid of reconfigure event. Preparation before the next step */
  gst_event_unref (gst_harness_pull_upstream_event (hrecv));
  fail_unless_equals_int (gst_harness_upstream_events_in_queue (hrecv), 0);

  /* Push 'packets_num' RTX events through rtxreceive to rtxsend.
     Push RTX packets from rtxsend to rtxreceive and
     check that the packet produced out of RTX packet is the same
     as an original packet */
  for (i = 0; i < packets_num; i++) {
    GstBuffer *outbuf;
    gst_harness_push_upstream_event (hrecv,
        create_rtx_event (master_ssrc, master_pt, 100 + i));
    gst_harness_push_upstream_event (hsend,
        gst_harness_pull_upstream_event (hrecv));
    gst_harness_push (hrecv, gst_harness_pull (hsend));

    outbuf = gst_harness_pull (hrecv);
    compare_rtp_packets (inbufs[i], outbuf);
    gst_buffer_unref (inbufs[i]);
    gst_buffer_unref (outbuf);
  }

  /* Check RTX stats */
  {
    guint rtx_requests;
    guint rtx_packets;
    guint rtx_assoc_packets;
    g_object_get (G_OBJECT (hsend->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets, NULL);
    fail_unless_equals_int (rtx_packets, packets_num);
    fail_unless_equals_int (rtx_requests, packets_num);

    g_object_get (G_OBJECT (hrecv->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets,
        "num-rtx-assoc-packets", &rtx_assoc_packets, NULL);
    fail_unless_equals_int (rtx_packets, packets_num);
    fail_unless_equals_int (rtx_requests, packets_num);
    fail_unless_equals_int (rtx_assoc_packets, packets_num);
  }

  gst_structure_free (pt_map);
  gst_harness_teardown (hrecv);
  gst_harness_teardown (hsend);
}

GST_END_TEST;

GST_START_TEST (test_rtxsend_rtxreceive_with_packet_loss)
{
  guint packets_num = 20;
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_pt = 99;
  guint seqnum = 100;
  guint expected_rtx_packets = 0;
  GstStructure *pt_map;
  GstHarness *hrecv = gst_harness_new ("rtprtxreceive");
  GstHarness *hsend = gst_harness_new ("rtprtxsend");
  guint drop_nth_packet, i;

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  g_object_set (hrecv->element, "payload-type-map", pt_map, NULL);
  g_object_set (hsend->element, "payload-type-map", pt_map, NULL);

  gst_harness_set_src_caps_str (hsend, "application/x-rtp, "
      "clock-rate = (int)90000");
  gst_harness_set_src_caps_str (hrecv, "application/x-rtp, "
      "clock-rate = (int)90000");

  /* Getting rid of reconfigure event. Making sure there is no upstream
     events in the queue. Preparation step before the test. */
  gst_event_unref (gst_harness_pull_upstream_event (hrecv));
  fail_unless_equals_int (gst_harness_upstream_events_in_queue (hrecv), 0);

  /* Push 'packets_num' packets through rtxsend to rtxreceive losing every
     'drop_every_n_packets' packet. When we loose the packet we send RTX event
     through rtxreceive to rtxsend, and verify the packet was retransmitted */
  for (drop_nth_packet = 2; drop_nth_packet < 10; drop_nth_packet++) {
    for (i = 0; i < packets_num; i++, seqnum++) {
      GstBuffer *outbuf;
      GstBuffer *inbuf = create_rtp_buffer (master_ssrc, master_pt, seqnum);
      gboolean drop_this_packet = ((i + 1) % drop_nth_packet) == 0;

      gst_harness_push (hsend, gst_buffer_ref (inbuf));
      if (drop_this_packet) {
        /* Dropping original packet */
        gst_buffer_unref (gst_harness_pull (hsend));
        /* Requesting retransmission */
        gst_harness_push_upstream_event (hrecv,
            create_rtx_event (master_ssrc, master_pt, seqnum));
        gst_harness_push_upstream_event (hsend,
            gst_harness_pull_upstream_event (hrecv));
        /* Pushing RTX packet to rtxreceive */
        gst_harness_push (hrecv, gst_harness_pull (hsend));
        expected_rtx_packets++;
      } else {
        gst_harness_push (hrecv, gst_harness_pull (hsend));
      }

      /* We making sure every buffer we pull is the same as original input
         buffer */
      outbuf = gst_harness_pull (hrecv);
      compare_rtp_packets (inbuf, outbuf);
      gst_buffer_unref (inbuf);
      gst_buffer_unref (outbuf);

      /*
         We should not have any packets in the harness queue by this point. It
         means rtxsend didn't send more packets than RTX events and rtxreceive
         didn't produce more than one packet per RTX packet.
       */
      fail_unless_equals_int (gst_harness_buffers_in_queue (hsend), 0);
      fail_unless_equals_int (gst_harness_buffers_in_queue (hrecv), 0);
    }
  }

  /* Check RTX stats */
  {
    guint rtx_requests;
    guint rtx_packets;
    guint rtx_assoc_packets;
    g_object_get (G_OBJECT (hsend->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets, NULL);
    fail_unless_equals_int (rtx_packets, expected_rtx_packets);
    fail_unless_equals_int (rtx_requests, expected_rtx_packets);

    g_object_get (G_OBJECT (hrecv->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets,
        "num-rtx-assoc-packets", &rtx_assoc_packets, NULL);
    fail_unless_equals_int (rtx_packets, expected_rtx_packets);
    fail_unless_equals_int (rtx_requests, expected_rtx_packets);
    fail_unless_equals_int (rtx_assoc_packets, expected_rtx_packets);
  }

  gst_structure_free (pt_map);
  gst_harness_teardown (hrecv);
  gst_harness_teardown (hsend);
}

GST_END_TEST;

typedef struct
{
  GstHarness *h;
  guint master_ssrc;
  guint master_pt;
  guint rtx_ssrc;
  guint rtx_pt;
  guint seqnum;
  guint expected_rtx_packets;
} RtxSender;

static GstStructure *
create_rtxsenders (RtxSender * senders, guint senders_num)
{
  GstStructure *recv_pt_map =
      gst_structure_new_empty ("application/x-rtp-pt-map");
  guint i;

  for (i = 0; i < senders_num; i++) {
    gchar *master_pt_str;
    gchar *master_caps_str;
    GstStructure *send_pt_map;

    senders[i].h = gst_harness_new ("rtprtxsend");
    senders[i].master_ssrc = 1234567 + i;
    senders[i].rtx_ssrc = 7654321 + i;
    senders[i].master_pt = 80 + i;
    senders[i].rtx_pt = 20 + i;
    senders[i].seqnum = i * 1000;
    senders[i].expected_rtx_packets = 0;

    master_pt_str = g_strdup_printf ("%u", senders[i].master_pt);
    master_caps_str = g_strdup_printf ("application/x-rtp, "
        "media = (string)video, payload = (int)%u, "
        "ssrc = (uint)%u, clock-rate = (int)90000, "
        "encoding-name = (string)RAW",
        senders[i].master_pt, senders[i].master_ssrc);

    send_pt_map = gst_structure_new ("application/x-rtp-pt-map",
        master_pt_str, G_TYPE_UINT, senders[i].rtx_pt, NULL);
    gst_structure_set (recv_pt_map,
        master_pt_str, G_TYPE_UINT, senders[i].rtx_pt, NULL);

    g_object_set (senders[i].h->element, "payload-type-map", send_pt_map, NULL);
    gst_harness_set_src_caps_str (senders[i].h, master_caps_str);

    gst_structure_free (send_pt_map);
    g_free (master_pt_str);
    g_free (master_caps_str);
  }
  return recv_pt_map;
}

static guint
check_rtxsenders_stats_and_teardown (RtxSender * senders, guint senders_num)
{
  guint total_pakets_num = 0;
  guint i;

  for (i = 0; i < senders_num; i++) {
    guint rtx_requests;
    guint rtx_packets;
    g_object_get (G_OBJECT (senders[i].h->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets, NULL);
    fail_unless_equals_int (rtx_packets, senders[i].expected_rtx_packets);
    fail_unless_equals_int (rtx_requests, senders[i].expected_rtx_packets);
    total_pakets_num += rtx_packets;

    gst_harness_teardown (senders[i].h);
  }
  return total_pakets_num;
}

GST_START_TEST (test_multi_rtxsend_rtxreceive_with_packet_loss)
{
  guint senders_num = 5;
  guint packets_num = 10;
  guint total_pakets_num = senders_num * packets_num;
  guint total_dropped_packets = 0;
  RtxSender senders[5];
  GstStructure *pt_map;
  GstHarness *hrecv = gst_harness_new ("rtprtxreceive");
  guint drop_nth_packet, i, j;

  pt_map = create_rtxsenders (senders, 5);
  g_object_set (hrecv->element, "payload-type-map", pt_map, NULL);
  gst_harness_set_src_caps_str (hrecv, "application/x-rtp, "
      "clock-rate = (int)90000");

  /* Getting rid of reconfigure event. Making sure there is no upstream
     events in the queue. Preparation step before the test. */
  gst_event_unref (gst_harness_pull_upstream_event (hrecv));
  fail_unless_equals_int (gst_harness_upstream_events_in_queue (hrecv), 0);

  /* We are going to push the 1st packet from the 1st sender, 2nd from the 2nd,
     3rd from the 3rd, etc. until all the senders will push 'packets_num' packets.
     We will drop every 'drop_nth_packet' packet and request its retransmission
     from all the senders. Because only one of them can produce RTX packet.
     We need to make sure that all other senders will ignore the RTX event they
     can't act upon.
   */
  for (drop_nth_packet = 2; drop_nth_packet < 5; drop_nth_packet++) {
    for (i = 0; i < total_pakets_num; i++) {
      RtxSender *sender = &senders[i % senders_num];
      gboolean drop_this_packet = ((i + 1) % drop_nth_packet) == 0;
      GstBuffer *outbuf, *inbuf;
      inbuf =
          create_rtp_buffer (sender->master_ssrc, sender->master_pt,
          sender->seqnum);

      gst_harness_push (sender->h, gst_buffer_ref (inbuf));
      if (drop_this_packet) {
        GstEvent *rtxevent;
        /* Dropping original packet */
        gst_buffer_unref (gst_harness_pull (sender->h));

        /* Pushing RTX event through rtxreceive to all the senders */
        gst_harness_push_upstream_event (hrecv,
            create_rtx_event (sender->master_ssrc, sender->master_pt,
                sender->seqnum));
        rtxevent = gst_harness_pull_upstream_event (hrecv);

        /* ... to all the senders */
        for (j = 0; j < senders_num; j++)
          gst_harness_push_upstream_event (senders[j].h,
              gst_event_ref (rtxevent));
        gst_event_unref (rtxevent);

        /* Pushing RTX packet to rtxreceive */
        gst_harness_push (hrecv, gst_harness_pull (sender->h));
        sender->expected_rtx_packets++;
        total_dropped_packets++;
      } else {
        gst_harness_push (hrecv, gst_harness_pull (sender->h));
      }

      /* It should not matter whether the buffer was dropped (and retransmitted)
         or it went straight through rtxsend to rtxreceive. We should always pull
         the same buffer that was pushed */
      outbuf = gst_harness_pull (hrecv);
      compare_rtp_packets (inbuf, outbuf);
      gst_buffer_unref (inbuf);
      gst_buffer_unref (outbuf);

      /*
         We should not have any packets in the harness queue by this point. It
         means our senders didn't produce the packets for the unknown RTX event.
       */
      for (j = 0; j < senders_num; j++)
        fail_unless_equals_int (gst_harness_buffers_in_queue (senders[j].h), 0);

      sender->seqnum++;
    }
  }

  /* Check RTX stats */
  {
    guint total_rtx_packets;
    guint rtx_requests;
    guint rtx_packets;
    guint rtx_assoc_packets;

    total_rtx_packets =
        check_rtxsenders_stats_and_teardown (senders, senders_num);
    fail_unless_equals_int (total_rtx_packets, total_dropped_packets);

    g_object_get (G_OBJECT (hrecv->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets,
        "num-rtx-assoc-packets", &rtx_assoc_packets, NULL);
    fail_unless_equals_int (rtx_packets, total_rtx_packets);
    fail_unless_equals_int (rtx_requests, total_rtx_packets);
    fail_unless_equals_int (rtx_assoc_packets, total_rtx_packets);
  }

  gst_structure_free (pt_map);
  gst_harness_teardown (hrecv);
}

GST_END_TEST;

static void
test_rtxsender_packet_retention (gboolean test_with_time,
    gboolean clock_rate_in_caps)
{
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_ssrc = 7654321;
  guint rtx_pt = 99;
  gint num_buffers = test_with_time ? 30 : 10;
  gint half_buffers = num_buffers / 2;
  guint timestamp_delta = 90000 / 30;
  guint timestamp = G_MAXUINT32 - half_buffers * timestamp_delta;
  GstHarness *h;
  GstStructure *pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  GstStructure *ssrc_map = gst_structure_new ("application/x-rtp-ssrc-map",
      "1234567", G_TYPE_UINT, rtx_ssrc, NULL);
  gint i, j;
  GstClockTime pts = 0;

  h = gst_harness_new ("rtprtxsend");

  /* In both cases we want the rtxsend queue to store 'half_buffers'
     amount of buffers at most. In max-size-packets mode, it's trivial.
     In max-size-time mode, we specify almost half a second, which is
     the equivalent of 15 frames in a 30fps video stream.
   */
  g_object_set (h->element,
      "max-size-packets", test_with_time ? 0 : half_buffers,
      "max-size-time", test_with_time ? 499 : 0,
      "payload-type-map", pt_map, "ssrc-map", ssrc_map, NULL);

  if (clock_rate_in_caps) {
    gst_harness_set_src_caps_str (h, "application/x-rtp, "
        "clock-rate = (int)90000");
  } else {
    gst_harness_set_src_caps_str (h, "application/x-rtp");
  }

  /* Now push all buffers and request retransmission every time for all of them */
  for (i = 0; i < num_buffers; i++) {
    pts += GST_SECOND / 30;
    timestamp += timestamp_delta;
    /* Request to retransmit all the previous ones */
    for (j = 0; j < i; j++) {
      guint rtx_seqnum = 0x100 + j;
      gst_harness_push_upstream_event (h,
          create_rtx_event (master_ssrc, master_pt, rtx_seqnum));

      /* Pull only the ones supposed to be retransmitted */
      if (j >= i - half_buffers)
        pull_and_verify (h, TRUE, rtx_ssrc, rtx_pt, rtx_seqnum);
    }
    /* Check there no extra buffers in the harness queue */
    fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

    /* We create RTP buffers with timestamps that will eventually wrap around 0
       to be sure, rtprtxsend can handle it properly */
    push_pull_and_verify (h,
        create_rtp_buffer_with_timestamp (master_ssrc, master_pt, 0x100 + i,
            timestamp, pts), FALSE, master_ssrc, master_pt, 0x100 + i);
  }

  gst_structure_free (pt_map);
  gst_structure_free (ssrc_map);
  gst_harness_teardown (h);
}

GST_START_TEST (test_rtxsender_max_size_packets)
{
  test_rtxsender_packet_retention (FALSE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_rtxsender_max_size_time)
{
  test_rtxsender_packet_retention (TRUE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_rtxsender_max_size_time_no_clock_rate)
{
  test_rtxsender_packet_retention (TRUE, FALSE);
}

GST_END_TEST;

static void
test_rtxqueue_packet_retention (gboolean test_with_time)
{
  guint ssrc = 1234567;
  guint pt = 96;
  gint num_buffers = test_with_time ? 30 : 10;
  gint half_buffers = num_buffers / 2;
  GstClockTime timestamp_delta = GST_SECOND / 30;
  GstClockTime timestamp = 0;
  GstBuffer *buf;
  GstHarness *h;
  gint i, j;

  h = gst_harness_new ("rtprtxqueue");

  /* In both cases we want the rtxqueue to store 'half_buffers'
     amount of buffers at most. In max-size-packets mode, it's trivial.
     In max-size-time mode, we specify almost half a second, which is
     the equivalent of 15 frames in a 30fps video stream.
   */
  g_object_set (h->element,
      "max-size-packets", test_with_time ? 0 : half_buffers,
      "max-size-time", test_with_time ? 498 : 0, NULL);

  gst_harness_set_src_caps_str (h, "application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");

  /* Now push all buffers and request retransmission every time for all of them.
   * Note that rtprtxqueue sends retransmissions in chain(), just before
   * pushing out the chained buffer, a differentiation from rtprtxsend above
   */
  for (i = 0; i < num_buffers; i++, timestamp += timestamp_delta) {
    /* Request to retransmit all the previous ones */
    for (j = 0; j < i; j++) {
      guint rtx_seqnum = 0x100 + j;
      gst_harness_push_upstream_event (h,
          create_rtx_event (ssrc, pt, rtx_seqnum));
    }

    /* push one packet */
    buf = create_rtp_buffer (ssrc, pt, 0x100 + i);
    GST_BUFFER_TIMESTAMP (buf) = timestamp;
    gst_harness_push (h, buf);

    /* Pull the ones supposed to be retransmitted */
    for (j = 0; j < i; j++) {
      guint rtx_seqnum = 0x100 + j;
      if (j >= i - half_buffers)
        pull_and_verify (h, FALSE, ssrc, pt, rtx_seqnum);
    }

    /* There should be only one packet remaining in the queue now */
    fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);

    /* pull the one that we just pushed (comes after the retransmitted ones) */
    pull_and_verify (h, FALSE, ssrc, pt, 0x100 + i);

    /* Check there no extra buffers in the harness queue */
    fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);
  }

  gst_harness_teardown (h);
}

GST_START_TEST (test_rtxqueue_max_size_packets)
{
  test_rtxqueue_packet_retention (FALSE);
}

GST_END_TEST;

GST_START_TEST (test_rtxqueue_max_size_time)
{
  test_rtxqueue_packet_retention (TRUE);
}

GST_END_TEST;

/* In this test, we verify the behaviour of rtprtxsend when
 * generic caps are provided to its sink pad, this is useful
 * when connected to an rtp funnel.
 */
GST_START_TEST (test_rtxsender_clock_rate_map)
{
  GstBuffer *inbuf, *outbuf;
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_pt = 99;
  guint master_clock_rate = 90000;
  GstStructure *pt_map;
  GstStructure *clock_rate_map;
  GstHarness *hsend = gst_harness_new ("rtprtxsend");

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  clock_rate_map = gst_structure_new ("application/x-rtp-clock-rate-map",
      "96", G_TYPE_UINT, master_clock_rate, NULL);
  g_object_set (hsend->element, "payload-type-map", pt_map,
      "clock-rate-map", clock_rate_map, "max-size-time", 1000, NULL);
  gst_structure_free (pt_map);
  gst_structure_free (clock_rate_map);

  gst_harness_set_src_caps_str (hsend, "application/x-rtp");

  inbuf = create_rtp_buffer (master_ssrc, master_pt, 100);
  gst_harness_push (hsend, inbuf);

  outbuf = gst_harness_pull (hsend);
  fail_unless (outbuf == inbuf);
  gst_buffer_unref (outbuf);

  gst_harness_push_upstream_event (hsend, create_rtx_event (master_ssrc,
          master_pt, 100));

  outbuf = gst_harness_pull (hsend);
  fail_unless (outbuf);
  gst_buffer_unref (outbuf);

  fail_unless_equals_int (gst_harness_buffers_in_queue (hsend), 0);

  /* Thanks to the provided clock rate, rtprtxsend should be able to
   * determine that the previously pushed buffer should be cleared from
   * its rtx queue */
  inbuf = create_rtp_buffer (master_ssrc, master_pt, 131);
  gst_harness_push (hsend, inbuf);

  outbuf = gst_harness_pull (hsend);
  fail_unless (outbuf == inbuf);
  gst_buffer_unref (outbuf);

  fail_unless_equals_int (gst_harness_buffers_in_queue (hsend), 0);

  gst_harness_push_upstream_event (hsend, create_rtx_event (master_ssrc,
          master_pt, 100));

  fail_unless_equals_int (gst_harness_buffers_in_queue (hsend), 0);

  gst_harness_teardown (hsend);
}

GST_END_TEST;

#define TWCC_EXTMAP_STR "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01"
GST_START_TEST (test_rtxsend_header_extensions_copy)
{
  const guint packets_num = 5;
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_pt = 99;
  GstStructure *pt_map;
  GstBuffer *inbufs[5];
  GstHarness *hrecv = gst_harness_new ("rtprtxreceive");
  GstHarness *hsend = gst_harness_new ("rtprtxsend");
  GstRTPHeaderExtension *twcc;
  guint twcc_hdr_id = 7;
  guint i;

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  g_object_set (hrecv->element, "payload-type-map", pt_map, NULL);
  g_object_set (hsend->element, "payload-type-map", pt_map, NULL);

  gst_harness_set_src_caps_str (hsend, "application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");
  gst_harness_set_src_caps_str (hrecv, "application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");

  twcc = gst_rtp_header_extension_create_from_uri (TWCC_EXTMAP_STR);
  gst_rtp_header_extension_set_id (twcc, twcc_hdr_id);

  /* Push 'packets_num' packets through rtxsend to rtxreceive */
  guint8 twcc_seq[2] = { 0, };
  for (i = 0; i < packets_num; ++i) {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

    twcc_seq[0] = i;
    inbufs[i] = create_rtp_buffer (master_ssrc, master_pt, 100 + i);

    fail_unless (gst_rtp_buffer_map (inbufs[i], GST_MAP_READWRITE, &rtp));
    fail_unless (gst_rtp_buffer_add_extension_onebyte_header (&rtp,
            twcc_hdr_id, "100", 2));
    gst_rtp_buffer_unmap (&rtp);

    fail_unless (gst_rtp_header_extension_write (twcc, inbufs[i],
            GST_RTP_HEADER_EXTENSION_ONE_BYTE, inbufs[i], twcc_seq,
            sizeof (twcc_seq)) >= 0);

    gst_harness_push (hsend, gst_buffer_ref (inbufs[i]));
    gst_harness_push (hrecv, gst_harness_pull (hsend));
    pull_and_verify (hrecv, FALSE, master_ssrc, master_pt, 100 + i);
  }
  gst_clear_object (&twcc);

  /* Getting rid of reconfigure event. Preparation before the next step */
  gst_event_unref (gst_harness_pull_upstream_event (hrecv));
  fail_unless_equals_int (gst_harness_upstream_events_in_queue (hrecv), 0);

  /* Push 'packets_num' RTX events through rtxreceive to rtxsend.
     Push RTX packets from rtxsend to rtxreceive and
     check that the packet produced out of RTX packet is the same
     as an original packet */
  for (i = 0; i < packets_num; ++i) {
    GstBuffer *outbuf;

    gst_harness_push_upstream_event (hrecv,
        create_rtx_event (master_ssrc, master_pt, 100 + i));
    gst_harness_push_upstream_event (hsend,
        gst_harness_pull_upstream_event (hrecv));
    gst_harness_push (hrecv, gst_harness_pull (hsend));

    outbuf = gst_harness_pull (hrecv);

    compare_rtp_packets (inbufs[i], outbuf);
    gst_buffer_unref (inbufs[i]);
    gst_buffer_unref (outbuf);
  }

  /* Check RTX stats */
  {
    guint rtx_requests;
    guint rtx_packets;
    guint rtx_assoc_packets;
    g_object_get (G_OBJECT (hsend->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets, NULL);
    fail_unless_equals_int (rtx_packets, packets_num);
    fail_unless_equals_int (rtx_requests, packets_num);

    g_object_get (G_OBJECT (hrecv->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets,
        "num-rtx-assoc-packets", &rtx_assoc_packets, NULL);
    fail_unless_equals_int (rtx_packets, packets_num);
    fail_unless_equals_int (rtx_requests, packets_num);
    fail_unless_equals_int (rtx_assoc_packets, packets_num);
  }

  gst_structure_free (pt_map);
  gst_harness_teardown (hrecv);
  gst_harness_teardown (hsend);
}

GST_END_TEST;


#define RTPHDREXT_STREAM_ID GST_RTP_HDREXT_BASE "sdes:rtp-stream-id"
#define RTPHDREXT_REPAIRED_STREAM_ID GST_RTP_HDREXT_BASE "sdes:repaired-rtp-stream-id"

GST_START_TEST (test_rtxsend_header_extensions)
{
  const guint packets_num = 5;
  guint master_ssrc = 1234567;
  guint master_pt = 96;
  guint rtx_pt = 99;
  GstStructure *pt_map;
  GstBuffer *inbufs[5];
  GstHarness *hrecv = gst_harness_new ("rtprtxreceive");
  GstHarness *hsend = gst_harness_new ("rtprtxsend");
  GstRTPHeaderExtension *send_stream_id, *send_repaired_stream_id;
  GstRTPHeaderExtension *recv_stream_id, *recv_repaired_stream_id;
  guint stream_hdr_id = 1, repaired_hdr_id = 2;
  gint i;

  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "96", G_TYPE_UINT, rtx_pt, NULL);
  g_object_set (hrecv->element, "payload-type-map", pt_map, NULL);
  g_object_set (hsend->element, "payload-type-map", pt_map, NULL);

  gst_harness_set_src_caps_str (hsend, "application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");
  gst_harness_set_src_caps_str (hrecv, "application/x-rtp, "
      "media = (string)video, payload = (int)96, "
      "ssrc = (uint)1234567, clock-rate = (int)90000, "
      "encoding-name = (string)RAW");

  send_stream_id =
      gst_rtp_header_extension_create_from_uri (RTPHDREXT_STREAM_ID);
  gst_rtp_header_extension_set_id (send_stream_id, stream_hdr_id);
  g_object_set (send_stream_id, "rid", "0", NULL);
  fail_unless (send_stream_id != NULL);
  g_signal_emit_by_name (hsend->element, "add-extension", send_stream_id);
  gst_clear_object (&send_stream_id);

  send_repaired_stream_id =
      gst_rtp_header_extension_create_from_uri (RTPHDREXT_REPAIRED_STREAM_ID);
  g_object_set (send_repaired_stream_id, "rid", "0", NULL);
  gst_rtp_header_extension_set_id (send_repaired_stream_id, repaired_hdr_id);
  fail_unless (send_repaired_stream_id != NULL);
  g_signal_emit_by_name (hsend->element, "add-extension",
      send_repaired_stream_id);
  gst_clear_object (&send_repaired_stream_id);

  recv_stream_id =
      gst_rtp_header_extension_create_from_uri (RTPHDREXT_STREAM_ID);
  gst_rtp_header_extension_set_id (recv_stream_id, stream_hdr_id);
  fail_unless (recv_stream_id != NULL);
  g_signal_emit_by_name (hrecv->element, "add-extension", recv_stream_id);
  gst_clear_object (&recv_stream_id);

  recv_repaired_stream_id =
      gst_rtp_header_extension_create_from_uri (RTPHDREXT_REPAIRED_STREAM_ID);
  gst_rtp_header_extension_set_id (recv_repaired_stream_id, repaired_hdr_id);
  fail_unless (recv_repaired_stream_id != NULL);
  g_signal_emit_by_name (hrecv->element, "add-extension",
      recv_repaired_stream_id);
  gst_clear_object (&recv_repaired_stream_id);

  /* Push 'packets_num' packets through rtxsend to rtxreceive */
  for (i = 0; i < packets_num; ++i) {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    inbufs[i] = create_rtp_buffer (master_ssrc, master_pt, 100 + i);
    fail_unless (gst_rtp_buffer_map (inbufs[i], GST_MAP_READWRITE, &rtp));
    fail_unless (gst_rtp_buffer_add_extension_onebyte_header (&rtp,
            stream_hdr_id, "0", 1));
    gst_rtp_buffer_unmap (&rtp);
    gst_harness_push (hsend, gst_buffer_ref (inbufs[i]));
    gst_harness_push (hrecv, gst_harness_pull (hsend));
    pull_and_verify (hrecv, FALSE, master_ssrc, master_pt, 100 + i);
  }

  /* Getting rid of reconfigure event. Preparation before the next step */
  gst_event_unref (gst_harness_pull_upstream_event (hrecv));
  fail_unless_equals_int (gst_harness_upstream_events_in_queue (hrecv), 0);

  /* Push 'packets_num' RTX events through rtxreceive to rtxsend.
     Push RTX packets from rtxsend to rtxreceive and
     check that the packet produced out of RTX packet is the same
     as an original packet */
  for (i = 0; i < packets_num; ++i) {
    GstBuffer *outbuf;
    gst_harness_push_upstream_event (hrecv,
        create_rtx_event (master_ssrc, master_pt, 100 + i));
    gst_harness_push_upstream_event (hsend,
        gst_harness_pull_upstream_event (hrecv));
    gst_harness_push (hrecv, gst_harness_pull (hsend));

    outbuf = gst_harness_pull (hrecv);
    compare_rtp_packets (inbufs[i], outbuf);
    gst_buffer_unref (inbufs[i]);
    gst_buffer_unref (outbuf);
  }

  /* Check RTX stats */
  {
    guint rtx_requests;
    guint rtx_packets;
    guint rtx_assoc_packets;
    g_object_get (G_OBJECT (hsend->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets, NULL);
    fail_unless_equals_int (rtx_packets, packets_num);
    fail_unless_equals_int (rtx_requests, packets_num);

    g_object_get (G_OBJECT (hrecv->element),
        "num-rtx-requests", &rtx_requests,
        "num-rtx-packets", &rtx_packets,
        "num-rtx-assoc-packets", &rtx_assoc_packets, NULL);
    fail_unless_equals_int (rtx_packets, packets_num);
    fail_unless_equals_int (rtx_requests, packets_num);
    fail_unless_equals_int (rtx_assoc_packets, packets_num);
  }

  gst_structure_free (pt_map);
  gst_harness_teardown (hrecv);
  gst_harness_teardown (hsend);
}

GST_END_TEST;

static Suite *
rtprtx_suite (void)
{
  Suite *s = suite_create ("rtprtx");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 120);

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_rtxsend_basic);
  tcase_add_test (tc_chain, test_rtxsend_disabled_enabled_disabled);
  tcase_add_test (tc_chain, test_rtxsend_configured_not_playing_cleans_up);

  tcase_add_test (tc_chain, test_rtxreceive_empty_rtx_packet);
  tcase_add_test (tc_chain, test_rtxsend_rtxreceive);
  tcase_add_test (tc_chain, test_rtxsend_rtxreceive_with_packet_loss);
  tcase_add_test (tc_chain, test_multi_rtxsend_rtxreceive_with_packet_loss);
  tcase_add_test (tc_chain, test_rtxsender_max_size_packets);
  tcase_add_test (tc_chain, test_rtxsender_max_size_time);
  tcase_add_test (tc_chain, test_rtxsender_max_size_time_no_clock_rate);

  tcase_add_test (tc_chain, test_rtxqueue_max_size_packets);
  tcase_add_test (tc_chain, test_rtxqueue_max_size_time);
  tcase_add_test (tc_chain, test_rtxsender_clock_rate_map);
  tcase_add_test (tc_chain, test_rtxsend_header_extensions);
  tcase_add_test (tc_chain, test_rtxsend_header_extensions_copy);

  return s;
}

GST_CHECK_MAIN (rtprtx);

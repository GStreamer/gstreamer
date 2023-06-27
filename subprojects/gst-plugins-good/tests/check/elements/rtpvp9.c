/* GStreamer
 *
 * Copyright (C) 2016 Pexip AS
 *   @author Stian Selnes <stian@pexip.com>
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

#include <gst/check/check.h>
#include <gst/check/gstharness.h>

#define RTP_VP9_CAPS_STR \
  "application/x-rtp,media=video,encoding-name=VP9,clock-rate=90000,payload=96"

GST_START_TEST (test_depay_flexible_mode)
{
  /* b-bit, e-bit, f-bit and marker bit set */
  /* First packet of first frame, handcrafted to also set the e-bit and marker
   * bit in addition to changing the seqnum */
  guint8 intra[] = {
    0x80, 0xf4, 0x00, 0x00, 0x49, 0xb5, 0xbe, 0x32, 0xb1, 0x01, 0x64, 0xd1,
    0xbc, 0x98, 0xbf, 0x00, 0x83, 0x49, 0x83, 0x42, 0x00, 0x77, 0xf0, 0x43,
    0x71, 0xd8, 0xe0, 0x90, 0x70, 0x66, 0x80, 0x60, 0x0e, 0xf0, 0x5f, 0xfd,
  };
  /* b-bit, e-bit, p-bit, f-bit and marker bit set */
  /* First packet of second frame, handcrafted to also set the e-bit and
   * marker bit in addition to changing the seqnum */
  guint8 inter[] = {
    0x80, 0xf4, 0x00, 0x01, 0x49, 0xb6, 0x02, 0xc0, 0xb1, 0x01, 0x64, 0xd1,
    0xfc, 0x98, 0xc0, 0x00, 0x02, 0x87, 0x01, 0x00, 0x09, 0x3f, 0x1c, 0x12,
    0x0e, 0x0c, 0xd0, 0x1b, 0xa7, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xda, 0x11,
  };

  GstHarness *h = gst_harness_new ("rtpvp9depay");
  gst_harness_set_src_caps_str (h, RTP_VP9_CAPS_STR);

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          intra, sizeof (intra), 0, sizeof (intra), NULL, NULL));
  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          inter, sizeof (inter), 0, sizeof (inter), NULL, NULL));
  fail_unless_equals_int (2, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_depay_non_flexible_mode)
{
  /* b-bit, e-bit and  marker bit set. f-bit NOT set */
  /* First packet of first frame, handcrafted to also set the e-bit and marker
   * bit in addition to changing the seqnum */
  guint8 intra[] = {
    0x80, 0xf4, 0x00, 0x00, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
    0x8c, 0x98, 0xc0, 0x87, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
    0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
  };
  /* b-bit, e-bit, p-bit  and marker bit set. f-bit NOT set */
  /* First packet of second frame, handcrafted to also set the e-bit and
   * marker bit in addition to changing the seqnum */
  guint8 inter[] = {
    0x80, 0xf4, 0x00, 0x01, 0x49, 0x88, 0xe5, 0x38, 0xa0, 0x6c, 0x65, 0x6c,
    0xcc, 0x98, 0xc1, 0x87, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
    0xd0, 0x1b, 0x97, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0x8a, 0x9f, 0x01, 0xbc
  };

  GstHarness *h = gst_harness_new ("rtpvp9depay");
  gst_harness_set_src_caps_str (h, RTP_VP9_CAPS_STR);

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          intra, sizeof (intra), 0, sizeof (intra), NULL, NULL));
  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          inter, sizeof (inter), 0, sizeof (inter), NULL, NULL));
  fail_unless_equals_int (2, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

static guint8 intra_picid6336_seqnum0[] = {
  0x80, 0xf4, 0x00, 0x00, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
  0x8c, 0x98, 0xc0, 0x87, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
  0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
};

static guint8 intra_picid24_seqnum0[] = {
  0x80, 0xf4, 0x00, 0x00, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
  0x8c, 0x18, 0x87, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
  0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
};

static guint8 intra_nopicid_seqnum0[] = {
  0x80, 0xf4, 0x00, 0x00, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
  0x0c, 0x87, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
  0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
};

enum
{
  BT_PLAIN_PICID_NONE,
  BT_PLAIN_PICID_7,
  BT_PLAIN_PICID_15,
  /* Commented out for now, until added VP9 equvivalents.
     BT_TS_PICID_NONE,
     BT_TS_PICID_7,
     BT_TS_PICID_15,
     BT_TS_PICID_7_NO_TLOPICIDX,
     BT_TS_PICID_7_NO_TID_Y_KEYIDX
   */
};

static GstBuffer *
create_rtp_vp9_buffer_full (guint seqnum, guint picid, guint buffer_type,
    GstClockTime buf_pts, gboolean B_bit_start_of_frame, gboolean marker_bit)
{
  static struct BufferTemplate
  {
    guint8 *template;
    gsize size;
    gint picid_bits;
  } templates[] = {
    {
        intra_nopicid_seqnum0, sizeof (intra_nopicid_seqnum0), 0}
    , {
        intra_picid24_seqnum0, sizeof (intra_picid24_seqnum0), 7}
    , {
        intra_picid6336_seqnum0, sizeof (intra_picid6336_seqnum0), 15}
    ,
    /*
       { intra_nopicid_seqnum0_tl1_sync_tl0picidx12,
       sizeof (intra_nopicid_seqnum0_tl1_sync_tl0picidx12),
       0
       },
       { intra_picid24_seqnum0_tl1_sync_tl0picidx12,
       sizeof (intra_picid24_seqnum0_tl1_sync_tl0picidx12),
       7
       },
       { intra_picid6336_seqnum0_tl1_sync_tl0picidx12,
       sizeof (intra_picid6336_seqnum0_tl1_sync_tl0picidx12),
       15
       },
       { intra_picid24_seqnum0_tl1_sync_no_tl0picidx,
       sizeof (intra_picid24_seqnum0_tl1_sync_no_tl0picidx),
       7
       },
       { intra_picid24_seqnum0_notyk_tl0picidx12,
       sizeof (intra_picid24_seqnum0_notyk_tl0picidx12),
       7
       }
     */
  };
  struct BufferTemplate *template = &templates[buffer_type];
  guint8 *packet = g_memdup2 (template->template, template->size);
  GstBuffer *ret;

  packet[2] = (seqnum >> 8) & 0xff;
  packet[3] = (seqnum >> 0) & 0xff;

  /* We're forcing the E-bit (EndOfFrame) together with the RTP marker bit here, which is a bit of a hack.
   * If we're to enable spatial scalability tests, we need to take that into account when setting the E bit.
   */
  if (marker_bit) {
    packet[1] |= 0x80;
    packet[12] |= 0x4;
  } else {
    packet[1] &= ~0x80;
    packet[12] &= ~0x4;
  }

  if (B_bit_start_of_frame)
    packet[12] |= 0x8;
  else
    packet[12] &= ~0x8;

  if (template->picid_bits == 7) {
    /* Prerequisites for this to be correct:
       ((packet[12] & 0x80) == 0x80); I bit set
     */
    g_assert ((packet[12] & 0x80) == 0x80);
    packet[13] = picid & 0x7f;

  } else if (template->picid_bits == 15) {
    /* Prerequisites for this to be correct:
       ((packet[12] & 0x80) == 0x80); I bit set
     */
    g_assert ((packet[12] & 0x80) == 0x80);
    packet[13] = ((picid >> 8) & 0xff) | 0x80;
    packet[14] = (picid >> 0) & 0xff;
  }

  ret = gst_buffer_new_wrapped (packet, template->size);
  GST_BUFFER_PTS (ret) = buf_pts;
  return ret;
}

static GstBuffer *
create_rtp_vp9_buffer (guint seqnum, guint picid, guint buffer_type,
    GstClockTime buf_pts)
{
  return create_rtp_vp9_buffer_full (seqnum, picid, buffer_type, buf_pts, TRUE,
      TRUE);
}

typedef struct _DepayGapEventTestData
{
  gint seq_num;
  gint picid;
  guint buffer_type;
} DepayGapEventTestData;

static void
test_depay_gap_event_base (const DepayGapEventTestData * data,
    gboolean send_lost_event, gboolean expect_gap_event)
{
  GstEvent *event;
  GstClockTime pts = 0;
  GstHarness *h = gst_harness_new ("rtpvp9depay");
  if (send_lost_event == FALSE && expect_gap_event) {
    /* Expect picture ID gaps to be concealed, so tell the element to do so. */
    g_object_set (h->element, "hide-picture-id-gap", TRUE, NULL);
  }
  gst_harness_set_src_caps_str (h, RTP_VP9_CAPS_STR);

  gst_harness_push (h, create_rtp_vp9_buffer (data[0].seq_num, data[0].picid,
          data[0].buffer_type, pts));
  pts += 33 * GST_MSECOND;

  /* Preparation before pushing gap event. Getting rid of all events which
   * came by this point - segment, caps, etc */
  for (gint i = 0; i < 3; i++)
    gst_event_unref (gst_harness_pull_event (h));
  fail_unless_equals_int (gst_harness_events_in_queue (h), 0);

  if (send_lost_event) {
    gst_harness_push_event (h,
        gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
            gst_structure_new ("GstRTPPacketLost", "timestamp", G_TYPE_UINT64,
                pts, "duration", G_TYPE_UINT64, 33 * GST_MSECOND,
                "might-have-been-fec", G_TYPE_BOOLEAN, TRUE, NULL)));
    pts += 33 * GST_MSECOND;
  }

  gst_harness_push (h, create_rtp_vp9_buffer (data[1].seq_num, data[1].picid,
          data[1].buffer_type, pts));
  fail_unless_equals_int (2, gst_harness_buffers_received (h));

  if (expect_gap_event) {
    gboolean noloss = FALSE;

    /* Making sure the GAP event was pushed downstream */
    event = gst_harness_pull_event (h);
    fail_unless_equals_string ("gap",
        gst_event_type_get_name (GST_EVENT_TYPE (event)));
    gst_structure_get_boolean (gst_event_get_structure (event),
        "no-packet-loss", &noloss);

    /* If we didn't send GstRTPPacketLost event, the gap
     * event should indicate that with 'no-packet-loss' parameter */
    fail_unless_equals_int (noloss, !send_lost_event);
    gst_event_unref (event);
  }

  fail_unless_equals_int (gst_harness_events_in_queue (h), 0);

  gst_harness_teardown (h);
}

static const DepayGapEventTestData stop_gap_events_test_data[][2] = {
  /* 7bit picture ids */
  {{100, 24, BT_PLAIN_PICID_7}, {102, 25, BT_PLAIN_PICID_7}},

  /* 15bit picture ids */
  {{100, 250, BT_PLAIN_PICID_15}, {102, 251, BT_PLAIN_PICID_15}},

  /* 7bit picture ids wrap */
  {{100, 127, BT_PLAIN_PICID_7}, {102, 0, BT_PLAIN_PICID_7}},

  /* 15bit picture ids wrap */
  {{100, 32767, BT_PLAIN_PICID_15}, {102, 0, BT_PLAIN_PICID_15}},

  /* 7bit to 15bit picture id */
  {{100, 127, BT_PLAIN_PICID_7}, {102, 128, BT_PLAIN_PICID_15}},
};

GST_START_TEST (test_depay_stop_gap_events)
{
  test_depay_gap_event_base (&stop_gap_events_test_data[__i__][0], TRUE, FALSE);
}

GST_END_TEST;

/* Packet loss + lost picture ids */
static const DepayGapEventTestData resend_gap_event_test_data[][2] = {
  /* 7bit picture ids */
  {{100, 24, BT_PLAIN_PICID_7}, {102, 26, BT_PLAIN_PICID_7}},

  /* 15bit picture ids */
  {{100, 250, BT_PLAIN_PICID_15}, {102, 252, BT_PLAIN_PICID_15}},

  /* 7bit picture ids wrap */
  {{100, 127, BT_PLAIN_PICID_7}, {102, 1, BT_PLAIN_PICID_7}},

  /* 15bit picture ids wrap */
  {{100, 32767, BT_PLAIN_PICID_15}, {102, 1, BT_PLAIN_PICID_15}},

  /* 7bit to 15bit picture id */
  {{100, 126, BT_PLAIN_PICID_7}, {102, 129, BT_PLAIN_PICID_15}},
};

GST_START_TEST (test_depay_resend_gap_event)
{
  test_depay_gap_event_base (&resend_gap_event_test_data[__i__][0], TRUE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_depay_svc_merge_layers)
{
  /* This simulates a simple SVC stream, for simplicity we handcraft a couple
   * of rtp packets. */

  /* First packet contains a complete base layer I-frame (s-bit and e-bit).
   * Note the marker bit is not set to indicate that there will be more
   * packets for this picture. */
  guint8 layer0[] = {
    0x80, 0x74, 0x00, 0x00, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
    0xac, 0x80, 0x01, 0x00, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
    0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
  };
  /* s-bit, e-bit, d-bit and sid=1 set to indicate a complete enhancement
   * frame. marker bit set to indicate last packet of picture. */
  guint8 layer1_with_marker[] = {
    0x80, 0xf4, 0x00, 0x01, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
    0xac, 0x80, 0x01, 0x03, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
    0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
  };

  GstBuffer *buf;
  GstHarness *h = gst_harness_new ("rtpvp9depay");
  gst_harness_set_src_caps_str (h, RTP_VP9_CAPS_STR);

  /* The first packet contains a complete base layer frame that. Since the
   * marker bit is not set, it will wait for an enhancement layer before it
   * pushes it downstream. */
  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          layer0, sizeof (layer0), 0, sizeof (layer0), NULL, NULL));
  fail_unless_equals_int (0, gst_harness_buffers_received (h));

  /* Next packet contains a complete enhancement frame. The picture is
   * complete (marker bit set) and can be pushed */
  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          layer1_with_marker, sizeof (layer1_with_marker), 0,
          sizeof (layer1_with_marker), NULL, NULL));
  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  /* The buffer should contain both layer 0 and layer 1. */
  buf = gst_harness_pull (h);
  fail_unless_equals_int (19 * 2, gst_buffer_get_size (buf));
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;


GST_START_TEST (test_depay_svc_forgive_invalid_sid)
{
  /* This simulates an invalid stream received from FF61 and Chromium 66
   * (Electron). The RTP header signals the same spatial layer ID for all
   * packets of a picture (SID=0), but the s-bit, e-bit and d-bit suggests
   * there is a second layer. The conservative approach would be to drop the
   * enhancement layers since we don't want to push a bitstream we're
   * uncertain of to the decoder. However, this reduces the quality
   * significantly and also sometimes results in an encoder/decoder mismatch
   * (altough it shouldn't). */

  /* The first packet contains a complete base layer frame. Since the
   * marker bit is not set, it will wait for an enhancement layer before it
   * pushes it downstream. s-bit, e-bit set, no marker*/
  guint8 layer0[] = {
    0x80, 0x74, 0x00, 0x00, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
    0xac, 0x80, 0x01, 0x00, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
    0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
  };

  /* Next packet contains a complete enhancement frame. The picture is
   * complete (marker bit set) and picture can be pushed. However, the SID is
   * invalid (SID=0, but should be SID=1). Let's forgive that and push the
   * packet downstream anyway. s-bit, e-bit, d-bit and sid=0 and marker
   * bit. */
  guint8 layer1_with_sid0_and_marker[] = {
    0x80, 0xf4, 0x00, 0x01, 0x49, 0x88, 0xd9, 0xf8, 0xa0, 0x6c, 0x65, 0x6c,
    0xac, 0x80, 0x01, 0x01, 0x01, 0x02, 0x49, 0x3f, 0x1c, 0x12, 0x0e, 0x0c,
    0xd0, 0x1b, 0xb9, 0x80, 0x80, 0xb0, 0x18, 0x0f, 0xa6, 0x4d, 0x01, 0xa5
  };

  GstBuffer *buf;
  GstHarness *h = gst_harness_new ("rtpvp9depay");
  gst_harness_set_src_caps_str (h, RTP_VP9_CAPS_STR);

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          layer0, sizeof (layer0), 0, sizeof (layer0), NULL, NULL));
  fail_unless_equals_int (0, gst_harness_buffers_received (h));

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          layer1_with_sid0_and_marker, sizeof (layer1_with_sid0_and_marker), 0,
          sizeof (layer1_with_sid0_and_marker), NULL, NULL));
  fail_unless_equals_int (1, gst_harness_buffers_received (h));

  /* The buffer should contain both layer 0 and layer 1. */
  buf = gst_harness_pull (h);
  fail_unless_equals_int (19 * 2, gst_buffer_get_size (buf));
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_pay_delta_unit_flag)
{
  guint8 vp9_bitstream_payload[] = {
    0xa2, 0x49, 0x83, 0x42, 0x20, 0x00, 0x1e, 0x00,
    0x1e, 0xc0, 0x07, 0x04, 0x83, 0x83, 0x08, 0x40,
    0x00, 0x06, 0x60, 0x00, 0x00, 0x10, 0xbf, 0xff,
    0x5a, 0x0f, 0xff, 0xff, 0xff, 0xfb, 0xc9, 0x83,
    0xff, 0xff, 0xff, 0xff, 0x34, 0xca, 0x00
  };

  /* set mtu so that the buffer is split into multiple packets */
  GstHarness *h = gst_harness_new_parse ("rtpvp9pay mtu=48");
  GstFlowReturn ret;
  GstBuffer *buffer;

  gst_harness_set_src_caps_str (h, "video/x-vp9");

  buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      vp9_bitstream_payload, sizeof (vp9_bitstream_payload), 0,
      sizeof (vp9_bitstream_payload), NULL, NULL);

  ret = gst_harness_push (h, buffer);
  fail_unless_equals_int (ret, GST_FLOW_OK);

  /* the input buffer should be split into two buffers and pushed as a buffer
   * list, only the first buffer of the first buffer list should be marked as a
   * non-delta unit */
  buffer = gst_harness_pull (h);
  fail_unless (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));
  gst_buffer_unref (buffer);
  buffer = gst_harness_pull (h);
  fail_unless (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT));
  gst_buffer_unref (buffer);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtpvp9_suite (void)
{
  Suite *s = suite_create ("rtpvp9");
  TCase *tc_chain;

  suite_add_tcase (s, (tc_chain = tcase_create ("vp9pay")));
  tcase_add_test (tc_chain, test_pay_delta_unit_flag);

  suite_add_tcase (s, (tc_chain = tcase_create ("vp9depay")));
  tcase_add_test (tc_chain, test_depay_flexible_mode);
  tcase_add_test (tc_chain, test_depay_non_flexible_mode);
  tcase_add_loop_test (tc_chain, test_depay_stop_gap_events, 0,
      G_N_ELEMENTS (stop_gap_events_test_data));
  tcase_add_loop_test (tc_chain, test_depay_resend_gap_event, 0,
      G_N_ELEMENTS (resend_gap_event_test_data));
  tcase_add_test (tc_chain, test_depay_svc_merge_layers);
  tcase_add_test (tc_chain, test_depay_svc_forgive_invalid_sid);

  return s;
}

GST_CHECK_MAIN (rtpvp9);

/* GStreamer
 *
 * Copyright (C) 2015 Pexip AS
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

#include <gst/check/check.h>
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>

#define RTP_H263_CAPS_STR(p)                                            \
  "application/x-rtp,media=video,encoding-name=H263,clock-rate=90000,"  \
  "payload=" G_STRINGIFY(p)

static GstBuffer *
create_rtp_buffer (guint8 * data, gsize size, guint ts, gint seqnum)
{
  GstBuffer *buf = gst_rtp_buffer_new_copy_data (data, size);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  GST_BUFFER_PTS (buf) = (ts) * (GST_SECOND / 30);

  gst_rtp_buffer_map (buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_seq (&rtp, seqnum);
  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

GST_START_TEST (test_h263depay_start_packet_too_small_mode_a)
{
  GstHarness *h = gst_harness_new ("rtph263depay");
  guint8 packet[] = {
    0x80, 0xa2, 0x17, 0x62, 0x57, 0xbb, 0x48, 0x98, 0x4a, 0x59, 0xe8, 0xdc,
    0x00, 0x00, 0x80, 0x00
  };

  gst_harness_set_src_caps_str (h, RTP_H263_CAPS_STR (34));
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (packet, sizeof (packet), 0, 0)));

  /* Packet should be dropped and depayloader not crash */
  fail_unless_equals_int (0, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_h263depay_start_packet_too_small_mode_b)
{
  GstHarness *h = gst_harness_new ("rtph263depay");
  guint8 packet[] = {
    0x80, 0xa2, 0x17, 0x62, 0x57, 0xbb, 0x48, 0x98, 0x4a, 0x59, 0xe8, 0xdc,
    0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  gst_harness_set_src_caps_str (h, RTP_H263_CAPS_STR (34));
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (packet, sizeof (packet), 0, 0)));

  /* Packet should be dropped and depayloader not crash */
  fail_unless_equals_int (0, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_h263depay_start_packet_too_small_mode_c)
{
  GstHarness *h = gst_harness_new ("rtph263depay");
  guint8 packet[] = {
    0x80, 0xa2, 0x17, 0x62, 0x57, 0xbb, 0x48, 0x98, 0x4a, 0x59, 0xe8, 0xdc,
    0xc0, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

  gst_harness_set_src_caps_str (h, RTP_H263_CAPS_STR (34));
  fail_unless_equals_int (GST_FLOW_OK,
      gst_harness_push (h, create_rtp_buffer (packet, sizeof (packet), 0, 0)));

  /* Packet should be dropped and depayloader not crash */
  fail_unless_equals_int (0, gst_harness_buffers_received (h));

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
rtph263_suite (void)
{
  Suite *s = suite_create ("rtph263");
  TCase *tc_chain;

  suite_add_tcase (s, (tc_chain = tcase_create ("h263depay")));
  tcase_add_test (tc_chain, test_h263depay_start_packet_too_small_mode_a);
  tcase_add_test (tc_chain, test_h263depay_start_packet_too_small_mode_b);
  tcase_add_test (tc_chain, test_h263depay_start_packet_too_small_mode_c);

  return s;
}

GST_CHECK_MAIN (rtph263);

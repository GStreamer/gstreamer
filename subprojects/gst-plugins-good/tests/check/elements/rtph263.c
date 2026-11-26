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

#define H263P_RTP_CAPS_STR(p)                                           \
  "application/x-rtp,media=video,encoding-name=H263-1998,clock-rate=90000," \
  "payload="G_STRINGIFY(p)

static gboolean
have_element (const gchar * element_name)
{
  GstElement *element;
  gboolean ret;

  element = gst_element_factory_make (element_name, NULL);
  ret = element != NULL;

  if (element)
    gst_object_unref (element);

  return ret;
}

static GstBuffer *
create_rtp_buffer_full (guint8 * data, gsize size, guint ts, gint seqnum,
    gboolean marker)
{
  GstBuffer *buf = gst_rtp_buffer_new_copy_data (data, size);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  GST_BUFFER_PTS (buf) = gst_util_uint64_scale (ts, GST_SECOND, 90000);

  gst_rtp_buffer_map (buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_seq (&rtp, seqnum);
  gst_rtp_buffer_set_timestamp (&rtp, ts);
  gst_rtp_buffer_set_marker (&rtp, marker);
  gst_rtp_buffer_unmap (&rtp);

  return buf;
}

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

GST_START_TEST (test_h263pay_mode_b_snow)
{
  /* Payloading one large frame (like snow) is more likely to use mode b and
   * trigger issues in valgrind seen previously like double free, invalid read
   * etc. */
  GstHarness *h;
  guint frames = 1;
  guint i;

  if (!have_element ("avenc_h263"))
    return;

  h = gst_harness_new_parse
      ("avenc_h263 rtp-payload-size=1 ! rtph263pay mtu=1350 ");
  gst_harness_add_src_parse (h,
      "videotestsrc pattern=snow is-live=1 ! "
      "capsfilter caps=\"video/x-raw,format=I420,width=176,height=144\"", TRUE);

  for (i = 0; i < frames; i++)
    gst_harness_push_from_src (h);
  fail_unless (gst_harness_buffers_received (h) >= frames);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* gst_rtp_buffer_get_payload() may return a copy of the payload. This test
 * makes sure that the rtph263pdepay also produces the correct output in this
 * case. */
GST_START_TEST (test_h263pdepay_fragmented_memory_non_writable_buffer)
{
  GstHarness *h;
  GstBuffer *header_buf, *payload_buf, *buf;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 header[] = {
    0x04, 0x00
  };
  guint8 payload[] = {
    0x80, 0x02, 0x1c, 0xb8, 0x01, 0x00, 0x11, 0xe0, 0x44, 0xc4
  };
  guint8 frame[] = {
    0x00, 0x00, 0x80, 0x02, 0x1c, 0xb8, 0x01, 0x00, 0x11, 0xe0, 0x44, 0xc4
  };

  h = gst_harness_new ("rtph263pdepay");
  gst_harness_set_src_caps_str (h, "application/x-rtp, media=video, "
      "clock-rate=90000, encoding-name=H263-1998");

  /* Packet with M=1, P=1 */
  header_buf = gst_rtp_buffer_new_allocate (sizeof (header), 0, 0);
  gst_rtp_buffer_map (header_buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_marker (&rtp, TRUE);
  memcpy (gst_rtp_buffer_get_payload (&rtp), header, sizeof (header));
  gst_rtp_buffer_unmap (&rtp);

  payload_buf = gst_buffer_new_allocate (NULL, sizeof (payload), NULL);
  gst_buffer_fill (payload_buf, 0, payload, sizeof (payload));
  buf = gst_buffer_append (header_buf, payload_buf);

  gst_harness_push (h, gst_buffer_ref (buf));
  gst_buffer_unref (buf);

  buf = gst_harness_pull (h);
  fail_unless (gst_buffer_memcmp (buf, 0, frame, sizeof (frame)) == 0);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* gst_rtp_buffer_get_payload() may return a copy of the payload. This test
 * makes sure that the rtph263pdepay also produces the correct output in this
 * case. */
GST_START_TEST
    (test_h263pdepay_fragmented_memory_non_writable_buffer_split_frame) {
  GstHarness *h;
  GstBuffer *header_buf, *payload_buf, *buf;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 header[] = {
    0x04, 0x00
  };
  guint8 payload[] = {
    0x80, 0x02, 0x1c, 0xb8, 0x01, 0x00, 0x11, 0xe0, 0x44, 0xc4
  };
  guint8 frame[] = {
    0x00, 0x00, 0x80, 0x02, 0x1c, 0xb8, 0x01, 0x00, 0x11, 0xe0, 0x44, 0xc4
  };

  h = gst_harness_new ("rtph263pdepay");
  gst_harness_set_src_caps_str (h, "application/x-rtp, media=video, "
      "clock-rate=90000, encoding-name=H263-1998");

  /* First packet, M=0, P=1 */
  header_buf = gst_rtp_buffer_new_allocate (sizeof (header), 0, 0);
  gst_rtp_buffer_map (header_buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_marker (&rtp, FALSE);
  gst_rtp_buffer_set_seq (&rtp, 0);
  memcpy (gst_rtp_buffer_get_payload (&rtp), header, sizeof (header));
  gst_rtp_buffer_unmap (&rtp);

  payload_buf = gst_buffer_new_allocate (NULL, sizeof (payload), NULL);
  gst_buffer_fill (payload_buf, 0, payload, sizeof (payload));
  buf = gst_buffer_append (header_buf, payload_buf);

  gst_harness_push (h, gst_buffer_ref (buf));
  gst_buffer_unref (buf);
  fail_unless_equals_int (gst_harness_buffers_received (h), 0);

  /* Second packet, M=1, P=1 */
  header_buf = gst_rtp_buffer_new_allocate (sizeof (header), 0, 0);
  gst_rtp_buffer_map (header_buf, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_marker (&rtp, TRUE);
  gst_rtp_buffer_set_seq (&rtp, 1);
  memcpy (gst_rtp_buffer_get_payload (&rtp), header, sizeof (header));
  gst_rtp_buffer_unmap (&rtp);

  payload_buf = gst_buffer_new_allocate (NULL, sizeof (payload), NULL);
  gst_buffer_memset (payload_buf, 0, 0, 10);
  buf = gst_buffer_append (header_buf, payload_buf);

  gst_harness_push (h, gst_buffer_ref (buf));
  gst_buffer_unref (buf);
  fail_unless_equals_int (gst_harness_buffers_received (h), 1);

  buf = gst_harness_pull (h);
  fail_unless (gst_buffer_memcmp (buf, 0, frame, sizeof (frame)) == 0);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_h263pdepay_dont_push_empty_frame)
{
  GstHarness *h = gst_harness_new ("rtph263pdepay");

  /* Packet that only contains header information and an extra picture header
   * (PLEN > 0). Partly handcrafted packet. Originally this packet did not
   * have P=1 (hence it was not start of a picture). With both P=1 and M=1 we
   * only need one packet to reproduce the issue where trying to push an empty
   * frame when PLEN is set */
  guint8 packet[] = {
    0x80, 0xe8, 0xbc, 0xaa, 0x14, 0x12, 0x16, 0x5c, 0xb8, 0x4e, 0x39, 0x04,
    0x25, 0x00, 0x54, 0x39, 0xd0, 0x12, 0x06, 0x9e, 0xb5, 0x0a, 0xf5, 0xe8,
    0x32, 0xeb, 0xd0, 0x6b, 0xd6, 0xa2, 0xfa, 0xd4, 0x3d, 0xd7, 0xa0, 0x2b,
    0x24, 0x97, 0xc3, 0xbf, 0xc0, 0xbb, 0xd7, 0xa0,
  };

  gst_harness_set_src_caps_str (h, H263P_RTP_CAPS_STR (100));

  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h,
          create_rtp_buffer (packet, sizeof (packet), 0, 0)));

  fail_unless_equals_int (gst_harness_buffers_received (h), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_h263pdepay_oversized_dropped)
{
  GstHarness *h;
  GstBuffer *buf, *out, *ref;
  guint8 start_packet[] = {
    /* RTP header, seq/timestamp/marker set per packet */
    0x80, 100, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* H263+ payload header, P=1 */ 0x04, 0x00,
    /* picture start code */ 0x80, 0x02, 0x1c, 0xb8, 0x01, 0x00, 0x11, 0xe0,
    0x44, 0xc4
  };
  guint8 last_packet[] = {
    /* RTP header, seq/timestamp/marker set per packet */
    0x80, 100, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* H263+ payload header, P=0 */ 0x00, 0x00, 0xff, 0xff, 0xff, 0xff
  };
  guint8 continuation[516];
  guint8 *ref_data;
  gsize ref_size;
  guint ts;
  guint i;

  /* continuation packet: RTP header plus H263+ payload header (P=0) filled
   * with frame data */
  memset (continuation, 0x5a, sizeof (continuation));
  continuation[0] = 0x80;
  continuation[1] = 100;
  /* SSRC must match the start_packet template (zeroes) or the base class
   * marks the stream discontinuous */
  memset (&continuation[8], 0, 4);
  continuation[12] = 0;
  continuation[13] = 0;

  h = gst_harness_new ("rtph263pdepay");
  gst_harness_set_src_caps_str (h, H263P_RTP_CAPS_STR (100));

  /* a complete frame below the built-in bound depayloads as usual */
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), 0, 0,
      TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_received (h));
  ref = gst_harness_pull (h);

  /* an oversized frame without marker set is dropped once it exceeds the
   * built-in 32 MiB bound; the packet with the marker bit set is dropped as
   * well, as the depayloader waits for a new picture start */
  ts = 90000;
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), ts, 1,
      FALSE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  for (i = 1; i < 67110; i++) {
    buf = create_rtp_buffer_full (continuation, sizeof (continuation), ts,
        (i + 1) & 0xffff, FALSE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  }
  buf = create_rtp_buffer_full (last_packet, sizeof (last_packet), ts,
      67111 & 0xffff, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));

  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  /* the next complete frame is depayloaded intact */
  ts = 180000;
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), ts,
      67112 & 0xffff, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));

  out = gst_harness_pull (h);
  fail_unless_equals_int (gst_buffer_get_size (out), gst_buffer_get_size (ref));
  ref_size = gst_buffer_get_size (ref);
  ref_data = g_malloc (ref_size);
  gst_buffer_extract (ref, 0, ref_data, ref_size);
  fail_unless (gst_buffer_memcmp (out, 0, ref_data, ref_size) == 0);
  g_free (ref_data);
  gst_buffer_unref (out);
  gst_buffer_unref (ref);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* The built-in 32 MiB bound is not settable; a frame is built to land on it
 * exactly: the start packet contributes its whole payload (its two-byte
 * picture header is reused as frame data), a continuation contributes its
 * payload less that header. With a 804932 byte start payload and a 500 byte
 * continuation contribution, 804932 + 65499 * 500 is exactly 32 MiB, so the
 * last checked fragment sits on the bound and no more. A frame that merely
 * fills the bound must still be depayloaded (the checks are strict), which
 * pins the boundary against an accidental >= comparison. Every checked
 * frame length is a multiple of four and the marker packet adds an odd
 * payload length, so the depayloader's padding is exactly four bytes and
 * the measured sizes stay exact. */
#define H263P_START_PAYLOAD 804932
#define H263P_CONTRIB 500
#define H263P_NCON 65499
GST_START_TEST (test_h263pdepay_at_limit)
{
  GstHarness *h;
  GstBuffer *buf, *out;
  static const guint8 psc[] = { 0x80, 0x02, 0x1c, 0xb8, 0x01, 0x00, 0x11,
    0xe0, 0x44, 0xc4
  };
  /* start packet: RTP header, H263+ payload header with P=1, picture start
   * code, then filler; its whole payload enters the assembly */
  guint8 start_packet[12 + H263P_START_PAYLOAD];
  /* continuation packet: RTP header plus H263+ payload header (P=0) whose
   * 502 byte payload contributes 500 bytes of frame data. SSRC matches the
   * start packet template */
  guint8 continuation[12 + 2 + H263P_CONTRIB];
  gsize single, extended, contrib;
  guint i;

  memset (start_packet, 0x5a, sizeof (start_packet));
  start_packet[0] = 0x80;
  start_packet[1] = 100;
  memset (&start_packet[8], 0, 4);
  start_packet[12] = 0x04;
  start_packet[13] = 0x00;
  memcpy (&start_packet[14], psc, sizeof (psc));

  memset (continuation, 0x5a, sizeof (continuation));
  continuation[0] = 0x80;
  continuation[1] = 100;
  memset (&continuation[8], 0, 4);
  continuation[12] = 0;
  continuation[13] = 0;

  h = gst_harness_new ("rtph263pdepay");
  gst_harness_set_src_caps_str (h, H263P_RTP_CAPS_STR (100));

  /* a one packet frame reveals what a start packet contributes */
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), 0, 0,
      TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  out = gst_harness_pull (h);
  single = gst_buffer_get_size (out);
  gst_buffer_unref (out);

  /* the frame is the payload plus the four bytes of padding; the payload
   * length is even, so the padding is exactly four bytes */
  fail_unless_equals_int (H263P_START_PAYLOAD + 4, single);

  /* start plus one continuation reveals what a continuation contributes */
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), 90000, 1,
      FALSE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  buf = create_rtp_buffer_full (continuation, sizeof (continuation), 90000, 2,
      TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  out = gst_harness_pull (h);
  extended = gst_buffer_get_size (out);
  gst_buffer_unref (out);
  contrib = extended - single;
  fail_unless_equals_int (sizeof (continuation) - 12 - 2, contrib);
  /* the padded one-packet frame is four bytes longer than what the start
   * packet contributed; what remains to the bound is a multiple of the
   * continuation contribution, so a frame can land on the bound exactly */
  fail_unless_equals_int (0, (32 * 1024 * 1024 - (single - 4)) % contrib);

  /* the last of H263P_NCON continuations leaves exactly 32 MiB in the
   * assembly: that checked fragment sits on the bound and no more, which a
   * strict > must let through, and the frame is still depayloaded, closed
   * by one more continuation with the marker */
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), 180000,
      3, FALSE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  for (i = 0; i < H263P_NCON; i++) {
    buf = create_rtp_buffer_full (continuation, sizeof (continuation), 180000,
        (4 + i) & 0xffff, FALSE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  }
  buf = create_rtp_buffer_full (continuation, sizeof (continuation), 180000,
      (4 + H263P_NCON) & 0xffff, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  out = gst_harness_pull (h);
  gst_buffer_unref (out);

  /* one continuation more than the bound reaches the check: the frame is
   * dropped and a following whole frame depayloads */
  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), 270000,
      (5 + H263P_NCON) & 0xffff, FALSE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  for (i = 0; i < H263P_NCON + 1; i++) {
    buf = create_rtp_buffer_full (continuation, sizeof (continuation), 270000,
        (6 + H263P_NCON + i) & 0xffff, FALSE);
    fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  }
  buf = create_rtp_buffer_full (continuation, sizeof (continuation), 270000,
      (7 + 2 * H263P_NCON) & 0xffff, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (0, gst_harness_buffers_in_queue (h));

  buf = create_rtp_buffer_full (start_packet, sizeof (start_packet), 360000,
      (8 + 2 * H263P_NCON) & 0xffff, TRUE);
  fail_unless_equals_int (GST_FLOW_OK, gst_harness_push (h, buf));
  fail_unless_equals_int (1, gst_harness_buffers_in_queue (h));
  out = gst_harness_pull (h);
  gst_buffer_unref (out);

  gst_harness_teardown (h);
}

GST_END_TEST;


GST_START_TEST (test_h263ppay_non_fixed_caps)
{
  GstHarness *h;
  guint8 frame[] = {
    0x00, 0x00, 0x80, 0x06, 0x1c, 0xa8, 0x01, 0x04, 0x91, 0xe0, 0x37, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  };

  h = gst_harness_new_parse ("rtph263ppay");

  /* Set non-fixed caps after payloader */
  gst_harness_set_caps_str (h, "video/x-h263, variant=(string)itu",
      "application/x-rtp, clock-rate=[1, MAX]");

  gst_harness_push (h, gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          frame, sizeof (frame), 0, sizeof (frame), NULL, NULL));

  fail_unless_equals_int (1, gst_harness_buffers_received (h));

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

  suite_add_tcase (s, (tc_chain = tcase_create ("h263pay")));
  tcase_add_test (tc_chain, test_h263pay_mode_b_snow);

  suite_add_tcase (s, (tc_chain = tcase_create ("h263pdepay")));
  tcase_add_test (tc_chain,
      test_h263pdepay_fragmented_memory_non_writable_buffer);
  tcase_add_test (tc_chain,
      test_h263pdepay_fragmented_memory_non_writable_buffer_split_frame);
  tcase_add_test (tc_chain, test_h263pdepay_dont_push_empty_frame);
  tcase_add_test (tc_chain, test_h263pdepay_oversized_dropped);
  tcase_add_test (tc_chain, test_h263pdepay_at_limit);

  suite_add_tcase (s, (tc_chain = tcase_create ("h263ppay")));
  tcase_add_test (tc_chain, test_h263ppay_non_fixed_caps);

  return s;
}

GST_CHECK_MAIN (rtph263);

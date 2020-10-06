/* GStreamer
 * Copyright (C) <2020> Mathieu Duponchelle <mathieu@centricular.com>
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
#include <gst/base/base.h>

typedef struct
{
  guint16 seq;
  guint16 len;
  guint8 E;
  guint8 pt;
  guint32 mask;
  guint32 timestamp;
  guint8 N;
  guint8 D;
  guint8 type;
  guint8 index;
  guint8 offset;
  guint8 NA;
  guint8 seq_ext;
  guint8 *payload;
  guint payload_len;
} Rtp2DFecHeader;

static void
parse_header (Rtp2DFecHeader * fec, guint8 * data, guint len)
{
  GstBitReader bits;

  fail_unless (len >= 16);

  gst_bit_reader_init (&bits, data, len);

  fec->seq = gst_bit_reader_get_bits_uint16_unchecked (&bits, 16);
  fec->len = gst_bit_reader_get_bits_uint16_unchecked (&bits, 16);
  fec->E = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);
  fec->pt = gst_bit_reader_get_bits_uint8_unchecked (&bits, 7);
  fec->mask = gst_bit_reader_get_bits_uint32_unchecked (&bits, 24);
  fec->timestamp = gst_bit_reader_get_bits_uint32_unchecked (&bits, 32);
  fec->N = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);
  fec->D = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);
  fec->type = gst_bit_reader_get_bits_uint8_unchecked (&bits, 3);
  fec->index = gst_bit_reader_get_bits_uint8_unchecked (&bits, 3);
  fec->offset = gst_bit_reader_get_bits_uint8_unchecked (&bits, 8);
  fec->NA = gst_bit_reader_get_bits_uint8_unchecked (&bits, 8);
  fec->seq_ext = gst_bit_reader_get_bits_uint8_unchecked (&bits, 8);
  fec->payload = data + 16;
  fec->payload_len = len - 16;
}

static GstBuffer *
make_media_sample (guint16 seq, guint32 ts, guint8 * payload, guint payload_len)
{
  GstBuffer *ret;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *data;

  ret = gst_rtp_buffer_new_allocate (payload_len, 0, 0);

  gst_rtp_buffer_map (ret, GST_MAP_WRITE, &rtp);
  gst_rtp_buffer_set_payload_type (&rtp, 33);
  gst_rtp_buffer_set_seq (&rtp, seq);
  gst_rtp_buffer_set_timestamp (&rtp, ts);
  data = gst_rtp_buffer_get_payload (&rtp);
  memcpy (data, payload, payload_len);
  gst_rtp_buffer_unmap (&rtp);

  return ret;
}

static void
pull_and_check (GstHarness * h, guint n_packets, guint16 seq,
    guint length_recovery, guint8 pt_recovery, guint32 ts_recovery,
    gboolean row, guint8 offset, guint8 NA, guint8 * payload, guint payload_len)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *buffer;
  Rtp2DFecHeader fec;

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), n_packets);
  buffer = gst_harness_pull (h);
  fail_unless (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp));

  parse_header (&fec, gst_rtp_buffer_get_payload (&rtp),
      gst_rtp_buffer_get_payload_len (&rtp));

  fail_unless_equals_int (fec.seq, seq);
  fail_unless_equals_int (fec.pt, pt_recovery);
  fail_unless_equals_int (fec.timestamp, ts_recovery);
  fail_unless_equals_int (fec.D, row ? 1 : 0);
  fail_unless_equals_int (fec.offset, offset);
  fail_unless_equals_int (fec.NA, NA);
  fail_unless_equals_int (fec.payload_len, payload_len);
  fail_unless_equals_int (memcmp (fec.payload, payload, fec.payload_len), 0);

  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (buffer);
}

GST_START_TEST (test_row)
{
  GstHarness *h, *h_fec_1;
  guint8 payload;
  GstElement *enc = gst_element_factory_make ("rtpst2022-1-fecenc", NULL);

  g_object_set (enc, "columns", 3, "enable-column-fec", FALSE, NULL);
  h = gst_harness_new_with_element (enc, "sink", "src");
  h_fec_1 = gst_harness_new_with_element (h->element, NULL, "fec_1");

  gst_harness_set_src_caps_str (h, "application/x-rtp");

  payload = 0x37;
  gst_harness_push (h, make_media_sample (0, 0, &payload, 1));
  payload = 0x28;
  gst_harness_push (h, make_media_sample (1, 0, &payload, 1));
  payload = 0xff;
  gst_harness_push (h, make_media_sample (2, 0, &payload, 1));

  payload = 0x37 ^ 0x28 ^ 0xff;
  pull_and_check (h_fec_1, 1, 0, 1, 33, 0, TRUE, 1, 3, &payload, 1);

  gst_object_unref (enc);
  gst_harness_teardown (h);
  gst_harness_teardown (h_fec_1);
}

GST_END_TEST;

GST_START_TEST (test_columns)
{
  GstHarness *h, *h_fec_0;
  guint8 payload;
  GstElement *enc = gst_element_factory_make ("rtpst2022-1-fecenc", NULL);

  g_object_set (enc, "columns", 3, "rows", 3, "enable-row-fec", FALSE, NULL);
  h = gst_harness_new_with_element (enc, "sink", "src");
  h_fec_0 = gst_harness_new_with_element (h->element, NULL, "fec_0");

  gst_harness_set_src_caps_str (h, "application/x-rtp");

  payload = 0x37;
  gst_harness_push (h, make_media_sample (0, 0, &payload, 1));
  payload = 0x28;
  gst_harness_push (h, make_media_sample (1, 0, &payload, 1));
  payload = 0xff;
  gst_harness_push (h, make_media_sample (2, 0, &payload, 1));
  payload = 0xde;
  gst_harness_push (h, make_media_sample (3, 0, &payload, 1));
  payload = 0xad;
  gst_harness_push (h, make_media_sample (4, 0, &payload, 1));
  payload = 0xbe;
  gst_harness_push (h, make_media_sample (5, 0, &payload, 1));
  payload = 0xef;
  gst_harness_push (h, make_media_sample (6, 0, &payload, 1));
  payload = 0x58;
  gst_harness_push (h, make_media_sample (7, 0, &payload, 1));
  payload = 0x92;
  gst_harness_push (h, make_media_sample (8, 0, &payload, 1));

  /* Let's check distribution of the column FEC over the repair window
   * We should receive column FEC packets upon pushing buffers with
   * seqnums 9, 12 and 15
   */

  /* At this point no column FEC should have been put out */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h_fec_0), 0);

  /* Now push the first buffer in the second 3 x 3 grid, it's at
   * this point we expect to receive our first column FEC packet
   */
  gst_harness_push (h, make_media_sample (9, 0, &payload, 1));
  payload = 0x37 ^ 0xde ^ 0xef;
  pull_and_check (h_fec_0, 1, 0, 1, 33, 0, FALSE, 3, 3, &payload, 1);

  gst_harness_push (h, make_media_sample (10, 0, &payload, 1));
  gst_harness_push (h, make_media_sample (11, 0, &payload, 1));
  fail_unless_equals_int (gst_harness_buffers_in_queue (h_fec_0), 0);
  gst_harness_push (h, make_media_sample (12, 0, &payload, 1));
  payload = 0x28 ^ 0xad ^ 0x58;
  pull_and_check (h_fec_0, 1, 1, 1, 33, 0, FALSE, 3, 3, &payload, 1);

  gst_harness_push (h, make_media_sample (13, 0, &payload, 1));
  gst_harness_push (h, make_media_sample (14, 0, &payload, 1));
  fail_unless_equals_int (gst_harness_buffers_in_queue (h_fec_0), 0);
  gst_harness_push (h, make_media_sample (15, 0, &payload, 1));
  payload = 0xff ^ 0xbe ^ 0x92;
  pull_and_check (h_fec_0, 1, 2, 1, 33, 0, FALSE, 3, 3, &payload, 1);

  gst_object_unref (enc);
  gst_harness_teardown (h);
  gst_harness_teardown (h_fec_0);
}

GST_END_TEST;

static Suite *
st2022_1_dec_suite (void)
{
  Suite *s = suite_create ("rtpst2022-1-fecdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_row);
  tcase_add_test (tc_chain, test_columns);

  return s;
}

GST_CHECK_MAIN (st2022_1_dec)

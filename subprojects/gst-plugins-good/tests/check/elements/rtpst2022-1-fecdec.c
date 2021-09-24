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

static GstBuffer *
make_fec_sample (guint16 seq, guint32 ts, guint16 seq_base, gboolean row,
    guint8 offset, guint8 NA, guint32 ts_recovery, guint8 * fec_payload,
    guint fec_payload_len, guint16 length_recovery)
{
  GstBuffer *ret;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBitWriter bits;
  guint8 *data;

  ret = gst_rtp_buffer_new_allocate (16 + fec_payload_len, 0, 0);

  fail_unless (gst_rtp_buffer_map (ret, GST_MAP_WRITE, &rtp));

  data = gst_rtp_buffer_get_payload (&rtp);
  memset (data, 0x00, 16);

  gst_bit_writer_init_with_data (&bits, data, 17, FALSE);

  gst_bit_writer_put_bits_uint16 (&bits, seq_base, 16); /* SNBase low bits */
  gst_bit_writer_put_bits_uint16 (&bits, length_recovery, 16);  /* Length Recovery */
  gst_bit_writer_put_bits_uint8 (&bits, 1, 1);  /* E */
  gst_bit_writer_put_bits_uint8 (&bits, 0x21, 7);       /* PT recovery */
  gst_bit_writer_put_bits_uint32 (&bits, 0, 24);        /* Mask */
  gst_bit_writer_put_bits_uint32 (&bits, ts_recovery, 32);      /* TS recovery */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 1);  /* N */
  gst_bit_writer_put_bits_uint8 (&bits, row ? 1 : 0, 1);        /* D */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 3);  /* type */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 3);  /* index */
  gst_bit_writer_put_bits_uint8 (&bits, offset, 8);     /* Offset */
  gst_bit_writer_put_bits_uint8 (&bits, NA, 8); /* NA */
  gst_bit_writer_put_bits_uint8 (&bits, 0, 8);  /* SNBase ext bits */

  memcpy (data + 16, fec_payload, fec_payload_len);

  gst_bit_writer_reset (&bits);

  GST_MEMDUMP ("fec", data, 16 + fec_payload_len);

  gst_rtp_buffer_set_payload_type (&rtp, 96);
  gst_rtp_buffer_set_seq (&rtp, seq);
  gst_rtp_buffer_set_timestamp (&rtp, ts);
  gst_rtp_buffer_unmap (&rtp);

  return ret;
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
pull_and_check (GstHarness * h, guint16 seq, guint32 ts, guint8 * payload,
    guint payload_len, guint n_in_queue)
{
  GstBuffer *buffer;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *data;
  guint i;

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), n_in_queue);
  buffer = gst_harness_pull (h);

  fail_unless (gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp));

  fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp), seq);
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&rtp), ts);
  fail_unless_equals_int (gst_rtp_buffer_get_payload_type (&rtp), 33);
  fail_unless_equals_int (gst_rtp_buffer_get_payload_len (&rtp), payload_len);
  data = gst_rtp_buffer_get_payload (&rtp);

  for (i = 0; i < payload_len; i++)
    fail_unless_equals_int (data[i], payload[i]);

  gst_rtp_buffer_unmap (&rtp);

  gst_buffer_unref (buffer);
}

/**
 * +--------------+
 * | 9  | 10 |  x | l1
 * | 12 | 13 |  x | l2
 * | x  | x  |  x |
 * +--------------+
 *   x    x     x
 *
 * Missing values:
 * 11: 0xc5
 * 14: 0xb8
 */
GST_START_TEST (test_row)
{
  guint8 payload;
  GstHarness *h =
      gst_harness_new_with_padnames ("rtpst2022-1-fecdec", NULL, "src");
  GstHarness *h0 = gst_harness_new_with_element (h->element, "sink", NULL);
  GstHarness *h_fec_1 =
      gst_harness_new_with_element (h->element, "fec_1", NULL);

  gst_harness_set_src_caps_str (h0, "application/x-rtp");
  gst_harness_set_src_caps_str (h_fec_1, "application/x-rtp");

  payload = 0x37;
  gst_harness_push (h0, make_media_sample (9, 0, &payload, 1));
  payload = 0x28;
  gst_harness_push (h0, make_media_sample (10, 0, &payload, 1));
  payload = 0xff;
  gst_harness_push (h0, make_media_sample (12, 0, &payload, 1));

  /* We receive 9, 10 and 12 */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 3);
  while (gst_harness_buffers_in_queue (h)) {
    gst_buffer_unref (gst_harness_pull (h));
  }

  payload = 0xda;
  gst_harness_push (h_fec_1, make_fec_sample (0, 0, 9, TRUE, 1, 3, 0, &payload,
          1, 1));

  /* After pushing l1, we should have enough info to reconstruct 11 */
  payload = 0xc5;
  pull_and_check (h, 11, 0, &payload, 1, 1);

  /* Now we try to push l2 before 13, to verify that 14 is eventually
   * reconstructed once 13 is pushed */
  payload = 0x02;
  gst_harness_push (h_fec_1, make_fec_sample (1, 0, 12, TRUE, 1, 3, 0, &payload,
          1, 1));
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);
  payload = 0x45;
  gst_harness_push (h0, make_media_sample (13, 0, &payload, 1));
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 2);
  payload = 0xb8;
  pull_and_check (h, 14, 0, &payload, 1, 2);
  payload = 0x45;
  pull_and_check (h, 13, 0, &payload, 1, 1);

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h_fec_1);
}

GST_END_TEST;

/**
 * +--------------+
 * | 7  | 8  |  x | x
 * | 10 | 11 |  x | x
 * | x  | x  |  x |
 * +--------------+
 *   d1   d2    x
 *
 * Missing values:
 * 13: 0xc5
 * 14: 0x51
 */
GST_START_TEST (test_column)
{
  guint8 payload;
  GstHarness *h =
      gst_harness_new_with_padnames ("rtpst2022-1-fecdec", NULL, "src");
  GstHarness *h0 = gst_harness_new_with_element (h->element, "sink", NULL);
  GstHarness *h_fec_0 =
      gst_harness_new_with_element (h->element, "fec_0", NULL);

  gst_harness_set_src_caps_str (h0, "application/x-rtp");
  gst_harness_set_src_caps_str (h_fec_0, "application/x-rtp");

  payload = 0x37;
  gst_harness_push (h0, make_media_sample (7, 0, &payload, 1));
  payload = 0x28;
  gst_harness_push (h0, make_media_sample (10, 0, &payload, 1));

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 2);
  while (gst_harness_buffers_in_queue (h))
    gst_buffer_unref (gst_harness_pull (h));

  payload = 0xda;
  gst_harness_push (h_fec_0, make_fec_sample (0, 0, 7, FALSE, 3, 3, 0, &payload,
          1, 1));

  /* After pushing d1, we should have enough info to reconstruct 13 */
  payload = 0xc5;
  pull_and_check (h, 13, 0, &payload, 1, 1);

  /* Now we try to push d2 before 8 and 11, to verify that 14 is eventually
   * reconstructed once 11 is pushed */
  payload = 0x04;
  gst_harness_push (h_fec_0, make_fec_sample (1, 0, 8, FALSE, 3, 3, 0, &payload,
          1, 1));
  payload = 0x21;
  gst_harness_push (h0, make_media_sample (8, 0, &payload, 1));

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  while (gst_harness_buffers_in_queue (h))
    gst_buffer_unref (gst_harness_pull (h));

  payload = 0x74;
  gst_harness_push (h0, make_media_sample (11, 0, &payload, 1));
  payload = 0x51;
  pull_and_check (h, 14, 0, &payload, 1, 2);
  payload = 0x74;
  pull_and_check (h, 11, 0, &payload, 1, 1);

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h_fec_0);
}

GST_END_TEST;


/*
 * +-----------+
 * | 0 | 1 | x | x
 * | 3 | 4 | x | l1
 * | 6 | x | x | l2
 * +-----------+
 *   d0  d1  d2
 *
 * We should be able to retrieve 2 by retrieving 5 7 and 8 first.
 *
 * Missing values:
 * 2: 0xfc
 * 5: 0x3a
 * 7: 0x5f
 * 8: 0x21
 */

GST_START_TEST (test_2d)
{
  guint8 payload;
  GstHarness *h =
      gst_harness_new_with_padnames ("rtpst2022-1-fecdec", NULL, "src");
  GstHarness *h0 = gst_harness_new_with_element (h->element, "sink", NULL);
  GstHarness *h_fec_0 =
      gst_harness_new_with_element (h->element, "fec_0", NULL);
  GstHarness *h_fec_1 =
      gst_harness_new_with_element (h->element, "fec_1", NULL);

  gst_harness_set_src_caps_str (h0, "application/x-rtp");
  gst_harness_set_src_caps_str (h_fec_0, "application/x-rtp");
  gst_harness_set_src_caps_str (h_fec_1, "application/x-rtp");

  payload = 0xde;
  gst_harness_push (h0, make_media_sample (0, 0, &payload, 1));
  payload = 0xad;
  gst_harness_push (h0, make_media_sample (1, 0, &payload, 1));
  payload = 0xbe;
  gst_harness_push (h0, make_media_sample (3, 0, &payload, 1));
  payload = 0xef;
  gst_harness_push (h0, make_media_sample (4, 0, &payload, 1));
  payload = 0x42;
  gst_harness_push (h0, make_media_sample (6, 0, &payload, 1));

  /* row FEC */
  /* l1 0xbe ^ 0xef ^ 0x3a */
  payload = 0x6b;
  gst_harness_push (h_fec_1, make_fec_sample (0, 0, 3, TRUE, 1, 3, 0, &payload,
          1, 1));
  /* l2 0x42 ^ 0x5f ^ 0x21 */
  payload = 0x3c;
  gst_harness_push (h_fec_1, make_fec_sample (0, 0, 6, TRUE, 1, 3, 0, &payload,
          1, 1));

  /* column FEC */
  /* d0 0xde ^ 0xbe ^ 0x42 */
  payload = 0x22;
  gst_harness_push (h_fec_0, make_fec_sample (0, 0, 0, FALSE, 3, 3, 0, &payload,
          1, 1));
  /* d1 0xad ^ 0xef ^ 0x5f */
  payload = 0x1d;
  gst_harness_push (h_fec_0, make_fec_sample (1, 0, 1, FALSE, 3, 3, 0, &payload,
          1, 1));
  /* d2 0xfc ^ 0x3a ^ 0x21 */
  payload = 0xe7;
  gst_harness_push (h_fec_0, make_fec_sample (2, 0, 2, FALSE, 3, 3, 0, &payload,
          1, 1));

  /* We should retrieve all 9 packets despite dropping 4! */
  payload = 0xde;
  pull_and_check (h, 0, 0, &payload, 1, 9);
  payload = 0xad;
  pull_and_check (h, 1, 0, &payload, 1, 8);
  payload = 0xbe;
  pull_and_check (h, 3, 0, &payload, 1, 7);
  payload = 0xef;
  pull_and_check (h, 4, 0, &payload, 1, 6);
  payload = 0x42;
  pull_and_check (h, 6, 0, &payload, 1, 5);
  payload = 0x3a;
  pull_and_check (h, 5, 0, &payload, 1, 4);
  payload = 0x21;
  pull_and_check (h, 8, 0, &payload, 1, 3);
  payload = 0x5f;
  pull_and_check (h, 7, 0, &payload, 1, 2);
  payload = 0xfc;
  pull_and_check (h, 2, 0, &payload, 1, 1);

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h_fec_0);
  gst_harness_teardown (h_fec_1);
}

GST_END_TEST;

static void
_xor_mem (guint8 * restrict dst, const guint8 * restrict src, gsize length)
{
  guint i;

  for (i = 0; i < (length / sizeof (guint64)); ++i) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    GST_WRITE_UINT64_LE (dst,
        GST_READ_UINT64_LE (dst) ^ GST_READ_UINT64_LE (src));
#else
    GST_WRITE_UINT64_BE (dst,
        GST_READ_UINT64_BE (dst) ^ GST_READ_UINT64_BE (src));
#endif
    dst += sizeof (guint64);
    src += sizeof (guint64);
  }
  for (i = 0; i < (length % sizeof (guint64)); ++i)
    dst[i] ^= src[i];
}

/**
 * +-----------------+
 * | 0-1 | 1-3 | x-4 | l1
 * +-----------------+
 *   x    x     x
 *
 * Missing values:
 * 2: 0xc5b74108
 */
GST_START_TEST (test_variable_length)
{
  guint8 payload[4];
  guint8 fec_payload[4];
  GstHarness *h =
      gst_harness_new_with_padnames ("rtpst2022-1-fecdec", NULL, "src");
  GstHarness *h0 = gst_harness_new_with_element (h->element, "sink", NULL);
  GstHarness *h_fec_1 =
      gst_harness_new_with_element (h->element, "fec_1", NULL);

  gst_harness_set_src_caps_str (h0, "application/x-rtp");
  gst_harness_set_src_caps_str (h_fec_1, "application/x-rtp");

  memset (fec_payload, 0x00, 4);

  payload[0] = 0x37;
  _xor_mem (fec_payload, payload, 1);
  gst_harness_push (h0, make_media_sample (0, 0, payload, 1));

  payload[0] = 0x28;
  payload[1] = 0x39;
  payload[2] = 0x56;
  _xor_mem (fec_payload, payload, 3);
  gst_harness_push (h0, make_media_sample (1, 0, payload, 3));

  /* We receive 0 and 1 */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 2);
  while (gst_harness_buffers_in_queue (h)) {
    gst_buffer_unref (gst_harness_pull (h));
  }

  payload[0] = 0xc5;
  payload[1] = 0xb7;
  payload[2] = 0x41;
  payload[3] = 0x08;

  _xor_mem (fec_payload, payload, 4);
  gst_harness_push (h_fec_1, make_fec_sample (0, 0, 0, TRUE, 1, 3, 0,
          fec_payload, 4, 1 ^ 3 ^ 4));

  pull_and_check (h, 2, 0, payload, 4, 1);

  gst_harness_teardown (h);
  gst_harness_teardown (h0);
  gst_harness_teardown (h_fec_1);
}

GST_END_TEST;


static Suite *
st2022_1_dec_suite (void)
{
  Suite *s = suite_create ("rtpst2022-1-fecdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_row);
  tcase_add_test (tc_chain, test_column);
  tcase_add_test (tc_chain, test_2d);
  tcase_add_test (tc_chain, test_variable_length);

  return s;
}

GST_CHECK_MAIN (st2022_1_dec)

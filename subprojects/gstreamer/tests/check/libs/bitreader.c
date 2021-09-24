/* GStreamer
 *
 * unit test for GstBitReader
 *
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstbitreader.h>

#ifndef fail_unless_equals_int64
#define fail_unless_equals_int64(a, b)					\
G_STMT_START {								\
  gint64 first = a;							\
  gint64 second = b;							\
  fail_unless(first == second,						\
    "'" #a "' (%" G_GINT64_FORMAT ") is not equal to '" #b"' (%"	\
    G_GINT64_FORMAT ")", first, second);				\
} G_STMT_END;
#endif

GST_START_TEST (test_initialization)
{
  guint8 data[] = { 0x01, 0x02, 0x03, 0x04 };
  GstBuffer *buffer = gst_buffer_new ();
  GstBitReader reader = GST_BIT_READER_INIT (data, 4);
  GstBitReader *reader2;
  guint8 x = 0;
  GstMapInfo info;

  gst_buffer_insert_memory (buffer, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY, data, 4, 0, 4, NULL,
          NULL));

  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x02);

  memset (&reader, 0, sizeof (GstBitReader));

  gst_bit_reader_init (&reader, data, 4);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x02);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  gst_bit_reader_init (&reader, info.data, info.size);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &x, 8));
  fail_unless_equals_int (x, 0x02);
  gst_buffer_unmap (buffer, &info);

  reader2 = gst_bit_reader_new (data, 4);
  fail_unless (gst_bit_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_bit_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x02);
  gst_bit_reader_free (reader2);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  reader2 = gst_bit_reader_new (info.data, info.size);
  fail_unless (gst_bit_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_bit_reader_get_bits_uint8 (reader2, &x, 8));
  fail_unless_equals_int (x, 0x02);
  gst_bit_reader_free (reader2);
  gst_buffer_unmap (buffer, &info);

  gst_buffer_unref (buffer);
}

GST_END_TEST;

#define GET_CHECK(reader, dest, bits, nbits, val) { \
  fail_unless (gst_bit_reader_get_bits_uint##bits (reader, &dest, nbits)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define PEEK_CHECK(reader, dest, bits, nbits, val) { \
  fail_unless (gst_bit_reader_peek_bits_uint##bits (reader, &dest, nbits)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define GET_CHECK_FAIL(reader, dest, bits, nbits) { \
  fail_if (gst_bit_reader_get_bits_uint##bits (reader, &dest, nbits)); \
}

#define PEEK_CHECK_FAIL(reader, dest, bits, nbits) { \
  fail_if (gst_bit_reader_peek_bits_uint##bits (reader, &dest, nbits)); \
}

GST_START_TEST (test_get_bits)
{
  guint8 data[] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21
  };
  GstBitReader reader = GST_BIT_READER_INIT (data, 16);
  guint8 a = 0;
  guint16 b = 0;
  guint32 c = 0;
  guint64 d = 0;

  /* 8 bit */
  GET_CHECK (&reader, a, 8, 8, 0x12);
  GET_CHECK (&reader, a, 8, 4, 0x03);
  GET_CHECK (&reader, a, 8, 4, 0x04);
  GET_CHECK (&reader, a, 8, 3, 0x02);
  GET_CHECK (&reader, a, 8, 1, 0x01);
  GET_CHECK (&reader, a, 8, 2, 0x01);
  GET_CHECK (&reader, a, 8, 2, 0x02);

  PEEK_CHECK (&reader, a, 8, 8, 0x78);
  PEEK_CHECK (&reader, a, 8, 8, 0x78);
  fail_unless (gst_bit_reader_skip (&reader, 8));

  PEEK_CHECK (&reader, a, 8, 8, 0x90);
  GET_CHECK (&reader, a, 8, 1, 0x01);
  GET_CHECK (&reader, a, 8, 1, 0x00);
  GET_CHECK (&reader, a, 8, 1, 0x00);
  GET_CHECK (&reader, a, 8, 1, 0x01);
  fail_unless (gst_bit_reader_skip (&reader, 4));

  fail_unless (gst_bit_reader_skip (&reader, 10 * 8));
  GET_CHECK (&reader, a, 8, 8, 0x21);
  GET_CHECK_FAIL (&reader, a, 8, 1);
  PEEK_CHECK_FAIL (&reader, a, 8, 1);

  /* 16 bit */
  gst_bit_reader_init (&reader, data, 16);

  GET_CHECK (&reader, b, 16, 16, 0x1234);
  PEEK_CHECK (&reader, b, 16, 13, 0x0acf);
  GET_CHECK (&reader, b, 16, 8, 0x56);
  GET_CHECK (&reader, b, 16, 4, 0x07);
  GET_CHECK (&reader, b, 16, 2, 0x02);
  GET_CHECK (&reader, b, 16, 2, 0x00);
  PEEK_CHECK (&reader, b, 16, 8, 0x90);
  fail_unless (gst_bit_reader_skip (&reader, 11 * 8));
  GET_CHECK (&reader, b, 16, 8, 0x21);
  GET_CHECK_FAIL (&reader, b, 16, 16);
  PEEK_CHECK_FAIL (&reader, b, 16, 16);

  /* 32 bit */
  gst_bit_reader_init (&reader, data, 16);

  GET_CHECK (&reader, c, 32, 32, 0x12345678);
  GET_CHECK (&reader, c, 32, 24, 0x90abcd);
  GET_CHECK (&reader, c, 32, 16, 0xeffe);
  GET_CHECK (&reader, c, 32, 8, 0xdc);
  GET_CHECK (&reader, c, 32, 4, 0x0b);
  GET_CHECK (&reader, c, 32, 2, 0x02);
  GET_CHECK (&reader, c, 32, 2, 0x02);
  PEEK_CHECK (&reader, c, 32, 8, 0x09);
  fail_unless (gst_bit_reader_skip (&reader, 3 * 8));
  GET_CHECK (&reader, c, 32, 15, 0x2190);
  GET_CHECK (&reader, c, 32, 1, 0x1);
  GET_CHECK_FAIL (&reader, c, 32, 1);

  /* 64 bit */
  gst_bit_reader_init (&reader, data, 16);

  GET_CHECK (&reader, d, 64, 64, G_GINT64_CONSTANT (0x1234567890abcdef));
  GET_CHECK (&reader, d, 64, 7, 0xfe >> 1);
  GET_CHECK (&reader, d, 64, 1, 0x00);
  GET_CHECK (&reader, d, 64, 24, 0xdcba09);
  GET_CHECK (&reader, d, 64, 32, 0x87654321);
  GET_CHECK_FAIL (&reader, d, 64, 32);
}

GST_END_TEST;

#undef GET_CHECK
#undef PEEK_CHECK
#undef GET_CHECK_FAIL
#undef PEEK_CHECK_FAIL

GST_START_TEST (test_position_tracking)
{
  guint8 data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };
  GstBitReader reader = GST_BIT_READER_INIT (data, 16);
  guint8 a = 0;

  fail_unless_equals_int (gst_bit_reader_get_pos (&reader), 0);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 16 * 8);

  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &a, 3));
  fail_unless_equals_int (gst_bit_reader_get_pos (&reader), 3);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 16 * 8 - 3);

  fail_unless (gst_bit_reader_set_pos (&reader, 9));
  fail_unless_equals_int (gst_bit_reader_get_pos (&reader), 9);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 16 * 8 - 9);

  fail_unless (gst_bit_reader_skip (&reader, 3));
  fail_unless_equals_int (gst_bit_reader_get_pos (&reader), 12);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 16 * 8 - 12);

  fail_unless (gst_bit_reader_skip_to_byte (&reader));
  fail_unless_equals_int (gst_bit_reader_get_pos (&reader), 16);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 16 * 8 - 16);

  fail_unless (gst_bit_reader_set_pos (&reader, 16 * 8));
  fail_unless_equals_int (gst_bit_reader_get_pos (&reader), 16 * 8);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 0);

  fail_unless (gst_bit_reader_skip (&reader, 0));
  fail_if (gst_bit_reader_skip (&reader, 1));
  fail_unless (gst_bit_reader_skip_to_byte (&reader));
}

GST_END_TEST;

static Suite *
gst_bit_reader_suite (void)
{
  Suite *s = suite_create ("GstBitReader");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_initialization);
  tcase_add_test (tc_chain, test_get_bits);
  tcase_add_test (tc_chain, test_position_tracking);

  return s;
}


GST_CHECK_MAIN (gst_bit_reader);

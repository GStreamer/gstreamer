/* GStreamer
 *
 * unit test for GstByteReader
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstbytereader.h>

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
  GstByteReader reader = GST_BYTE_READER_INIT (data, 4);
  GstByteReader *reader2;
  guint8 x;

  GST_BUFFER_DATA (buffer) = data;
  GST_BUFFER_SIZE (buffer) = 4;

  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x02);

  memset (&reader, 0, sizeof (GstByteReader));

  gst_byte_reader_init (&reader, data, 4);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x02);

  gst_byte_reader_init_from_buffer (&reader, buffer);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x02);

  reader2 = gst_byte_reader_new (data, 4);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x02);
  gst_byte_reader_free (reader2);

  reader2 = gst_byte_reader_new_from_buffer (buffer);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x02);
  gst_byte_reader_free (reader2);

  gst_buffer_unref (buffer);
}

GST_END_TEST;

#define GET_CHECK8(reader, dest, val) { \
  fail_unless (gst_byte_reader_get_uint8 (reader, &dest)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define GET_CHECK(reader, dest, bits, endianness, val) { \
  fail_unless (gst_byte_reader_get_uint##bits##_##endianness (reader, &dest)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define GET_CHECK_FAIL8(reader, dest) { \
  fail_if (gst_byte_reader_get_uint8 (reader, &dest)); \
}

#define GET_CHECK_FAIL(reader, dest, bits, endianness) { \
  fail_if (gst_byte_reader_get_uint##bits##_##endianness (reader, &dest)); \
}

#define PEEK_CHECK8(reader, dest, val) { \
  fail_unless (gst_byte_reader_peek_uint8 (reader, &dest)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define PEEK_CHECK(reader, dest, bits, endianness, val) { \
  fail_unless (gst_byte_reader_peek_uint##bits##_##endianness (reader, &dest)); \
  fail_unless_equals_uint64 (dest, val); \
}

#define PEEK_CHECK_FAIL8(reader, dest) { \
  fail_if (gst_byte_reader_peek_uint8 (reader, &dest)); \
}

#define PEEK_CHECK_FAIL(reader, dest, bits, endianness) { \
  fail_if (gst_byte_reader_peek_uint##bits##_##endianness (reader, &dest)); \
}

GST_START_TEST (test_get_uint_le)
{
  guint8 data[] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21
  };
  GstByteReader reader = GST_BYTE_READER_INIT (data, 16);
  guint8 a;
  guint16 b;
  guint32 c;
  guint64 d;

  GET_CHECK8 (&reader, a, 0x12);
  GET_CHECK (&reader, b, 16, le, 0x5634);
  GET_CHECK (&reader, c, 24, le, 0xab9078);
  GET_CHECK (&reader, c, 32, le, 0xdcfeefcd);
  fail_unless (gst_byte_reader_set_pos (&reader, 0));
  GET_CHECK (&reader, d, 64, le, G_GINT64_CONSTANT (0xefcdab9078563412));
  GET_CHECK (&reader, d, 64, le, G_GINT64_CONSTANT (0x2143658709badcfe));

  GET_CHECK_FAIL8 (&reader, a);
  GET_CHECK_FAIL (&reader, b, 16, le);
  GET_CHECK_FAIL (&reader, c, 24, le);
  GET_CHECK_FAIL (&reader, c, 32, le);
  GET_CHECK_FAIL (&reader, d, 64, le);

  fail_unless (gst_byte_reader_set_pos (&reader, 0));

  PEEK_CHECK8 (&reader, a, 0x12);
  PEEK_CHECK (&reader, b, 16, le, 0x3412);
  PEEK_CHECK (&reader, c, 24, le, 0x563412);
  PEEK_CHECK (&reader, c, 32, le, 0x78563412);
  PEEK_CHECK (&reader, d, 64, le, G_GINT64_CONSTANT (0xefcdab9078563412));

  fail_unless (gst_byte_reader_set_pos (&reader, 16));
  PEEK_CHECK_FAIL8 (&reader, a);
  PEEK_CHECK_FAIL (&reader, b, 16, le);
  PEEK_CHECK_FAIL (&reader, c, 24, le);
  PEEK_CHECK_FAIL (&reader, c, 32, le);
  PEEK_CHECK_FAIL (&reader, d, 64, le);
}

GST_END_TEST;

GST_START_TEST (test_get_uint_be)
{
  guint8 data[] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x09, 0x87, 0x65, 0x43, 0x21
  };
  GstByteReader reader = GST_BYTE_READER_INIT (data, 16);
  guint8 a;
  guint16 b;
  guint32 c;
  guint64 d;

  GET_CHECK8 (&reader, a, 0x12);
  GET_CHECK (&reader, b, 16, be, 0x3456);
  GET_CHECK (&reader, c, 24, be, 0x7890ab);
  GET_CHECK (&reader, c, 32, be, 0xcdeffedc);
  fail_unless (gst_byte_reader_set_pos (&reader, 0));
  GET_CHECK (&reader, d, 64, be, G_GINT64_CONSTANT (0x1234567890abcdef));
  GET_CHECK (&reader, d, 64, be, G_GINT64_CONSTANT (0xfedcba0987654321));

  GET_CHECK_FAIL8 (&reader, a);
  GET_CHECK_FAIL (&reader, b, 16, be);
  GET_CHECK_FAIL (&reader, c, 24, be);
  GET_CHECK_FAIL (&reader, c, 32, be);
  GET_CHECK_FAIL (&reader, d, 64, be);

  fail_unless (gst_byte_reader_set_pos (&reader, 0));

  PEEK_CHECK8 (&reader, a, 0x12);
  PEEK_CHECK (&reader, b, 16, be, 0x1234);
  PEEK_CHECK (&reader, c, 24, be, 0x123456);
  PEEK_CHECK (&reader, c, 32, be, 0x12345678);
  PEEK_CHECK (&reader, d, 64, be, G_GINT64_CONSTANT (0x1234567890abcdef));

  fail_unless (gst_byte_reader_set_pos (&reader, 16));
  PEEK_CHECK_FAIL8 (&reader, a);
  PEEK_CHECK_FAIL (&reader, b, 16, be);
  PEEK_CHECK_FAIL (&reader, c, 24, be);
  PEEK_CHECK_FAIL (&reader, c, 32, be);
  PEEK_CHECK_FAIL (&reader, d, 64, be);
}

GST_END_TEST;

#undef GET_CHECK8
#undef GET_CHECK
#undef PEEK_CHECK8
#undef PEEK_CHECK
#undef GET_CHECK_FAIL8
#undef GET_CHECK_FAIL
#undef PEEK_CHECK_FAIL8
#undef PEEK_CHECK_FAIL

#define GET_CHECK8(reader, dest, val) { \
  fail_unless (gst_byte_reader_get_int8 (reader, &dest)); \
  fail_unless_equals_int64 (dest, val); \
}

#define GET_CHECK(reader, dest, bits, endianness, val) { \
  fail_unless (gst_byte_reader_get_int##bits##_##endianness (reader, &dest)); \
  fail_unless_equals_int64 (dest, val); \
}

#define GET_CHECK_FAIL8(reader, dest) { \
  fail_if (gst_byte_reader_get_int8 (reader, &dest)); \
}

#define GET_CHECK_FAIL(reader, dest, bits, endianness) { \
  fail_if (gst_byte_reader_get_int##bits##_##endianness (reader, &dest)); \
}

#define PEEK_CHECK8(reader, dest, val) { \
  fail_unless (gst_byte_reader_peek_int8 (reader, &dest)); \
  fail_unless_equals_int64 (dest, val); \
}

#define PEEK_CHECK(reader, dest, bits, endianness, val) { \
  fail_unless (gst_byte_reader_peek_int##bits##_##endianness (reader, &dest)); \
  fail_unless_equals_int64 (dest, val); \
}

#define PEEK_CHECK_FAIL8(reader, dest) { \
  fail_if (gst_byte_reader_peek_int8 (reader, &dest)); \
}

#define PEEK_CHECK_FAIL(reader, dest, bits, endianness) { \
  fail_if (gst_byte_reader_peek_int##bits##_##endianness (reader, &dest)); \
}

GST_START_TEST (test_get_int_le)
{
  guint8 data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };
  GstByteReader reader = GST_BYTE_READER_INIT (data, 16);
  gint8 a;
  gint16 b;
  gint32 c;
  gint64 d;

  GET_CHECK8 (&reader, a, -1);
  GET_CHECK (&reader, b, 16, le, -1);
  GET_CHECK (&reader, c, 24, le, -1);
  GET_CHECK (&reader, c, 32, le, -1);
  fail_unless (gst_byte_reader_set_pos (&reader, 0));
  GET_CHECK (&reader, d, 64, le, G_GINT64_CONSTANT (-1));
  GET_CHECK (&reader, d, 64, le, G_GINT64_CONSTANT (-1));

  GET_CHECK_FAIL8 (&reader, a);
  GET_CHECK_FAIL (&reader, b, 16, le);
  GET_CHECK_FAIL (&reader, c, 24, le);
  GET_CHECK_FAIL (&reader, c, 32, le);
  GET_CHECK_FAIL (&reader, d, 64, le);

  fail_unless (gst_byte_reader_set_pos (&reader, 0));

  PEEK_CHECK8 (&reader, a, -1);
  PEEK_CHECK (&reader, b, 16, le, -1);
  PEEK_CHECK (&reader, c, 24, le, -1);
  PEEK_CHECK (&reader, c, 32, le, -1);
  PEEK_CHECK (&reader, d, 64, le, G_GINT64_CONSTANT (-1));

  fail_unless (gst_byte_reader_set_pos (&reader, 16));
  PEEK_CHECK_FAIL8 (&reader, a);
  PEEK_CHECK_FAIL (&reader, b, 16, le);
  PEEK_CHECK_FAIL (&reader, c, 24, le);
  PEEK_CHECK_FAIL (&reader, c, 32, le);
  PEEK_CHECK_FAIL (&reader, d, 64, le);

}

GST_END_TEST;

GST_START_TEST (test_get_int_be)
{
  guint8 data[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };
  GstByteReader reader = GST_BYTE_READER_INIT (data, 16);
  gint8 a;
  gint16 b;
  gint32 c;
  gint64 d;

  GET_CHECK8 (&reader, a, -1);
  GET_CHECK (&reader, b, 16, be, -1);
  GET_CHECK (&reader, c, 24, be, -1);
  GET_CHECK (&reader, c, 32, be, -1);
  fail_unless (gst_byte_reader_set_pos (&reader, 0));
  GET_CHECK (&reader, d, 64, be, G_GINT64_CONSTANT (-1));
  GET_CHECK (&reader, d, 64, be, G_GINT64_CONSTANT (-1));

  GET_CHECK_FAIL8 (&reader, a);
  GET_CHECK_FAIL (&reader, b, 16, be);
  GET_CHECK_FAIL (&reader, c, 24, be);
  GET_CHECK_FAIL (&reader, c, 32, be);
  GET_CHECK_FAIL (&reader, d, 64, be);

  fail_unless (gst_byte_reader_set_pos (&reader, 0));

  PEEK_CHECK8 (&reader, a, -1);
  PEEK_CHECK (&reader, b, 16, be, -1);
  PEEK_CHECK (&reader, c, 24, be, -1);
  PEEK_CHECK (&reader, c, 32, be, -1);
  PEEK_CHECK (&reader, d, 64, be, G_GINT64_CONSTANT (-1));

  fail_unless (gst_byte_reader_set_pos (&reader, 16));
  PEEK_CHECK_FAIL8 (&reader, a);
  PEEK_CHECK_FAIL (&reader, b, 16, be);
  PEEK_CHECK_FAIL (&reader, c, 24, be);
  PEEK_CHECK_FAIL (&reader, c, 32, be);
  PEEK_CHECK_FAIL (&reader, d, 64, be);

}

GST_END_TEST;

#undef GET_CHECK8
#undef GET_CHECK
#undef PEEK_CHECK8
#undef PEEK_CHECK
#undef GET_CHECK_FAIL8
#undef GET_CHECK_FAIL
#undef PEEK_CHECK_FAIL8
#undef PEEK_CHECK_FAIL

#define GET_CHECK(reader, dest, bits, endianness, val) { \
  fail_unless (gst_byte_reader_get_float##bits##_##endianness (reader, &dest)); \
  fail_unless_equals_float (dest, val); \
}

#define GET_CHECK_FAIL(reader, dest, bits, endianness) { \
  fail_if (gst_byte_reader_get_float##bits##_##endianness (reader, &dest)); \
}

#define PEEK_CHECK(reader, dest, bits, endianness, val) { \
  fail_unless (gst_byte_reader_peek_float##bits##_##endianness (reader, &dest)); \
  fail_unless_equals_float (dest, val); \
}

#define PEEK_CHECK_FAIL(reader, dest, bits, endianness) { \
  fail_if (gst_byte_reader_peek_float##bits##_##endianness (reader, &dest)); \
}

GST_START_TEST (test_get_float_le)
{
  guint8 data[] = {
    0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x80, 0xbf,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xbf,
  };
  GstByteReader reader = GST_BYTE_READER_INIT (data, 24);
  gfloat a;
  gdouble b;

  PEEK_CHECK (&reader, a, 32, le, 1.0);
  GET_CHECK (&reader, a, 32, le, 1.0);
  GET_CHECK (&reader, a, 32, le, -1.0);
  PEEK_CHECK (&reader, b, 64, le, 1.0);
  GET_CHECK (&reader, b, 64, le, 1.0);
  GET_CHECK (&reader, b, 64, le, -1.0);
  GET_CHECK_FAIL (&reader, a, 32, le);
  GET_CHECK_FAIL (&reader, b, 64, le);
  PEEK_CHECK_FAIL (&reader, a, 32, le);
  PEEK_CHECK_FAIL (&reader, b, 64, le);
}

GST_END_TEST;

GST_START_TEST (test_get_float_be)
{
  guint8 data[] = {
    0x3f, 0x80, 0x00, 0x00,
    0xbf, 0x80, 0x00, 0x00,
    0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xbf, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  GstByteReader reader = GST_BYTE_READER_INIT (data, 24);
  gfloat a;
  gdouble b;

  PEEK_CHECK (&reader, a, 32, be, 1.0);
  GET_CHECK (&reader, a, 32, be, 1.0);
  GET_CHECK (&reader, a, 32, be, -1.0);
  PEEK_CHECK (&reader, b, 64, be, 1.0);
  GET_CHECK (&reader, b, 64, be, 1.0);
  GET_CHECK (&reader, b, 64, be, -1.0);
  GET_CHECK_FAIL (&reader, a, 32, be);
  GET_CHECK_FAIL (&reader, b, 64, be);
  PEEK_CHECK_FAIL (&reader, a, 32, be);
  PEEK_CHECK_FAIL (&reader, b, 64, be);
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
  GstByteReader reader = GST_BYTE_READER_INIT (data, 16);
  guint8 a;

  fail_unless_equals_int (gst_byte_reader_get_pos (&reader), 0);
  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 16);

  fail_unless (gst_byte_reader_get_uint8 (&reader, &a));
  fail_unless_equals_int (gst_byte_reader_get_pos (&reader), 1);
  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 16 - 1);

  fail_unless (gst_byte_reader_set_pos (&reader, 8));
  fail_unless_equals_int (gst_byte_reader_get_pos (&reader), 8);
  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 16 - 8);

  fail_unless (gst_byte_reader_skip (&reader, 4));
  fail_unless_equals_int (gst_byte_reader_get_pos (&reader), 12);
  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 16 - 12);

  fail_unless (gst_byte_reader_set_pos (&reader, 16));
  fail_unless_equals_int (gst_byte_reader_get_pos (&reader), 16);
  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 0);

  fail_unless (gst_byte_reader_skip (&reader, 0));
  fail_if (gst_byte_reader_skip (&reader, 1));
}

GST_END_TEST;

#define do_scan(r,m,p,o,s,x) \
    fail_unless_equals_int (gst_byte_reader_masked_scan_uint32 (r,m,p,o,s), x);

GST_START_TEST (test_scan)
{
  GstByteReader reader;
  guint8 data[200];
  guint i;

  /* fill half the buffer with a pattern */
  for (i = 0; i < 100; i++)
    data[i] = i;

  gst_byte_reader_init (&reader, data, 100);

  /* find first bytes */
  do_scan (&reader, 0xffffffff, 0x00010203, 0, 100, 0);
  do_scan (&reader, 0xffffffff, 0x01020304, 0, 100, 1);
  do_scan (&reader, 0xffffffff, 0x01020304, 1, 99, 1);
  /* offset is past the pattern start */
  do_scan (&reader, 0xffffffff, 0x01020304, 2, 98, -1);
  /* not enough bytes to find the pattern */
  do_scan (&reader, 0xffffffff, 0x02030405, 2, 3, -1);
  do_scan (&reader, 0xffffffff, 0x02030405, 2, 4, 2);
  /* size does not include the last scanned byte */
  do_scan (&reader, 0xffffffff, 0x40414243, 0, 0x41, -1);
  do_scan (&reader, 0xffffffff, 0x40414243, 0, 0x43, -1);
  do_scan (&reader, 0xffffffff, 0x40414243, 0, 0x44, 0x40);
  /* past the start */
  do_scan (&reader, 0xffffffff, 0x40414243, 65, 10, -1);
  do_scan (&reader, 0xffffffff, 0x40414243, 64, 5, 64);
  do_scan (&reader, 0xffffffff, 0x60616263, 65, 35, 0x60);
  do_scan (&reader, 0xffffffff, 0x60616263, 0x60, 4, 0x60);
  /* past the start */
  do_scan (&reader, 0xffffffff, 0x60616263, 0x61, 3, -1);
  do_scan (&reader, 0xffffffff, 0x60616263, 99, 1, -1);

  /* add more data to the buffer */
  for (i = 100; i < 200; i++)
    data[i] = i;
  gst_byte_reader_init (&reader, data, 200);

  /* past the start */
  do_scan (&reader, 0xffffffff, 0x60616263, 0x61, 6, -1);
  /* this should work */
  do_scan (&reader, 0xffffffff, 0x61626364, 0x61, 4, 0x61);
  /* not enough data */
  do_scan (&reader, 0xffffffff, 0x62636465, 0x61, 4, -1);
  do_scan (&reader, 0xffffffff, 0x62636465, 0x61, 5, 0x62);
  do_scan (&reader, 0xffffffff, 0x62636465, 0, 120, 0x62);

  /* border conditions */
  do_scan (&reader, 0xffffffff, 0x62636465, 0, 200, 0x62);
  do_scan (&reader, 0xffffffff, 0x63646566, 0, 200, 0x63);
  /* we completely searched the first list */
  do_scan (&reader, 0xffffffff, 0x64656667, 0, 200, 0x64);
  /* skip first buffer */
  do_scan (&reader, 0xffffffff, 0x64656667, 0x64, 100, 0x64);
  /* past the start */
  do_scan (&reader, 0xffffffff, 0x64656667, 0x65, 10, -1);
  /* not enough data to scan */
  do_scan (&reader, 0xffffffff, 0x64656667, 0x63, 4, -1);
  do_scan (&reader, 0xffffffff, 0x64656667, 0x63, 5, 0x64);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0, 199, -1);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x62, 102, 0xc4);
  /* different masks */
  do_scan (&reader, 0x00ffffff, 0x00656667, 0x64, 100, 0x64);
  do_scan (&reader, 0x000000ff, 0x00000000, 0, 100, -1);
  do_scan (&reader, 0x000000ff, 0x00000003, 0, 100, 0);
  do_scan (&reader, 0x000000ff, 0x00000061, 0x61, 100, -1);
  do_scan (&reader, 0xff000000, 0x61000000, 0, 0x62, -1);
  /* does not even exist */
  do_scan (&reader, 0x00ffffff, 0xffffffff, 0x65, 99, -1);

  /* flush some bytes */
  gst_byte_reader_skip (&reader, 0x20);

  do_scan (&reader, 0xffffffff, 0x20212223, 0, 100, 0);
  do_scan (&reader, 0xffffffff, 0x20212223, 0, 4, 0);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x62, 70, 0xa4);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0, 168, 0xa4);

  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 164, 4, 0xa4);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x44, 100, 0xa4);

  /* not enough bytes */
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x44, 99, -1);
}

GST_END_TEST;

static Suite *
gst_byte_reader_suite (void)
{
  Suite *s = suite_create ("GstByteReader");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_initialization);
  tcase_add_test (tc_chain, test_get_uint_le);
  tcase_add_test (tc_chain, test_get_uint_be);
  tcase_add_test (tc_chain, test_get_int_le);
  tcase_add_test (tc_chain, test_get_int_be);
  tcase_add_test (tc_chain, test_get_float_le);
  tcase_add_test (tc_chain, test_get_float_be);
  tcase_add_test (tc_chain, test_position_tracking);
  tcase_add_test (tc_chain, test_scan);

  return s;
}


GST_CHECK_MAIN (gst_byte_reader);

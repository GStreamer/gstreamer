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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
  guint8 x = 0;
  GstMapInfo info;

  gst_buffer_insert_memory (buffer, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY, data, 4, 0, 4, NULL,
          NULL));

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

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  gst_byte_reader_init (&reader, info.data, info.size);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0x02);
  gst_buffer_unmap (buffer, &info);

  reader2 = gst_byte_reader_new (data, 4);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x02);
  gst_byte_reader_free (reader2);

  fail_unless (gst_buffer_map (buffer, &info, GST_MAP_READ));
  reader2 = gst_byte_reader_new (info.data, info.size);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x01);
  fail_unless (gst_byte_reader_get_uint8 (reader2, &x));
  fail_unless_equals_int (x, 0x02);
  gst_byte_reader_free (reader2);
  gst_buffer_unmap (buffer, &info);

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
  guint8 a = 0;
  guint16 b = 0;
  guint32 c = 0;
  guint64 d = 0;

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
  guint8 a = 0;
  guint16 b = 0;
  guint32 c = 0;
  guint64 d = 0;

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
  gint8 a = 0;
  gint16 b = 0;
  gint32 c = 0;
  gint64 d = 0;

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
  gint8 a = 0;
  gint16 b = 0;
  gint32 c = 0;
  gint64 d = 0;

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
  gfloat a = 0.0;
  gdouble b = 0.0;

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
  gfloat a = 0.0;
  gdouble b = 0.0;

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
  guint8 a = 0;

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
G_STMT_START {								\
    fail_unless_equals_int (gst_byte_reader_masked_scan_uint32 (r,m,p,o,s), x); \
    if (x != -1) { \
      guint32 v, res_v; \
      const guint8 *rdata = NULL; \
      fail_unless (gst_byte_reader_peek_data (r, x + 4, &rdata)); \
      res_v = GST_READ_UINT32_BE (rdata + x); \
      fail_unless_equals_int (gst_byte_reader_masked_scan_uint32_peek (r,m,p,o,s,&v), x); \
      fail_unless_equals_int (v, res_v); \
    } \
} G_STMT_END;

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
  fail_unless (gst_byte_reader_skip (&reader, 0x20));

  do_scan (&reader, 0xffffffff, 0x20212223, 0, 100, 0);
  do_scan (&reader, 0xffffffff, 0x20212223, 0, 4, 0);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x62, 70, 0xa4);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0, 168, 0xa4);

  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 164, 4, 0xa4);
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x44, 100, 0xa4);

  /* not enough bytes */
  do_scan (&reader, 0xffffffff, 0xc4c5c6c7, 0x44, 99, -1);

  /* check special code path that exists for 00 00 01 sync marker */
  {
    const guint8 sync_data[] = { 0xA0, 0x00, 0x00, 0x00, 0x01, 0xA5, 0xA6,
      0xA7, 0xA8, 0xA9, 0xAA, 0x00, 0x00, 0x00, 0x01, 0xAF, 0xB0, 0xB1
    };
    guint32 val = 0;
    guint8 *m;
    gint found;

    /* dup so valgrind can detect out of bounds access more easily */
    m = g_memdup (sync_data, sizeof (sync_data));
    gst_byte_reader_init (&reader, m, sizeof (sync_data));

    found = gst_byte_reader_masked_scan_uint32_peek (&reader, 0xffffff00,
        0x00000100, 0, sizeof (sync_data), &val);
    fail_unless_equals_int (found, 2);
    fail_unless_equals_int (val, 0x000001A5);

    found = gst_byte_reader_masked_scan_uint32_peek (&reader, 0xffffff00,
        0x00000100, 2, sizeof (sync_data) - 2, &val);
    fail_unless_equals_int (found, 2);
    fail_unless_equals_int (val, 0x000001A5);

    found = gst_byte_reader_masked_scan_uint32_peek (&reader, 0xffffff00,
        0x00000100, 3, sizeof (sync_data) - 3, &val);
    fail_unless_equals_int (found, 12);
    fail_unless_equals_int (val, 0x000001AF);

    found = gst_byte_reader_masked_scan_uint32_peek (&reader, 0xffffff00,
        0x00000100, 12, sizeof (sync_data) - 12, &val);
    fail_unless_equals_int (found, 12);
    fail_unless_equals_int (val, 0x000001AF);

    found = gst_byte_reader_masked_scan_uint32_peek (&reader, 0xffffff00,
        0x00000100, 13, sizeof (sync_data) - 13, &val);
    fail_unless_equals_int (found, -1);

    g_free (m);
  }
}

GST_END_TEST;

GST_START_TEST (test_string_funcs)
{
  GstByteReader reader, backup;
  const gchar *s8;
  guint32 *c32;
  guint16 *c16;
  gchar *c8;
  guint8 data[200], *d = 0;
  guint i;

  /* fill half the buffer with a pattern */
  for (i = 0; i < 100; i++)
    data[i] = i + 1;

  gst_byte_reader_init (&reader, data, 100);

  /* no NUL terminator, so these should all fail */
  fail_if (gst_byte_reader_get_string (&reader, &s8));
  fail_if (gst_byte_reader_get_string_utf8 (&reader, &s8));
  fail_if (gst_byte_reader_dup_string (&reader, &c8));
  fail_if (gst_byte_reader_dup_string_utf8 (&reader, &c8));
  fail_if (gst_byte_reader_skip_string (&reader));
  fail_if (gst_byte_reader_skip_string_utf8 (&reader));
  fail_if (gst_byte_reader_skip_string_utf16 (&reader));
  fail_if (gst_byte_reader_skip_string_utf32 (&reader));
  fail_if (gst_byte_reader_peek_string (&reader, &s8));
  fail_if (gst_byte_reader_peek_string_utf8 (&reader, &s8));
  fail_if (gst_byte_reader_dup_string_utf16 (&reader, &c16));
  fail_if (gst_byte_reader_dup_string_utf32 (&reader, &c32));

  /* let's add a single NUL terminator */
  data[80] = '\0';
  backup = reader;
  fail_if (gst_byte_reader_skip_string_utf32 (&reader));
  fail_if (gst_byte_reader_skip_string_utf16 (&reader));
  fail_if (gst_byte_reader_dup_string_utf16 (&reader, &c16));
  fail_if (gst_byte_reader_dup_string_utf32 (&reader, &c32));
  fail_unless (gst_byte_reader_skip_string (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_skip_string_utf8 (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_peek_string (&reader, &s8));
  fail_unless (gst_byte_reader_peek_string_utf8 (&reader, &s8));
  fail_if (gst_byte_reader_dup_string_utf16 (&reader, &c16));
  fail_if (gst_byte_reader_dup_string_utf32 (&reader, &c32));

  /* let's add another NUL terminator */
  data[81] = '\0';
  reader = backup;
  fail_if (gst_byte_reader_skip_string_utf32 (&reader));
  fail_if (gst_byte_reader_dup_string_utf32 (&reader, &c32));
  fail_unless (gst_byte_reader_skip_string_utf16 (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_dup_string_utf16 (&reader, &c16));
  g_free (c16);
  reader = backup;
  fail_unless (gst_byte_reader_skip_string (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_skip_string_utf8 (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_peek_string (&reader, &s8));
  fail_unless (gst_byte_reader_peek_string_utf8 (&reader, &s8));
  fail_if (gst_byte_reader_dup_string_utf32 (&reader, &c32));

  /* two more NUL terminators */
  data[79] = '\0';
  data[82] = '\0';
  reader = backup;
  /* we're at pos. 80 now, so have only 3 NUL terminators in front of us */
  fail_if (gst_byte_reader_skip_string_utf32 (&reader));
  /* let's rewind */
  gst_byte_reader_init (&reader, data, 100);
  backup = reader;
  /* oops, 79 is not dividable by 4, so not aligned, so should fail as well! */
  fail_if (gst_byte_reader_skip_string_utf32 (&reader));
  /* let's try that again */
  data[83] = '\0';
  gst_byte_reader_init (&reader, data, 100);
  backup = reader;
  fail_unless (gst_byte_reader_skip_string_utf16 (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_skip_string (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_skip_string_utf8 (&reader));
  reader = backup;
  fail_unless (gst_byte_reader_peek_string (&reader, &s8));
  fail_unless (gst_byte_reader_peek_string_utf8 (&reader, &s8));
  fail_unless (gst_byte_reader_dup_string_utf16 (&reader, &c16));
  g_free (c16);
  reader = backup;
  fail_unless (gst_byte_reader_dup_string_utf32 (&reader, &c32));
  g_free (c32);

  /* and again from the start */
  gst_byte_reader_init (&reader, data, 100);
  fail_unless (gst_byte_reader_skip_string_utf16 (&reader));
  fail_if (gst_byte_reader_dup_data (&reader, 200, &d));
  fail_if (gst_byte_reader_dup_data (&reader, 100, &d));
  fail_if (gst_byte_reader_dup_data (&reader, 20, &d));
  fail_unless (gst_byte_reader_dup_data (&reader, 10, &d));
  fail_unless_equals_int (d[0], 0);
  fail_unless_equals_int (d[1], 0);
  fail_unless_equals_int (d[2], 85);
  fail_unless_equals_int (d[3], 86);
  g_free (d);
}

GST_END_TEST;

GST_START_TEST (test_dup_string)
{
  const gchar moredata[] = { 0x99, 0x10, 'f', '0', '0', '!', '\0', 0xff };
  GstByteReader reader;
  guint16 num = 0;
  guint8 x = 0;
  gchar *s;

  gst_byte_reader_init (&reader, (guint8 *) moredata, sizeof (moredata));
  fail_unless (gst_byte_reader_get_uint16_be (&reader, &num));
  fail_unless_equals_int (num, 0x9910);
  fail_unless (gst_byte_reader_dup_string (&reader, &s));
  fail_unless_equals_string (s, "f00!");
  fail_unless (gst_byte_reader_get_uint8 (&reader, &x));
  fail_unless_equals_int (x, 0xff);
  g_free (s);
}

GST_END_TEST;

GST_START_TEST (test_sub_reader)
{
  const guint8 memdata[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
  };
  GstByteReader reader = GST_BYTE_READER_INIT (memdata, sizeof (memdata));
  GstByteReader sub;
  const guint8 *data = NULL, *sub_data = NULL;
  guint16 v = 0;

  /* init sub reader */
  fail_if (gst_byte_reader_peek_sub_reader (&reader, &sub, 17));
  fail_unless (gst_byte_reader_peek_sub_reader (&reader, &sub, 16));
  fail_unless_equals_int (gst_byte_reader_get_remaining (&sub), 16);
  fail_unless (gst_byte_reader_peek_data (&reader, 16, &data));
  fail_unless (gst_byte_reader_peek_data (&sub, 16, &sub_data));
  fail_unless (memcmp (data, sub_data, 16) == 0);

  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 16);
  fail_unless (gst_byte_reader_skip (&reader, 3));
  fail_if (gst_byte_reader_peek_sub_reader (&reader, &sub, 14));
  fail_unless (gst_byte_reader_peek_sub_reader (&reader, &sub, 13));
  fail_unless_equals_int (gst_byte_reader_get_remaining (&sub), 13);
  fail_unless (gst_byte_reader_peek_data (&reader, 13, &data));
  fail_unless (gst_byte_reader_peek_data (&sub, 13, &sub_data));
  fail_unless (memcmp (data, sub_data, 13) == 0);
  fail_unless (memcmp (memdata + 3, sub_data, 13) == 0);

  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 13);
  fail_unless (gst_byte_reader_peek_sub_reader (&reader, &sub, 3));
  fail_unless_equals_int (gst_byte_reader_get_remaining (&sub), 3);
  fail_if (gst_byte_reader_peek_data (&sub, 10, &sub_data));
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0304);
  fail_if (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (gst_byte_reader_get_remaining (&sub), 1);

  fail_unless (gst_byte_reader_get_uint16_be (&reader, &v));
  fail_unless_equals_int (v, 0x0304);
  fail_unless (gst_byte_reader_get_uint16_be (&reader, &v));
  fail_unless_equals_int (v, 0x0506);
  fail_unless_equals_int (gst_byte_reader_get_remaining (&reader), 9);

  /* get sub reader */
  gst_byte_reader_init (&reader, memdata, sizeof (memdata));
  fail_if (gst_byte_reader_get_sub_reader (&reader, &sub, 17));
  fail_unless (gst_byte_reader_get_sub_reader (&reader, &sub, 16));
  fail_if (gst_byte_reader_get_sub_reader (&reader, &sub, 1));
  fail_unless (gst_byte_reader_get_sub_reader (&reader, &sub, 0));

  gst_byte_reader_init (&reader, memdata, sizeof (memdata));
  fail_unless (gst_byte_reader_get_sub_reader (&reader, &sub, 2));
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0001);
  fail_if (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless (gst_byte_reader_get_sub_reader (&reader, &sub, 3));
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0203);
  fail_if (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (gst_byte_reader_get_uint8_unchecked (&sub), 0x04);
  fail_unless (gst_byte_reader_get_sub_reader (&reader, &sub, 9));
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0506);
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0708);
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x090a);
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0b0c);
  fail_if (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (gst_byte_reader_get_uint8_unchecked (&sub), 0x0d);
  fail_if (gst_byte_reader_get_sub_reader (&reader, &sub, 3));
  fail_unless (gst_byte_reader_get_sub_reader (&reader, &sub, 2));
  fail_unless (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_unless_equals_int (v, 0x0e0f);
  fail_if (gst_byte_reader_get_uint16_be (&sub, &v));
  fail_if (gst_byte_reader_get_uint16_be (&reader, &v));
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
  tcase_add_test (tc_chain, test_string_funcs);
  tcase_add_test (tc_chain, test_dup_string);
  tcase_add_test (tc_chain, test_sub_reader);

  return s;
}


GST_CHECK_MAIN (gst_byte_reader);

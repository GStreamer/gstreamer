/* GStreamer
 *
 * unit test for GstByteWriter
 *
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <gst/base/gstbytewriter.h>

GST_START_TEST (test_initialization)
{
  GstByteWriter writer, *writer2;
  static guint8 sdata[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  guint8 *data, *tmp;

  gst_byte_writer_init_with_size (&writer, 24, FALSE);
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 0);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), 0);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), -1);
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), 0);
  gst_byte_writer_reset (&writer);

  data = g_memdup (sdata, sizeof (sdata));
  gst_byte_writer_init_with_data (&writer, data, sizeof (sdata), FALSE);
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 0);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), 0);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer),
      sizeof (sdata));
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), 0)
      tmp = gst_byte_writer_reset_and_get_data (&writer);
  fail_if (tmp == data);
  g_free (tmp);
  g_free (data);
  data = tmp = NULL;

  data = g_memdup (sdata, sizeof (sdata));
  gst_byte_writer_init_with_data (&writer, data, sizeof (sdata), TRUE);
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 0);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), sizeof (sdata));
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer),
      sizeof (sdata));
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), sizeof (sdata));
  tmp = gst_byte_writer_reset_and_get_data (&writer);
  fail_unless (tmp != data);
  g_free (tmp);
  g_free (data);
  data = tmp = NULL;

  writer2 = gst_byte_writer_new_with_size (24, FALSE);
  data = gst_byte_writer_free_and_get_data (writer2);
  fail_unless (data != NULL);
  g_free (data);
}

GST_END_TEST;

GST_START_TEST (test_write_fixed)
{
  GstByteWriter writer;
  guint64 end_data = G_GUINT64_CONSTANT (0xff34567890abcdef);
  guint8 *data;
  guint8 b = 0;
  guint64 l = 0;

  end_data = GUINT64_TO_BE (end_data);

  gst_byte_writer_init_with_size (&writer, 8, TRUE);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), 8);

  fail_unless (gst_byte_writer_put_uint8 (&writer, 0x12));
  fail_unless (gst_byte_writer_put_uint16_be (&writer, 0x3456));
  fail_unless (gst_byte_writer_put_uint16_le (&writer, 0x9078));
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 5);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), 5);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), 8 - 5);
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), 0);
  fail_if (gst_byte_reader_get_uint8 (GST_BYTE_READER (&writer), &b));
  fail_unless (gst_byte_writer_put_uint24_be (&writer, 0xabcdef));
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 8);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), 8);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), 8 - 8);
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), 0);
  fail_if (gst_byte_writer_put_uint8 (&writer, 0x12));
  fail_unless (gst_byte_writer_set_pos (&writer, 0));
  fail_unless (gst_byte_reader_peek_uint64_be (GST_BYTE_READER (&writer), &l));
  fail_unless_equals_uint64 (l, G_GUINT64_CONSTANT (0x1234567890abcdef));
  fail_unless (gst_byte_writer_put_uint8 (&writer, 0xff));
  fail_unless (gst_byte_writer_set_pos (&writer, 0));
  fail_unless (gst_byte_reader_get_uint64_be (GST_BYTE_READER (&writer), &l));
  fail_unless_equals_uint64 (l, G_GUINT64_CONSTANT (0xff34567890abcdef));
  fail_if (gst_byte_writer_put_uint64_be (&writer,
          G_GUINT64_CONSTANT (0x1234567890abcdef)));

  data = gst_byte_writer_reset_and_get_data (&writer);
  fail_unless (data != NULL);
  fail_unless (memcmp (&end_data, data, 8) == 0);
  g_free (data);
}

GST_END_TEST;

GST_START_TEST (test_write_non_fixed)
{
  GstByteWriter writer;
  guint64 end_data = G_GUINT64_CONSTANT (0xff34567890abcdef);
  guint8 *data;
  guint64 l = 0;

  end_data = GUINT64_TO_BE (end_data);

  gst_byte_writer_init_with_size (&writer, 6, FALSE);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), -1);

  fail_unless (gst_byte_writer_put_uint8 (&writer, 0x12));
  fail_unless (gst_byte_writer_put_uint16_be (&writer, 0x3456));
  fail_unless (gst_byte_writer_put_uint16_le (&writer, 0x9078));
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 5);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), 5);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), -1);
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), 0);
  fail_unless (gst_byte_writer_put_uint24_be (&writer, 0xabcdef));
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 8);
  fail_unless_equals_int (gst_byte_writer_get_size (&writer), 8);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), -1);
  fail_unless_equals_int (gst_byte_reader_get_remaining (GST_BYTE_READER
          (&writer)), 0);
  fail_unless (gst_byte_writer_set_pos (&writer, 0));
  fail_unless (gst_byte_reader_peek_uint64_be (GST_BYTE_READER (&writer), &l));
  fail_unless_equals_uint64 (l, G_GUINT64_CONSTANT (0x1234567890abcdef));
  fail_unless (gst_byte_writer_put_uint8 (&writer, 0xff));
  fail_unless (gst_byte_writer_set_pos (&writer, 0));
  fail_unless (gst_byte_reader_get_uint64_be (GST_BYTE_READER (&writer), &l));
  fail_unless_equals_uint64 (l, G_GUINT64_CONSTANT (0xff34567890abcdef));
  fail_unless (gst_byte_writer_set_pos (&writer, 8));
  fail_unless (gst_byte_writer_put_uint64_be (&writer,
          G_GUINT64_CONSTANT (0x1234567890abcdef)));

  data = gst_byte_writer_reset_and_get_data (&writer);
  fail_unless (data != NULL);
  fail_unless (memcmp (&end_data, data, 8) == 0);
  end_data = GUINT64_TO_BE (G_GUINT64_CONSTANT (0x1234567890abcdef));
  fail_unless (memcmp (&end_data, data + 8, 8) == 0);
  g_free (data);
}

GST_END_TEST;

GST_START_TEST (test_from_data)
{
  GstByteWriter writer;
  guint8 data[] = { 0x12, 0x34, 0x56, 0x78,
    0x90, 0xab, 0xcd, 0xef
  };
  guint8 *data2;

  gst_byte_writer_init_with_data (&writer, data, sizeof (data), TRUE);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), 8);
  fail_unless (gst_byte_writer_put_uint8 (&writer, 0xff));
  fail_unless_equals_int (data[0], 0xff);
  fail_unless_equals_int (gst_byte_writer_get_remaining (&writer), 7);
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer), 1);
  fail_if (gst_byte_writer_put_uint64_be (&writer,
          G_GUINT64_CONSTANT (0x1234567890abcdef)));
  data2 = gst_byte_writer_reset_and_get_data (&writer);
  fail_unless (data2 != NULL);
  fail_if (data2 == data);
  fail_unless (memcmp (data, data2, 8) == 0);
  g_free (data2);
}

GST_END_TEST;

GST_START_TEST (test_put_data_strings)
{
  GstByteWriter writer;
  guint8 data[] = { 0x12, 0x34, 0x56, 0x78,
    0x90, 0xab, 0xcd, 0xef
  };
  guint8 *data2;

  gst_byte_writer_init (&writer);
  fail_unless (gst_byte_writer_put_data (&writer, data, 8));
  fail_unless (gst_byte_writer_put_string (&writer, "somerandomteststring"));
  fail_unless_equals_int (gst_byte_writer_get_pos (&writer),
      8 + sizeof ("somerandomteststring"));

  data2 = gst_byte_writer_reset_and_get_data (&writer);
  fail_unless (data2 != NULL);
  fail_unless (memcmp (data2, data, 8) == 0);
  g_free (data2);
}

GST_END_TEST;

GST_START_TEST (test_fill)
{
  GstByteWriter writer;
  guint8 data[] = { 0x0, 0x0, 0x0, 0x0, 0x5, 0x5 };
  guint8 *data2;

  gst_byte_writer_init (&writer);
  fail_unless (gst_byte_writer_fill (&writer, 0, 4));
  fail_unless (gst_byte_writer_fill (&writer, 5, 2));

  data2 = gst_byte_writer_reset_and_get_data (&writer);
  fail_unless (data2 != NULL);
  fail_unless (memcmp (data2, data, 6) == 0);
  g_free (data2);
}

GST_END_TEST;
static Suite *
gst_byte_writer_suite (void)
{
  Suite *s = suite_create ("GstByteWriter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_initialization);
  tcase_add_test (tc_chain, test_write_fixed);
  tcase_add_test (tc_chain, test_write_non_fixed);
  tcase_add_test (tc_chain, test_from_data);
  tcase_add_test (tc_chain, test_put_data_strings);
  tcase_add_test (tc_chain, test_fill);

  return s;
}


GST_CHECK_MAIN (gst_byte_writer);

/* GStreamer
 *
 * unit test for GstBitWriter
 *
 * Copyright (C) <2014> Intel Corporation
 * Copyright (C) <2014> Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
#include <gst/base/gstbitwriter.h>
#include <gst/base/gstbitreader.h>
#include "gst/glib-compat-private.h"

GST_START_TEST (test_initialization)
{
  GstBitWriter writer;
  GstBitReader reader;

  static guint8 sdata[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

  gst_bit_writer_init_with_size (&writer, 4, FALSE);
  /* should be aligned to 256 bytes */
  fail_unless_equals_int (gst_bit_writer_get_remaining (&writer), 2048);
  fail_unless_equals_int (gst_bit_writer_get_size (&writer), 0);
  fail_unless_equals_int (gst_bit_writer_set_pos (&writer, 32), 1);
  fail_unless_equals_int (gst_bit_writer_get_size (&writer), 32);
  fail_unless_equals_int (gst_bit_writer_get_remaining (&writer), 2048 - 32);
  gst_bit_writer_reset (&writer);

  gst_bit_writer_init_with_data (&writer, sdata, 8, FALSE);
  gst_bit_reader_init (&reader, sdata, 8);
  fail_unless_equals_int (gst_bit_reader_get_size (&reader), 64);
  fail_unless_equals_int (gst_bit_reader_get_remaining (&reader), 64);
  gst_bit_writer_reset (&writer);
}

GST_END_TEST;

GST_START_TEST (test_data)
{
  GstBitWriter writer;
  GstBitReader reader;
  guint8 val_uint8 = 0;
  guint16 val_uint16 = 0;
  guint32 val_uint32 = 0;
  guint64 val_uint64 = 0;
  static guint8 sdata[] = { 0xff, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7 };
  guint8 *data, i;

  gst_bit_writer_init_with_size (&writer, 32, TRUE);
  fail_unless_equals_int (gst_bit_writer_get_remaining (&writer), 2048);

  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x3f, 6));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x3, 2));
  fail_unless (gst_bit_writer_put_bits_uint16 (&writer, 0x15, 5));
  fail_unless (gst_bit_writer_put_bits_uint32 (&writer, 0x31, 10));
  fail_unless (gst_bit_writer_put_bits_uint64 (&writer, 0x45, 48));
  fail_unless_equals_int (gst_bit_writer_get_remaining (&writer), 2048 - 71);
  fail_unless (gst_bit_writer_align_bytes (&writer, 0));
  data = g_memdup2 (sdata, sizeof (sdata));
  fail_unless (gst_bit_writer_put_bytes (&writer, data, sizeof (sdata)));

  gst_bit_reader_init (&reader, gst_bit_writer_get_data (&writer), 256);
  fail_unless_equals_int (gst_bit_reader_get_size (&reader), 256 * 8);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &val_uint8, 6));
  fail_unless_equals_int (val_uint8, 0x3f);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &val_uint8, 2));
  fail_unless_equals_int (val_uint8, 0x3);
  fail_unless (gst_bit_reader_get_bits_uint16 (&reader, &val_uint16, 5));
  fail_unless_equals_int_hex (val_uint16, 0x15);
  fail_unless (gst_bit_reader_get_bits_uint32 (&reader, &val_uint32, 10));
  fail_unless_equals_int_hex (val_uint32, 0x31);
  fail_unless (gst_bit_reader_get_bits_uint64 (&reader, &val_uint64, 48));
  fail_unless_equals_int_hex (val_uint64, 0x45);
  fail_unless (gst_bit_reader_set_pos (&reader, 72));

  for (i = 0; i < 8; i++) {
    fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &val_uint8, 8));
    fail_unless (memcmp (&val_uint8, &data[i], 1) == 0);
  }

  g_free (data);
  gst_bit_writer_reset (&writer);
}

GST_END_TEST;

GST_START_TEST (test_reset)
{
  GstBitWriter writer, *writer2;
  GstBitReader reader;
  GstBuffer *buf;
  GstMapInfo info;
  static guint8 sdata[] = { 0xff, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7 };
  guint8 *data;
  guint8 val_uint8 = 0;

  gst_bit_writer_init_with_data (&writer, sdata, sizeof (sdata), TRUE);
  data = gst_bit_writer_reset_and_get_data (&writer);
  fail_unless (data != NULL);

  gst_bit_writer_init_with_data (&writer, sdata, sizeof (sdata), TRUE);
  buf = gst_bit_writer_reset_and_get_buffer (&writer);
  fail_unless (buf != NULL);
  fail_unless (gst_buffer_map (buf, &info, GST_MAP_READWRITE));
  fail_unless (info.data);
  fail_unless_equals_int (info.size, 8);
  gst_bit_reader_init (&reader, info.data, 8);
  fail_unless (gst_bit_reader_set_pos (&reader, 64 - 10));
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &val_uint8, 8));
  fail_unless_equals_int_hex (val_uint8, 0xbd);
  fail_unless (gst_bit_reader_get_bits_uint8 (&reader, &val_uint8, 2));
  fail_unless_equals_int_hex (val_uint8, 0x3);
  gst_buffer_unmap (buf, &info);
  gst_buffer_unref (buf);

  writer2 = gst_bit_writer_new_with_size (3, TRUE);
  data = gst_bit_writer_free_and_get_data (writer2);
  /* no data, no buffer allocated */
  fail_unless (data == NULL);

  writer2 = gst_bit_writer_new_with_size (1, FALSE);
  fail_unless (gst_bit_writer_put_bits_uint8 (writer2, 0xff, 8));
  data = gst_bit_writer_free_and_get_data (writer2);
  fail_unless (data != NULL);
  fail_unless_equals_int_hex (*data, 0xff);
  g_free (data);
}

GST_END_TEST;

GST_START_TEST (test_reset_data_unaligned)
{
  GstBitWriter writer;
  static guint8 sdata[] = { 0xff, 0xf1, 0xf2, 0x80 };
  guint8 *data, i;
  GstBuffer *buf;
  GstMapInfo info;

  gst_bit_writer_init_with_size (&writer, 32, TRUE);
  fail_unless_equals_int (gst_bit_writer_get_remaining (&writer), 2048);

  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0xf, 4));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x7, 3));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x3, 2));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x3, 2));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x8, 4));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x1, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0xf2, 8));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x2, 2));

  fail_unless_equals_int (gst_bit_writer_get_remaining (&writer), 2048 - 26);

  data = gst_bit_writer_reset_and_get_data (&writer);
  fail_unless (data != NULL);

  for (i = 0; i < 4; i++)
    fail_unless (memcmp (&sdata[i], &data[i], 1) == 0);

  gst_bit_writer_init (&writer);

  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x7, 3));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0xf, 4));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x1, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x1, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x3, 2));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x8, 4));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x1, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0xf2, 8));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x1, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x0, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x0, 1));
  fail_unless (gst_bit_writer_put_bits_uint8 (&writer, 0x0, 3));

  fail_unless_equals_int (gst_bit_writer_get_size (&writer), 30);

  buf = gst_bit_writer_reset_and_get_buffer (&writer);
  fail_unless (buf != NULL);
  fail_unless (gst_buffer_map (buf, &info, GST_MAP_READWRITE));
  fail_unless (info.data);
  fail_unless_equals_int (info.size, 4);

  for (i = 0; i < 4; i++)
    fail_unless (memcmp (&sdata[i], &info.data[i], 1) == 0);

  g_free (data);
  gst_buffer_unmap (buf, &info);
  gst_buffer_unref (buf);
}

GST_END_TEST;

static Suite *
gst_bit_writer_suite (void)
{
  Suite *s = suite_create ("GstBitWriter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_initialization);
  tcase_add_test (tc_chain, test_data);
  tcase_add_test (tc_chain, test_reset);
  tcase_add_test (tc_chain, test_reset_data_unaligned);

  return s;
}

GST_CHECK_MAIN (gst_bit_writer);

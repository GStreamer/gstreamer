/* GStreamer
 *
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
#include <gst/codecparsers/nalutils.h>
#include <string.h>

GST_START_TEST (test_nal_writer_init)
{
  NalWriter nw;

  /* init with invalid params */
  ASSERT_CRITICAL (nal_writer_init (&nw, 0, TRUE));
  ASSERT_CRITICAL (nal_writer_init (&nw, 0, FALSE));
  ASSERT_CRITICAL (nal_writer_init (&nw, 5, TRUE));
  ASSERT_CRITICAL (nal_writer_init (&nw, 5, FALSE));

  nal_writer_init (&nw, 4, FALSE);
  nal_writer_reset (&nw);
}

GST_END_TEST;

GST_START_TEST (test_nal_writer_emulation_preventation)
{
  NalWriter nw;
  gint i, j;
  static guint8 patterns[][3] = {
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x01},
    {0x00, 0x00, 0x02},
    {0x00, 0x00, 0x03},
  };
  static guint8 expected_rst[][4] = {
    {0x00, 0x00, 0x03, 0x00},
    {0x00, 0x00, 0x03, 0x01},
    {0x00, 0x00, 0x03, 0x02},
    {0x00, 0x00, 0x03, 0x03},
  };

  /* Within the NAL unit, the following three-byte sequences shall not occur
   * at any byte-aligned position:
   * – 0x000000
   * – 0x000001
   * – 0x000002
   * Within the NAL unit, any four-byte sequence that starts with 0x000003
   * other than the following sequences shall not occur
   * at any byte-aligned position:
   * – 0x00000300
   * – 0x00000301
   * – 0x00000302
   * – 0x00000303
   */

  for (i = 0; i < G_N_ELEMENTS (patterns); i++) {
    GstMemory *mem;
    GstMapInfo info;

    nal_writer_init (&nw, 4, FALSE);

    /* forbidden_zero_bit */
    fail_unless (nal_writer_put_bits_uint8 (&nw, 0, 1));
    /* nal_ref_idc, just set zero for test */
    fail_unless (nal_writer_put_bits_uint8 (&nw, 0, 2));
    /* nal_unit_type, unknown h264 nal type */
    fail_unless (nal_writer_put_bits_uint8 (&nw, 0x1f, 5));

    for (j = 0; j < 3; j++)
      fail_unless (nal_writer_put_bits_uint8 (&nw, patterns[i][j], 8));

    mem = nal_writer_reset_and_get_memory (&nw);

    fail_unless (mem != NULL);
    fail_unless (gst_memory_map (mem, &info, GST_MAP_READ));

    /* start code prefix 4 + nalu header 1 + written bytes 3 +
     * emulation prevention byte 1 */
    assert_equals_int (info.size, (4 + 1 + 3 + 1));

    fail_if (memcmp (info.data + 5, expected_rst[i], 4));
    gst_memory_unmap (mem, &info);
    gst_memory_unref (mem);
  }
}

GST_END_TEST;

GST_START_TEST (test_nal_reader_read_epb)
{
  /* 00 00 03 is an emulation prevention sequence: the 03 carries no
   * data, so a 32-bit read over these 5 raw bytes only finds 3 data
   * bytes and must fail. */
  const guint8 data[] = { 0xaa, 0x00, 0x00, 0x03, 0xbb };
  NalReader nr;
  guint8 val;

  nal_reader_init (&nr, data, sizeof (data));
  fail_unless (nal_reader_get_bits_uint8 (&nr, &val, 8));
  assert_equals_int (val, 0xaa);

  fail_if (nal_reader_get_bits_uint8 (&nr, &val, 32));

  /* the failed read must not have consumed or corrupted anything */
  fail_unless (nal_reader_is_byte_aligned (&nr));
  assert_equals_int (nal_reader_get_remaining (&nr), 32);
  fail_unless (nal_reader_get_bits_uint8 (&nr, &val, 8));
  assert_equals_int (val, 0x00);
}

GST_END_TEST;

GST_START_TEST (test_nal_reader_read_epb_success)
{
  /* the emulation prevention byte must be skipped transparently */
  const guint8 data[] = { 0x11, 0x22, 0x00, 0x00, 0x03, 0x33, 0x44 };
  NalReader nr;
  guint32 val32;
  guint8 val8;

  nal_reader_init (&nr, data, sizeof (data));
  fail_unless (nal_reader_get_bits_uint32 (&nr, &val32, 32));
  assert_equals_int (val32, 0x11220000);
  fail_unless (nal_reader_get_bits_uint8 (&nr, &val8, 8));
  assert_equals_int (val8, 0x33);
  fail_unless (nal_reader_get_bits_uint8 (&nr, &val8, 8));
  assert_equals_int (val8, 0x44);
}

GST_END_TEST;

GST_START_TEST (test_nal_reader_skip_long_epb)
{
  /* 4 data bytes (01 02 00 00) plus one emulation prevention byte:
   * skipping 40 bits runs out of data and must leave the reader
   * untouched. */
  const guint8 data[] = { 0x01, 0x02, 0x00, 0x00, 0x03 };
  NalReader nr;
  guint8 val;

  nal_reader_init (&nr, data, sizeof (data));
  fail_if (nal_reader_skip_long (&nr, 40));

  assert_equals_int (nal_reader_get_pos (&nr), 0);
  fail_unless (nal_reader_get_bits_uint8 (&nr, &val, 8));
  assert_equals_int (val, 0x01);
}

GST_END_TEST;

static Suite *
nalutils_suite (void)
{
  Suite *s = suite_create ("H264/H264 Nal Utils");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_nal_writer_init);
  tcase_add_test (tc_chain, test_nal_writer_emulation_preventation);
  tcase_add_test (tc_chain, test_nal_reader_read_epb);
  tcase_add_test (tc_chain, test_nal_reader_read_epb_success);
  tcase_add_test (tc_chain, test_nal_reader_skip_long_epb);

  return s;
}

GST_CHECK_MAIN (nalutils);

/* GStreamer
 *
 * unit test for data protocol
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include "config.h"

#include <gst/check/gstcheck.h>

#ifndef GST_REMOVE_DEPRECATED
#undef GST_DISABLE_DEPRECATED
#endif

#include <gst/dataprotocol/dataprotocol.h>
#include "libs/gst/dataprotocol/dp-private.h"   /* private header */

/* test our method of reading and writing headers using TO/FROM_BE */
GST_START_TEST (test_conversion)
{
  guint8 array[9];
  guint8 write_array[9];
  guint16 read_two, expect_two;
  guint32 read_four, expect_four;
  guint64 read_eight, expect_eight;
  int i;

  for (i = 0; i < 9; ++i) {
    array[i] = i * 0x10;
  }

  /* read 8 16 bits */
  for (i = 0; i < 8; ++i) {
    read_two = GST_READ_UINT16_BE (array + i);
    expect_two = array[i] * (1 << 8) + array[i + 1];
    fail_unless (read_two == expect_two,
        "GST_READ_UINT16_BE %d: read %d != %d", i, read_two, expect_two);
  }

  /* write 8 16 bits */
  for (i = 0; i < 8; ++i) {
    GST_WRITE_UINT16_BE (&write_array[i], read_two);
    fail_unless (memcmp (array + 7, write_array + i, 2) == 0,
        "GST_WRITE_UINT16_BE %d: memcmp failed", i);
  }

  /* read 5 32 bits */
  for (i = 0; i < 5; ++i) {
    read_four = GST_READ_UINT32_BE (array + i);
    expect_four = array[i] * (1 << 24) + array[i + 1] * (1 << 16)
        + array[i + 2] * (1 << 8) + array[i + 3];
    fail_unless (read_four == expect_four,
        "GST_READ_UINT32_BE %d: read %d != %d", i, read_four, expect_four);
  }

  /* read 2 64 bits */
  for (i = 0; i < 2; ++i) {
    read_eight = GST_READ_UINT64_BE (array + i);
    expect_eight = array[i] * (1LL << 56) + array[i + 1] * (1LL << 48)
        + array[i + 2] * (1LL << 40) + array[i + 3] * (1LL << 32)
        + array[i + 4] * (1 << 24) + array[i + 5] * (1 << 16)
        + array[i + 6] * (1 << 8) + array[i + 7];
    fail_unless (read_eight == expect_eight,
        "GST_READ_UINT64_BE %d: read %" G_GUINT64_FORMAT
        " != %" G_GUINT64_FORMAT, i, read_eight, expect_eight);
  }

  /* write 1 64 bit */
  GST_WRITE_UINT64_BE (&write_array[0], read_eight);
  fail_unless (memcmp (array + 1, write_array, 8) == 0,
      "GST_WRITE_UINT64_BE: memcmp failed");
}

GST_END_TEST;

static Suite *
gst_dp_suite (void)
{
  Suite *s = suite_create ("data protocol");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, gst_dp_init, NULL);
  tcase_add_test (tc_chain, test_conversion);

  return s;
}

GST_CHECK_MAIN (gst_dp);

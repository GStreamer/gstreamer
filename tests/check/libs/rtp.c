/* GStreamer unit tests for the RTP support library
 *
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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

#include <gst/check/gstcheck.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>

#define RTP_HEADER_LEN 12

GST_START_TEST (test_rtp_buffer)
{
  GstBuffer *buf;
  guint8 *data;

  /* check GstRTPHeader structure alignment and packing */
  buf = gst_rtp_buffer_new_allocate (16, 4, 0);
  fail_unless (buf != NULL);
  fail_unless_equals_int (GST_BUFFER_SIZE (buf), RTP_HEADER_LEN + 16 + 4);
  data = GST_BUFFER_DATA (buf);

  /* check defaults */
  fail_unless_equals_int (gst_rtp_buffer_get_version (buf), 2);
  fail_unless (gst_rtp_buffer_get_padding (buf) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension (buf) == FALSE);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (buf), 0);
  fail_unless (gst_rtp_buffer_get_marker (buf) == FALSE);
  fail_unless (gst_rtp_buffer_get_payload_type (buf) == 0);
  fail_unless_equals_int (GST_READ_UINT16_BE (data), 0x8000);

  /* check version in bitfield */
  gst_rtp_buffer_set_version (buf, 3);
  fail_unless_equals_int (gst_rtp_buffer_get_version (buf), 3);
  fail_unless_equals_int ((data[0] & 0xC0) >> 6, 3);
  gst_rtp_buffer_set_version (buf, 2);
  fail_unless_equals_int (gst_rtp_buffer_get_version (buf), 2);
  fail_unless_equals_int ((data[0] & 0xC0) >> 6, 2);

  /* check padding bit */
  gst_rtp_buffer_set_padding (buf, TRUE);
  fail_unless (gst_rtp_buffer_get_padding (buf) == TRUE);
  fail_unless_equals_int ((data[0] & 0x20) >> 5, 1);
  gst_rtp_buffer_set_padding (buf, FALSE);
  fail_unless (gst_rtp_buffer_get_padding (buf) == FALSE);
  fail_unless_equals_int ((data[0] & 0x20) >> 5, 0);

  /* check marker bit */
  gst_rtp_buffer_set_marker (buf, TRUE);
  fail_unless (gst_rtp_buffer_get_marker (buf) == TRUE);
  fail_unless_equals_int ((data[1] & 0x80) >> 7, 1);
  gst_rtp_buffer_set_marker (buf, FALSE);
  fail_unless (gst_rtp_buffer_get_marker (buf) == FALSE);
  fail_unless_equals_int ((data[1] & 0x80) >> 7, 0);

  /* check sequence offset */
  gst_rtp_buffer_set_seq (buf, 0xF2C9);
  fail_unless_equals_int (gst_rtp_buffer_get_seq (buf), 0xF2C9);
  fail_unless_equals_int (GST_READ_UINT16_BE (data + 2), 0xF2C9);
  gst_rtp_buffer_set_seq (buf, 0);
  fail_unless_equals_int (gst_rtp_buffer_get_seq (buf), 0);
  fail_unless_equals_int (GST_READ_UINT16_BE (data + 2), 0);

  /* check timestamp offset */
  gst_rtp_buffer_set_timestamp (buf, 432191);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4), 432191);
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (buf), 432191);
  gst_rtp_buffer_set_timestamp (buf, 0);
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (buf), 0);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4), 0);

  /* check ssrc offset */
  gst_rtp_buffer_set_ssrc (buf, 0xf04043C2);
  fail_unless_equals_int (gst_rtp_buffer_get_ssrc (buf), (gint) 0xf04043c2);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4 + 4), (gint) 0xf04043c2);
  gst_rtp_buffer_set_ssrc (buf, 0);
  fail_unless_equals_int (gst_rtp_buffer_get_ssrc (buf), 0);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4 + 4), 0);

  /* check csrc bits */
  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (buf), 0);
  ASSERT_CRITICAL (gst_rtp_buffer_get_csrc (buf, 0));
  fail_unless_equals_int (data[0] & 0xf, 0);
  gst_buffer_unref (buf);

  /* and again, this time with CSRCs */
  buf = gst_rtp_buffer_new_allocate (16, 4, 3);
  fail_unless (buf != NULL);
  fail_unless_equals_int (GST_BUFFER_SIZE (buf),
      RTP_HEADER_LEN + 16 + 4 + 4 * 3);

  data = GST_BUFFER_DATA (buf);

  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (buf), 3);
  ASSERT_CRITICAL (gst_rtp_buffer_get_csrc (buf, 3));
  fail_unless_equals_int (data[0] & 0xf, 3);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc (buf, 0), 0);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc (buf, 1), 0);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc (buf, 2), 0);

  data += RTP_HEADER_LEN;       /* skip the other header stuff */
  gst_rtp_buffer_set_csrc (buf, 0, 0xf7c0);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 0 * 4), 0xf7c0);
  gst_rtp_buffer_set_csrc (buf, 1, 0xf7c1);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 1 * 4), 0xf7c1);
  gst_rtp_buffer_set_csrc (buf, 2, 0xf7c2);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 2 * 4), 0xf7c2);
  ASSERT_CRITICAL (gst_rtp_buffer_set_csrc (buf, 3, 0xf123));
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_rtp_buffer_set_extension_data)
{
  GstBuffer *buf;
  guint8 *data;
  guint16 bits;
  guint size;
  gpointer pointer;

  /* check GstRTPHeader structure alignment and packing */
  buf = gst_rtp_buffer_new_allocate (4, 0, 0);
  data = GST_BUFFER_DATA (buf);

  /* should be impossible to set the extension data */
  fail_unless (gst_rtp_buffer_set_extension_data (buf, 0, 4) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension (buf) == TRUE);

  /* should be possible to set the extension data */
  fail_unless (gst_rtp_buffer_set_extension_data (buf, 270, 0) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension (buf) == TRUE);
  gst_rtp_buffer_get_extension_data (buf, &bits, &pointer, &size);
  fail_unless (bits == 270);
  fail_unless (size == 0);
  fail_unless (pointer == GST_BUFFER_DATA (buf) + 16);
  pointer = gst_rtp_buffer_get_payload (buf);
  fail_unless (pointer == GST_BUFFER_DATA (buf) + 16);
  gst_buffer_unref (buf);

  buf = gst_rtp_buffer_new_allocate (20, 0, 0);
  data = GST_BUFFER_DATA (buf);
  fail_unless (gst_rtp_buffer_get_extension (buf) == FALSE);
  fail_unless (gst_rtp_buffer_set_extension_data (buf, 333, 2) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension (buf) == TRUE);
  gst_rtp_buffer_get_extension_data (buf, &bits, &pointer, &size);
  fail_unless (bits == 333);
  fail_unless (size == 2);
  fail_unless (pointer == GST_BUFFER_DATA (buf) + 16);
  pointer = gst_rtp_buffer_get_payload (buf);
  fail_unless (pointer == GST_BUFFER_DATA (buf) + 24);
  gst_buffer_unref (buf);
}

GST_END_TEST;

static Suite *
rtp_suite (void)
{
  Suite *s = suite_create ("rtp support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtp_buffer);
  tcase_add_test (tc_chain, test_rtp_buffer_set_extension_data);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = rtp_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

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
#include <gst/rtp/gstrtcpbuffer.h>
#include <string.h>

#define RTP_HEADER_LEN 12

GST_START_TEST (test_rtp_buffer)
{
  GstBuffer *buf;
  GstMapInfo map;
  guint8 *data;
  gsize size;
  GstRTPBuffer rtp = { NULL, };

  /* check GstRTPHeader structure alignment and packing */
  buf = gst_rtp_buffer_new_allocate (16, 4, 0);
  fail_unless (buf != NULL);
  gst_buffer_map (buf, &map, GST_MAP_READWRITE);
  data = map.data;
  size = map.size;
  fail_unless_equals_int (size, RTP_HEADER_LEN + 16 + 4);

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);

  /* check defaults */
  fail_unless_equals_int (gst_rtp_buffer_get_version (&rtp), 2);
  fail_unless (gst_rtp_buffer_get_padding (&rtp) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension (&rtp) == FALSE);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (&rtp), 0);
  fail_unless (gst_rtp_buffer_get_marker (&rtp) == FALSE);
  fail_unless (gst_rtp_buffer_get_payload_type (&rtp) == 0);
  fail_unless_equals_int (GST_READ_UINT16_BE (data), 0xa000);

  /* check version in bitfield */
  gst_rtp_buffer_set_version (&rtp, 3);
  fail_unless_equals_int (gst_rtp_buffer_get_version (&rtp), 3);
  fail_unless_equals_int ((data[0] & 0xC0) >> 6, 3);
  gst_rtp_buffer_set_version (&rtp, 2);
  fail_unless_equals_int (gst_rtp_buffer_get_version (&rtp), 2);
  fail_unless_equals_int ((data[0] & 0xC0) >> 6, 2);

  /* check padding bit */
  gst_rtp_buffer_set_padding (&rtp, TRUE);
  fail_unless (gst_rtp_buffer_get_padding (&rtp) == TRUE);
  fail_unless_equals_int ((data[0] & 0x20) >> 5, 1);
  gst_rtp_buffer_set_padding (&rtp, FALSE);
  fail_unless (gst_rtp_buffer_get_padding (&rtp) == FALSE);
  fail_unless_equals_int ((data[0] & 0x20) >> 5, 0);

  /* check marker bit */
  gst_rtp_buffer_set_marker (&rtp, TRUE);
  fail_unless (gst_rtp_buffer_get_marker (&rtp) == TRUE);
  fail_unless_equals_int ((data[1] & 0x80) >> 7, 1);
  gst_rtp_buffer_set_marker (&rtp, FALSE);
  fail_unless (gst_rtp_buffer_get_marker (&rtp) == FALSE);
  fail_unless_equals_int ((data[1] & 0x80) >> 7, 0);

  /* check sequence offset */
  gst_rtp_buffer_set_seq (&rtp, 0xF2C9);
  fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp), 0xF2C9);
  fail_unless_equals_int (GST_READ_UINT16_BE (data + 2), 0xF2C9);
  gst_rtp_buffer_set_seq (&rtp, 0);
  fail_unless_equals_int (gst_rtp_buffer_get_seq (&rtp), 0);
  fail_unless_equals_int (GST_READ_UINT16_BE (data + 2), 0);

  /* check timestamp offset */
  gst_rtp_buffer_set_timestamp (&rtp, 432191);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4), 432191);
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&rtp), 432191);
  gst_rtp_buffer_set_timestamp (&rtp, 0);
  fail_unless_equals_int (gst_rtp_buffer_get_timestamp (&rtp), 0);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4), 0);

  /* check ssrc offset */
  gst_rtp_buffer_set_ssrc (&rtp, 0xf04043C2);
  fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp), (gint) 0xf04043c2);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4 + 4), (gint) 0xf04043c2);
  gst_rtp_buffer_set_ssrc (&rtp, 0);
  fail_unless_equals_int (gst_rtp_buffer_get_ssrc (&rtp), 0);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 4 + 4), 0);

  /* check csrc bits */
  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (&rtp), 0);
  ASSERT_CRITICAL (gst_rtp_buffer_get_csrc (&rtp, 0));
  fail_unless_equals_int (data[0] & 0xf, 0);

  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);

  /* and again, this time with CSRCs */
  buf = gst_rtp_buffer_new_allocate (16, 4, 3);
  fail_unless (buf != NULL);
  gst_buffer_map (buf, &map, GST_MAP_READWRITE);
  data = map.data;
  size = map.size;
  fail_unless_equals_int (size, RTP_HEADER_LEN + 16 + 4 + 4 * 3);

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);

  fail_unless_equals_int (gst_rtp_buffer_get_csrc_count (&rtp), 3);
  ASSERT_CRITICAL (gst_rtp_buffer_get_csrc (&rtp, 3));
  fail_unless_equals_int (data[0] & 0xf, 3);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc (&rtp, 0), 0);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc (&rtp, 1), 0);
  fail_unless_equals_int (gst_rtp_buffer_get_csrc (&rtp, 2), 0);

  data += RTP_HEADER_LEN;       /* skip the other header stuff */
  gst_rtp_buffer_set_csrc (&rtp, 0, 0xf7c0);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 0 * 4), 0xf7c0);
  gst_rtp_buffer_set_csrc (&rtp, 1, 0xf7c1);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 1 * 4), 0xf7c1);
  gst_rtp_buffer_set_csrc (&rtp, 2, 0xf7c2);
  fail_unless_equals_int (GST_READ_UINT32_BE (data + 2 * 4), 0xf7c2);
  ASSERT_CRITICAL (gst_rtp_buffer_set_csrc (&rtp, 3, 0xf123));

  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_rtp_buffer_validate_corrupt)
{
  GstBuffer *buf;
  guint8 corrupt_rtp_packet[58] = {
    0x90, 0x7a, 0xbf, 0x28, 0x3a, 0x8a, 0x0a, 0xf4, 0x69, 0x6b, 0x76, 0xc0,
    0x21, 0xe0, 0xe0, 0x60, 0x81, 0x10, 0x84, 0x30, 0x21, 0x52, 0x06, 0xc2,
    0xb8, 0x30, 0x10, 0x4c, 0x08, 0x62, 0x67, 0xc2, 0x6e, 0x1a, 0x53, 0x3f,
    0xaf, 0xd6, 0x1b, 0x29, 0x40, 0xe0, 0xa5, 0x83, 0x01, 0x4b, 0x04, 0x02,
    0xb0, 0x97, 0x63, 0x08, 0x10, 0x4b, 0x43, 0x85, 0x37, 0x2c
  };
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  buf = gst_buffer_new_and_alloc (sizeof (corrupt_rtp_packet));
  gst_buffer_fill (buf, 0, corrupt_rtp_packet, sizeof (corrupt_rtp_packet));
  fail_if (gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp));
  gst_buffer_unref (buf);
}

GST_END_TEST;

#if 0
GST_START_TEST (test_rtp_buffer_list)
{
  GstBuffer *rtp_header;
  GstBuffer *rtp_payload;
  GstBufferList *list = NULL;
  GstBufferListIterator *it;
  guint i;

  list = gst_buffer_list_new ();
  it = gst_buffer_list_iterate (list);

  /* Creating a list of two RTP packages */

  /* Create first group to hold the rtp header and the payload */
  gst_buffer_list_iterator_add_group (it);
  rtp_header = gst_rtp_buffer_new_allocate (0, 0, 0);
  gst_buffer_list_iterator_add (it, rtp_header);
  rtp_payload = gst_buffer_new_and_alloc (42);
  gst_buffer_list_iterator_add (it, rtp_payload);

  /* Create second group to hold an rtp header and a payload */
  gst_buffer_list_iterator_add_group (it);
  rtp_header = gst_rtp_buffer_new_allocate (0, 0, 0);
  gst_buffer_list_iterator_add (it, rtp_header);
  rtp_payload = gst_buffer_new_and_alloc (42);
  gst_buffer_list_iterator_add (it, rtp_payload);

  gst_buffer_list_iterator_free (it);

  /* Test SEQ number */
  i = gst_rtp_buffer_list_set_seq (list, 1024);
  fail_if (1026 != i);
  fail_if (!gst_rtp_buffer_list_validate (list));

  /* Timestamp */
  gst_rtp_buffer_list_set_timestamp (list, 432191);
  fail_unless_equals_int (gst_rtp_buffer_list_get_timestamp (list), 432191);

  /* SSRC */
  gst_rtp_buffer_list_set_ssrc (list, 0xf04043C2);
  fail_unless_equals_int (gst_rtp_buffer_list_get_ssrc (list),
      (gint) 0xf04043c2);

  /* Payload type */
  gst_rtp_buffer_list_set_payload_type (list, 127);
  fail_unless_equals_int (gst_rtp_buffer_list_get_payload_type (list), 127);

  gst_buffer_list_unref (list);
}

GST_END_TEST;
#endif

GST_START_TEST (test_rtp_buffer_set_extension_data)
{
  GstBuffer *buf;
  guint8 *data;
  guint16 bits;
  guint size;
  guint8 misc_data[4] = { 1, 2, 3, 4 };
  gpointer pointer;
  guint8 appbits;
  GstRTPBuffer rtp = { NULL, };

  /* check GstRTPHeader structure alignment and packing */
  buf = gst_rtp_buffer_new_allocate (4, 0, 0);

  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);

  /* should be possible to set the extension data */
  fail_unless (gst_rtp_buffer_set_extension_data (&rtp, 270, 4) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension (&rtp) == TRUE);
  gst_rtp_buffer_get_extension_data (&rtp, &bits, &pointer, &size);
  fail_unless (bits == 270);
  fail_unless (size == 4);
  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (buf);

  buf = gst_rtp_buffer_new_allocate (20, 0, 0);
  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);

  fail_unless (gst_rtp_buffer_get_extension (&rtp) == FALSE);
  fail_unless (gst_rtp_buffer_set_extension_data (&rtp, 333, 2) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension (&rtp) == TRUE);
  gst_rtp_buffer_get_extension_data (&rtp, &bits, &pointer, &size);
  fail_unless (bits == 333);
  fail_unless (size == 2);

  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (buf);

  /* Test header extensions with a one byte header */
  buf = gst_rtp_buffer_new_allocate (20, 0, 0);
  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);

  fail_unless (gst_rtp_buffer_get_extension (&rtp) == FALSE);

  fail_unless (gst_rtp_buffer_add_extension_onebyte_header (&rtp, 5,
          misc_data, 2) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension_data (&rtp, &bits, &pointer,
          &size));
  fail_unless (bits == 0xBEDE);
  fail_unless (size == 1);
  data = (guint8 *) pointer;
  fail_unless (data[0] == ((5 << 4) | 1));
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 2,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  fail_unless (gst_rtp_buffer_add_extension_onebyte_header (&rtp, 5,
          misc_data, 4) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 2,
          1, &pointer, &size) == FALSE);

  fail_unless (gst_rtp_buffer_add_extension_onebyte_header (&rtp, 6,
          misc_data, 2) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          3, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 2,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 6,
          2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (buf);

  /* Test header extensions with a two bytes header */
  buf = gst_rtp_buffer_new_allocate (20, 0, 0);
  gst_rtp_buffer_map (buf, GST_MAP_READWRITE, &rtp);

  fail_unless (gst_rtp_buffer_get_extension (&rtp) == FALSE);

  fail_unless (gst_rtp_buffer_add_extension_twobytes_header (&rtp, 0, 5,
          misc_data, 2) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension_data (&rtp, &bits, &pointer,
          &size));
  fail_unless (bits == 0x100 << 4);
  fail_unless (size == 1);
  data = (guint8 *) pointer;
  fail_unless (data[0] == 5);
  fail_unless (data[1] == 2);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 2,
          0, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  fail_unless (gst_rtp_buffer_add_extension_twobytes_header (&rtp, 0, 5,
          misc_data, 4) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 2,
          0, &pointer, &size) == FALSE);

  fail_unless (gst_rtp_buffer_add_extension_twobytes_header (&rtp, 0, 6,
          misc_data, 2) == TRUE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 2,
          0, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 6,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_get_extension_twobytes_header (&rtp, &appbits, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (buf);
}

GST_END_TEST;

#if 0
GST_START_TEST (test_rtp_buffer_list_set_extension)
{
  GstBufferList *list;
  GstBuffer *buf;
  guint8 *data;
  guint16 bits;
  guint size;
  guint8 misc_data[4] = { 1, 2, 3, 4 };
  gpointer pointer;
  guint8 appbits;
  GstBufferListIterator *it;

  buf = gst_rtp_buffer_new_allocate (20, 0, 0);
  list = gst_rtp_buffer_list_from_buffer (buf);
  gst_buffer_unref (buf);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf != NULL);
  fail_unless (GST_BUFFER_SIZE (buf) == 12);
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (buf != NULL);
  fail_unless (GST_BUFFER_SIZE (buf) == 20);
  gst_buffer_list_iterator_free (it);

  /* Test header extensions with a one byte header */
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 2,
          1, &pointer, &size) == FALSE);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_rtp_buffer_list_add_extension_onebyte_header (it, 5,
          misc_data, 2) == TRUE);
  gst_buffer_list_iterator_free (it);
  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (gst_rtp_buffer_get_extension_data (buf, &bits, &pointer, &size));
  fail_unless (bits == 0xBEDE);
  fail_unless (size == 1);
  data = (guint8 *) pointer;
  fail_unless (data[0] == ((5 << 4) | 1));
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 2,
          0, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_rtp_buffer_list_add_extension_onebyte_header (it, 5,
          misc_data, 4) == TRUE);
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 2,
          0, &pointer, &size) == FALSE);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_rtp_buffer_list_add_extension_onebyte_header (it, 6,
          misc_data, 2) == TRUE);
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 2,
          0, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 6,
          1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_onebyte_header (list, 0, 5,
          0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  gst_buffer_list_unref (list);


  /* Test header extensions with a two bytes header */
  buf = gst_rtp_buffer_new_allocate (20, 0, 0);
  list = gst_rtp_buffer_list_from_buffer (buf);
  gst_buffer_unref (buf);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_rtp_buffer_list_add_extension_twobytes_header (it, 0, 5,
          misc_data, 2) == TRUE);
  gst_buffer_list_iterator_free (it);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  buf = gst_buffer_list_iterator_next (it);
  fail_unless (gst_rtp_buffer_get_extension_data (buf, &bits, &pointer, &size));
  fail_unless (bits == 0x100 << 4);
  fail_unless (size == 1);
  data = (guint8 *) pointer;
  fail_unless (data[0] == 5);
  fail_unless (data[1] == 2);
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 2, 0, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_rtp_buffer_list_add_extension_twobytes_header (it, 0, 5,
          misc_data, 4) == TRUE);
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 2, 0, &pointer, &size) == FALSE);

  it = gst_buffer_list_iterate (list);
  fail_unless (gst_buffer_list_iterator_next_group (it));
  fail_unless (gst_buffer_list_iterator_next (it) != NULL);
  fail_unless (gst_rtp_buffer_list_add_extension_twobytes_header (it, 0, 6,
          misc_data, 2) == TRUE);
  gst_buffer_list_iterator_free (it);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 1, &pointer, &size) == TRUE);
  fail_unless (size == 4);
  fail_unless (memcmp (pointer, misc_data, 4) == 0);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 2, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 2, 0, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 6, 1, &pointer, &size) == FALSE);
  fail_unless (gst_rtp_buffer_list_get_extension_twobytes_header (list, 0,
          &appbits, 5, 0, &pointer, &size) == TRUE);
  fail_unless (size == 2);
  fail_unless (memcmp (pointer, misc_data, 2) == 0);

  gst_buffer_list_unref (list);
}

GST_END_TEST;
#endif

GST_START_TEST (test_rtp_seqnum_compare)
{
#define ASSERT_COMP(a,b,c) fail_unless (gst_rtp_buffer_compare_seqnum ((guint16)a,(guint16)b) == c);
  ASSERT_COMP (0xfffe, 0xfffd, -1);
  ASSERT_COMP (0xffff, 0xfffe, -1);
  ASSERT_COMP (0x0000, 0xffff, -1);
  ASSERT_COMP (0x0001, 0x0000, -1);
  ASSERT_COMP (0x0002, 0x0001, -1);

  ASSERT_COMP (0xffff, 0xfffd, -2);
  ASSERT_COMP (0x0000, 0xfffd, -3);
  ASSERT_COMP (0x0001, 0xfffd, -4);
  ASSERT_COMP (0x0002, 0xfffd, -5);

  ASSERT_COMP (0x7ffe, 0x7ffd, -1);
  ASSERT_COMP (0x7fff, 0x7ffe, -1);
  ASSERT_COMP (0x8000, 0x7fff, -1);
  ASSERT_COMP (0x8001, 0x8000, -1);
  ASSERT_COMP (0x8002, 0x8001, -1);

  ASSERT_COMP (0x7fff, 0x7ffd, -2);
  ASSERT_COMP (0x8000, 0x7ffd, -3);
  ASSERT_COMP (0x8001, 0x7ffd, -4);
  ASSERT_COMP (0x8002, 0x7ffd, -5);

  ASSERT_COMP (0x7ffd, 0xffff, -0x7ffe);
  ASSERT_COMP (0x7ffe, 0x0000, -0x7ffe);
  ASSERT_COMP (0x7fff, 0x0001, -0x7ffe);
  ASSERT_COMP (0x7fff, 0x0000, -0x7fff);
  ASSERT_COMP (0x8000, 0x0001, -0x7fff);
  ASSERT_COMP (0x8001, 0x0002, -0x7fff);

  ASSERT_COMP (0xfffd, 0x7ffe, -0x7fff);
  ASSERT_COMP (0xfffe, 0x7fff, -0x7fff);
  ASSERT_COMP (0xffff, 0x8000, -0x7fff);
  ASSERT_COMP (0x0000, 0x8001, -0x7fff);
  ASSERT_COMP (0x0001, 0x8002, -0x7fff);

  ASSERT_COMP (0xfffe, 0x7ffe, -0x8000);
  ASSERT_COMP (0xffff, 0x7fff, -0x8000);
  ASSERT_COMP (0x0000, 0x8000, -0x8000);
  ASSERT_COMP (0x0001, 0x8001, -0x8000);

  ASSERT_COMP (0x7ffe, 0xfffe, -0x8000);
  ASSERT_COMP (0x7fff, 0xffff, -0x8000);
  ASSERT_COMP (0x8000, 0x0000, -0x8000);
  ASSERT_COMP (0x8001, 0x0001, -0x8000);

  ASSERT_COMP (0x0001, 0x0002, 1);
  ASSERT_COMP (0x0000, 0x0001, 1);
  ASSERT_COMP (0xffff, 0x0000, 1);
  ASSERT_COMP (0xfffe, 0xffff, 1);
  ASSERT_COMP (0xfffd, 0xfffe, 1);

  ASSERT_COMP (0x0000, 0x0002, 2);
  ASSERT_COMP (0xffff, 0x0002, 3);
  ASSERT_COMP (0xfffe, 0x0002, 4);
  ASSERT_COMP (0xfffd, 0x0002, 5);

  ASSERT_COMP (0x8001, 0x8002, 1);
  ASSERT_COMP (0x8000, 0x8001, 1);
  ASSERT_COMP (0x7fff, 0x8000, 1);
  ASSERT_COMP (0x7ffe, 0x7fff, 1);
  ASSERT_COMP (0x7ffd, 0x7ffe, 1);

  ASSERT_COMP (0x8000, 0x8002, 2);
  ASSERT_COMP (0x7fff, 0x8002, 3);
  ASSERT_COMP (0x7ffe, 0x8002, 4);
  ASSERT_COMP (0x7ffd, 0x8002, 5);

  ASSERT_COMP (0xfffe, 0x7ffd, 0x7fff);
  ASSERT_COMP (0xffff, 0x7ffe, 0x7fff);
  ASSERT_COMP (0x0000, 0x7fff, 0x7fff);
  ASSERT_COMP (0x0001, 0x8000, 0x7fff);
  ASSERT_COMP (0x0002, 0x8001, 0x7fff);

  ASSERT_COMP (0x7ffe, 0xfffd, 0x7fff);
  ASSERT_COMP (0x7fff, 0xfffe, 0x7fff);
  ASSERT_COMP (0x8000, 0xffff, 0x7fff);
  ASSERT_COMP (0x8001, 0x0000, 0x7fff);
  ASSERT_COMP (0x8002, 0x0001, 0x7fff);
#undef ASSERT_COMP
}

GST_END_TEST;

GST_START_TEST (test_rtcp_buffer)
{
  GstBuffer *buf;
  GstRTCPPacket packet;
  GstRTCPBuffer rtcp = { NULL, };
  gsize offset;
  gsize maxsize;

  buf = gst_rtcp_buffer_new (1400);
  fail_unless (buf != NULL);
  fail_unless_equals_int (gst_buffer_get_sizes (buf, &offset, &maxsize), 0);
  fail_unless_equals_int (offset, 0);
  fail_unless_equals_int (maxsize, 1400);

  gst_rtcp_buffer_map (buf, GST_MAP_READWRITE, &rtcp);

  fail_unless (gst_rtcp_buffer_validate (buf) == FALSE);
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &packet) == FALSE);
  fail_unless (gst_rtcp_buffer_get_packet_count (&rtcp) == 0);

  /* add an SR packet */
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_SR,
          &packet) == TRUE);

  fail_unless (gst_rtcp_packet_get_padding (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_count (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_type (&packet) == GST_RTCP_TYPE_SR);
  fail_unless (gst_rtcp_packet_get_length (&packet) == 6);

  gst_rtcp_packet_sr_set_sender_info (&packet, 0x44556677,
      G_GUINT64_CONSTANT (1), 0x11111111, 101, 123456);
  {
    guint32 ssrc;
    guint64 ntptime;
    guint32 rtptime;
    guint32 packet_count;
    guint32 octet_count;

    gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, &ntptime, &rtptime,
        &packet_count, &octet_count);

    fail_unless (ssrc == 0x44556677);
    fail_unless (ntptime == G_GUINT64_CONSTANT (1));
    fail_unless (rtptime == 0x11111111);
    fail_unless (packet_count == 101);
    fail_unless (octet_count == 123456);
  }

  /* go to first packet, this should be the packet we just added */
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &packet) == TRUE);

  fail_unless (gst_rtcp_packet_get_padding (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_count (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_type (&packet) == GST_RTCP_TYPE_SR);
  fail_unless (gst_rtcp_packet_get_length (&packet) == 6);

  fail_unless (gst_rtcp_packet_move_to_next (&packet) == FALSE);

  /* add some SDES */
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_SDES,
          &packet) == TRUE);
  fail_unless (gst_rtcp_packet_sdes_add_item (&packet, 0xff658743) == TRUE);
  fail_unless (gst_rtcp_packet_sdes_add_entry (&packet, GST_RTCP_SDES_CNAME,
          sizeof ("test@foo.bar"), (guint8 *) "test@foo.bar") == TRUE);

  /* add some BYE */
  fail_unless (gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_BYE,
          &packet) == TRUE);
  fail_unless (gst_rtcp_packet_bye_add_ssrc (&packet, 0x5613212f) == TRUE);
  fail_unless (gst_rtcp_packet_bye_add_ssrc (&packet, 0x00112233) == TRUE);
  fail_unless (gst_rtcp_packet_bye_get_ssrc_count (&packet) == 2);

  fail_unless (gst_rtcp_packet_get_padding (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_count (&packet) == 2);
  fail_unless (gst_rtcp_packet_get_type (&packet) == GST_RTCP_TYPE_BYE);
  fail_unless (gst_rtcp_packet_get_length (&packet) == 2);

  /* move to SDES */
  fail_unless (gst_rtcp_buffer_get_first_packet (&rtcp, &packet) == TRUE);
  fail_unless (gst_rtcp_packet_move_to_next (&packet) == TRUE);

  fail_unless (gst_rtcp_packet_get_padding (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_count (&packet) == 1);
  fail_unless (gst_rtcp_packet_get_type (&packet) == GST_RTCP_TYPE_SDES);
  fail_unless (gst_rtcp_packet_get_length (&packet) == 5);

  /* remove the SDES */
  fail_unless (gst_rtcp_packet_remove (&packet) == TRUE);

  /* we are now at the BYE packet */
  fail_unless (gst_rtcp_packet_get_padding (&packet) == 0);
  fail_unless (gst_rtcp_packet_get_count (&packet) == 2);
  fail_unless (gst_rtcp_packet_get_type (&packet) == GST_RTCP_TYPE_BYE);
  fail_unless (gst_rtcp_packet_get_length (&packet) == 2);

  /* close and validate */
  gst_rtcp_buffer_unmap (&rtcp);
  fail_unless (gst_rtcp_buffer_validate (buf) == TRUE);
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
  tcase_add_test (tc_chain, test_rtp_buffer_validate_corrupt);
  tcase_add_test (tc_chain, test_rtp_buffer_set_extension_data);
  //tcase_add_test (tc_chain, test_rtp_buffer_list_set_extension);
  tcase_add_test (tc_chain, test_rtp_seqnum_compare);

  tcase_add_test (tc_chain, test_rtcp_buffer);

  //tcase_add_test (tc_chain, test_rtp_buffer_list);

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

/* GStreamer
 *
 * unit test for jpegparse
 *
 * Copyright (C) <2009> Arnout Vandecappelle (Essensium/Mind) <arnout@mind.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>

/* This test doesn't use actual JPEG data, but some fake data that we know
   will trigger certain paths in jpegparse. */

guint8 test_data_garbage[] = { 0x00, 0x01, 0xff, 0x32, 0x00, 0xff };
guint8 test_data_short_frame[] = { 0xff, 0xd8, 0xff, 0xd9 };

guint8 test_data_normal_frame[] = { 0xff, 0xd8, 0xff, 0x12, 0x00, 0x03, 0x33,
  0xff, 0xd9
};

guint8 test_data_entropy[] = { 0xff, 0xd8, 0xff, 0xda, 0x00, 0x04, 0x22, 0x33,
  0x44, 0xff, 0x00, 0x55, 0xff, 0x04, 0x00, 0x04, 0x22, 0x33, 0xff, 0xd9
};
guint8 test_data_ff[] = { 0xff, 0xff };

guint8 test_data_extra_ff[] = { 0xff, 0xd8, 0xff, 0xff, 0xff, 0x12, 0x00, 0x03,
  0x33, 0xff, 0xff, 0xff, 0xd9
};

static GList *
_make_buffers_in (GList * buffer_in, guint8 * test_data, gsize test_data_size)
{
  GstBuffer *buffer;
  gsize i;

  for (i = 0; i < test_data_size; i++) {
    buffer = gst_buffer_new ();
    gst_buffer_set_data (buffer, test_data + i, 1);
    gst_buffer_set_caps (buffer, gst_caps_new_simple ("image/jpeg", "parsed",
            G_TYPE_BOOLEAN, FALSE, NULL));
    buffer_in = g_list_append (buffer_in, buffer);
  }
  return buffer_in;
}

#define make_buffers_in(buffer_in, test_data) \
    _make_buffers_in(buffer_in, test_data, sizeof(test_data))

static GList *
_make_buffers_out (GList * buffer_out, guint8 * test_data, gsize test_data_size)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new ();
  gst_buffer_set_data (buffer, test_data, test_data_size);
  gst_buffer_set_caps (buffer, gst_caps_new_simple ("image/jpeg", "parsed",
          G_TYPE_BOOLEAN, TRUE, NULL));
  buffer_out = g_list_append (buffer_out, buffer);
  return buffer_out;
}

#define make_buffers_out(buffer_out, test_data) \
    _make_buffers_out(buffer_out, test_data, sizeof(test_data))

GST_START_TEST (test_parse_single_byte)
{
  GList *buffer_in = NULL, *buffer_out = NULL;

  /* Push the data byte by byte, injecting some garbage. */
  buffer_in = make_buffers_in (buffer_in, test_data_garbage);
  buffer_in = make_buffers_in (buffer_in, test_data_short_frame);
  buffer_in = make_buffers_in (buffer_in, test_data_garbage);
  buffer_in = make_buffers_in (buffer_in, test_data_normal_frame);
  buffer_in = make_buffers_in (buffer_in, test_data_ff);
  buffer_in = make_buffers_in (buffer_in, test_data_entropy);
  buffer_in = make_buffers_in (buffer_in, test_data_extra_ff);

  buffer_out = make_buffers_out (buffer_out, test_data_short_frame);
  buffer_out = make_buffers_out (buffer_out, test_data_normal_frame);
  buffer_out = make_buffers_out (buffer_out, test_data_entropy);
  buffer_out = make_buffers_out (buffer_out, test_data_extra_ff);
  gst_check_element_push_buffer_list ("jpegparse", buffer_in, buffer_out,
      GST_FLOW_OK);
}

GST_END_TEST;



GST_START_TEST (test_parse_all_in_one_buf)
{
  GList *buffer_in = NULL, *buffer_out = NULL;
  GstBuffer *buffer = NULL;
  gsize total_size = 0;
  gsize offset = 0;

  /* Push the data in a single buffer, injecting some garbage. */
  total_size += sizeof (test_data_garbage);
  total_size += sizeof (test_data_short_frame);
  total_size += sizeof (test_data_garbage);
  total_size += sizeof (test_data_normal_frame);
  total_size += sizeof (test_data_ff);
  total_size += sizeof (test_data_entropy);
  total_size += sizeof (test_data_extra_ff);
  buffer = gst_buffer_new_and_alloc (total_size);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_garbage,
      sizeof (test_data_garbage));
  offset += sizeof (test_data_garbage);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_short_frame,
      sizeof (test_data_short_frame));
  offset += sizeof (test_data_short_frame);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_garbage,
      sizeof (test_data_garbage));
  offset += sizeof (test_data_garbage);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_normal_frame,
      sizeof (test_data_normal_frame));
  offset += sizeof (test_data_normal_frame);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_ff,
      sizeof (test_data_ff));
  offset += sizeof (test_data_ff);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_entropy,
      sizeof (test_data_entropy));
  offset += sizeof (test_data_entropy);
  memcpy (GST_BUFFER_DATA (buffer) + offset, test_data_extra_ff,
      sizeof (test_data_extra_ff));
  offset += sizeof (test_data_extra_ff);

  gst_buffer_set_caps (buffer, gst_caps_new_simple ("image/jpeg", "parsed",
          G_TYPE_BOOLEAN, FALSE, NULL));
  GST_LOG ("Pushing single buffer of %u bytes.", total_size);
  buffer_in = g_list_append (buffer_in, buffer);

  buffer_out = make_buffers_out (buffer_out, test_data_short_frame);
  buffer_out = make_buffers_out (buffer_out, test_data_normal_frame);
  buffer_out = make_buffers_out (buffer_out, test_data_entropy);
  buffer_out = make_buffers_out (buffer_out, test_data_extra_ff);
  gst_check_element_push_buffer_list ("jpegparse", buffer_in, buffer_out,
      GST_FLOW_OK);
}

GST_END_TEST;

Suite *
jpegparse_suite (void)
{
  Suite *s = suite_create ("jpegparse");
  TCase *tc_chain = tcase_create ("jpegparse");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_single_byte);
  tcase_add_test (tc_chain, test_parse_all_in_one_buf);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = jpegparse_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

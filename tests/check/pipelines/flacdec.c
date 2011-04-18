/* GStreamer
 * Copyright (C) 2009 Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <gst/check/gstcheck.h>
#include <glib/gstdio.h>

static guint16
buffer_get_first_sample (GstBuffer * buf)
{
  GstStructure *s;
  gint w, d, c, r, e;

  fail_unless (buf != NULL, "NULL buffer");
  fail_unless (GST_BUFFER_CAPS (buf) != NULL, "buffer without caps");

  /* log buffer details */
  GST_DEBUG ("buffer with size=%u, caps=%" GST_PTR_FORMAT,
      GST_BUFFER_SIZE (buf), GST_BUFFER_CAPS (buf));
  GST_MEMDUMP ("buffer data from decoder", GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));

  /* make sure it's the format we expect */
  s = gst_caps_get_structure (GST_BUFFER_CAPS (buf), 0);
  fail_unless_equals_string (gst_structure_get_name (s), "audio/x-raw-int");
  fail_unless (gst_structure_get_int (s, "width", &w));
  fail_unless_equals_int (w, 16);
  fail_unless (gst_structure_get_int (s, "depth", &d));
  fail_unless_equals_int (d, 16);
  fail_unless (gst_structure_get_int (s, "rate", &r));
  fail_unless_equals_int (r, 44100);
  fail_unless (gst_structure_get_int (s, "channels", &c));
  fail_unless_equals_int (c, 1);
  fail_unless (gst_structure_get_int (s, "endianness", &e));
  if (e == G_BIG_ENDIAN)
    return GST_READ_UINT16_BE (GST_BUFFER_DATA (buf));
  else
    return GST_READ_UINT16_LE (GST_BUFFER_DATA (buf));
}

GST_START_TEST (test_decode)
{
  GstElement *pipeline;
  GstElement *appsink;
  GstBuffer *buffer = NULL;
  guint16 first_sample = 0;
  guint size = 0;
  gchar *path =
      g_build_filename (GST_TEST_FILES_PATH, "audiotestsrc.flac", NULL);
  gchar *pipe_desc =
      g_strdup_printf ("filesrc location=\"%s\" ! flacdec ! appsink name=sink",
      path);

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);

  g_free (path);
  g_free (pipe_desc);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
    if (buffer == NULL)
      break;
    if (first_sample == 0)
      first_sample = buffer_get_first_sample (buffer);
    GST_DEBUG ("buffer: %d\n", buffer->size);
    GST_DEBUG ("buffer: %04x\n", buffer_get_first_sample (buffer));
    size += buffer->size;

    gst_buffer_unref (buffer);
    buffer = NULL;
  }
  while (TRUE);

  /* audiotestsrc with samplesperbuffer 1024 and 10 num-buffers */
  fail_unless_equals_int (size, 20480);
  fail_unless_equals_int (first_sample, 0x066a);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (pipeline);
  g_object_unref (appsink);
}

GST_END_TEST;

GST_START_TEST (test_decode_seek_full)
{
  GstElement *pipeline;
  GstElement *appsink;
  GstEvent *event;
  GstBuffer *buffer = NULL;
  guint16 first_sample = 0;
  guint size = 0;
  gchar *path =
      g_build_filename (GST_TEST_FILES_PATH, "audiotestsrc.flac", NULL);
  gchar *pipe_desc =
      g_strdup_printf ("filesrc location=\"%s\" ! flacdec ! appsink name=sink",
      path);

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);

  g_free (pipe_desc);
  g_free (path);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* do a seek that should give us the complete output */
  event = gst_event_new_seek (1.0, GST_FORMAT_DEFAULT, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 20480);
  fail_unless (gst_element_send_event (appsink, event));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
    if (buffer == NULL)
      break;
    if (first_sample == 0)
      first_sample = buffer_get_first_sample (buffer);
    size += buffer->size;

    gst_buffer_unref (buffer);
    buffer = NULL;
  }
  while (TRUE);

  /* file was generated with audiotestsrc
   * with 1024 samplesperbuffer and 10 num-buffers in 16 bit audio */
  fail_unless_equals_int (size, 20480);
  fail_unless_equals_int (first_sample, 0x066a);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_unref (pipeline);
  g_object_unref (appsink);
}

GST_END_TEST;

GST_START_TEST (test_decode_seek_partial)
{
  GstElement *pipeline;
  GstElement *appsink;
  GstEvent *event;
  GstBuffer *buffer = NULL;
  guint size = 0;
  guint16 first_sample = 0;
  gchar *path =
      g_build_filename (GST_TEST_FILES_PATH, "audiotestsrc.flac", NULL);
  gchar *pipe_desc =
      g_strdup_printf ("filesrc location=\"%s\" ! flacdec ! appsink name=sink",
      path);

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);

  g_free (path);
  g_free (pipe_desc);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* do a partial seek to get the first 1024 samples or 2048 bytes */
  event = gst_event_new_seek (1.0, GST_FORMAT_DEFAULT, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 1024);
  GST_DEBUG ("seeking");
  fail_unless (gst_element_send_event (appsink, event));
  GST_DEBUG ("seeked");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    GST_DEBUG ("pulling buffer");
    g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
    GST_DEBUG ("pulled buffer %p", buffer);
    if (buffer == NULL)
      break;
    if (first_sample == 0) {
      fail_unless_equals_int (GST_BUFFER_OFFSET (buffer), 0L);
      first_sample = buffer_get_first_sample (buffer);
    }
    size += buffer->size;

    gst_buffer_unref (buffer);
    buffer = NULL;
  }
  while (TRUE);

  fail_unless_equals_int (size, 2048);
  fail_unless_equals_int (first_sample, 0x066a);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_unref (pipeline);
  g_object_unref (appsink);
}

GST_END_TEST;


static Suite *
flacdec_suite (void)
{
  Suite *s = suite_create ("flacdec");

  TCase *tc_chain = tcase_create ("linear");

  /* time out after 60s, not the default 3 */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_decode);
  tcase_add_test (tc_chain, test_decode_seek_full);
  tcase_add_test (tc_chain, test_decode_seek_partial);

  return s;
}

GST_CHECK_MAIN (flacdec);

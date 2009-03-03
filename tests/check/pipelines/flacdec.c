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

GST_START_TEST (test_decode)
{
  GstElement *pipeline;

  GstElement *appsink;

  GstBuffer *buffer = NULL;

  guint8 firstbyte = 0;
  guint size = 0;

  pipeline = gst_parse_launch ("filesrc location=audiotestsrc.flac"
      " ! flacdec ! appsink name=sink", NULL);
  fail_unless (pipeline != NULL);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
    if (buffer == NULL)
      break;
    if (firstbyte == 0)
      firstbyte = GST_BUFFER_DATA (buffer)[0];
    g_print ("buffer: %d\n", buffer->size);
    g_print ("buffer: %08x\n", GST_BUFFER_DATA (buffer)[0]);
    size += buffer->size;
  }
  while (TRUE);

  /* audiotestsrc with samplesperbuffer 1024 and 10 num-buffers */
  fail_unless_equals_int (size, 20480);
  fail_unless_equals_int (firstbyte, 0x6a);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_decode_seek_full)
{
  GstElement *pipeline;

  GstElement *appsink;

  GstEvent *event;

  GstBuffer *buffer = NULL;

  guint8 firstbyte = 0;

  gboolean result;
  guint size = 0;

  pipeline = gst_parse_launch ("filesrc location=audiotestsrc.flac"
      " ! flacdec ! appsink name=sink", NULL);
  fail_unless (pipeline != NULL);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* do a seek that should give us the complete output */
  event = gst_event_new_seek (1.0, GST_FORMAT_DEFAULT, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 20480);
  result = gst_element_send_event (appsink, event);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
    if (buffer == NULL)
      break;
    if (firstbyte == 0)
      firstbyte = GST_BUFFER_DATA (buffer)[0];
    size += buffer->size;
  }
  while (TRUE);

  /* file was generated with audiotestsrc
   * with 1024 samplesperbuffer and 10 num-buffers in 16 bit audio */
  fail_unless_equals_int (size, 20480);
  fail_unless_equals_int (firstbyte, 0x6a);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_decode_seek_partial)
{
  GstElement *pipeline;

  GstElement *appsink;

  GstEvent *event;

  GstBuffer *buffer = NULL;

  gboolean result;
  guint size = 0;
  guint8 firstbyte = 0;

  pipeline = gst_parse_launch ("filesrc location=audiotestsrc.flac"
      " ! flacdec ! appsink name=sink", NULL);
  fail_unless (pipeline != NULL);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* do a partial seek to get the first 1024 samples or 2048 bytes */
  event = gst_event_new_seek (1.0, GST_FORMAT_DEFAULT, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 1024);
  result = gst_element_send_event (appsink, event);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    g_signal_emit_by_name (appsink, "pull-buffer", &buffer);
    if (buffer == NULL)
      break;
    if (firstbyte == 0)
      firstbyte = GST_BUFFER_DATA (buffer)[0];
    size += buffer->size;
  }
  while (TRUE);

  fail_unless_equals_int (size, 2048);
  fail_unless_equals_int (firstbyte, 0x6a);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_unref (pipeline);
}

GST_END_TEST;


Suite *
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

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = flacdec_suite ();

  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}

/* GStreamer
 *
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * Pipeline tests for AV1 av1C round-trip via qtmux/qtdemux.
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
#include <glib/gstdio.h>

static gboolean
codec_data_buffers_equal (GstBuffer * a, GstBuffer * b)
{
  gsize size;
  GstMapInfo map;
  gint cmp;

  size = gst_buffer_get_size (a);
  if (size != gst_buffer_get_size (b))
    return FALSE;

  if (!gst_buffer_map (b, &map, GST_MAP_READ))
    return FALSE;

  cmp = gst_buffer_memcmp (a, 0, map.data, map.size);
  gst_buffer_unmap (b, &map);

  return cmp == 0;
}

static void
run_pipeline_to_eos (const gchar * pipe_desc)
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, 10 * GST_SECOND,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg != NULL);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &err, &debug);
    fail ("Pipeline failed: %s", err ? err->message : "unknown error");
    g_clear_error (&err);
    g_free (debug);
  }

  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

static gsize
get_codec_data_size_for_path (const gchar * path)
{
  GstElement *pipeline;
  GstElement *appsink;
  GstSample *sample = NULL;
  GstCaps *caps;
  GstStructure *s;
  const GValue *codec_data_value;
  GstBuffer *codec_data;
  gsize size;
  gchar *pipe_desc = g_strdup_printf ("filesrc location=\"%s\" ! "
      "qtdemux name=d d.video_0 ! appsink name=sink sync=false", path);

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);

  g_free (pipe_desc);
  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_signal_emit_by_name (appsink, "pull-sample", &sample);
  fail_unless (sample != NULL);

  caps = gst_sample_get_caps (sample);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  codec_data_value = gst_structure_get_value (s, "codec_data");
  fail_unless (codec_data_value != NULL);
  codec_data = gst_value_get_buffer (codec_data_value);
  fail_unless (codec_data != NULL);

  size = gst_buffer_get_size (codec_data);

  gst_sample_unref (sample);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (appsink);
  gst_object_unref (pipeline);

  return size;
}

static guint
count_distinct_codec_data_for_path (const gchar * path)
{
  GstElement *pipeline;
  GstElement *appsink;
  GstBus *bus;
  GPtrArray *buffers;
  guint distinct = 0;
  gboolean done = FALSE;
  gchar *pipe_desc = g_strdup_printf ("filesrc location=\"%s\" ! "
      "qtdemux name=d d.video_0 ! appsink name=sink sync=false", path);

  pipeline = gst_parse_launch (pipe_desc, NULL);
  fail_unless (pipeline != NULL);

  g_free (pipe_desc);
  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  bus = gst_element_get_bus (pipeline);
  buffers = g_ptr_array_new_with_free_func ((GDestroyNotify) gst_buffer_unref);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  while (!done) {
    GstSample *sample = NULL;
    GstCaps *caps;
    GstStructure *s;
    const GValue *codec_data_value;
    GstBuffer *codec_data;
    gboolean seen = FALSE;
    guint i;

    g_signal_emit_by_name (appsink, "try-pull-sample",
        200 * GST_MSECOND, &sample);

    if (sample == NULL) {
      GstMessage *msg = gst_bus_timed_pop_filtered (bus, 0,
          GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

      if (msg != NULL) {
        if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
          GError *err = NULL;
          gchar *debug = NULL;

          gst_message_parse_error (msg, &err, &debug);
          fail ("Pipeline failed: %s", err ? err->message : "unknown error");
          g_clear_error (&err);
          g_free (debug);
        }
        gst_message_unref (msg);
        done = TRUE;
      }
      continue;
    }

    caps = gst_sample_get_caps (sample);
    fail_unless (caps != NULL);
    s = gst_caps_get_structure (caps, 0);
    codec_data_value = gst_structure_get_value (s, "codec_data");
    fail_unless (codec_data_value != NULL);
    codec_data = gst_value_get_buffer (codec_data_value);
    fail_unless (codec_data != NULL);

    for (i = 0; i < buffers->len; i++) {
      if (codec_data_buffers_equal (codec_data, g_ptr_array_index (buffers, i))) {
        seen = TRUE;
        break;
      }
    }

    if (!seen) {
      g_ptr_array_add (buffers, gst_buffer_ref (codec_data));
      distinct++;
    }

    gst_sample_unref (sample);
  }

  g_ptr_array_unref (buffers);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (appsink);
  gst_object_unref (pipeline);

  return distinct;
}

static gsize
get_codec_data_size_for_file (const gchar * filename)
{
  gchar *path = g_build_filename (GST_TEST_FILES_PATH, filename, NULL);
  gsize size = get_codec_data_size_for_path (path);

  g_free (path);
  return size;
}

static gchar *
remux_av1_file (const gchar * filename)
{
  gchar *in_path = g_build_filename (GST_TEST_FILES_PATH, filename, NULL);
  gchar *tmp_path = NULL;
  GError *err = NULL;
  gint fd = g_file_open_tmp ("av1c-qtmux-qtdemux-XXXXXX.mp4", &tmp_path, &err);
  gchar *pipe_desc;

  fail_unless (fd != -1);
  fail_unless (err == NULL);
  g_close (fd, NULL);

  pipe_desc = g_strdup_printf ("filesrc location=\"%s\" ! qtdemux name=d "
      "d.video_0 ! qtmux ! filesink location=\"%s\"", in_path, tmp_path);

  run_pipeline_to_eos (pipe_desc);

  g_free (pipe_desc);
  g_free (in_path);

  return tmp_path;
}

GST_START_TEST (test_av1c_minimal_qtdemux)
{
  gsize size = get_codec_data_size_for_file ("av1c_min.mp4");
  fail_unless (size == 4);
}

GST_END_TEST;

GST_START_TEST (test_av1c_with_obu_qtdemux)
{
  gsize size = get_codec_data_size_for_file ("av1c_with_obu.mp4");
  fail_unless (size > 4);
}

GST_END_TEST;

GST_START_TEST (test_av1c_two_entries_qtdemux)
{
  gchar *path = g_build_filename (GST_TEST_FILES_PATH,
      "av1c_two_entries.mp4", NULL);
  guint count = count_distinct_codec_data_for_path (path);

  fail_unless (count == 2);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_av1c_minimal_qtmux_roundtrip)
{
  gchar *tmp = remux_av1_file ("av1c_min.mp4");
  gsize size = get_codec_data_size_for_path (tmp);
  fail_unless (size == 4);
  g_unlink (tmp);
  g_free (tmp);
}

GST_END_TEST;

GST_START_TEST (test_av1c_with_obu_qtmux_roundtrip)
{
  gchar *tmp = remux_av1_file ("av1c_with_obu.mp4");
  gsize size = get_codec_data_size_for_path (tmp);
  fail_unless (size > 4);
  g_unlink (tmp);
  g_free (tmp);
}

GST_END_TEST;

GST_START_TEST (test_av1c_two_entries_qtmux_roundtrip)
{
  gchar *tmp = remux_av1_file ("av1c_two_entries.mp4");
  guint count = count_distinct_codec_data_for_path (tmp);

  fail_unless (count == 2);
  g_unlink (tmp);
  g_free (tmp);
}

GST_END_TEST;

static Suite *
av1c_qtmux_qtdemux_suite (void)
{
  Suite *s;
  TCase *tc_chain;

  s = suite_create ("av1c-qtmux-qtdemux");
  tc_chain = tcase_create ("general");
  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_av1c_minimal_qtdemux);
  tcase_add_test (tc_chain, test_av1c_with_obu_qtdemux);
  tcase_add_test (tc_chain, test_av1c_two_entries_qtdemux);
  tcase_add_test (tc_chain, test_av1c_minimal_qtmux_roundtrip);
  tcase_add_test (tc_chain, test_av1c_with_obu_qtmux_roundtrip);
  tcase_add_test (tc_chain, test_av1c_two_entries_qtmux_roundtrip);

  return s;
}

GST_CHECK_MAIN (av1c_qtmux_qtdemux);

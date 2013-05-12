/* GStreamer
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
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

static gboolean found_fingerprint = FALSE;

static gboolean
bus_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING:
    case GST_MESSAGE_ERROR:{
      GError *gerror;
      gchar *debug;

      if (message->type == GST_MESSAGE_WARNING)
        gst_message_parse_warning (message, &gerror, &debug);
      else
        gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_TAG:
    {
      GstTagList *tag_list;
      gchar *fpr, *p;

      gst_message_parse_tag (message, &tag_list);

      GST_DEBUG ("tag message: %" GST_PTR_FORMAT, tag_list);

      if (!gst_tag_list_get_value_index (tag_list, "ofa-fingerprint", 0)) {
        gst_tag_list_unref (tag_list);
        break;
      }

      fail_unless (gst_tag_list_get_string (tag_list, "ofa-fingerprint", &fpr));

      p = fpr;
      while (*p) {
        fail_unless (g_ascii_isalnum (*p) || *p == '=' || *p == '+'
            || *p == '/');
        p++;
      }

      g_free (fpr);
      gst_tag_list_unref (tag_list);

      found_fingerprint = TRUE;

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

GST_START_TEST (test_ofa_le_1ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;

  GstBus *bus;
  GMainLoop *loop;
  GstCaps *caps;
  gint64 position;
  GstFormat fmt = GST_FORMAT_TIME;
  guint bus_watch = 0;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 1, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  bus_watch = gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  fail_unless (gst_element_query_position (audiotestsrc, fmt, &position));
  fail_unless (position >= 135 * GST_SECOND);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
  g_source_remove (bus_watch);
}

GST_END_TEST;


GST_START_TEST (test_ofa_be_1ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;
  GstBus *bus;
  GMainLoop *loop;
  GstCaps *caps;
  gint64 position;
  GstFormat fmt = GST_FORMAT_TIME;
  guint bus_watch = 0;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16BE",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 1, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  bus_watch = gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  fail_unless (gst_element_query_position (audiotestsrc, fmt, &position));
  fail_unless (position >= 135 * GST_SECOND);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
  g_source_remove (bus_watch);
}

GST_END_TEST;

GST_START_TEST (test_ofa_le_2ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;
  GstBus *bus;
  GMainLoop *loop;
  GstCaps *caps;
  gint64 position;
  GstFormat fmt = GST_FORMAT_TIME;
  guint bus_watch = 0;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16LE",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 2, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  bus_watch = gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  fail_unless (gst_element_query_position (audiotestsrc, fmt, &position));
  fail_unless (position >= 135 * GST_SECOND);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
  g_source_remove (bus_watch);
}

GST_END_TEST;


GST_START_TEST (test_ofa_be_2ch)
{
  GstElement *pipeline;
  GstElement *audiotestsrc, *audioconvert, *capsfilter, *ofa, *fakesink;
  GstBus *bus;
  GMainLoop *loop;
  GstCaps *caps;
  gint64 position;
  GstFormat fmt = GST_FORMAT_TIME;
  guint bus_watch = 0;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  audiotestsrc = gst_element_factory_make ("audiotestsrc", "src");
  fail_unless (audiotestsrc != NULL);
  g_object_set (G_OBJECT (audiotestsrc), "wave", 0, "freq", 440.0, NULL);

  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  fail_unless (audioconvert != NULL);
  g_object_set (G_OBJECT (audioconvert), "dithering", 0, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
  fail_unless (capsfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw", "format", G_TYPE_STRING, "S16BE",
      "rate", G_TYPE_INT, 44100, "channels", G_TYPE_INT, 2, NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  ofa = gst_element_factory_make ("ofa", "ofa");
  fail_unless (ofa != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), audiotestsrc, audioconvert, capsfilter,
      ofa, fakesink, NULL);

  fail_unless (gst_element_link_many (audiotestsrc, audioconvert, capsfilter,
          ofa, fakesink, NULL));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  bus_watch = gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  found_fingerprint = FALSE;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  fail_unless (gst_element_query_position (audiotestsrc, fmt, &position));
  fail_unless (position >= 135 * GST_SECOND);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless (found_fingerprint == TRUE);
  g_object_unref (pipeline);
  g_main_loop_unref (loop);
  g_source_remove (bus_watch);
}

GST_END_TEST;

static Suite *
ofa_suite (void)
{
  Suite *s = suite_create ("OFA");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 120s, not the default 3 */
  tcase_set_timeout (tc_chain, 120);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ofa_le_1ch);
  tcase_add_test (tc_chain, test_ofa_be_1ch);
  tcase_add_test (tc_chain, test_ofa_le_2ch);
  tcase_add_test (tc_chain, test_ofa_be_2ch);

  return s;
}

GST_CHECK_MAIN (ofa)

/* GStreamer
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <string.h>

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <gst/app/gstappsrc.h>

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
      gst_message_unref (message);
      g_error_free (gerror);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

typedef struct
{
  GstClockTime ts;
  GstClockTime duration;
  const gchar buf[];
} TestBuffer;

static const TestBuffer buf0 = {
  0,
  0,
  "[Script Info]\n"
      "; This is a Sub Station Alpha v4 script.\n"
      "; For Sub Station Alpha info and downloads,\n"
      "; go to http://www.eswat.demon.co.uk/\n"
      "Title: Some Test\n"
      "Script Updated By: version 2.8.01\n"
      "ScriptType: v4.00\n"
      "Collisions: Normal\n"
      "PlayResY: 600\n"
      "PlayDepth: 0\n"
      "Timer: 100,0000\n"
      " \n"
      "[V4 Styles]\n"
      "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, TertiaryColour, BackColour, Bold, Italic, BorderStyle, Outline, Shadow, \n"
      "   Alignment, MarginL, MarginR, MarginV, AlphaLevel, Encoding\n"
      "Style: DefaultVCD, Arial,28,11861244,11861244,11861244,-2147483640,-1,0,1,1,2,2,30,30,30,0,0\n"
      " \n"
      "[Events]\n"
      "Format: Marked, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
};

static const TestBuffer buf1 = {
  40 * GST_MSECOND,
  40 * GST_MSECOND,
  "Dialogue: Marked=0,0:00:00.04,0:00:00.10,DefaultVCD, NTP,0000,0000,0000,,Some Test Blabla\n"
};

static void
sink_handoff_cb (GstElement * object, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  guint *sink_pos = (guint *) user_data;
  gboolean contains_text = (*sink_pos == 1 || *sink_pos == 2);
  guint i, j;
  guint8 *data = GST_BUFFER_DATA (buffer);
  gboolean all_red = TRUE;

  fail_unless_equals_int (GST_BUFFER_SIZE (buffer), 640 * 480 * 4);
  for (i = 0; i < 640; i++) {
    for (j = 0; j < 480; j++) {
      all_red = all_red && (data[i * 480 * 4 + j * 4 + 1] == 255 &&
          data[i * 480 * 4 + j * 4 + 2] == 0 &&
          data[i * 480 * 4 + j * 4 + 3] == 0);
    }
  }

  fail_unless (contains_text || all_red);
  *sink_pos = *sink_pos + 1;
}

GST_START_TEST (test_assrender_basic)
{
  GstElement *pipeline;
  GstElement *appsrc, *videotestsrc, *capsfilter, *assrender, *fakesink;
  guint sink_pos = 0;
  GstCaps *video_caps;
  GstCaps *text_caps;
  GstBuffer *buf;
  GstBus *bus;
  GMainLoop *loop;

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  appsrc = gst_element_factory_make ("appsrc", NULL);
  fail_unless (appsrc != NULL);
  text_caps = gst_caps_new_simple ("application/x-ssa", NULL);
  gst_app_src_set_caps (GST_APP_SRC (appsrc), text_caps);
  g_object_set (appsrc, "format", GST_FORMAT_TIME, NULL);

  videotestsrc = gst_element_factory_make ("videotestsrc", NULL);
  fail_unless (videotestsrc != NULL);
  g_object_set (videotestsrc, "num-buffers", 5, "pattern", 4, NULL);

  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  fail_unless (capsfilter != NULL);
  video_caps =
      gst_video_format_new_caps (GST_VIDEO_FORMAT_xRGB, 640, 480, 25, 1, 1, 1);
  g_object_set (capsfilter, "caps", video_caps, NULL);
  gst_caps_unref (video_caps);

  assrender = gst_element_factory_make ("assrender", NULL);
  fail_unless (assrender != NULL);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  g_object_set (fakesink, "signal-handoffs", TRUE, "async", FALSE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (sink_handoff_cb),
      &sink_pos);

  gst_bin_add_many (GST_BIN (pipeline), appsrc, videotestsrc, capsfilter,
      assrender, fakesink, NULL);

  fail_unless (gst_element_link_pads (appsrc, "src", assrender, "text_sink"));
  fail_unless (gst_element_link_pads (videotestsrc, "src", capsfilter, "sink"));
  fail_unless (gst_element_link_pads (capsfilter, "src", assrender,
          "video_sink"));
  fail_unless (gst_element_link_pads (assrender, "src", fakesink, "sink"));

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  buf = gst_buffer_new_and_alloc (strlen (buf0.buf) + 1);
  memcpy (GST_BUFFER_DATA (buf), buf0.buf, GST_BUFFER_SIZE (buf));
  gst_buffer_set_caps (buf, text_caps);
  GST_BUFFER_TIMESTAMP (buf) = buf0.ts;
  GST_BUFFER_DURATION (buf) = buf0.duration;
  gst_app_src_push_buffer (GST_APP_SRC (appsrc), buf);
  buf = gst_buffer_new_and_alloc (strlen (buf1.buf) + 1);
  memcpy (GST_BUFFER_DATA (buf), buf1.buf, GST_BUFFER_SIZE (buf));
  gst_buffer_set_caps (buf, text_caps);
  GST_BUFFER_TIMESTAMP (buf) = buf1.ts;
  GST_BUFFER_DURATION (buf) = buf1.duration;
  gst_app_src_push_buffer (GST_APP_SRC (appsrc), buf);
  gst_caps_unref (text_caps);
  gst_app_src_end_of_stream (GST_APP_SRC (appsrc));

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  fail_unless_equals_int (sink_pos, 5);

  g_object_unref (pipeline);
  g_main_loop_unref (loop);
}

GST_END_TEST;

Suite *
assrender_suite (void)
{
  Suite *s = suite_create ("assrender");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 120s, not the default 3 */
  tcase_set_timeout (tc_chain, 120);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_assrender_basic);

  return s;
}

GST_CHECK_MAIN (assrender);

/* GStreamer
 * Copyright (C) 2024 Jan Schmidt <jan@centricular.com>
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

/*
 * This example creates a test recording using splitmuxsink,
 * listening for the fragment-closed messages from splitmuxsink
 * and writing a CSV file with the fragment offsets and durations
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

GMainLoop *loop;
FILE *out_csv = NULL;

static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "splitmuxsink-fragment-closed") == 0) {
      const gchar *fname = gst_structure_get_string (s, "location");
      GstClockTime start_offset, duration;
      if (!gst_structure_get_uint64 (s, "fragment-offset", &start_offset) ||
          !gst_structure_get_uint64 (s, "fragment-duration", &duration)) {
        g_assert_not_reached ();
      }
      fprintf (out_csv, "\"%s\",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT "\n",
          fname, start_offset, duration);
    }
  } else if (message->type == GST_MESSAGE_EOS) {
    g_main_loop_quit (loop);
  } else if (message->type == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error (message, &err, &debug_info);
    g_printerr ("Error received from element %s: %s\n",
        GST_OBJECT_NAME (message->src), err->message);
    g_printerr ("Debugging information: %s\n",
        debug_info ? debug_info : "none");
    g_main_loop_quit (loop);
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *pipe;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc < 3) {
    g_printerr
        ("Usage: %s target_dir out.csv\n  Pass splitmuxsink target directory for generated recording, and out.csv to receive the fragment info\n",
        argv[0]);
    return 1;
  }

  out_csv = fopen (argv[2], "w");
  if (out_csv == NULL) {
    g_printerr ("Failed to open output file %s", argv[2]);
    return 2;
  }

  GError *error = NULL;
  pipe =
      gst_parse_launch
      ("videotestsrc num-buffers=300 ! video/x-raw,framerate=30/1 ! timeoverlay ! x264enc key-int-max=30 ! "
      "h264parse ! queue ! splitmuxsink name=sink "
      "audiotestsrc samplesperbuffer=1600 num-buffers=300 ! audio/x-raw,rate=48000 ! opusenc ! queue ! sink.audio_0 ",
      &error);

  if (pipe == NULL || error != NULL) {
    g_print ("Failed to create pipeline. Error %s\n", error->message);
    return 3;
  }

  GstElement *splitmuxsink = gst_bin_get_by_name (GST_BIN (pipe), "sink");

  /* Set the files glob on src */
  gchar *file_pattern = g_strdup_printf ("%s/test%%05d.mp4", argv[1]);
  g_object_set (splitmuxsink, "location", file_pattern, NULL);
  g_object_set (splitmuxsink, "max-size-time", GST_SECOND, NULL);

  gst_object_unref (splitmuxsink);

  bus = gst_element_get_bus (pipe);
  gst_bus_add_watch (bus, message_handler, NULL);
  gst_object_unref (bus);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  fclose (out_csv);

  gst_element_set_state (pipe, GST_STATE_NULL);

  gst_object_unref (pipe);

  return 0;
}

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
 * This example uses splitmuxsrc to play a set of splitmuxed-files,
 * listening for the fragment-info messages from splitmuxsrc
 * and writing a CSV file with the fragment offsets and durations
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

GMainLoop *loop;
FILE *out_csv = NULL;
gsize num_fragments = 0;

static gboolean
message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  GstElement *pipe = data;

  if (message->type == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    const gchar *name = gst_structure_get_name (s);

    if (strcmp (name, "splitmuxsrc-fragment-info") == 0) {
      const gchar *fname = gst_structure_get_string (s, "location");
      GstClockTime start_offset, duration;
      if (!gst_structure_get_uint64 (s, "fragment-offset", &start_offset) ||
          !gst_structure_get_uint64 (s, "fragment-duration", &duration)) {
        g_assert_not_reached ();
      }
      fprintf (out_csv, "\"%s\",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT "\n",
          fname, start_offset, duration);
      num_fragments++;
    }
  } else if (message->type == GST_MESSAGE_EOS) {
    g_main_loop_quit (loop);
  } else if (message->type == GST_MESSAGE_STATE_CHANGED) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (message, &old_state, &new_state,
        &pending_state);
    if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipe)
        && new_state == GST_STATE_PLAYING) {
      g_print ("splitmuxsrc scanned %" G_GSIZE_FORMAT " files. Exiting\n",
          num_fragments);
      g_main_loop_quit (loop);
    }
  }
  return TRUE;
}

static void
on_pad_added (GstElement * src, GstPad * pad, GstBin * pipe)
{
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add (pipe, sink);

  GstPad *sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  gst_element_sync_state_with_parent (sink);
}

int
main (int argc, char *argv[])
{
  GstElement *pipe;
  GstElement *src;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc < 3) {
    g_printerr
        ("Usage: %s *.mp4 out.csv\n  Pass splitmux file glob and fragment info will be dumped to out.csv\n",
        argv[0]);
    return 1;
  }

  out_csv = fopen (argv[2], "w");
  if (out_csv == NULL) {
    g_printerr ("Failed to open output file %s", argv[2]);
    return 2;
  }

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("splitmuxsrc", "src");
  g_assert (src != NULL);

  /* Set the files glob on src */
  g_object_set (src, "location", argv[1], NULL);

  /* Connect to pad-added to attach fakesink elements */
  g_signal_connect (src, "pad-added", G_CALLBACK (on_pad_added), pipe);

  gst_bin_add (GST_BIN (pipe), src);

  bus = gst_element_get_bus (pipe);
  gst_bus_add_watch (bus, message_handler, pipe);
  gst_object_unref (bus);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  fclose (out_csv);

  gst_element_set_state (pipe, GST_STATE_NULL);

  gst_object_unref (pipe);

  return 0;
}

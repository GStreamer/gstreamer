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
 * and using them to pass fragments to splitmuxsrc for live playback
 * as fragments are generated
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

typedef struct
{
  GMainLoop *loop;
  gboolean running;

  GstElement *record_pipe;
  GstElement *playback_pipe;
  GstElement *splitmuxsrc;

  gboolean playback_started;
  gsize num_fragments;

  GMutex lock;
  GCond cond;

  // Pending fragment info for initial fragment
  const gchar *fname;
  GstClockTime start_offset;
  GstClockTime duration;
} State;

static gboolean
record_message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  State *state = data;

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

      g_mutex_lock (&state->lock);

      if (!state->playback_started) {
        g_print ("Finished first fragment. Starting playback\n");

        state->fname = fname;
        state->start_offset = start_offset;
        state->duration = duration;

        g_mutex_unlock (&state->lock);
        gst_element_set_state (state->playback_pipe, GST_STATE_PLAYING);
        g_mutex_lock (&state->lock);

        state->playback_started = TRUE;

        while (state->splitmuxsrc == NULL && state->running) {
          g_cond_wait (&state->cond, &state->lock);
        }
      } else {
        gboolean add_result;
        g_signal_emit_by_name (G_OBJECT (state->splitmuxsrc), "add-fragment",
            fname, start_offset, duration, &add_result);
        if (!add_result) {
          g_printerr ("Failed to add fragment %" G_GSIZE_FORMAT
              ": %s for playback\n", state->num_fragments, fname);
          g_main_loop_quit (state->loop);
          return FALSE;
        }
      }

      state->num_fragments++;
      g_mutex_unlock (&state->lock);
    }
  } else if (message->type == GST_MESSAGE_EOS) {
    g_print ("Recording finished.\n");
  } else if (message->type == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error (message, &err, &debug_info);
    g_printerr ("Error received from element %s: %s\n",
        GST_OBJECT_NAME (message->src), err->message);
    g_printerr ("Debugging information: %s\n",
        debug_info ? debug_info : "none");
    g_main_loop_quit (state->loop);
  }
  return TRUE;
}

static void
setup_splitmuxsrc (GstElement * playbin, GstElement * src, gpointer userdata)
{
  State *state = userdata;

  g_mutex_lock (&state->lock);
  state->splitmuxsrc = src;
  g_cond_broadcast (&state->cond);

  /* We need to give splitmuxsrc a first fragment when it starts to avoid races */
  gboolean add_result;
  g_signal_emit_by_name (G_OBJECT (state->splitmuxsrc), "add-fragment",
      state->fname, state->start_offset, state->duration, &add_result);
  if (!add_result) {
    g_printerr ("Failed to add fragment %" G_GSIZE_FORMAT ": %s for playback\n",
        state->num_fragments, state->fname);
    g_main_loop_quit (state->loop);
  }
  g_mutex_unlock (&state->lock);
}

static gboolean
playback_message_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  State *state = data;

  if (message->type == GST_MESSAGE_ERROR) {
    GError *err;
    gchar *debug_info;

    gst_message_parse_error (message, &err, &debug_info);
    g_printerr ("Error received from element %s: %s\n",
        GST_OBJECT_NAME (message->src), err->message);
    g_printerr ("Debugging information: %s\n",
        debug_info ? debug_info : "none");
    g_mutex_lock (&state->lock);
    state->running = FALSE;
    g_cond_broadcast (&state->cond);
    g_mutex_unlock (&state->lock);
    g_main_loop_quit (state->loop);
  }
  if (message->type == GST_MESSAGE_EOS) {
    g_print ("Playback finished exiting.\n");
    g_main_loop_quit (state->loop);
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  State state = { 0, };
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_printerr
        ("Usage: %s target_dir\n  Pass splitmuxsink target directory for generated recording\n",
        argv[0]);
    return 1;
  }

  g_mutex_init (&state.lock);
  g_cond_init (&state.cond);

  /* First create our playback pipeline that the recording pipe will pass fragments to */
  state.playback_pipe = gst_element_factory_make ("playbin3", NULL);
  if (state.playback_pipe == NULL) {
    g_print ("Failed to create playback pipeline. Check your installation\n");
    return 3;
  }

  /* Connect to source-setup to set fragments on splitmuxsrc */
  g_signal_connect (state.playback_pipe, "source-setup",
      G_CALLBACK (setup_splitmuxsrc), &state);
  g_object_set (state.playback_pipe, "uri", "splitmux://", NULL);

  bus = gst_element_get_bus (state.playback_pipe);
  gst_bus_add_watch (bus, playback_message_handler, &state);
  gst_object_unref (bus);

  GError *error = NULL;
  state.record_pipe =
      gst_parse_launch
      ("videotestsrc num-buffers=300 ! video/x-raw,framerate=30/1 ! timeoverlay ! x264enc key-int-max=30 ! "
      "h264parse ! queue ! splitmuxsink name=sink "
      "audiotestsrc samplesperbuffer=1600 num-buffers=300 ! audio/x-raw,rate=48000 ! opusenc ! queue ! sink.audio_0 ",
      &error);

  if (state.record_pipe == NULL || error != NULL) {
    g_print ("Failed to create generator pipeline. Error %s\n", error->message);
    return 3;
  }

  GstElement *splitmuxsink =
      gst_bin_get_by_name (GST_BIN (state.record_pipe), "sink");

  /* Set the files glob on src */
  gchar *file_pattern = g_strdup_printf ("%s/test%%05d.mp4", argv[1]);
  g_object_set (splitmuxsink, "location", file_pattern, NULL);
  g_object_set (splitmuxsink, "max-size-time", GST_SECOND, NULL);

  gst_object_unref (splitmuxsink);

  bus = gst_element_get_bus (state.record_pipe);
  gst_bus_add_watch (bus, record_message_handler, &state);
  gst_object_unref (bus);

  /* Start the recording pipeline. It will start playback once the first
   * fragment is available */
  gst_element_set_state (state.record_pipe, GST_STATE_PLAYING);

  state.loop = g_main_loop_new (NULL, FALSE);
  state.running = TRUE;
  g_main_loop_run (state.loop);

  gst_element_set_state (state.record_pipe, GST_STATE_NULL);
  gst_element_set_state (state.playback_pipe, GST_STATE_NULL);

  gst_object_unref (state.record_pipe);
  gst_object_unref (state.playback_pipe);

  g_mutex_clear (&state.lock);
  g_cond_clear (&state.cond);

  return 0;
}

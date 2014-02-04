/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#include <stdlib.h>
#include <ges/ges.h>
#include <stdlib.h>

int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  GOptionContext *ctx;
  GESPipeline *pipeline;
  GESTimeline *timeline;
  GESTrack *tracka, *trackv;
  GESLayer *layer1, *layer2;
  GESUriClip *src;
  GMainLoop *mainloop;

  gint inpoint = 0, duration = 10;
  gboolean mute = FALSE;
  gchar *audiofile = NULL;
  GOptionEntry options[] = {
    {"inpoint", 'i', 0, G_OPTION_ARG_INT, &inpoint,
        "in-point in the file (in seconds, default:0s)", "seconds"},
    {"duration", 'd', 0, G_OPTION_ARG_INT, &duration,
        "duration to use from the file (in seconds, default:10s)", "seconds"},
    {"mute", 'm', 0, G_OPTION_ARG_NONE, &mute,
        "Whether to mute the audio from the file",},
    {"audiofile", 'a', 0, G_OPTION_ARG_FILENAME, &audiofile,
          "Use this audiofile instead of the original audio from the file",
        "audiofile"},
    {NULL}
  };

  ctx =
      g_option_context_new
      ("- Plays an video file with sound (origin/muted/replaced)");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing %s\n", err->message);
    exit (1);
  }

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    exit (0);
  }
  g_option_context_free (ctx);

  ges_init ();

  /* Create an Audio/Video pipeline with two layers */
  pipeline = ges_pipeline_new ();

  timeline = ges_timeline_new ();

  tracka = GES_TRACK (ges_audio_track_new ());
  trackv = GES_TRACK (ges_video_track_new ());

  layer1 = ges_layer_new ();
  layer2 = ges_layer_new ();
  g_object_set (layer2, "priority", 1, NULL);

  if (!ges_timeline_add_layer (timeline, layer1) ||
      !ges_timeline_add_layer (timeline, layer2) ||
      !ges_timeline_add_track (timeline, tracka) ||
      !ges_timeline_add_track (timeline, trackv) ||
      !ges_pipeline_set_timeline (pipeline, timeline))
    return -1;

  if (1) {
    gchar *uri = gst_filename_to_uri (argv[1], NULL);
    /* Add the main audio/video file */
    src = ges_uri_clip_new (uri);
    g_free (uri);
    g_object_set (src, "start", 0, "in-point", inpoint * GST_SECOND,
        "duration", duration * GST_SECOND, "mute", mute, NULL);
    ges_layer_add_clip (layer1, GES_CLIP (src));
  }

  /* Play the pipeline */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds (duration + 1, (GSourceFunc) g_main_loop_quit,
      mainloop);
  g_main_loop_run (mainloop);

  return 0;
}

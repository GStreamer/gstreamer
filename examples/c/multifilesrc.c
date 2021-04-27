/* GStreamer Editing Services
 * Copyright (C) 2013 Lubosz Sarnecki <lubosz@gmail.com>
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

/* A image sequence test */
int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  GOptionContext *ctx;
  GESPipeline *pipeline;
  GESTimeline *timeline;
  GESAsset *asset;
  GESLayer *layer;
  GMainLoop *mainloop;
  GESTrack *track;

  gint duration = 10;
  gchar *filepattern = NULL;

  GOptionEntry options[] = {
    {"duration", 'd', 0, G_OPTION_ARG_INT, &duration,
        "duration to use from the file (in seconds, default:10s)", "seconds"},
    {"pattern-url", 'u', 0, G_OPTION_ARG_FILENAME, &filepattern,
          "Pattern of the files. i.e. multifile:///foo/%04d.jpg",
        "pattern-url"},
    {NULL}
  };

  ctx = g_option_context_new ("- Plays an image sequence");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    gst_print ("Error initializing %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    exit (1);
  }

  if (filepattern == NULL) {
    gst_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    exit (0);
  }
  g_option_context_free (ctx);

  gst_init (&argc, &argv);
  ges_init ();

  timeline = ges_timeline_new ();
  track = GES_TRACK (ges_video_track_new ());
  ges_timeline_add_track (timeline, track);

  layer = ges_layer_new ();
  if (!ges_timeline_add_layer (timeline, layer))
    return -1;

  asset = GES_ASSET (ges_uri_clip_asset_request_sync (filepattern, &err));

  ges_layer_add_asset (layer, asset, 0, 0, 5 * GST_SECOND,
      GES_TRACK_TYPE_VIDEO);

  pipeline = ges_pipeline_new ();

  if (!ges_pipeline_set_timeline (pipeline, timeline))
    return -1;

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  mainloop = g_main_loop_new (NULL, FALSE);

  g_timeout_add_seconds (4, (GSourceFunc) g_main_loop_quit, mainloop);
  g_main_loop_run (mainloop);

  return 0;
}

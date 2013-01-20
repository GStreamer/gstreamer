/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include <ges/ges.h>

/* A simple timeline with 3 audio/video sources */
int
main (int argc, gchar ** argv)
{
  GESAsset *src_asset;
  GESTimelinePipeline *pipeline;
  GESTimeline *timeline;
  GESClip *source;
  GESTimelineLayer *layer;
  GMainLoop *mainloop;

  /* Initialize GStreamer (this will parse environment variables and commandline
   * arguments. */
  gst_init (&argc, &argv);

  /* Initialize the GStreamer Editing Services */
  ges_init ();

  /* Setup of a A/V timeline */

  /* This is our main GESTimeline */
  timeline = ges_timeline_new_audio_video ();

  /* We are only going to be doing one layer of timeline objects */
  layer = ges_timeline_layer_new ();

  /* Add the tracks and the layer to the timeline */
  if (!ges_timeline_add_layer (timeline, layer))
    return -1;

  /* We create a simple asset able to extract GESTimelineTestSource */
  src_asset = ges_asset_request (GES_TYPE_TIMELINE_TEST_SOURCE, NULL, NULL);

  /* Add sources to our layer */
  ges_timeline_layer_add_asset (layer, src_asset, 0, 0, GST_SECOND, 1,
      GES_TRACK_TYPE_UNKNOWN);
  source = ges_timeline_layer_add_asset (layer, src_asset, GST_SECOND, 0,
      GST_SECOND, 1, GES_TRACK_TYPE_UNKNOWN);
  g_object_set (source, "freq", 480.0, "vpattern", 2, NULL);
  ges_timeline_layer_add_asset (layer, src_asset, 2 * GST_SECOND, 0,
      GST_SECOND, 1, GES_TRACK_TYPE_UNKNOWN);


  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */
  pipeline = ges_timeline_pipeline_new ();

  /* Add the timeline to that pipeline */
  if (!ges_timeline_pipeline_add_timeline (pipeline, timeline))
    return -1;

  /* The following is standard usage of a GStreamer pipeline (note how you haven't
   * had to care about GStreamer so far ?).
   *
   * We set the pipeline to playing ... */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  /* .. and we start a GMainLoop. GES **REQUIRES** a GMainLoop to be running in
   * order to function properly ! */
  mainloop = g_main_loop_new (NULL, FALSE);

  /* Simple code to have the mainloop shutdown after 4s */
  g_timeout_add_seconds (4, (GSourceFunc) g_main_loop_quit, mainloop);
  g_main_loop_run (mainloop);

  return 0;
}

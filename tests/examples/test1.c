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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>

/* A simple timeline with 3 audio/video sources */

static gboolean
fill_customsrc (GESTimelineObject * object, GESTrackObject * trobject,
    GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;
  guint var = GPOINTER_TO_UINT (user_data);

  /* Based on the Track type, we will either put a videotestsrc
   * or an audiotestsrc */

  if (trobject->track->type == GES_TRACK_TYPE_VIDEO) {
    src = gst_element_factory_make ("videotestsrc", NULL);
    g_object_set (src, "pattern", var, NULL);
  } else if (trobject->track->type == GES_TRACK_TYPE_AUDIO) {
    src = gst_element_factory_make ("audiotestsrc", NULL);
    g_object_set (src, "freq", 440.0 * (var + 1), NULL);
  } else
    return FALSE;

  /* Finally we fill in the gnlobject */
  return gst_bin_add (GST_BIN (gnlobj), src);
}

int
main (int argc, gchar ** argv)
{
  GESTimelinePipeline *pipeline;
  GESTimeline *timeline;
  GESTrack *trackv, *tracka;
  GESTimelineLayer *layer;
  GESCustomTimelineSource *src1, *src2, *src3;
  GMainLoop *mainloop;

  /* Initialize GStreamer (this will parse environment variables and commandline
   * arguments. */
  gst_init (&argc, &argv);

  /* Initialize the GStreamer Editing Services */
  ges_init ();

  /* Setup of a A/V timeline */

  /* This is our main GESTimeline */
  timeline = ges_timeline_new ();

  /* We want to output both audio and video, therefore we
   * grab two tracks, one for each output type. */
  trackv = ges_track_video_raw_new ();
  tracka = ges_track_audio_raw_new ();

  /* We are only going to be doing one layer of timeline objects */
  layer = ges_timeline_layer_new ();

  /* Add the tracks and the layer to the timeline */
  if (!ges_timeline_add_layer (timeline, layer))
    return -1;
  if (!ges_timeline_add_track (timeline, trackv))
    return -1;
  if (!ges_timeline_add_track (timeline, tracka))
    return -1;

  /* Here we've finished initializing our timeline, we're 
   * ready to start using it... by solely working with the layer !*/



  /* We are here creating 3 sources of 1s each which we will put one after
   * the other.
   *
   * For our sources, and since we want to prototype quickly, we just use
   * a GESCustomTimelineSource. This will use the callback we provide to
   * fill in the GESTrackObject.
   * */

  src1 = ges_custom_timeline_source_new (fill_customsrc, GUINT_TO_POINTER (0));
  g_object_set (src1, "start", (guint64) 0, "duration", GST_SECOND, NULL);
  src2 = ges_custom_timeline_source_new (fill_customsrc, GUINT_TO_POINTER (1));
  g_object_set (src2, "start", GST_SECOND, "duration", GST_SECOND, NULL);
  src3 = ges_custom_timeline_source_new (fill_customsrc, GUINT_TO_POINTER (0));
  g_object_set (src3, "start", 2 * GST_SECOND, "duration", GST_SECOND, NULL);

  /* Add those sources to our layer */
  ges_timeline_layer_add_object (layer, (GESTimelineObject *) src1);
  ges_timeline_layer_add_object (layer, (GESTimelineObject *) src2);
  ges_timeline_layer_add_object (layer, (GESTimelineObject *) src3);


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

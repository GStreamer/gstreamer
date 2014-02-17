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

#include <gio/gio.h>
#include <ges/ges.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <gst/pbutils/encoding-profile.h>

static void
bus_message_cb (GstBus * bus, GstMessage * message, GMainLoop * mainloop);

static GstEncodingProfile *make_profile_from_info (GstDiscovererInfo * info);

GESPipeline *pipeline = NULL;
gchar *output_uri = NULL;
guint assetsCount = 0;
guint assetsLoaded = 0;

static void
asset_loaded_cb (GObject * source_object, GAsyncResult * res,
    GMainLoop * mainloop)
{
  GError *error = NULL;

  GESUriClipAsset *mfs =
      GES_URI_CLIP_ASSET (ges_asset_request_finish (res, &error));

  if (error) {
    GST_WARNING ("error creating asseti %s", error->message);

    return;
  }

  assetsLoaded++;
  /*
   * Check if we have loaded last asset and trigger concatenating
   */
  if (assetsLoaded == assetsCount) {
    GstDiscovererInfo *info = ges_uri_clip_asset_get_info (mfs);
    GstEncodingProfile *profile = make_profile_from_info (info);
    ges_pipeline_set_render_settings (pipeline, output_uri, profile);
    /* We want the pipeline to render (without any preview) */
    if (!ges_pipeline_set_mode (pipeline, GES_PIPELINE_MODE_SMART_RENDER)) {
      g_main_loop_quit (mainloop);
      return;
    }
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  }

  gst_object_unref (mfs);
}

int
main (int argc, char **argv)
{
  GMainLoop *mainloop = NULL;
  GESTimeline *timeline;
  GESLayer *layer = NULL;
  GstBus *bus = NULL;
  guint i;


  if (argc < 3) {
    g_print ("Usage: %s <output uri> <list of files>\n", argv[0]);
    return -1;
  }

  gst_init (&argc, &argv);
  ges_init ();

  timeline = ges_timeline_new_audio_video ();

  layer = (GESLayer *) ges_layer_new ();
  if (!ges_timeline_add_layer (timeline, layer))
    return -1;

  output_uri = argv[1];
  assetsCount = argc - 2;

  for (i = 2; i < argc; i++) {
    ges_asset_request_async (GES_TYPE_URI_CLIP, argv[i],
        NULL, (GAsyncReadyCallback) asset_loaded_cb, mainloop);
  }

  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */
  pipeline = ges_pipeline_new ();

  /* Add the timeline to that pipeline */
  if (!ges_pipeline_set_timeline (pipeline, timeline))
    return -1;

  mainloop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), mainloop);

  g_main_loop_run (mainloop);

  return 0;

}

static void
bus_message_cb (GstBus * bus, GstMessage * message, GMainLoop * mainloop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("ERROR\n");
      g_main_loop_quit (mainloop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("Done\n");
      g_main_loop_quit (mainloop);
      break;
    default:
      break;
  }
}

static GstEncodingProfile *
make_profile_from_info (GstDiscovererInfo * info)
{
  GstEncodingContainerProfile *profile = NULL;
  GstDiscovererStreamInfo *sinfo = gst_discoverer_info_get_stream_info (info);

  /* Get the container format */
  if (GST_IS_DISCOVERER_CONTAINER_INFO (sinfo)) {
    GList *tmp, *substreams;

    profile = gst_encoding_container_profile_new ((gchar *) "concatenate", NULL,
        gst_discoverer_stream_info_get_caps (sinfo), NULL);

    substreams =
        gst_discoverer_container_info_get_streams ((GstDiscovererContainerInfo
            *) sinfo);

    /* For each on the formats add stream profiles */
    for (tmp = substreams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *stream = GST_DISCOVERER_STREAM_INFO (tmp->data);
      GstEncodingProfile *sprof = NULL;

      if (GST_IS_DISCOVERER_VIDEO_INFO (stream)) {
        sprof = (GstEncodingProfile *)
            gst_encoding_video_profile_new (gst_discoverer_stream_info_get_caps
            (stream), NULL, NULL, 1);
      } else if (GST_IS_DISCOVERER_AUDIO_INFO (stream)) {
        sprof = (GstEncodingProfile *)
            gst_encoding_audio_profile_new (gst_discoverer_stream_info_get_caps
            (stream), NULL, NULL, 1);
      } else {
        GST_WARNING ("Unsupported streams");
      }

      if (sprof)
        gst_encoding_container_profile_add_profile (profile, sprof);
    }
    if (substreams)
      gst_discoverer_stream_info_list_free (substreams);
  } else {
    GST_ERROR ("No container format !!!");
  }

  if (sinfo)
    gst_discoverer_stream_info_unref (sinfo);

  return GST_ENCODING_PROFILE (profile);
}

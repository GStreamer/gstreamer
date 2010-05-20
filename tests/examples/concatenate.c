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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>
#include <gst/profile/gstprofile.h>
#include <gst/discoverer/gstdiscoverer.h>

GstDiscovererInformation *
get_info_for_file (GstDiscoverer * disco, gchar * filename)
{
  GError *err;
  gchar *path, *uri;

  /* Convert to a URI */
  if (!g_path_is_absolute (filename)) {
    gchar *cur_dir;

    cur_dir = g_get_current_dir ();
    path = g_build_filename (cur_dir, filename, NULL);
    g_free (cur_dir);
  } else {
    path = g_strdup (filename);
  }

  uri = g_filename_to_uri (path, NULL, &err);
  g_free (path);
  path = NULL;

  /* Get information */
  return gst_discoverer_discover_uri (disco, uri, &err);
}

static GstEncodingProfile *
make_profile_from_info (GstDiscovererInformation * info)
{
  GstEncodingProfile *profile = NULL;

  /* Get the container format */
  if (info->stream_info->streamtype == GST_STREAM_CONTAINER) {
    GstStreamContainerInformation *container =
        GST_STREAM_CONTAINER_INFORMATION (info->stream_info);
    GList *tmp;

    profile = gst_encoding_profile_new ("concatenate",
        gst_caps_copy (info->stream_info->caps), NULL, FALSE);
    /* For each on the formats add stream profiles */
    for (tmp = container->streams; tmp; tmp = tmp->next) {
      GstStreamInformation *stream = GST_STREAM_INFORMATION (tmp->data);
      GstStreamEncodingProfile *sprof;

      sprof =
          gst_stream_encoding_profile_new (stream->streamtype ==
          GST_STREAM_VIDEO ? GST_ENCODING_PROFILE_VIDEO :
          GST_ENCODING_PROFILE_AUDIO, gst_caps_copy (stream->caps), NULL,
          NULL, 1);
      gst_encoding_profile_add_stream (profile, sprof);
    }
  } else {
    GST_ERROR ("No container format !!!");
  }

  return profile;
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

int
main (int argc, gchar ** argv)
{
  GESTimelinePipeline *pipeline;
  GESTimeline *timeline;
  GESTimelineLayer *layer;
  GList *sources = NULL;
  GMainLoop *mainloop;
  GstEncodingProfile *profile;
  gchar *output_uri;
  guint i;
  gboolean gotprofile = FALSE;
  GstDiscoverer *disco;
  GstBus *bus;

  if (argc < 3) {
    g_print ("Usage: %s <output uri> <list of files>\n", argv[0]);
    return -1;
  }

  gst_init (&argc, &argv);
  ges_init ();

  timeline = ges_timeline_new_audio_video ();

  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();
  if (!ges_timeline_add_layer (timeline, layer))
    return -1;

  disco = gst_discoverer_new (10 * GST_SECOND);

  for (i = 2; i < argc; i++) {
    GstDiscovererInformation *info;
    GESTimelineFileSource *src;

    info = get_info_for_file (disco, argv[i]);

    if (!gotprofile) {
      profile = make_profile_from_info (info);
      gotprofile = TRUE;
    }

    src = ges_timeline_filesource_new (info->uri);
    g_object_set (src, "duration", info->duration, NULL);
    /* Since we're using a GESSimpleTimelineLayer, objects will be automatically
     * appended to the end of the layer */
    ges_timeline_layer_add_object (layer, (GESTimelineObject *) src);

    gst_discoverer_information_free (info);
    sources = g_list_append (sources, src);
  }

  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */
  pipeline = ges_timeline_pipeline_new ();

  /* Add the timeline to that pipeline */
  if (!ges_timeline_pipeline_add_timeline (pipeline, timeline))
    return -1;


  /* RENDER SETTINGS ! */
  /* We set our output URI and rendering setting on the pipeline */
  output_uri = argv[1];
  if (!ges_timeline_pipeline_set_render_settings (pipeline, output_uri,
          profile))
    return -1;

  /* We want the pipeline to render (without any preview) */
  if (!ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_SMART_RENDER))
    return -1;


  mainloop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), mainloop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  g_main_loop_run (mainloop);

  return 0;
}

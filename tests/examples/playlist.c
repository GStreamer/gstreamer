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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <glib.h>
#include <ges/ges.h>
#include <gst/profile/gstprofile.h>

static GstEncodingProfile *
make_encoding_profile (gchar * audio, gchar * video, gchar * container)
{
  GstEncodingProfile *profile;
  GstStreamEncodingProfile *stream;

  profile = gst_encoding_profile_new ("ges-test4",
      gst_caps_new_simple (container, NULL), NULL, FALSE);
  stream = gst_stream_encoding_profile_new (GST_ENCODING_PROFILE_AUDIO,
      gst_caps_from_string (audio), NULL, NULL, 0);
  gst_encoding_profile_add_stream (profile, stream);
  stream = gst_stream_encoding_profile_new (GST_ENCODING_PROFILE_VIDEO,
      gst_caps_from_string (video), NULL, NULL, 0);
  gst_encoding_profile_add_stream (profile, stream);
  return profile;
}

static GESTimelinePipeline *
create_timeline (int nbargs, gchar ** argv)
{
  GESTimelinePipeline *pipeline;
  GESTimelineLayer *layer;
  GESTrack *tracka, *trackv;
  GESTimeline *timeline;
  guint i;

  timeline = ges_timeline_new ();

  tracka = ges_track_audio_raw_new ();
  trackv = ges_track_video_raw_new ();

  /* We are only going to be doing one layer of timeline objects */
  layer = (GESTimelineLayer *) ges_simple_timeline_layer_new ();

  /* Add the tracks and the layer to the timeline */
  if (!ges_timeline_add_layer (timeline, layer) ||
      !ges_timeline_add_track (timeline, tracka) ||
      !ges_timeline_add_track (timeline, trackv))
    return NULL;

  /* Here we've finished initializing our timeline, we're 
   * ready to start using it... by solely working with the layer !*/

  for (i = 0; i < nbargs / 3; i++) {
    gchar *uri = g_strdup_printf ("file://%s", argv[i * 3]);
    GESTimelineFileSource *src = ges_timeline_filesource_new (uri);

    g_assert (src);
    g_free (uri);

    g_object_set (src,
        "in-point", atoi (argv[i * 3 + 1]) * GST_SECOND,
        "duration", atoi (argv[i * 3 + 2]) * GST_SECOND, NULL);
    /* Since we're using a GESSimpleTimelineLayer, objects will be automatically
     * appended to the end of the layer */
    ges_timeline_layer_add_object (layer, (GESTimelineObject *) src);
  }

  /* In order to view our timeline, let's grab a convenience pipeline to put
   * our timeline in. */
  pipeline = ges_timeline_pipeline_new ();

  /* Add the timeline to that pipeline */
  if (!ges_timeline_pipeline_add_timeline (pipeline, timeline))
    return NULL;

  return pipeline;
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
  GError *err = NULL;
  gchar *outputuri = NULL;
  gchar *container = "application/ogg";
  gchar *audio = "audio/x-vorbis";
  gchar *video = "video/x-theora";
  static gboolean render = FALSE;
  static gboolean smartrender = FALSE;
  GOptionEntry options[] = {
    {"render", 'r', 0, G_OPTION_ARG_NONE, &render,
        "Render to outputuri", NULL},
    {"smartrender", 's', 0, G_OPTION_ARG_NONE, &smartrender,
        "Render to outputuri, and avoid decoding/reencoding", NULL},
    {"outputuri", 'o', 0, G_OPTION_ARG_STRING, &outputuri,
        "URI to encode to", "URI (<protocol>://<location>)"},
    {"format", 'f', 0, G_OPTION_ARG_STRING, &container,
        "Container format", "<GstCaps>"},
    {"vformat", 'v', 0, G_OPTION_ARG_STRING, &video,
        "Video format", "<GstCaps>"},
    {"aformat", 'a', 0, G_OPTION_ARG_STRING, &audio,
        "Audio format", "<GstCaps>"},
    {NULL}
  };
  GOptionContext *ctx;
  GESTimelinePipeline *pipeline;
  GMainLoop *mainloop;
  GstBus *bus;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("- plays/render a list of files");
  g_option_context_set_summary (ctx,
      "If not specified, this example will playback the files\n" "\n"
      "The files should be layed out in triplets of:\n" " * filename\n"
      " * inpoint (in seconds)\n" " * duration (in seconds)");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  if ((argc < 4) || (outputuri && (!render && !smartrender))) {
    g_print ("outputuri:%s, render:%d, smartrender:%d, argc:%d\n",
        outputuri, render, smartrender, argc);
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    g_option_context_free (ctx);
    exit (-1);
  }

  g_option_context_free (ctx);
  /* Initialize the GStreamer Editing Services */
  ges_init ();

  /* Create the pipeline */
  pipeline = create_timeline (argc - 1, argv + 1);
  if (!pipeline)
    exit (-1);

  /* Setup profile/encoding if needed */
  if (render || smartrender) {
    GstEncodingProfile *prof;
    prof = make_encoding_profile (audio, video, container);

    if (!prof ||
        !ges_timeline_pipeline_set_render_settings (pipeline, outputuri, prof)
        || !ges_timeline_pipeline_set_mode (pipeline,
            smartrender ? TIMELINE_MODE_SMART_RENDER : TIMELINE_MODE_RENDER))
      exit (-1);
  } else {
    ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_PREVIEW);
  }

  /* Play the pipeline */
  mainloop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), mainloop);

  if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_print ("Failed to start the encoding\n");
    return 1;
  }
  g_main_loop_run (mainloop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  return 0;
}

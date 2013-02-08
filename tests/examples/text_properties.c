/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon@alum.berkeley.edu>
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

typedef struct
{
  int type;
  char *name;
} transition_type;

GESClip *make_source (char *path, guint64 start, guint64 duration,
    gint priority, gchar * text);

GESTimelinePipeline *make_timeline (char *path, float duration, char *text);

GESClip *
make_source (char *path, guint64 start, guint64 duration, gint priority,
    gchar * text)
{
  gchar *uri = gst_filename_to_uri (path, NULL);

  GESClip *ret = GES_CLIP (ges_uri_clip_new (uri));

  g_object_set (ret,
      "start", (guint64) start,
      "duration", (guint64) duration,
      "priority", (guint32) priority, "in-point", (guint64) 0,
      "text", text, NULL);

  g_free (uri);

  return ret;
}

GESTimelinePipeline *
make_timeline (char *path, float duration, char *text)
{
  GESTimeline *timeline;
  GESTrack *trackv, *tracka;
  GESTimelineLayer *layer1;
  GESClip *srca;
  GESTimelinePipeline *pipeline;
  guint64 aduration;

  pipeline = ges_timeline_pipeline_new ();

  ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_PREVIEW_VIDEO);

  timeline = ges_timeline_new ();
  ges_timeline_pipeline_add_timeline (pipeline, timeline);

  trackv = ges_track_video_raw_new ();
  ges_timeline_add_track (timeline, trackv);

  tracka = ges_track_audio_raw_new ();
  ges_timeline_add_track (timeline, tracka);

  layer1 = GES_TIMELINE_LAYER (ges_timeline_layer_new ());
  g_object_set (layer1, "priority", (gint32) 0, NULL);

  if (!ges_timeline_add_layer (timeline, layer1))
    exit (-1);

  aduration = (guint64) (duration * GST_SECOND);
  srca = make_source (path, 0, aduration, 1, text);
  ges_timeline_layer_add_clip (layer1, srca);

  return pipeline;
}

int
main (int argc, char **argv)
{
  GError *err = NULL;
  GOptionContext *ctx;
  GESTimelinePipeline *pipeline;
  GMainLoop *mainloop;
  gdouble duration;
  char *path, *text;

  GOptionEntry options[] = {
    {"duration", 'd', 0, G_OPTION_ARG_DOUBLE, &duration,
        "duration of transition", "seconds"},
    {"path", 'p', 0, G_OPTION_ARG_STRING, &path,
        "path to file", "path"},
    {"text", 't', 0, G_OPTION_ARG_STRING, &text,
        "text to render", "text"},
    {NULL}
  };

  ctx = g_option_context_new ("- transition between two media files");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing %s\n", err->message);
    exit (1);
  }

  if (argc > 1) {
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    exit (0);
  }

  g_option_context_free (ctx);

  ges_init ();

  pipeline = make_timeline (path, duration, text);

  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds ((duration) + 1, (GSourceFunc) g_main_loop_quit,
      mainloop);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (mainloop);

  return 0;
}

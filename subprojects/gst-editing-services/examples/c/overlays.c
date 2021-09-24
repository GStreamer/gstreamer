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
    gint priority);

GESClip *make_overlay (char *text, guint64 start, guint64 duration,
    gint priority, guint32 color, gdouble xpos, gdouble ypos);

GESPipeline *make_timeline (char *path, float duration, char *text,
    guint32 color, gdouble xpos, gdouble ypos);

#define DEFAULT_DURATION 5
#define DEFAULT_POS 0.5

GESClip *
make_source (char *path, guint64 start, guint64 duration, gint priority)
{
  gchar *uri = gst_filename_to_uri (path, NULL);

  GESClip *ret = GES_CLIP (ges_uri_clip_new (uri));

  g_object_set (ret,
      "start", (guint64) start,
      "duration", (guint64) duration,
      "priority", (guint32) priority, "in-point", (guint64) 0, NULL);

  g_free (uri);

  return ret;
}

GESClip *
make_overlay (char *text, guint64 start, guint64 duration, gint priority,
    guint32 color, gdouble xpos, gdouble ypos)
{
  GESClip *ret = GES_CLIP (ges_text_overlay_clip_new ());

  g_object_set (ret,
      "text", (gchar *) text,
      "start", (guint64) start,
      "duration", (guint64) duration,
      "priority", (guint32) priority,
      "in-point", (guint64) 0,
      "color", (guint32) color,
      "valignment", (gint) GES_TEXT_VALIGN_POSITION,
      "halignment", (gint) GES_TEXT_HALIGN_POSITION,
      "xpos", (gdouble) xpos, "ypos", (gdouble) ypos, NULL);

  return ret;
}

GESPipeline *
make_timeline (char *path, float duration, char *text, guint32 color,
    gdouble xpos, gdouble ypos)
{
  GESTimeline *timeline;
  GESTrack *trackv, *tracka;
  GESLayer *layer1;
  GESClip *srca;
  GESClip *overlay;
  GESPipeline *pipeline;
  guint64 aduration;

  pipeline = ges_pipeline_new ();

  ges_pipeline_set_mode (pipeline, GES_PIPELINE_MODE_PREVIEW_VIDEO);

  timeline = ges_timeline_new ();
  ges_pipeline_set_timeline (pipeline, timeline);

  trackv = GES_TRACK (ges_video_track_new ());
  ges_timeline_add_track (timeline, trackv);

  tracka = GES_TRACK (ges_audio_track_new ());
  ges_timeline_add_track (timeline, tracka);

  layer1 = GES_LAYER (ges_layer_new ());
  g_object_set (layer1, "priority", (gint32) 0, NULL);

  if (!ges_timeline_add_layer (timeline, layer1))
    exit (-1);

  aduration = (guint64) (duration * GST_SECOND);
  srca = make_source (path, 0, aduration, 1);
  overlay = make_overlay (text, 0, aduration, 0, color, xpos, ypos);
  ges_layer_add_clip (layer1, srca);
  ges_layer_add_clip (layer1, overlay);

  return pipeline;
}

int
main (int argc, char **argv)
{
  GError *err = NULL;
  GOptionContext *ctx;
  GESPipeline *pipeline;
  GMainLoop *mainloop;
  gdouble duration = DEFAULT_DURATION;
  char *path = NULL, *text;
  guint64 color;
  gdouble xpos = DEFAULT_POS, ypos = DEFAULT_POS;

  GOptionEntry options[] = {
    {"duration", 'd', 0, G_OPTION_ARG_DOUBLE, &duration,
        "duration of segment", "seconds"},
    {"path", 'p', 0, G_OPTION_ARG_STRING, &path,
        "path to file", "path"},
    {"text", 't', 0, G_OPTION_ARG_STRING, &text,
        "text to render", "text"},
    {"color", 'c', 0, G_OPTION_ARG_INT64, &color,
        "color of the text", "color"},
    {"xpos", 'x', 0, G_OPTION_ARG_DOUBLE, &xpos,
        "horizontal position of the text", "color"},
    {"ypos", 'y', 0, G_OPTION_ARG_DOUBLE, &ypos,
        "vertical position of the text", "color"},
    {NULL}
  };

  ctx = g_option_context_new ("- file segment playback with text overlay");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    gst_print ("Error initializing %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    exit (1);
  }

  if (argc > 1) {
    gst_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    exit (0);
  }

  g_option_context_free (ctx);

  ges_init ();

  if (path == NULL)
    g_error ("Must specify --path=/path/to/media/file option\n");

  pipeline = make_timeline (path, duration, text, color, xpos, ypos);

  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds ((duration) + 1, (GSourceFunc) g_main_loop_quit,
      mainloop);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (mainloop);

  return 0;
}

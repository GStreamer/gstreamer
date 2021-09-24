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
#include <stdlib.h>

typedef struct
{
  int type;
  char *name;
} transition_type;

GESClip *make_source (gchar * path, guint64 start, guint64 inpoint,
    guint64 duration, gint priority);

gboolean print_transition_data (GESClip * tr);

GESPipeline *make_timeline (gchar * nick, double tdur, gchar * patha,
    gfloat adur, gdouble ainpoint, gchar * pathb, gfloat bdur,
    gdouble binpoint);

GESClip *
make_source (gchar * path, guint64 start, guint64 duration, guint64 inpoint,
    gint priority)
{
  gchar *uri = gst_filename_to_uri (path, NULL);

  GESClip *ret = GES_CLIP (ges_uri_clip_new (uri));

  g_object_set (ret,
      "start", (guint64) start,
      "duration", (guint64) duration,
      "priority", (guint32) priority, "in-point", (guint64) inpoint, NULL);

  g_free (uri);

  return ret;
}

gboolean
print_transition_data (GESClip * tr)
{
  GESTrackElement *trackelement;
  GstElement *nleobj;
  guint64 start, duration;
  gint priority;
  char *name;
  GList *trackelements;

  if (!tr)
    return FALSE;

  if (!(trackelements = GES_CONTAINER_CHILDREN (tr)))
    return FALSE;
  if (!(trackelement = GES_TRACK_ELEMENT (trackelements->data)))
    return FALSE;
  if (!(nleobj = ges_track_element_get_nleobject (trackelement)))
    return FALSE;

  g_object_get (nleobj, "start", &start, "duration", &duration,
      "priority", &priority, "name", &name, NULL);
  gst_print ("nleobject for %s: %f %f %d\n", name,
      ((gfloat) start) / GST_SECOND,
      ((gfloat) duration) / GST_SECOND, priority);

  return FALSE;
}

GESPipeline *
make_timeline (gchar * nick, gdouble tdur, gchar * patha, gfloat adur,
    gdouble ainp, gchar * pathb, gfloat bdur, gdouble binp)
{
  GESTimeline *timeline;
  GESTrack *trackv, *tracka;
  GESLayer *layer1;
  GESClip *srca, *srcb;
  GESPipeline *pipeline;
  guint64 aduration, bduration, tduration, tstart, ainpoint, binpoint;
  GESTransitionClip *tr = NULL;

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

  aduration = (guint64) (adur * GST_SECOND);
  bduration = (guint64) (bdur * GST_SECOND);
  tduration = (guint64) (tdur * GST_SECOND);
  ainpoint = (guint64) (ainp * GST_SECOND);
  binpoint = (guint64) (binp * GST_SECOND);
  tstart = aduration - tduration;
  srca = make_source (patha, 0, aduration, ainpoint, 1);
  srcb = make_source (pathb, tstart, bduration, binpoint, 2);
  ges_layer_add_clip (layer1, srca);
  ges_layer_add_clip (layer1, srcb);
  g_timeout_add_seconds (1, (GSourceFunc) print_transition_data, srca);
  g_timeout_add_seconds (1, (GSourceFunc) print_transition_data, srcb);

  if (tduration != 0) {
    gst_print ("creating transition at %" GST_TIME_FORMAT " of %f duration (%"
        GST_TIME_FORMAT ")\n", GST_TIME_ARGS (tstart), tdur,
        GST_TIME_ARGS (tduration));
    if (!(tr = ges_transition_clip_new_for_nick (nick)))
      g_error ("invalid transition type %s\n", nick);

    g_object_set (tr,
        "start", (guint64) tstart,
        "duration", (guint64) tduration, "in-point", (guint64) 0, NULL);
    ges_layer_add_clip (layer1, GES_CLIP (tr));
    g_timeout_add_seconds (1, (GSourceFunc) print_transition_data, tr);
  }

  return pipeline;
}

int
main (int argc, char **argv)
{
  GError *err = NULL;
  GOptionContext *ctx;
  GESPipeline *pipeline;
  GMainLoop *mainloop;
  gchar *type = (gchar *) "crossfade";
  gchar *patha, *pathb;
  gdouble adur, bdur, tdur, ainpoint, binpoint;

  GOptionEntry options[] = {
    {"type", 't', 0, G_OPTION_ARG_STRING, &type,
        "type of transition to create", "<smpte-transition>"},
    {"duration", 'd', 0, G_OPTION_ARG_DOUBLE, &tdur,
        "duration of transition", "seconds"},
    {NULL}
  };

  ctx = g_option_context_new ("- transition between two media files");
  g_option_context_set_summary (ctx,
      "Select two files, and optionally a transition duration and type.\n"
      "A file is a triplet of filename, inpoint (in seconds) and duration (in seconds).\n"
      "Example:\n" "transition file1.avi 0 5 file2.avi 25 5 -d 2 -t crossfade");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    gst_print ("Error initializing %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    exit (1);
  }

  if (argc < 4) {
    gst_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    exit (0);
  }

  g_option_context_free (ctx);

  ges_init ();

  patha = argv[1];
  ainpoint = (gdouble) atof (argv[2]);
  adur = (gdouble) atof (argv[3]);
  pathb = argv[4];
  binpoint = (gdouble) atof (argv[5]);
  bdur = (gdouble) atof (argv[6]);

  pipeline =
      make_timeline (type, tdur, patha, adur, ainpoint, pathb, bdur, binpoint);

  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds ((adur + bdur) + 1, (GSourceFunc) g_main_loop_quit,
      mainloop);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (mainloop);

  return 0;
}

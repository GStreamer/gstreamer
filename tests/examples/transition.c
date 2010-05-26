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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <ges/ges.h>

typedef struct
{
  int type;
  char *name;
} transition_type;

GESTimelineObject *
make_source (char *path, guint64 start, guint64 duration, gint priority)
{
  char *uri = g_strdup_printf ("file://%s", path);

  GESTimelineObject *ret =
      GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));

  g_object_set (ret,
      "start", (guint64) start,
      "duration", (guint64) duration,
      "priority", (guint32) priority, "in-point", (guint64) 0, NULL);

  g_free (uri);

  return ret;
}

gboolean
print_transition_data (GESTimelineObject * tr)
{
  if (!tr)
    return FALSE;

  GESTrackObject *trackobj = GES_TRACK_OBJECT (tr->trackobjects->data);
  GstElement *gnlobj = trackobj->gnlobject;
  guint64 start, duration;
  gint priority;
  char *name;

  g_object_get (gnlobj, "start", &start, "duration", &duration,
      "priority", &priority, "name", &name, NULL);
  g_print ("gnlobject for %s: %f %f %d\n", name,
      ((gfloat) start) / GST_SECOND,
      ((gfloat) duration) / GST_SECOND, priority);

  return FALSE;
}

int
main (int argc, char **argv)
{
  GError *err = NULL;
  GOptionContext *ctx;
  GESTimelinePipeline *pipeline;
  GESTimeline *timeline;
  GESTrack *trackv;
  GESTimelineLayer *layer1;
  GESTimelineObject *srca, *srcb;
  GMainLoop *mainloop;
  gint type;
  gchar *uri = NULL;
  gdouble transition_duration;

  GOptionEntry options[] = {
    {"type", 't', 0, G_OPTION_ARG_INT, &type,
        "type of transition to create (smpte numeric)", "<smpte" "transition>"},
    {"duration", 'd', 0.0, G_OPTION_ARG_DOUBLE, &transition_duration,
        "duration of transition", "seconds"},
    {NULL}
  };

  if (!g_thread_supported ())
    g_thread_init (NULL);

  ctx = g_option_context_new ("- transition between two media files");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing %s\n", err->message);
    exit (1);
  }

  if (argc < 4) {
    g_print ("%s", g_option_context_get_help (ctx, TRUE, NULL));
    exit (0);
  }

  guint64 tdur = (guint64) transition_duration * GST_SECOND;

  g_option_context_free (ctx);

  ges_init ();

  pipeline = ges_timeline_pipeline_new ();
  ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_PREVIEW_VIDEO);

  timeline = ges_timeline_new ();
  ges_timeline_pipeline_add_timeline (pipeline, timeline);

  trackv = ges_track_video_raw_new ();
  ges_timeline_add_track (timeline, trackv);

  layer1 = GES_TIMELINE_LAYER (ges_timeline_layer_new ());
  g_object_set (layer1, "priority", (gint32) 0, NULL);

  if (!ges_timeline_add_layer (timeline, layer1))
    return -1;

  guint64 aduration = (guint64) (atof (argv[2]) * GST_SECOND);
  guint64 bduration = (guint64) (atof (argv[4]) * GST_SECOND);
  guint64 tstart = aduration - tdur;
  srca = make_source (argv[1], 0, aduration, 1);
  srcb = make_source (argv[3], tstart, bduration, 2);
  ges_timeline_layer_add_object (layer1, srca);
  ges_timeline_layer_add_object (layer1, srcb);
  g_timeout_add_seconds (1, (GSourceFunc) print_transition_data, srca);
  g_timeout_add_seconds (1, (GSourceFunc) print_transition_data, srcb);

  GESTimelineTransition *tr = NULL;

  if (tdur != 0) {
    g_print ("creating transition at %ld of %f duration (%ld ns)\n",
        tstart, transition_duration, tdur);
    tr = ges_timeline_transition_new ();
    g_object_set (tr,
        "start", (guint64) tstart,
        "duration", (guint64) tdur, "in-point", (guint64) 0, NULL);
    ges_timeline_layer_add_object (layer1, GES_TIMELINE_OBJECT (tr));
    g_timeout_add_seconds (1, (GSourceFunc) print_transition_data, tr);
  }

  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds (((aduration + bduration) / GST_SECOND) + 1,
      (GSourceFunc) g_main_loop_quit, mainloop);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (mainloop);

  return 0;
}

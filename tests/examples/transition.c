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

transition_type transitions[] = {
  {-1, "fade"},
  {0, "wipe_ltr"},
  {1, "wipe_ttb"},
};

#define N_TRANSITIONS 3
#define INVALID_TRANSITION -2

int
transition_for_name (char *name)
{
  return -1;
}

void
notify_max_duration_cb (GObject * object)
{
  g_print ("got here\n");
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
  GESTimelineFileSource *srca, *srcb;
  GESCustomTimelineSource *src;
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

  g_option_context_free (ctx);

  ges_init ();

  pipeline = ges_timeline_pipeline_new ();
  ges_timeline_pipeline_set_mode (pipeline, TIMELINE_MODE_PREVIEW_VIDEO);

  timeline = ges_timeline_new ();
  ges_timeline_pipeline_add_timeline (pipeline, timeline);

  trackv = ges_track_video_raw_new ();
  ges_timeline_add_track (timeline, trackv);

  layer1 = GES_TIMELINE_LAYER (ges_timeline_layer_new ());
  g_object_set (layer1, "priority", 1, NULL);

  if (!ges_timeline_add_layer (timeline, layer1))
    return -1;

  uri = g_strdup_printf ("file://%s", argv[1]);
  srca = ges_timeline_filesource_new (uri);

  guint64 aduration = (guint64) (atof (argv[2]) * GST_SECOND);
  g_object_set (srca, "start", 0, "duration", aduration, NULL);
  g_signal_connect (srca, "notify::max_duration",
      G_CALLBACK (notify_max_duration_cb), NULL);

  g_free (uri);

  uri = g_strdup_printf ("file://%s", argv[3]);
  srcb = ges_timeline_filesource_new (uri);
  guint64 bduration = (guint64) (atof (argv[4]) * GST_SECOND);
  g_object_set (srcb, "start", aduration, "duration", bduration, NULL);
  g_signal_connect (srcb, "notify::max_duration",
      G_CALLBACK (notify_max_duration_cb), NULL);

  g_free (uri);

  ges_timeline_layer_add_object (layer1, GES_TIMELINE_OBJECT (srca));
  ges_timeline_layer_add_object (layer1, GES_TIMELINE_OBJECT (srcb));

  GESTimelineTransition *tr;

  guint64 tdur = (guint64) transition_duration * GST_SECOND;
  if (tdur != 0) {
    g_print ("creating transition of %f duration (%ld ns)",
        transition_duration, tdur);
    tr = ges_timeline_transition_new ();
    ges_timeline_layer_add_object (layer1, GES_TIMELINE_OBJECT (tr));
    g_object_set (tr, "start", aduration - tdur, "duration", tdur, NULL);
  }

  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add_seconds (((aduration + bduration) / GST_SECOND) + 1,
      (GSourceFunc) g_main_loop_quit, mainloop);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (mainloop);

  return 0;
}

/*
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gst/gst.h>

static GTimeVal start_time;
gboolean done = FALSE;
GstClockTime total = 0;
guint counted = 0;

static void
handoff (GstElement * fakesink, GstBuffer * data)
{
  GTimeVal end_time;
  GstClockTime diff;

  if (!GST_IS_BUFFER (data))
    return;
  g_get_current_time (&end_time);
  diff = ((GstClockTime) end_time.tv_sec - start_time.tv_sec) * GST_SECOND +
      ((GstClockTime) end_time.tv_usec -
      start_time.tv_usec) * (GST_SECOND / G_USEC_PER_SEC);
  g_print ("time to launch spider pipeline: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (diff));
  done = TRUE;
  /* don't count first try, it loads the plugins */
  if (counted++)
    total += diff;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  guint i, count = 20;
  gchar *file, *pipeline_str;
  gchar **bla;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage : %s <file>\n", argv[0]);
    return -1;
  }
  bla = g_strsplit (argv[1], " ", -1);
  file = g_strjoinv ("\\ ", bla);
  pipeline_str =
      g_strdup_printf
      ("filesrc location=\"%s\" ! spider ! audio/x-raw-int ! fakesink name = sink",
      file);

  for (i = 0; i <= count; i++) {
    GstElement *sink;

    g_get_current_time (&start_time);
    pipeline = gst_parse_launch (pipeline_str, NULL);
    sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
    g_object_set (sink, "signal-handoffs", TRUE, NULL);
    g_signal_connect (sink, "handoff", (GCallback) handoff, NULL);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    done = FALSE;
    while (!done && gst_bin_iterate (GST_BIN (pipeline)));
    g_object_unref (pipeline);
  }

  g_print ("\ntime to launch spider pipeline (average): %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (total / count));

  pipeline = NULL;
  return 0;
}

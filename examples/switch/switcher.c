/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
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

#include <gst/gst.h>

static GMainLoop *loop = NULL;



static void
got_eos (GstElement *pipeline)
{
  g_main_loop_quit (loop);
}

static gboolean
idle_iterate (GstElement *pipeline)
{
  gst_bin_iterate (GST_BIN (pipeline));
  return (GST_STATE (GST_ELEMENT (pipeline)) == GST_STATE_PLAYING);
}

static gboolean
switch_timer (GstElement *video_switch)
{
  gint nb_sources, active_source;
  
  g_object_get (G_OBJECT (video_switch), "nb_sources", &nb_sources, NULL);
  g_object_get (G_OBJECT (video_switch), "active_source",
                &active_source, NULL);
  
  active_source ++;
  
  if (active_source > nb_sources - 1)
    active_source = 0;
  
  g_object_set (G_OBJECT (video_switch), "active_source",
                active_source, NULL);
  
  g_message ("current number of sources : %d, active source %d",
             nb_sources, active_source);
  
  return (GST_STATE (GST_ELEMENT (video_switch)) == GST_STATE_PLAYING);
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *src1, *src2, *video_switch, *video_sink;

  /* Initing GStreamer library */
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  
  pipeline = gst_pipeline_new ("pipeline");
  src1 = gst_element_factory_make ("videotestsrc", "src1");
  g_object_set (G_OBJECT (src1), "pattern", 0, NULL);
  src2 = gst_element_factory_make ("videotestsrc", "src2");
  g_object_set (G_OBJECT (src2), "pattern", 1, NULL);
  video_switch = gst_element_factory_make ("switch", "video_switch");
  video_sink = gst_element_factory_make ("ximagesink", "video_sink");
  
  gst_bin_add_many (GST_BIN (pipeline), src1, src2, video_switch,
                    video_sink, NULL);
  
  gst_element_link (src1, video_switch);
  gst_element_link (src2, video_switch);
  gst_element_link (video_switch, video_sink);
  
  g_signal_connect (G_OBJECT (pipeline), "eos",
                    G_CALLBACK (got_eos), NULL);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  
  g_idle_add ((GSourceFunc) idle_iterate, pipeline);
  g_timeout_add (2000, (GSourceFunc) switch_timer, video_switch);
  
  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_READY);
  
  /* unref */
  gst_object_unref (GST_OBJECT (pipeline));

  exit (0);
}

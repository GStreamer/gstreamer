/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * srcstate.c: Tests alsasrc for state changes
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

GstElement *pipeline;

static void
set_state (GstElementState state)
{
  GstElementState old_state = gst_element_get_state (pipeline);

  g_print ("Setting state from %s to %s...",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (state));

  if (!gst_element_set_state (pipeline, state)) {
    g_print (" ERROR\n");
    exit (-1);
  }

  if (state == GST_STATE_PLAYING) {
    gint i;

    g_print (" DONE - iterating a bit...");
    for (i = 0; i < 5; i++) {
      if (!gst_bin_iterate (GST_BIN (pipeline))) {
        g_print (" ERROR in iteration %d\n", i);
        exit (-2);
      }
    }
  }
  g_print (" DONE\n");
}

static void
create_pipeline (void)
{
  GstElement *alsasrc;
  GstElement *fakesink;

  pipeline = gst_pipeline_new ("pipeline");
  alsasrc = gst_element_factory_make ("alsasrc", "alsasrc");
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  gst_bin_add_many (GST_BIN (pipeline), alsasrc, fakesink, NULL);
  gst_element_link (alsasrc, fakesink);

}

gint
main (gint argc, gchar * argv[])
{
  gst_init (&argc, &argv);

  g_print ("\n" "This test will check if state changes work on the alsasrc.\n");
  create_pipeline ();

  /* simulate some state changes here */
  set_state (GST_STATE_READY);
  set_state (GST_STATE_NULL);
  set_state (GST_STATE_READY);
  set_state (GST_STATE_NULL);
  set_state (GST_STATE_PAUSED);
  set_state (GST_STATE_NULL);
  set_state (GST_STATE_PLAYING);
  set_state (GST_STATE_PAUSED);
  set_state (GST_STATE_PLAYING);
  set_state (GST_STATE_READY);
  set_state (GST_STATE_PLAYING);
  set_state (GST_STATE_NULL);
  set_state (GST_STATE_PLAYING);

  g_print ("The alsa plugin mastered another test.\n");

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}

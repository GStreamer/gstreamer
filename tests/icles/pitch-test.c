/* GStreamer
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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

/* compile with :
 * gcc -Wall $(pkg-config --cflags --libs gstreamer-0.10 gstreamer-controller-0.10) pitch.c -o pitch
 */

#include <string.h>
#include <unistd.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>

int
main (int argc, char **argv)
{
  GMainLoop *loop;
  gint i;

  GstElement *audiotestsrc;
  GstElement *audioconvert1, *audioconvert2;
  GstElement *pitch;
  GstElement *sink;
  GstElement *pipeline;
  GstController *ctl;
  GValue val = { 0, };

  if (argc != 2) {
    g_printerr ("Usage: %s <audiosink>\n", argv[0]);
    return 1;
  }

  /* initialize GStreamer */
  gst_init (&argc, &argv);
  gst_controller_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("audio-player");
  audiotestsrc = gst_element_factory_make ("audiotestsrc", "audiotestsrc");
  g_assert (audiotestsrc != NULL);
  audioconvert1 = gst_element_factory_make ("audioconvert", "audioconvert1");
  g_assert (audioconvert1 != NULL);
  audioconvert2 = gst_element_factory_make ("audioconvert", "audioconvert2");
  g_assert (audioconvert2 != NULL);
  pitch = gst_element_factory_make ("pitch", "pitch");
  g_assert (pitch != NULL);
  sink = gst_element_factory_make (argv[1], "sink");
  g_assert (sink != NULL);

  gst_bin_add_many (GST_BIN (pipeline),
      audiotestsrc, audioconvert1, pitch, audioconvert2, sink, NULL);
  gst_element_link_many (audiotestsrc, audioconvert1, pitch, audioconvert2,
      sink, NULL);

  ctl = gst_object_control_properties (G_OBJECT (pitch), "pitch", NULL);
  gst_controller_set_interpolation_mode (ctl, "pitch", GST_INTERPOLATE_LINEAR);

  g_value_init (&val, G_TYPE_FLOAT);

  for (i = 0; i < 100; ++i) {
    if (i % 2)
      g_value_set_float (&val, 0.5);
    else
      g_value_set_float (&val, 1.5);

    gst_controller_set (ctl, "pitch", i * GST_SECOND, &val);
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Running\n");
  g_main_loop_run (loop);

  /* set up a controller */

  /* clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}

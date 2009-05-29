/* GStreamer
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstlfocontrolsource.h>

static gboolean
on_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_warning ("Got ERROR");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING:
      g_warning ("Got WARNING");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline;
  GstElement *shapewipe;
  GstController *ctrl;
  GstLFOControlSource *csource;
  GValue val = { 0, };
  GMainLoop *loop;
  GstBus *bus;
  gchar *pipeline_string;

  if (argc != 2) {
    g_print ("Usage: shapewipe mask.png\n");
    return -1;
  }

  gst_init (&argc, &argv);
  gst_controller_init (&argc, &argv);

  pipeline_string =
      g_strdup_printf
      ("videotestsrc ! video/x-raw-yuv,width=640,height=480 ! shapewipe name=shape border=0.01 ! videomixer name=mixer ! ffmpegcolorspace ! autovideosink     filesrc location=%s ! typefind ! decodebin2 ! ffmpegcolorspace ! videoscale ! queue ! shape.mask_sink    videotestsrc pattern=snow ! video/x-raw-yuv,width=640,height=480 ! queue ! mixer.",
      argv[1]);

  pipeline = gst_parse_launch (pipeline_string, NULL);
  g_free (pipeline_string);

  if (pipeline == NULL) {
    g_print ("Failed to create pipeline\n");
    return -2;
  }

  shapewipe = gst_bin_get_by_name (GST_BIN (pipeline), "shape");

  if (!(ctrl = gst_controller_new (G_OBJECT (shapewipe), "position", NULL))) {
    g_print ("can't control shapewipe element\n");
    return -3;
  }

  csource = gst_lfo_control_source_new ();

  gst_controller_set_control_source (ctrl, "position",
      GST_CONTROL_SOURCE (csource));

  g_value_init (&val, G_TYPE_FLOAT);
  g_value_set_float (&val, 0.5);
  g_object_set (G_OBJECT (csource), "amplitude", &val, NULL);
  g_value_set_float (&val, 0.5);
  g_object_set (G_OBJECT (csource), "offset", &val, NULL);
  g_value_unset (&val);

  g_object_set (G_OBJECT (csource), "frequency", 0.5, NULL);
  g_object_set (G_OBJECT (csource), "timeshift", 500 * GST_MSECOND, NULL);

  g_object_unref (csource);

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (on_message), loop);
  gst_object_unref (GST_OBJECT (bus));

  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Failed to go into PLAYING state");
    return -4;
  }

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_main_loop_unref (loop);

  g_object_unref (G_OBJECT (ctrl));
  gst_object_unref (G_OBJECT (pipeline));

  return 0;
}

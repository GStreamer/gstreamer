/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "unistd.h"

#include <gst/gst.h>

static GRand *myrand;
static GMainLoop *loop;

static gboolean
message_received (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  g_print ("message %p\n", message);

  if (message->type == GST_MESSAGE_EOS) {
    g_print ("EOS!!\n");
    if (g_main_loop_is_running (loop))
      g_main_loop_quit (loop);
  }
  gst_message_unref (message);

  return TRUE;
}

static gboolean
state_change (GstElement * element)
{
  g_usleep (g_rand_int_range (myrand, 100, 600));
  g_print ("pause..\n");
  gst_element_set_state (element, GST_STATE_PAUSED);
  gst_element_get_state (element, NULL, NULL, NULL);
  g_print ("done\n");
  g_usleep (g_rand_int_range (myrand, 50, 100));
  g_print ("play..\n");
  gst_element_set_state (element, GST_STATE_PLAYING);
  gst_element_get_state (element, NULL, NULL, NULL);
  g_print ("done\n");

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc1, *fakesink1;
  GstBus *bus;

  gst_init (&argc, &argv);

  myrand = g_rand_new ();

  pipeline = gst_pipeline_new ("pipeline");

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, (GstBusHandler) message_received, pipeline);
  gst_object_unref (GST_OBJECT (bus));

  fakesrc1 = gst_element_factory_make ("fakesrc", "fakesrc1");
  g_object_set (G_OBJECT (fakesrc1), "num_buffers", 1000, NULL);
  fakesink1 = gst_element_factory_make ("fakesink", "fakesink1");

  gst_bin_add (GST_BIN (pipeline), fakesrc1);
  gst_bin_add (GST_BIN (pipeline), fakesink1);
  gst_pad_link (gst_element_get_pad (fakesrc1, "src"),
      gst_element_get_pad (fakesink1, "sink"));

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (gst_object_default_deep_notify), NULL);

  g_idle_add ((GSourceFunc) state_change, pipeline);
  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}

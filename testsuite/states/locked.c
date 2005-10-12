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

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc1, *fakesink1;
  GstElement *fakesrc2, *fakesink2;
  GstBus *bus;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_watch (bus, GST_MESSAGE_EOS, (GstBusFunc) message_received,
      (gpointer) pipeline);
  gst_object_unref (bus);

  fakesrc1 = gst_element_factory_make ("fakesrc", "fakesrc1");
  g_object_set (G_OBJECT (fakesrc1), "num_buffers", 5, NULL);
  fakesink1 = gst_element_factory_make ("fakesink", "fakesink1");

  gst_bin_add (GST_BIN (pipeline), fakesrc1);
  gst_bin_add (GST_BIN (pipeline), fakesink1);
  gst_pad_link (gst_element_get_pad (fakesrc1, "src"),
      gst_element_get_pad (fakesink1, "sink"));

  fakesrc2 = gst_element_factory_make ("fakesrc", "fakesrc2");
  g_object_set (G_OBJECT (fakesrc2), "num_buffers", 5, NULL);
  fakesink2 = gst_element_factory_make ("fakesink", "fakesink2");

  gst_bin_add (GST_BIN (pipeline), fakesrc2);
  gst_bin_add (GST_BIN (pipeline), fakesink2);
  gst_pad_link (gst_element_get_pad (fakesrc2, "src"),
      gst_element_get_pad (fakesink2, "sink"));

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (gst_object_default_deep_notify), NULL);

  GST_OBJECT_FLAG_SET (fakesrc2, GST_ELEMENT_LOCKED_STATE);
  GST_OBJECT_FLAG_SET (fakesink2, GST_ELEMENT_LOCKED_STATE);

  g_print ("play..\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_object_set (G_OBJECT (fakesrc1), "num_buffers", 5, NULL);

  gst_element_set_state (pipeline, GST_STATE_READY);

  GST_OBJECT_FLAG_UNSET (fakesrc2, GST_ELEMENT_LOCKED_STATE);
  GST_OBJECT_FLAG_UNSET (fakesink2, GST_ELEMENT_LOCKED_STATE);

  g_print ("play..\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);

  return 0;
}

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

#include <gst/gst.h>

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *fakesrc1, *fakesink1;
  GstElement *fakesrc2, *fakesink2;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  fakesrc1 = gst_element_factory_make ("fakesrc", "fakesrc1");
  g_object_set (G_OBJECT (fakesrc1), "num_buffers", 5, NULL);
  fakesink1 = gst_element_factory_make ("fakesink", "fakesink1");

  gst_bin_add_many (GST_BIN (pipeline), fakesrc1, fakesink1, NULL);
  gst_element_link_pads (fakesrc1, "src", fakesink1, "sink");

  fakesrc2 = gst_element_factory_make ("fakesrc", "fakesrc2");
  g_object_set (G_OBJECT (fakesrc2), "num_buffers", 5, NULL);
  fakesink2 = gst_element_factory_make ("fakesink", "fakesink2");

  gst_bin_add_many (GST_BIN (pipeline), fakesrc2, fakesink2, NULL);
  gst_element_link_pads (fakesrc2, "src", fakesink2, "sink");

  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);

  GST_FLAG_SET (fakesrc2, GST_ELEMENT_LOCKED_STATE);
  GST_FLAG_SET (fakesink2, GST_ELEMENT_LOCKED_STATE);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_READY);

  g_object_set (G_OBJECT (fakesrc1), "num_buffers", 5, NULL);

  GST_FLAG_UNSET (fakesrc2, GST_ELEMENT_LOCKED_STATE);
  GST_FLAG_UNSET (fakesink2, GST_ELEMENT_LOCKED_STATE);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}

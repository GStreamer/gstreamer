/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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

#include <unistd.h>

#include <gst/gst.h>

static void
handoff_identity (GstElement * element)
{
  GstBin *parent;

  parent = GST_BIN (gst_element_get_parent (element));
  g_print ("identity handoff\n");
  gst_bin_remove (parent, element);
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *sink, *id;

  gst_init (&argc, &argv);

  g_print ("setting up...\n");
  /* setup pipeline */
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  id = gst_element_factory_make ("identity", NULL);
  g_assert (id);
  /* ref object here as it will be unparented and destroyed in the
   * handoff signal, normally the scheduler should keep a ref to the
   * currently scheduled elements but that's another bug displayed in
   * 142183-2.c */
  gst_object_ref (GST_OBJECT (id));
  g_signal_connect (G_OBJECT (id), "handoff", (GCallback) handoff_identity,
      NULL);
  g_object_set (G_OBJECT (id), "loop-based", TRUE, NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);

  gst_bin_add_many (GST_BIN (pipeline), src, id, sink, NULL);
  /* this is what triggers the bug */
  gst_element_enable_threadsafe_properties (GST_ELEMENT (src));
  gst_element_enable_threadsafe_properties (GST_ELEMENT (id));
  gst_element_enable_threadsafe_properties (GST_ELEMENT (sink));

  gst_element_link_pads (src, "src", id, "sink");
  gst_element_link_pads (id, "src", sink, "sink");

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  gst_bin_iterate (GST_BIN (pipeline));
  gst_bin_iterate (GST_BIN (pipeline));

  /* 'cause we're going into deadlock mode */
  alarm (5);

  g_print ("adding identity back...\n");
  /* add identity back in */
  gst_bin_add_many (GST_BIN (pipeline), id, NULL);

  g_print ("going into possible deadlock... alarm at 5 seconds\n");
  gst_bin_iterate (GST_BIN (pipeline));
  gst_bin_iterate (GST_BIN (pipeline));
  g_print ("ok, no deadlock. bug 142183 fixed!\n");

  g_print ("cleaning up...\n");
  gst_object_unref (GST_OBJECT (pipeline));
  gst_object_unref (GST_OBJECT (id));
  src = id = sink = pipeline = NULL;

  g_print ("done.\n");
  return 0;
}

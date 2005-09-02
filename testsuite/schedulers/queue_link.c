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

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *thread, *bin, *src, *queue, *id1, *sink;

  gst_init (&argc, &argv);

  g_print ("setting up...\n");
  /* setup pipeline */
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  queue = gst_element_factory_make ("queue", NULL);
  g_assert (queue);

  thread = gst_element_factory_make ("thread", NULL);
  g_assert (thread);
  bin = gst_element_factory_make ("bin", NULL);
  g_assert (bin);
  id1 = gst_element_factory_make ("identity", NULL);
  g_assert (id1);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);

  gst_bin_add_many (GST_BIN (bin), id1, sink, NULL);
  gst_bin_add_many (GST_BIN (thread), bin, NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, queue, thread, NULL);

  gst_element_link_pads (src, "src", queue, "sink");
  gst_element_link_pads (queue, "src", id1, "sink");
  gst_element_link_pads (id1, "src", sink, "sink");

  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();

  g_print ("unlinking...\n");

  gst_object_ref (queue);
  gst_bin_remove (GST_BIN (pipeline), queue);
  gst_object_ref (bin);
  gst_bin_remove (GST_BIN (thread), bin);

  g_print ("done.\n");
  return 0;
}

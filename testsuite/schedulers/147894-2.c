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

static gboolean empty;
static gboolean bug;
static gboolean handoff;
static GstElement *pipeline2;

static void
queue_empty (GstElement * element)
{
  g_print ("queue empty\n");
  if (!handoff)
    bug = TRUE;
}

static void
queue_filled (GstElement * element)
{
  g_print ("queue filled\n");
  empty = FALSE;

  /* read from the other end */
  handoff = FALSE;
  bug = FALSE;

  alarm (5);

  g_print ("emptying queue with 5 second timeout...\n");
  while (!bug && !handoff) {
    gst_bin_iterate (GST_BIN (pipeline2));
  }
}

static void
handoff_identity (GstElement * element)
{
  g_print ("identity handoff\n");
  handoff = TRUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *sink, *queue, *id;

  gst_init (&argc, &argv);

  g_print ("setting up...\n");
  /* setup pipeline */
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  queue = gst_element_factory_make ("queue", NULL);
  g_assert (queue);
  g_signal_connect (G_OBJECT (queue), "overrun", (GCallback) queue_filled,
      NULL);
  g_signal_connect (G_OBJECT (queue), "underrun", (GCallback) queue_empty,
      NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, queue, NULL);

  gst_element_link_pads (src, "src", queue, "sink");

  /* second pipeline for sinks */
  pipeline2 = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline2);
  id = gst_element_factory_make ("identity", NULL);
  g_assert (id);
  g_signal_connect (G_OBJECT (id), "handoff", (GCallback) handoff_identity,
      NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);
  gst_bin_add_many (GST_BIN (pipeline2), id, sink, NULL);

  gst_element_link_pads (queue, "src", id, "sink");
  gst_element_link_pads (id, "src", sink, "sink");

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  if (gst_element_set_state (pipeline2, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();

  g_print ("running...\n");
  /* fill queue */
  empty = TRUE;
  while (empty) {
    gst_bin_iterate (GST_BIN (pipeline));
  }
  g_assert (!bug);

  g_print ("relinking...\n");
  /* now unlink and link id and sink */
  gst_element_unlink_pads (id, "src", sink, "sink");
  gst_element_link_pads (id, "src", sink, "sink");

  g_print ("running again...\n");
  /* fill queue */
  empty = TRUE;
  while (empty) {
    gst_bin_iterate (GST_BIN (pipeline));
  }
  g_assert (!bug);

  /* trigger the bug */


  g_print ("cleaning up...\n");
  gst_object_unref (GST_OBJECT (pipeline));
  gst_object_unref (GST_OBJECT (pipeline2));
  src = id = sink = pipeline = pipeline2 = NULL;

  g_print ("done.\n");
  return 0;
}

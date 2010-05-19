/* GStreamer
 * Copyright (C) 2010 Stefan Kost <ensonic@users.sf.net>
 *
 * capsnego.c: benchmark for caps negotiation
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

/* the code below recursively builds a pipeline, the GRAPH_DEPTH is the depth
 * of the tree, NUM_CHILDREN is the number of branches on each level
 */
#define GRAPH_DEPTH 4
#define NUM_CHILDREN 3

static gboolean
create_node (GstBin * bin, GstElement * sink, GstElement ** adder,
    GstElement ** vol, GstElement ** ac)
{

  *adder = gst_element_factory_make ("adder", NULL);
  if (!*adder) {
    GST_WARNING ("need adder from gst-plugins-base");
    return FALSE;
  }
  *vol = gst_element_factory_make ("volume", NULL);
  if (!*vol) {
    GST_WARNING ("need volume from gst-plugins-base");
    return FALSE;
  }
  *ac = gst_element_factory_make ("audioconvert", NULL);
  if (!*ac) {
    GST_WARNING ("need audioconvert from gst-plugins-base");
    return FALSE;
  }
  gst_bin_add_many (bin, *adder, *vol, *ac, NULL);
  if (!gst_element_link_many (*adder, *vol, *ac, sink, NULL)) {
    GST_WARNING ("can't link elements");
    return FALSE;
  }
  return TRUE;
}

static gboolean
create_nodes (GstBin * bin, GstElement * sink, gint depth)
{
  GstElement *adder, *vol, *ac, *src;
  gint i;

  for (i = 0; i < NUM_CHILDREN; i++) {
    if (depth > 0) {
      if (!create_node (bin, sink, &adder, &vol, &ac)) {
        return FALSE;
      }
      if (!create_nodes (bin, adder, depth - 1)) {
        return FALSE;
      }
    } else {
      src = gst_element_factory_make ("audiotestsrc", NULL);
      if (!src) {
        GST_WARNING ("need audiotestsrc from gst-plugins-base");
        return FALSE;
      }
      gst_bin_add (bin, src);
      if (!gst_element_link (src, sink)) {
        GST_WARNING ("can't link elements");
        return FALSE;
      }
    }
  }
  return TRUE;
}

static void
event_loop (GstElement * bin)
{
  GstBus *bus;
  GstMessage *msg = NULL;
  gboolean running = TRUE;

  bus = gst_element_get_bus (bin);

  while (running) {
    msg = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);

    if (GST_MESSAGE_SRC (msg) == (GstObject *) bin) {
      GstState old_state, new_state;

      gst_message_parse_state_changed (msg, &old_state, &new_state, NULL);
      if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
        running = FALSE;
      }
    }
    gst_message_unref (msg);
  }
}


gint
main (gint argc, gchar * argv[])
{
  GstBin *bin;
  GstClockTime start, end;
  GstElement *sink;
  GstElement *adder, *vol, *ac;

  gst_init (&argc, &argv);

  bin = GST_BIN (gst_pipeline_new ("pipeline"));

  g_print ("building pipeline\n");
  sink = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add (bin, sink);
  if (!create_node (bin, sink, &adder, &vol, &ac)) {
    goto Error;
  }
  if (!create_nodes (bin, adder, GRAPH_DEPTH)) {
    goto Error;
  }
  g_print ("built pipeline with %d elements\n", GST_BIN_NUMCHILDREN (bin));

  g_print ("starting pipeline\n");
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  GST_DEBUG_BIN_TO_DOT_FILE (bin, GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE, "capsnego");
  start = gst_util_get_timestamp ();
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
  event_loop (GST_ELEMENT (bin));
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " reached paused\n",
      GST_TIME_ARGS (end - start));

Error:
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  gst_object_unref (bin);
  return 0;
}

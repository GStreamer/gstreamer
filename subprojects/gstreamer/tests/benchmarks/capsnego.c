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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* This benchmark recursively builds a pipeline and measures the time to go
 * from READY to PAUSED state.
 *
 * The graph size and type can be controlled with a few command line options:
 *
 *  -d depth: is the depth of the tree
 *  -c children: is the number of branches on each level
 *  -f <flavour>: can be "audio" or "video" and is controlling the kind of
 *                elements that are used.
 */

#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>

enum
{
  FLAVOUR_AUDIO = 0,
  FLAVOUR_VIDEO,
  NUM_FLAVOURS
};

enum
{
  ELEM_SRC = 0,
  ELEM_MIX,
  ELEM_PROC,
  ELEM_CONV,
  NUM_ELEM
};

static const gchar *factories[NUM_FLAVOURS][NUM_ELEM] = {
  {"audiotestsrc", "adder", "volume", "audioconvert"},
  {"videotestsrc", "videomixer", "videoscale", "videoconvert"}
};

static const gchar *sink_pads[NUM_FLAVOURS][NUM_ELEM] = {
  {NULL, "sink_%u", NULL, NULL},
  {NULL, "sink_%u", NULL, NULL}
};


static gboolean
create_node (GstBin * bin, GstElement * sink, const gchar * sinkpadname,
    GstElement ** new_sink, gint children, gint flavour)
{
  GstElement *mix, *proc, *conv;

  if (children >= 1) {
    mix = gst_element_factory_make (factories[flavour][ELEM_MIX], NULL);
    if (!mix) {
      GST_WARNING ("need element '%s'", factories[flavour][ELEM_MIX]);
      return FALSE;
    }
  } else {
    mix = gst_element_factory_make ("identity", NULL);
  }
  proc = gst_element_factory_make (factories[flavour][ELEM_PROC], NULL);
  if (!proc) {
    GST_WARNING ("need element '%s'", factories[flavour][ELEM_PROC]);
    return FALSE;
  }
  conv = gst_element_factory_make (factories[flavour][ELEM_CONV], NULL);
  if (!conv) {
    GST_WARNING ("need element '%s'", factories[flavour][ELEM_CONV]);
    return FALSE;
  }
  gst_bin_add_many (bin, mix, proc, conv, NULL);
  if (!gst_element_link_pads_full (mix, "src", proc, "sink",
          GST_PAD_LINK_CHECK_NOTHING)
      || !gst_element_link_pads_full (proc, "src", conv, "sink",
          GST_PAD_LINK_CHECK_NOTHING)
      || !gst_element_link_pads_full (conv, "src", sink, sinkpadname,
          GST_PAD_LINK_CHECK_NOTHING)) {
    GST_WARNING ("can't link elements");
    return FALSE;
  }
  *new_sink = mix;
  return TRUE;
}

static gboolean
create_nodes (GstBin * bin, GstElement * sink, gint depth, gint children,
    gint flavour)
{
  GstElement *new_sink, *src;
  gint i;

  for (i = 0; i < children; i++) {
    if (depth > 0) {
      if (!create_node (bin, sink, sink_pads[flavour][ELEM_MIX], &new_sink,
              children, flavour)) {
        return FALSE;
      }
      if (!create_nodes (bin, new_sink, depth - 1, children, flavour)) {
        return FALSE;
      }
    } else {
      src = gst_element_factory_make (factories[flavour][ELEM_SRC], NULL);
      if (!src) {
        GST_WARNING ("need element '%s'", factories[flavour][ELEM_SRC]);
        return FALSE;
      }
      gst_bin_add (bin, src);
      if (!gst_element_link_pads_full (src, "src", sink,
              sink_pads[flavour][ELEM_MIX], GST_PAD_LINK_CHECK_NOTHING)) {
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
    msg = gst_bus_poll (bus,
        GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_WARNING, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ASYNC_DONE:
        running = FALSE;
        break;
      case GST_MESSAGE_WARNING:{
        GError *err = NULL;
        gchar *dbg = NULL;

        gst_message_parse_warning (msg, &err, &dbg);
        GST_WARNING_OBJECT (GST_MESSAGE_SRC (msg), "%s (%s)", err->message,
            (dbg ? dbg : "no details"));
        g_clear_error (&err);
        g_free (dbg);
        break;
      }
      case GST_MESSAGE_ERROR:{
        GError *err = NULL;
        gchar *dbg = NULL;

        gst_message_parse_error (msg, &err, &dbg);
        GST_ERROR_OBJECT (GST_MESSAGE_SRC (msg), "%s (%s)", err->message,
            (dbg ? dbg : "no details"));
        g_clear_error (&err);
        g_free (dbg);
        running = FALSE;
        break;
      }
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);
}

gint
main (gint argc, gchar * argv[])
{
  /* default parameters */
  gchar *flavour_str = g_strdup ("audio");
  gint flavour = FLAVOUR_AUDIO;
  gint children = 3;
  gint depth = 4;
  gint loops = 50;

  GOptionContext *ctx;
  GOptionEntry options[] = {
    {"children", 'c', 0, G_OPTION_ARG_INT, &children,
        "Number of children (branches on each level) (default: 3)", NULL}
    ,
    {"depth", 'd', 0, G_OPTION_ARG_INT, &depth,
        "Depth of pipeline hierarchy tree (default: 4)", NULL}
    ,
    {"flavour", 'f', 0, G_OPTION_ARG_STRING, &flavour_str,
        "Flavour (video|audio) controlling the kind of elements used "
          "(default: audio)", NULL}
    ,
    {"loops", 'l', 0, G_OPTION_ARG_INT, &loops,
        "How many loops to run (default: 50)", NULL}
    ,
    {NULL}
  };
  GError *err = NULL;
  GstBin *bin;
  GstClockTime start, end;
  GstElement *sink, *new_sink;
  gint i;

  g_set_prgname ("capsnego");

  /* check command line options */
  ctx = g_option_context_new ("");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_clear_error (&err);
    g_option_context_free (ctx);
    return 1;
  }
  g_option_context_free (ctx);

  if (strcmp (flavour_str, "video") == 0)
    flavour = FLAVOUR_VIDEO;

  /* build pipeline */
  g_print ("building %s pipeline with depth = %d and children = %d\n",
      flavour_str, depth, children);
  g_free (flavour_str);

  start = gst_util_get_timestamp ();
  bin = GST_BIN (gst_pipeline_new ("pipeline"));
  sink = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add (bin, sink);
  if (!create_node (bin, sink, "sink", &new_sink, children, flavour)) {
    goto Error;
  }
  if (!create_nodes (bin, new_sink, depth, children, flavour)) {
    goto Error;
  }
  end = gst_util_get_timestamp ();
  /* num-threads = num-sources = pow (children, depth) */
  g_print ("%" GST_TIME_FORMAT " built pipeline with %d elements\n",
      GST_TIME_ARGS (end - start), GST_BIN_NUMCHILDREN (bin));

  /* measure */
  g_print ("starting pipeline\n");
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  GST_DEBUG_BIN_TO_DOT_FILE (bin, GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE, "capsnego");

  start = gst_util_get_timestamp ();
  for (i = 0; i < loops; ++i) {
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
    event_loop (GST_ELEMENT (bin));
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  }
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " reached PAUSED state (%d loop iterations)\n",
      GST_TIME_ARGS (end - start), loops);
  /* clean up */
Error:
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  gst_object_unref (bin);
  return 0;
}

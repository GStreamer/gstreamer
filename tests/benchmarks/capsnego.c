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

/* this benchmark recursively builds a pipeline and meassures the time to go
 * from ready to paused.
 * The graph size and type can be controlled with a few commandline args:
 *  -d depth: is the depth of the tree
 *  -c children: is the number of branches on each level
 *  -f <flavour>: can be a=udio/v=ideo and is conttrolling the kind of elements
 *                that are used.
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
  SINKPAD_MIX,
  ELEM_PROC,
  ELEM_CONV,
  NUM_ELEM
};

static const gchar *factories[NUM_FLAVOURS][NUM_ELEM] = {
  {"audiotestsrc", "adder", "sink%d", "volume", "audioconvert"},
  {"videotestsrc", "videomixer", "sink_%d", "videoscale", "ffmpegcolorspace"}
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
      if (!create_node (bin, sink, factories[flavour][SINKPAD_MIX], &new_sink,
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
              factories[flavour][SINKPAD_MIX], GST_PAD_LINK_CHECK_NOTHING)) {
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
  GstElement *sink, *new_sink;

  /* default parameters */
  gint depth = 4;
  gint children = 3;
  gint flavour = FLAVOUR_AUDIO;
  const gchar *flavour_str = "audio";

  gst_init (&argc, &argv);

  /* check command line options */
  if (argc) {
    gint arg;
    for (arg = 0; arg < argc; arg++) {
      if (!strcmp (argv[arg], "-d")) {
        arg++;
        if (arg < argc)
          depth = atoi (argv[arg]);
      } else if (!strcmp (argv[arg], "-c")) {
        arg++;
        if (arg < argc)
          children = atoi (argv[arg]);
      } else if (!strcmp (argv[arg], "-f")) {
        arg++;
        if (arg < argc) {
          flavour_str = argv[arg];
          switch (*flavour_str) {
            case 'a':
              flavour = FLAVOUR_AUDIO;
              break;
            case 'v':
              flavour = FLAVOUR_VIDEO;
              break;
            default:
              break;
          }
        }
      }
    }
  }

  /* build pipeline */
  g_print ("building %s pipeline with depth = %d and children = %d\n",
      flavour_str, depth, children);
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
  g_print ("%" GST_TIME_FORMAT " built pipeline with %d elements\n",
      GST_TIME_ARGS (end - start), GST_BIN_NUMCHILDREN (bin));

  /* meassure */
  g_print ("starting pipeline\n");
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_READY);
  GST_DEBUG_BIN_TO_DOT_FILE (bin, GST_DEBUG_GRAPH_SHOW_MEDIA_TYPE, "capsnego");
  start = gst_util_get_timestamp ();
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
  event_loop (GST_ELEMENT (bin));
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " reached paused\n",
      GST_TIME_ARGS (end - start));

  /* clean up */
Error:
  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
  gst_object_unref (bin);
  return 0;
}

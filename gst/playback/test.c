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

static GstElement *
gen_video_element ()
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;

  element = gst_thread_new ("vbin");
  conv = gst_element_factory_make ("ffmpegcolorspace", "conv");
  sink = gst_element_factory_make ("ximagesink", "sink");

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (conv, "src", sink, "sink");

  gst_element_add_ghost_pad (element, gst_element_get_pad (conv, "sink"),
      "sink");

  return element;
}

static GstElement *
gen_audio_element ()
{
  GstElement *element;
  GstElement *conv;
  GstElement *sink;

  element = gst_thread_new ("abin");
  conv = gst_element_factory_make ("audioconvert", "conv");
  sink = gst_element_factory_make ("osssink", "sink");

  gst_bin_add (GST_BIN (element), conv);
  gst_bin_add (GST_BIN (element), sink);
  gst_element_link_pads (conv, "src", sink, "sink");

  gst_element_add_ghost_pad (element,
      gst_element_get_pad (conv, "sink"), "sink");

  return element;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *player;
  GstElementStateReturn res;
  gint streams;
  GList *streaminfo;
  GList *s;

  gst_init (&argc, &argv);

  player = gst_element_factory_make ("playbin", "player");
  g_assert (player);

  g_object_set (G_OBJECT (player), "uri", argv[1], NULL);

  res = gst_element_set_state (player, GST_STATE_PAUSED);
  if (res != GST_STATE_SUCCESS) {
    g_print ("could not pause\n");
    return -1;
  }
  /* get info about the stream */
  g_print ("stream info:\n");
  g_object_get (G_OBJECT (player), "nstreams", &streams, NULL);
  g_print (" number of streams: %d\n", streams);
  g_object_get (G_OBJECT (player), "stream-info", &streaminfo, NULL);

  for (s = streaminfo; s; s = g_list_next (s)) {
    GObject *obj = G_OBJECT (s->data);
    int type;
    GstPad *srcpad, *sinkpad;
    GstElement *sink;
    gboolean res;

    g_object_get (obj, "type", &type, NULL);
    g_print (" type: %d\n", type);
    g_object_get (obj, "pad", &srcpad, NULL);
    g_print (" pad: %p\n", srcpad);

    if (type == 1) {
      sink = gen_audio_element ();
    } else if (type == 2) {
      sink = gen_video_element ();
    } else {
      g_warning ("unknown stream found");
      continue;
    }

    gst_bin_add (GST_BIN (player), sink);
    sinkpad = gst_element_get_pad (sink, "sink");
    res = gst_pad_link (srcpad, sinkpad);
    if (!res) {
      gchar *capsstr;

      capsstr = gst_caps_to_string (gst_pad_get_caps (srcpad));
      g_warning ("could not link %s", capsstr);
      g_free (capsstr);
    }
    //g_signal_emit_by_name (G_OBJECT (player), "link_stream", obj, sinkpad);
  }

  /* set effects sinks */
  res = gst_element_set_state (player, GST_STATE_PLAYING);
  if (res != GST_STATE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  gst_main ();

  return 0;
}

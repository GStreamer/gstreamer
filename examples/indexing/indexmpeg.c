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

static void
entry_added (GstCache *tc, GstCacheEntry *entry)
{
  switch (entry->type) {
    case GST_CACHE_ENTRY_ID:
      g_print ("id %d describes writer %s\n", entry->id, 
		      GST_CACHE_ID_DESCRIPTION (entry));
      break;
    case GST_CACHE_ENTRY_FORMAT:
      g_print ("%d: registered format %d for %s\n", entry->id, 
		      GST_CACHE_FORMAT_FORMAT (entry),
		      GST_CACHE_FORMAT_KEY (entry));
      break;
    case GST_CACHE_ENTRY_ASSOCIATION:
    {
      gint i;

      g_print ("%d: %08x ", entry->id, GST_CACHE_ASSOC_FLAGS (entry));
      for (i = 0; i < GST_CACHE_NASSOCS (entry); i++) {
	g_print ("%d %lld ", GST_CACHE_ASSOC_FORMAT (entry, i), 
			     GST_CACHE_ASSOC_VALUE (entry, i));
      }
      g_print ("\n");
      break;
    }
    default:
      break;
  }
}

static GstElement*
make_mpeg_pipeline (const gchar *path)
{
  GstElement *pipeline;
  GstElement *src, *demux;
  GstCache *cache;

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", path, NULL);

  demux = gst_element_factory_make ("mpegdemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);

  cache = gst_cache_new ();
  g_signal_connect (G_OBJECT (cache), "entry_added", G_CALLBACK (entry_added), NULL);
  gst_element_set_cache (demux, cache);

  gst_element_connect_pads (src, "src", demux, "sink");
  
  return pipeline;
}

gint 
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  
  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    return -1;
  }
    

  pipeline = make_mpeg_pipeline (argv[1]);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 1;
}


/* GStreamer
 * Copyright (C) 2004 Andy Wingo <wingo at pobox.com>
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
main (gint argc, gchar *argv[]) 
{
  GstElement *pipeline, *bin;
  GstElement *fakesrc, *fakesink, *identity;
  GstPad *sink, *src, *real = (GstPad*)0xdeadbeef;

  gst_init (&argc, &argv);

  pipeline = gst_element_factory_make ("pipeline", NULL);
  bin = gst_element_factory_make ("bin", NULL);
  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  identity = gst_element_factory_make ("identity", NULL);
  
  gst_bin_add_many (GST_BIN (pipeline), fakesrc, bin, fakesink, NULL);
  gst_bin_add (GST_BIN (bin), identity);
  
  sink = gst_element_add_ghost_pad (bin,
                                    gst_element_get_pad (identity, "sink"),
                                    "sink");
  src = gst_element_add_ghost_pad (bin,
                                   gst_element_get_pad (identity, "src"),
                                   "src");

  gst_element_link_many (fakesrc, bin, fakesink, NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  
  if (!gst_bin_iterate (GST_BIN (pipeline)))
    g_assert_not_reached ();
  
  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* test the cleanup */
  gst_object_ref (GST_OBJECT (sink));
  gst_object_unref ((GstObject*)pipeline);
  g_object_get (sink, "real-pad", &real, NULL);
  g_assert (real == NULL);
  g_assert (G_OBJECT (sink)->ref_count == 1);
  gst_object_unref (GST_OBJECT (sink));
  
  return 0;
}

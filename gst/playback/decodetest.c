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
  GstElement *decoder;
  GstElement *source;
  GstElement *pipeline;
  GstElementStateReturn res;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  source = gst_element_factory_make ("gnomevfssrc", "source");
  g_assert (source);
  g_object_set (G_OBJECT (source), "location", argv[1], NULL);

  decoder = gst_element_factory_make ("decodebin", "decoder");
  g_assert (decoder);

  gst_bin_add (GST_BIN (pipeline), source);
  gst_bin_add (GST_BIN (pipeline), decoder);

  gst_element_link_pads (source, "src", decoder, "sink");

  res = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (res != GST_STATE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  gst_main ();

  return 0;
}

/*
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include <gst/gst.h>

gint
main (gint argc, gchar ** argv)
{
  GstCaps *caps;
  GstElement *sink, *identity;
  GstElement *pipeline;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline);
  identity = gst_element_factory_make ("identity", NULL);
  g_assert (identity);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);
  gst_bin_add_many (GST_BIN (pipeline), identity, sink, NULL);
  gst_element_link_filtered (identity, sink,
      gst_caps_new_simple ("audio/x-raw-int", NULL));
  caps = gst_pad_get_caps (gst_element_get_pad (identity, "sink"));
  g_print ("caps:         %s\n", gst_caps_to_string (caps));
  g_assert (!gst_caps_is_any (caps));
  caps = gst_pad_get_allowed_caps (gst_element_get_pad (identity, "sink"));
  g_print ("allowed caps: %s\n", gst_caps_to_string (caps));
  g_assert (gst_caps_is_any (caps));

  return 0;
}

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

static GstElement*
make_bin (gint count)
{
  GstElement *bin;
  GstElement *src;

  bin = gst_bin_new (g_strdup_printf ("bin%d", count));
  src = gst_element_factory_make ("fakesrc", g_strdup_printf ("fakesrc%d", count));

  gst_bin_add (GST_BIN (bin), src);

  gst_element_add_ghost_pad (bin, gst_element_get_pad (src, "src"), "src");

  return bin;
}

static void
property_change_callback (GObject *object, GstObject *orig, GParamSpec *pspec, gpointer data)
{
  GValue value = { 0, }; /* the important thing is that value.type = 0 */
  gchar *str = 0;
	        
  if (pspec->flags & G_PARAM_READABLE) {
    /* let's not print these out for excluded properties... */
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);
    str = g_strdup_value_contents (&value);
    g_print ("%s: %s = %s\n", GST_OBJECT_NAME (orig), pspec->name, str);
    g_free (str);
    g_value_unset(&value);
  } else { 
    g_warning ("Parameter not readable. What's up with that?");
  } 
}

gint
main (gint argc, gchar *argv[]) 
{
  GstElement *pipeline;
  GstElement *aggregator, *sink;
  GstElement *bin1, *bin2;
  GstPad *pad1, *pad2;
  gint i;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("main");
  g_signal_connect (pipeline, "deep_notify", G_CALLBACK (property_change_callback), NULL);
  
  aggregator = gst_element_factory_make ("aggregator", "mixer");
  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add (GST_BIN (pipeline), aggregator);
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_connect_pads (aggregator, "src", sink, "sink");

  bin1 = make_bin (1);
  pad1 = gst_element_get_request_pad (aggregator, "sink%d");
  gst_pad_connect (gst_element_get_pad (bin1, "src"), pad1);
  gst_bin_add (GST_BIN (pipeline), bin1);

  bin2 = make_bin (2);
  pad2 = gst_element_get_request_pad (aggregator, "sink%d");
  gst_pad_connect (gst_element_get_pad (bin2, "src"), pad2);
  gst_bin_add (GST_BIN (pipeline), bin2);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  i = 2;
  while (i--)
    gst_bin_iterate (GST_BIN (pipeline));

  g_print ("pause bin1\n");
  gst_element_set_state (bin1, GST_STATE_PAUSED);
  gst_pad_set_active (pad1, FALSE);

  i = 4;
  while (i--)
    gst_bin_iterate (GST_BIN (pipeline));
		  
  g_print ("playing bin1\n");
  gst_pad_set_active (pad1, TRUE);
  gst_element_set_state (bin1, GST_STATE_PLAYING);

  i = 4;
  while (i--)
    gst_bin_iterate (GST_BIN (pipeline));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}

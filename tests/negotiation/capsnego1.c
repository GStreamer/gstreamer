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
caps_nego_failed (GstPad * pad, GstCaps * caps)
{
  gboolean res;
  GstPad *peer;
  GstCaps *allowed;
  GValue v_caps = { 0, };
  GValue v_allowed = { 0, };
  GstCaps *toset;

  peer = gst_pad_get_peer (pad);
  g_value_init (&v_caps, GST_TYPE_CAPS);
  g_value_set_boxed (&v_caps, caps);
  allowed = gst_pad_get_allowed_caps (pad);
  g_value_init (&v_allowed, GST_TYPE_CAPS);
  g_value_set_boxed (&v_allowed, allowed);

  g_print ("caps nego failed on pad %s:%s\n"
      " caps:    %p (%s)\n"
      " allowed: %p (%s)\n",
      gst_element_get_name (gst_pad_get_parent (pad)),
      gst_pad_get_name (pad),
      caps,
      g_strdup_value_contents (&v_caps),
      allowed, g_strdup_value_contents (&v_allowed));

  if (GST_CAPS_IS_FIXED (caps))
    /* if the elements suggested fixed caps, we just relink that way */
    toset = caps;
  else
    /* else we use our hardcoded caps as an exazmple */
    toset = GST_CAPS_NEW ("testcaps", "test/test", "prop", GST_PROPS_INT (2)
	);

  res = gst_pad_relink_filtered (pad, peer, toset);
  if (!res) {
    g_warning ("could not relink identity and sink\n");
  }
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *src, *identity, *sink;
  gboolean res;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("fakesrc", "src");
  g_object_set (G_OBJECT (src), "num_buffers", 4, NULL);
  identity = gst_element_factory_make ("identity", "identity");
  g_object_set (G_OBJECT (identity), "delay_capsnego", TRUE, NULL);

  sink = gst_element_factory_make ("fakesink", "sink");

  gst_bin_add_many (GST_BIN (pipeline), src, identity, sink, NULL);

  res = gst_element_link_pads_filtered (src, "src",
      identity, "sink",
      GST_CAPS_NEW ("testcaps", "test/test", "prop", GST_PROPS_INT (1)
      ));
  if (!res) {
    g_print ("could not link src and identity\n");
    return -1;
  }

  res = gst_element_link_pads_filtered (identity, "src",
      sink, "sink",
      GST_CAPS_NEW ("testcaps", "test/test", "prop", GST_PROPS_INT_RANGE (2, 3)
      ));
  if (!res) {
    g_print ("could not link identity and sink\n");
    return -1;
  }

  g_signal_connect (gst_element_get_pad (identity, "src"),
      "caps_nego_failed", G_CALLBACK (caps_nego_failed), NULL);

  g_signal_connect (pipeline, "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);
  g_signal_connect (pipeline, "error", G_CALLBACK (gst_element_default_error),
      NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}

/* GStreamer
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

static void
cb_handoff (GstElement * element, GstBuffer * buffer, GstPad * pad,
    gpointer unused)
{
  if (GST_PAD_PEER (pad)) {
    g_print ("unlinking...\n");
    gst_pad_unlink (pad, GST_PAD_PEER (pad));
  }
}

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *sink;

  gst_init (&argc, &argv);

  g_print ("setting up...\n");
  /* setup pipeline */
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);
  /* setup special stuff */
  g_object_set (ELEMENT, "signal-handoffs", TRUE, NULL);
  g_signal_connect (ELEMENT, "handoff", (GCallback) cb_handoff, NULL);

  /* run pipeline */
  g_print ("running...\n");
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();
  while (gst_bin_iterate (GST_BIN (pipeline)));

  g_print ("cleaning up...\n");
  gst_object_unref (GST_OBJECT (pipeline));
  pipeline = NULL;

  g_print ("done.\n");
  return 0;
}

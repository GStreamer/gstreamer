/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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

/*
 * This file reproduces the bug in the bugreport #143777, as can be seen at
 * http://bugzilla.gnome.org/show_bug.cgi?id=143777 - the issue is that when
 * pausing a pipeline while the chainhandler is still running, then unlinking
 * the pad that's chain function is called and relinking it clears the buffer
 * that was stored for sending the event. gst_pad_call_chain_function needs
 * to check that.
 * The fix is in gstpad.c, revision 1.327
 */

#include <gst/gst.h>

gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *sink, *id;
  guint i = 0, j;

  gst_init (&argc, &argv);

  g_print ("setting up...\n");
  /* setup pipeline */
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  id = gst_element_factory_make ("identity", NULL);
  g_assert (id);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);

  gst_bin_add_many (GST_BIN (pipeline), src, id, sink, NULL);
  while (i < 100) {
    g_print ("running... (%d iterations)\n", i);
    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) != GST_STATE_SUCCESS)
      g_assert_not_reached ();
    gst_element_link_many (src, id, sink, NULL);
    for (j = 0; j < i; j++)
      gst_bin_iterate (GST_BIN (pipeline));
    if (gst_element_set_state (pipeline, GST_STATE_PAUSED) != GST_STATE_SUCCESS)
      g_assert_not_reached ();
    gst_element_unlink_many (src, id, sink, NULL);
    i++;
  }

  g_print ("cleaning up...\n");
  g_assert (i == 100);
  gst_object_unref (GST_OBJECT (pipeline));
  src = id = sink = pipeline = NULL;

  g_print ("done.\n");
  return 0;
}

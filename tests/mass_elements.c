/*
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

#include <gst/gst.h>

#define IDENTITY_COUNT (1000)
#define BUFFER_COUNT (1000)


static GstClockTime
gst_get_current_time (void)
{
  GTimeVal tv;

  g_get_current_time (&tv);
  return GST_TIMEVAL_TO_TIME (tv);
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline, *src, *sink, *current, *last;
  guint i, buffers = BUFFER_COUNT, identities = IDENTITY_COUNT;
  GstClockTime start, end;

  gst_init (&argc, &argv);

  if (argc > 1)
    identities = atoi (argv[1]);
  if (argc > 2)
    buffers = atoi (argv[2]);

  g_print
      ("*** benchmarking this pipeline: fakesrc num-buffers=%u ! %u * identity ! fakesink\n",
      buffers, identities);
  start = gst_get_current_time ();
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  g_object_set (src, "num-buffers", buffers, NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);
  last = src;
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  for (i = 0; i < identities; i++) {
    current = gst_element_factory_make ("identity", NULL);
    g_assert (current);
    gst_bin_add (GST_BIN (pipeline), current);
    if (!gst_element_link (last, current))
      g_assert_not_reached ();
    last = current;
  }
  if (!gst_element_link (last, sink))
    g_assert_not_reached ();
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - creating %u identity elements\n",
      GST_TIME_ARGS (end - start), identities);

  start = gst_get_current_time ();
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - setting pipeline to playing\n",
      GST_TIME_ARGS (end - start));

  start = gst_get_current_time ();
  while (gst_bin_iterate (GST_BIN (pipeline)));
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - putting %u buffers through\n",
      GST_TIME_ARGS (end - start), buffers);

  start = gst_get_current_time ();
  g_object_unref (pipeline);
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - unreffing pipeline\n",
      GST_TIME_ARGS (end - start));

  return 0;
}

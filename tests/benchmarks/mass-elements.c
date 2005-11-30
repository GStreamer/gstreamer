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

#include <stdlib.h>
#include <gst/gst.h>

#define IDENTITY_COUNT (1000)
#define BUFFER_COUNT (1000)
#define SRC_ELEMENT "fakesrc"
#define SINK_ELEMENT "fakesink"


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
  gchar *src_name = SRC_ELEMENT, *sink_name = SINK_ELEMENT;

  gst_init (&argc, &argv);

  if (argc > 1)
    identities = atoi (argv[1]);
  if (argc > 2)
    buffers = atoi (argv[2]);
  if (argc > 3)
    src_name = argv[3];
  if (argc > 4)
    sink_name = argv[4];

  g_print
      ("*** benchmarking this pipeline: %s num-buffers=%u ! %u * identity ! %s\n",
      src_name, buffers, identities, sink_name);
  start = gst_get_current_time ();
  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make (src_name, NULL);
  if (!src) {
    g_print ("no element named \"%s\" found, aborting...\n", src_name);
    return 1;
  }
  g_object_set (src, "num-buffers", buffers, NULL);
  sink = gst_element_factory_make (sink_name, NULL);
  if (!sink) {
    g_print ("no element named \"%s\" found, aborting...\n", sink_name);
    return 1;
  }
  last = src;
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  for (i = 0; i < identities; i++) {
    current = gst_element_factory_make ("identity", NULL);
    g_assert (current);
    /* shut this element up (no g_strdup_printf please) */
    g_object_set (current, "silent", TRUE, NULL);
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
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - setting pipeline to playing\n",
      GST_TIME_ARGS (end - start));

  start = gst_get_current_time ();
  gst_bus_poll (gst_element_get_bus (pipeline),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
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

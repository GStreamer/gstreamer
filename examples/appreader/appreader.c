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
fill_queue (GstElement * queue, gint level, GstBin * pipeline)
{
  /* this needs to iterate till something is pushed 
   * in the queue */
  gst_bin_iterate (pipeline);
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *queue, *src, *pipeline;
  GstBuffer *buffer;
  gboolean done = FALSE;
  GstPad *pad;

  gst_init (&argc, &argv);

  queue = gst_element_factory_make ("queue", "queue");
  g_object_set (G_OBJECT (queue), "signal_marks", TRUE, NULL);

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", "appreader.c", NULL);

  pipeline = gst_pipeline_new ("pipeline");

  gst_bin_add_many (GST_BIN (pipeline), src, queue, NULL);

  gst_element_link_many (src, queue, NULL);

  pad = gst_element_get_pad (queue, "src");
  g_signal_connect (G_OBJECT (queue), "low_watermark", G_CALLBACK (fill_queue),
      pipeline);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  do {
    /* get buffer into the app */
    buffer = GST_RPAD_GETFUNC (pad) (pad);

    /* just exit on any event */
    if (GST_IS_EVENT (buffer)) {
      done = TRUE;
    } else {
      gst_util_dump_mem (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
    }
    gst_data_unref (GST_DATA (buffer));

  } while (!done);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}

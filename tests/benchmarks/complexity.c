/*
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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
  GstElement *pipeline, *src, *e;
  GSList *saved_src_list, *src_list, *new_src_list;
  guint complexity_order, n_elements, i, j, max_this_level;
  GstClockTime start, end;
  gboolean all_srcs_linked;

  gst_init (&argc, &argv);

  if (argc != 3) {
    g_print ("usage: %s COMPLEXITY_ORDER N_ELEMENTS\n", argv[0]);
    return 1;
  }

  complexity_order = atoi (argv[1]);
  n_elements = atoi (argv[2]);

  start = gst_get_current_time ();

  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);

  e = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (e, "num-buffers", BUFFER_COUNT, NULL);
  g_object_set (e, "silent", TRUE, NULL);
  gst_bin_add (GST_BIN (pipeline), e);
  src_list = saved_src_list = g_slist_append (NULL, e);

  new_src_list = NULL;

  max_this_level = 1;
  j = 0;
  i = 0;
  all_srcs_linked = FALSE;
  for (i = 0, j = 0; i < n_elements; i++, j++) {
    if (j >= max_this_level) {
      g_slist_free (saved_src_list);
      saved_src_list = g_slist_reverse (new_src_list);
      new_src_list = NULL;
      j = 0;
      all_srcs_linked = FALSE;
      max_this_level *= complexity_order;
    }

    if (!src_list) {
      if (j)
        all_srcs_linked = TRUE;
      src_list = saved_src_list;
    }

    src = (GstElement *) src_list->data;
    src_list = src_list->next;

    if (i + max_this_level < n_elements) {
      e = gst_element_factory_make ("tee", NULL);
    } else {
      e = gst_element_factory_make ("fakesink", NULL);
      g_object_set (e, "preroll-queue-len", 1, NULL);
    }
    g_object_set (e, "silent", TRUE, NULL);
    new_src_list = g_slist_prepend (new_src_list, e);

    gst_bin_add (GST_BIN (pipeline), e);
    if (!gst_element_link (src, e))
      g_assert_not_reached ();
  }

  g_slist_free (saved_src_list);
  g_slist_free (new_src_list);

  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - creating and linking %d elements\n",
      GST_TIME_ARGS (end - start), i);

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
      GST_TIME_ARGS (end - start), BUFFER_COUNT);

  start = gst_get_current_time ();
  g_object_unref (pipeline);
  end = gst_get_current_time ();
  g_print ("%" GST_TIME_FORMAT " - unreffing pipeline\n",
      GST_TIME_ARGS (end - start));

  return 0;
}

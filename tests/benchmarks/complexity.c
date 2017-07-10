/*
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <gst/gst.h>

#define BUFFER_COUNT (1000)

gint
main (gint argc, gchar * argv[])
{
  GstMessage *msg;
  GstElement *pipeline, *src, *e;
  GSList *saved_src_list, *src_list, *new_src_list;
  guint complexity_order, n_elements, i, j, max_this_level;
  GstClockTime start, end;

  gst_init (&argc, &argv);

  if (argc != 3) {
    g_print ("Usage: %s COMPLEXITY_ORDER N_ELEMENTS\n", argv[0]);
    return 1;
  }

  complexity_order = atoi (argv[1]);
  n_elements = atoi (argv[2]);

  start = gst_util_get_timestamp ();

  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);

  e = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (e, "num-buffers", BUFFER_COUNT, NULL);
  g_object_set (e, "silent", TRUE, NULL);
  gst_bin_add (GST_BIN (pipeline), e);
  src_list = saved_src_list = g_slist_append (NULL, e);

  new_src_list = NULL;

  max_this_level = 1;
  for (i = 0, j = 0; i < n_elements; i++, j++) {
    if (j >= max_this_level) {
      g_slist_free (saved_src_list);
      saved_src_list = g_slist_reverse (new_src_list);
      new_src_list = NULL;
      j = 0;
      max_this_level *= complexity_order;
    }

    if (!src_list) {
      src_list = saved_src_list;
    }

    src = (GstElement *) src_list->data;
    src_list = src_list->next;

    if (i + max_this_level < n_elements) {
      e = gst_element_factory_make ("tee", NULL);
    } else {
      e = gst_element_factory_make ("fakesink", NULL);
      g_object_set (e, "async", FALSE, NULL);
    }
    g_object_set (e, "silent", TRUE, NULL);
    new_src_list = g_slist_prepend (new_src_list, e);

    gst_bin_add (GST_BIN (pipeline), e);
    if (!gst_element_link (src, e))
      g_assert_not_reached ();
  }

  g_slist_free (saved_src_list);
  g_slist_free (new_src_list);

  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - creating and linking %u elements\n",
      GST_TIME_ARGS (end - start), i);

  start = gst_util_get_timestamp ();
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    g_assert_not_reached ();
  if (gst_element_get_state (pipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE) == GST_STATE_CHANGE_FAILURE)
    g_assert_not_reached ();
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - setting pipeline to playing\n",
      GST_TIME_ARGS (end - start));

  start = gst_util_get_timestamp ();
  msg = gst_bus_poll (gst_element_get_bus (pipeline),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  end = gst_util_get_timestamp ();
  gst_message_unref (msg);
  g_print ("%" GST_TIME_FORMAT " - putting %d buffers through\n",
      GST_TIME_ARGS (end - start), BUFFER_COUNT);

  start = gst_util_get_timestamp ();
  if (gst_element_set_state (pipeline,
          GST_STATE_NULL) != GST_STATE_CHANGE_SUCCESS)
    g_assert_not_reached ();
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - setting pipeline to NULL\n",
      GST_TIME_ARGS (end - start));

  start = gst_util_get_timestamp ();
  g_object_unref (pipeline);
  end = gst_util_get_timestamp ();
  g_print ("%" GST_TIME_FORMAT " - unreffing pipeline\n",
      GST_TIME_ARGS (end - start));

  return 0;
}

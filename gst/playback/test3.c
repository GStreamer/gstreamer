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

#define UPDATE_INTERVAL 500

static gboolean
update_scale (GstElement * element)
{
  gint64 duration;
  gint64 position;
  GstFormat format = GST_FORMAT_TIME;

  gst_element_query_position (element, &format, &position, &duration);

  g_print ("%" G_GINT64_FORMAT " %" G_GINT64_FORMAT "\n", duration, position);

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *player;
  GstStateChangeReturn res;

  gst_init (&argc, &argv);

  player = gst_element_factory_make ("playbin", "player");
  g_assert (player);

  g_object_set (G_OBJECT (player), "uri", argv[1], NULL);

  res = gst_element_set_state (player, GST_STATE_PLAYING);
  if (res != GST_STATE_CHANGE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  g_timeout_add (UPDATE_INTERVAL, (GSourceFunc) update_scale, player);

  g_main_loop_run (g_main_loop_new (NULL, TRUE));

  return 0;
}

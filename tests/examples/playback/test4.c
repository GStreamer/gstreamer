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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>             /* exit() */
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gst/gst.h>

gint
main (gint argc, gchar * argv[])
{
  GstElement *player;
  GstStateChangeReturn res;

  gst_init (&argc, &argv);

  player = gst_element_factory_make ("playbin", "player");
  g_assert (player);

  if (argc < 2) {
    g_print ("usage: %s <uri>\n", argv[0]);
    exit (-1);
  }

  g_object_set (G_OBJECT (player), "uri", argv[1], NULL);

  g_print ("play...\n");
  res = gst_element_set_state (player, GST_STATE_PLAYING);
  if (res != GST_STATE_CHANGE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  g_print ("sleep 2...\n");
  g_usleep (2 * G_USEC_PER_SEC);

  g_print ("pause...\n");
  res = gst_element_set_state (player, GST_STATE_PAUSED);
  if (res != GST_STATE_CHANGE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  g_print ("sleep 2...\n");
  g_usleep (2 * G_USEC_PER_SEC);

  g_print ("play...\n");
  res = gst_element_set_state (player, GST_STATE_PLAYING);
  if (res != GST_STATE_CHANGE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  g_print ("sleep 2...\n");
  g_usleep (2 * G_USEC_PER_SEC);

  g_print ("ready...\n");
  res = gst_element_set_state (player, GST_STATE_READY);
  if (res != GST_STATE_CHANGE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  g_print ("sleep 2...\n");
  g_usleep (2 * G_USEC_PER_SEC);

  g_print ("play...\n");
  res = gst_element_set_state (player, GST_STATE_PLAYING);
  if (res != GST_STATE_CHANGE_SUCCESS) {
    g_print ("could not play\n");
    return -1;
  }

  g_main_loop_run (g_main_loop_new (NULL, TRUE));

  return 0;
}

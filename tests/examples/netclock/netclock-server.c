/* GStreamer
 * Copyright (C) 2014 Jan Schmidt <jan@centricular.com>
 *
 * netclock-server.c: Publish a network clock provider
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <gst/gst.h>
#include <gst/net/gstnettimeprovider.h>

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *loop;
  GstClock *clock;
  GstNetTimeProvider *net_clock;
  int clock_port = 0;

  gst_init (&argc, &argv);

  if (argc > 1) {
    clock_port = atoi (argv[1]);
  }

  loop = g_main_loop_new (NULL, FALSE);

  clock = gst_system_clock_obtain ();
  net_clock = gst_net_time_provider_new (clock, NULL, clock_port);
  gst_object_unref (clock);

  g_object_get (net_clock, "port", &clock_port, NULL);

  g_print ("Published network clock on port %u\n", clock_port);

  g_main_loop_run (loop);

  /* cleanup */
  g_main_loop_unref (loop);

  return 0;
}

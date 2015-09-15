/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim taymans at gmail dot com>
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

#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <gst/glib-compat-private.h>

#define MAX_THREADS  100

static gboolean running = TRUE;
static gint count = 0;

static void *
run_test (void *user_data)
{
  gint prev;
  GstClock *sysclock = GST_CLOCK_CAST (user_data);

  while (running) {
    gst_clock_get_time (sysclock);
    prev = g_atomic_int_add (&count, 1);
    if (prev == G_MAXINT)
      g_warning ("overflow");
  }
  g_thread_exit (NULL);
  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GThread *threads[MAX_THREADS];
  gint num_threads;
  gint t;
  GstClock *sysclock;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <num_threads>\n", argv[0]);
    exit (-1);
  }

  num_threads = atoi (argv[1]);

  if (num_threads <= 0 || num_threads > MAX_THREADS) {
    g_print ("number of threads must be between 0 and %d\n", MAX_THREADS);
    exit (-2);
  }

  sysclock = gst_system_clock_obtain ();

  for (t = 0; t < num_threads; t++) {
    GError *error = NULL;

    threads[t] = g_thread_try_new ("clockstresstest", run_test,
        sysclock, &error);

    if (error) {
      printf ("ERROR: g_thread_try_new() %s\n", error->message);
      g_clear_error (&error);
      exit (-1);
    }
  }
  printf ("main(): Created %d threads.\n", t);

  /* run for 5 seconds */
  g_usleep (G_USEC_PER_SEC * 5);

  printf ("main(): Stopping threads...\n");

  running = FALSE;

  for (t = 0; t < num_threads; t++) {
    g_thread_join (threads[t]);
  }

  g_print ("performed %d get_time operations\n", count);

  gst_object_unref (sysclock);

  return 0;
}

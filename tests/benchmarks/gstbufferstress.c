/* GStreamer
 * Copyright (C) <2009> Edward Hervey <bilboed@bilboed.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#define MAX_THREADS  1000

static guint64 nbbuffers;
static GMutex *mutex;


static void *
run_test (void *user_data)
{
  gint threadid = GPOINTER_TO_INT (user_data);
  guint64 nb;
  GstBuffer *buf;
  GstClockTime start, end;

  g_mutex_lock (mutex);
  g_mutex_unlock (mutex);

  start = gst_util_get_timestamp ();

  g_assert (nbbuffers > 0);

  for (nb = nbbuffers; nb; nb--) {
    buf = gst_buffer_new ();
    gst_buffer_unref (buf);
  }

  end = gst_util_get_timestamp ();
  g_print ("total %" GST_TIME_FORMAT " - average %" GST_TIME_FORMAT
      "  - Thread %d\n", GST_TIME_ARGS (end - start),
      GST_TIME_ARGS ((end - start) / nbbuffers), threadid);


  g_thread_exit (NULL);
  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GThread *threads[MAX_THREADS];
  gint num_threads;
  gint t;
  GstBuffer *tmp;
  GstClockTime start, end;

  gst_init (&argc, &argv);
  mutex = g_mutex_new ();

  if (argc != 3) {
    g_print ("usage: %s <num_threads> <nbbuffers>\n", argv[0]);
    exit (-1);
  }

  num_threads = atoi (argv[1]);
  nbbuffers = atoi (argv[2]);

  if (num_threads <= 0 || num_threads > MAX_THREADS) {
    g_print ("number of threads must be between 0 and %d\n", MAX_THREADS);
    exit (-2);
  }

  if (nbbuffers <= 0) {
    g_print ("number of buffers must be greater than 0\n");
    exit (-3);
  }

  g_mutex_lock (mutex);
  /* Let's just make sure the GstBufferClass is loaded ... */
  tmp = gst_buffer_new ();

  printf ("main(): Creating %d threads.\n", num_threads);
  for (t = 0; t < num_threads; t++) {
    GError *error = NULL;

    threads[t] = g_thread_create (run_test, GINT_TO_POINTER (t), TRUE, &error);
    if (error) {
      printf ("ERROR: g_thread_create() %s\n", error->message);
      exit (-1);
    }
  }

  /* Signal all threads to start */
  start = gst_util_get_timestamp ();
  g_mutex_unlock (mutex);

  for (t = 0; t < num_threads; t++) {
    if (threads[t])
      g_thread_join (threads[t]);
  }

  end = gst_util_get_timestamp ();
  g_print ("*** total %" GST_TIME_FORMAT " - average %" GST_TIME_FORMAT
      "  - Done creating %" G_GUINT64_FORMAT " buffers\n",
      GST_TIME_ARGS (end - start),
      GST_TIME_ARGS ((end - start) / (num_threads * nbbuffers)),
      num_threads * nbbuffers);


  gst_buffer_unref (tmp);

  return 0;
}

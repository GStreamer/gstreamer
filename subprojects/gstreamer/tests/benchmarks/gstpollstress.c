/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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
#include "gst/glib-compat-private.h"

static GstPoll *set;
static GList *fds = NULL;
static GMutex fdlock;
static GTimer *timer;

#define MAX_THREADS  100

static void
mess_some_more (void)
{
  GList *walk;
  gint random;
  gint removed = 0;

  g_mutex_lock (&fdlock);

  for (walk = fds; walk;) {
    GstPollFD *fd = (GstPollFD *) walk->data;

    walk = g_list_next (walk);

    random = (gint) (10.0 * rand () / (RAND_MAX + 1.0));
    switch (random) {
      case 0:
      {
        /*
           GstPollFD *newfd = g_new0 (GstPollFD, 1);

           gst_poll_add_fd (set, newfd);
           fds = g_list_prepend (fds, newfd);
         */
        break;
      }
      case 1:
        if ((gint) (10.0 * rand () / (RAND_MAX + 1.0)) < 2) {
          gst_poll_remove_fd (set, fd);
          fds = g_list_remove (fds, fd);
          g_free (fd);
          removed++;
        }
        break;

      case 2:
        gst_poll_fd_ctl_write (set, fd, TRUE);
        break;
      case 3:
        gst_poll_fd_ctl_write (set, fd, FALSE);
        break;
      case 4:
        gst_poll_fd_ctl_read (set, fd, TRUE);
        break;
      case 5:
        gst_poll_fd_ctl_read (set, fd, FALSE);
        break;

      case 6:
        gst_poll_fd_has_closed (set, fd);
        break;
      case 7:
        gst_poll_fd_has_error (set, fd);
        break;
      case 8:
        gst_poll_fd_can_read (set, fd);
        break;
      case 9:
        gst_poll_fd_can_write (set, fd);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  if (g_list_length (fds) < 900) {
    random = removed + (gint) (2.0 * rand () / (RAND_MAX + 1.0));
    while (random) {
      GstPollFD *newfd = g_new0 (GstPollFD, 1);

      gst_poll_add_fd (set, newfd);
      fds = g_list_prepend (fds, newfd);
      random--;
    }
  }

  g_mutex_unlock (&fdlock);
}

static void *
run_test (void *threadid)
{
  gint id = GPOINTER_TO_INT (threadid);

  while (TRUE) {
    if (id == 0) {
      gint res = gst_poll_wait (set, 10);

      if (res < 0) {
        g_print ("error %d %s\n", errno, g_strerror (errno));
      }
    } else {
      mess_some_more ();
      if (g_timer_elapsed (timer, NULL) > 0.5) {
        g_mutex_lock (&fdlock);
        g_print ("active fds :%u\n", g_list_length (fds));
        g_timer_start (timer);
        g_mutex_unlock (&fdlock);
      }
      g_usleep (1);
    }
  }

  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  GThread *threads[MAX_THREADS];
  gint num_threads;
  gint t;

  gst_init (&argc, &argv);

  g_mutex_init (&fdlock);
  timer = g_timer_new ();

  if (argc != 2) {
    g_print ("usage: %s <num_threads>\n", argv[0]);
    exit (-1);
  }

  num_threads = atoi (argv[1]);

  set = gst_poll_new (TRUE);

  for (t = 0; t < num_threads; t++) {
    GError *error = NULL;

    threads[t] = g_thread_try_new ("pollstresstest", run_test,
        GINT_TO_POINTER (t), &error);

    if (error) {
      printf ("ERROR: g_thread_try_new() %s\n", error->message);
      g_clear_error (&error);
      exit (-1);
    }
  }
  printf ("main(): Created %d threads.\n", t);

  for (t = 0; t < num_threads; t++) {
    g_thread_join (threads[t]);
  }

  gst_poll_free (set);

  return 0;
}

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

#include <glib.h>

typedef struct
{
  GMutex *mutex;
  GCond *cond_t;
  GCond *cond_p;
  gint var;
} ThreadInfo;

static void *
thread_loop (void *arg)
{
  ThreadInfo *info = (ThreadInfo *) arg;

  g_print ("thread: entering %p\n", info);

  g_print ("thread: lock\n");
  g_mutex_lock (info->mutex);
  g_print ("thread: signal spinup\n");
  g_cond_signal (info->cond_t);

  g_print ("thread: wait ACK\n");
  g_cond_wait (info->cond_p, info->mutex);
  info->var = 1;
  g_print ("thread: signal var change\n");
  g_cond_signal (info->cond_t);
  g_print ("thread: unlock\n");
  g_mutex_unlock (info->mutex);

  g_print ("thread: exit\n");
  return NULL;
}

gint
main (gint argc, gchar * argv[])
{
  ThreadInfo *info;
  GThread *thread;
  GError *error = NULL;
  gint res = 0;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  info = g_new (ThreadInfo, 1);
  info->mutex = g_mutex_new ();
  info->cond_t = g_cond_new ();
  info->cond_p = g_cond_new ();
  info->var = 0;

  g_print ("main: lock\n");
  g_mutex_lock (info->mutex);

  thread = g_thread_create (thread_loop, info, TRUE, &error);

  if (error != NULL) {
    g_print ("Unable to start thread: %s\n", error->message);
    g_error_free (error);
    res = -1;
    goto done;
  }

  g_print ("main: wait spinup\n");
  g_cond_wait (info->cond_t, info->mutex);

  g_print ("main: signal ACK\n");
  g_cond_signal (info->cond_p);

  g_print ("main: waiting for thread to change var\n");
  g_cond_wait (info->cond_t, info->mutex);

  g_print ("main: var == %d\n", info->var);
  if (info->var != 1) {
    g_print ("main: !!error!! expected var == 1, got %d\n", info->var);
  }
  g_mutex_unlock (info->mutex);

  g_print ("main: join\n");
  g_thread_join (thread);

done:
  g_mutex_free (info->mutex);
  g_cond_free (info->cond_t);
  g_cond_free (info->cond_p);
  g_free (info);

  return res;
}

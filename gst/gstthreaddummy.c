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

#include <unistd.h>
#include <sys/time.h>
#include <glib.h>
#include "gstlog.h"

static GMutex *mutex = NULL;
static GCond *cond = NULL;

static GMutex *
gst_mutex_new_dummy_impl (void)
{
  if (!mutex)
    mutex = g_malloc (8);

  return mutex;
}

static void
gst_mutex_dummy_impl (GMutex * mutex)
{                               /* NOP */
}

static gboolean
gst_mutex_trylock_dummy_impl (GMutex * mutex)
{
  return TRUE;
}

static GCond *
gst_cond_new_dummy_impl (void)
{
  if (!cond)
    cond = g_malloc (8);

  return cond;
}

static void
gst_cond_dummy_impl (GCond * cond)
{                               /* NOP */
}

static gboolean
gst_cond_timed_wait_dummy_impl (GCond * cond, GMutex * mutex,
    GTimeVal * end_time)
{
  struct timeval tvtarget;
  GTimeVal tvnow;
  guint64 now, target;
  gint64 diff;

  target = end_time->tv_sec * 1000000 + end_time->tv_usec;

  g_get_current_time (&tvnow);
  now = tvnow.tv_sec * 1000000 + tvnow.tv_usec;

  diff = target - now;
  if (diff > 1000) {
    tvtarget.tv_usec = diff % 1000000;
    tvtarget.tv_sec = diff / 1000000;

    select (0, NULL, NULL, NULL, &tvtarget);
  }

  return TRUE;
}

static GPrivate *
gst_private_new_dummy_impl (GDestroyNotify destructor)
{
  gpointer data;

  data = g_new0 (gpointer, 1);

  return (GPrivate *) data;
}

static gpointer
gst_private_get_dummy_impl (GPrivate * private_key)
{
  gpointer *data = (gpointer) private_key;

  return *data;
}

static void
gst_private_set_dummy_impl (GPrivate * private_key, gpointer data)
{
  *((gpointer *) private_key) = data;
}

static void
gst_thread_create_dummy_impl (GThreadFunc func, gpointer data,
    gulong stack_size, gboolean joinable, gboolean bound,
    GThreadPriority priority, gpointer thread, GError ** error)
{
  g_warning ("GStreamer configured to not use threads");
}

static void
gst_thread_dummy_impl (void)
{                               /* NOP */
}

static void
gst_thread_dummy_impl_1 (gpointer thread)
{                               /* NOP */
}

static void
gst_thread_set_priority_dummy_impl (gpointer thread, GThreadPriority priority)
{                               /* NOP */
}

static gboolean
gst_thread_equal_dummy_impl (gpointer thread1, gpointer thread2)
{
  return (thread1 == thread2);
}

GThreadFunctions gst_thread_dummy_functions = {
  gst_mutex_new_dummy_impl,
  (void (*)(GMutex *)) gst_mutex_dummy_impl,
  gst_mutex_trylock_dummy_impl,
  (void (*)(GMutex *)) gst_mutex_dummy_impl,
  gst_mutex_dummy_impl,
  gst_cond_new_dummy_impl,
  (void (*)(GCond *)) gst_cond_dummy_impl,
  (void (*)(GCond *)) gst_cond_dummy_impl,
  (void (*)(GCond *, GMutex *)) gst_cond_dummy_impl,
  gst_cond_timed_wait_dummy_impl,
  gst_cond_dummy_impl,
  gst_private_new_dummy_impl,
  gst_private_get_dummy_impl,
  gst_private_set_dummy_impl,
  gst_thread_create_dummy_impl,
  gst_thread_dummy_impl,
  gst_thread_dummy_impl_1,
  gst_thread_dummy_impl,
  gst_thread_set_priority_dummy_impl,
  gst_thread_dummy_impl_1, gst_thread_equal_dummy_impl
};

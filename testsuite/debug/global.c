/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * global.c: Test global parameter setting/getting
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#define THREAD_COUNT	5
#define ITERATIONS	20

/* stupid logging functions */
static void
gst_debug_log_one (GstDebugCategory * category,
    GstDebugLevel level,
    const gchar * file,
    const gchar * function,
    gint line, GObject * object, gchar * message, gpointer thread)
    G_GNUC_NO_INSTRUMENT;
     static void gst_debug_log_two (GstDebugCategory * category,
    GstDebugLevel level,
    const gchar * file,
    const gchar * function,
    gint line,
    GObject * object, gchar * message, gpointer thread) G_GNUC_NO_INSTRUMENT;

     static void
	 gst_debug_log_one (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line, GObject * object,
    gchar * message, gpointer thread)
{
}
static void
gst_debug_log_two (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line, GObject * object,
    gchar * message, gpointer thread)
{
}

static gpointer
thread_main (gpointer threadnum)
{
  gint num;
  gint i;

  num = GPOINTER_TO_INT (threadnum);
  for (i = 0; i < ITERATIONS; i++) {
    g_print ("iteration %d of thread %d starting\n", i, num);
    /* do some stuff with global settings */
    gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
    gst_debug_add_log_function (gst_debug_log_one, g_thread_self ());
    gst_debug_add_log_function (gst_debug_log_two, NULL);

    /* reset all the stuff we did */
    gst_debug_set_default_threshold (GST_LEVEL_DEFAULT);
    g_assert (gst_debug_remove_log_function_by_data (g_thread_self ()) == 1);
  }

  g_print ("Thread %d is done.\n", num);
  return threadnum;
}

gint
main (gint argc, gchar * argv[])
{
  gint i;
  GThread *threads[THREAD_COUNT];

  g_print ("initializing GStreamer\n");
  gst_init (&argc, &argv);
  g_assert (gst_debug_remove_log_function (gst_debug_log_default) == 1);

  /* some checks for defaults */
  g_print ("Doing startup checks\n");
  g_assert (gst_debug_get_default_threshold () == GST_LEVEL_DEFAULT);

  g_print ("creating %d threads\n", THREAD_COUNT);
  for (i = 0; i < THREAD_COUNT; i++) {
    g_assert ((threads[i] =
	    g_thread_create (thread_main, GINT_TO_POINTER (i), TRUE, NULL)));
  }
  g_print ("joining %d threads\n", THREAD_COUNT);
  for (i = 0; i < THREAD_COUNT; i++) {
    g_assert (GPOINTER_TO_INT (g_thread_join (threads[i])) == i);
  }

  /* some checks if everything worked */
  g_print ("Doing shutdown checks\n");
  g_assert (gst_debug_get_default_threshold () == GST_LEVEL_DEFAULT);
  g_assert (gst_debug_remove_log_function (gst_debug_log_two) ==
      THREAD_COUNT * ITERATIONS);

  return 0;
}

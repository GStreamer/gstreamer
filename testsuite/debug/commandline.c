/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * commandline.c: Test if the command line arguments work
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

GST_DEBUG_CATEGORY (cat);
GST_DEBUG_CATEGORY_STATIC (cat_static);

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *lines[] = {
  "--gst-debug-disable",
  "--gst-debug-no-color",
  "--gst-debug-level=4",
  "--gst-debug=cat:4,cat_*:3",
  "--gst-debug-level=4 --gst-debug=cat_*:5"
};

static void
debug_not_reached (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line, GObject * object,
    GstDebugMessage * message, gpointer thread)
{
  g_assert_not_reached ();
}
#endif

gint
main (gint argc, gchar * argv[])
{
  if (argc == 1) {
    /* this is the main run that calls the others */

    unsetenv ("GST_DEBUG");
    gst_init (&argc, &argv);
#ifndef GST_DISABLE_GST_DEBUG
    {
      gint i, runs, exit;
      gchar *command;

      runs = G_N_ELEMENTS (lines);
      for (i = 0; i < runs; i++) {
        int ret;

        command = g_strdup_printf ("./commandline %s %d", lines[i], i);
        g_print ("running \"%s\"\n", command);
        ret = g_spawn_command_line_sync (command, NULL, NULL, &exit, NULL);
        g_assert (ret == TRUE);
        g_assert (exit == 0);
        g_print ("\"%s\" worked as expected.\n", command);
        g_free (command);
      }
    }
#endif

    return 0;
  } else {
    gst_init (&argc, &argv);
    if (argc != 2) {
      g_print
          ("something funny happened to the command line arguments, aborting.\n");
      return 1;
    }
#ifndef GST_DISABLE_GST_DEBUG
    g_assert (gst_debug_remove_log_function (gst_debug_log_default) == 1);
#endif
    GST_DEBUG_CATEGORY_INIT (cat, "cat", 0, "non-static category");
    GST_DEBUG_CATEGORY_INIT (cat_static, "cat_static", 0, "static category");
    switch (argv[1][0]) {
      case '0':
        g_assert (gst_debug_is_active () == FALSE);
#ifndef GST_DISABLE_GST_DEBUG
        gst_debug_add_log_function (debug_not_reached, NULL);
#endif
        GST_ERROR ("This will not be seen");
        return 0;
      case '1':
        return gst_debug_is_colored ()? 1 : 0;
      case '2':
        g_assert (gst_debug_get_default_threshold () == 4);
        g_assert (gst_debug_category_get_threshold (cat) == 4);
        return 0;
      case '3':
        g_assert (gst_debug_get_default_threshold () == GST_LEVEL_DEFAULT);
        g_assert (gst_debug_category_get_threshold (cat) == 4);
        g_assert (gst_debug_category_get_threshold (cat_static) == 3);
        return 0;
      case '4':
        g_assert (gst_debug_get_default_threshold () == 4);
        g_assert (gst_debug_category_get_threshold (cat) == 4);
        g_assert (gst_debug_category_get_threshold (cat_static) == 5);
        return 0;
      default:
        g_print ("usupported command, aborting...\n");
        return -1;
    }
  }
  g_assert_not_reached ();
}

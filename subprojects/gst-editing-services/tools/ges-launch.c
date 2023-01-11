/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#include <stdlib.h>
#include <locale.h>             /* for LC_ALL */
#include "ges-launcher.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

static int
real_main (int argc, gchar ** argv)
{
  GESLauncher *launcher;
  gint ret;

  setlocale (LC_ALL, "");

  launcher = ges_launcher_new ();

  ret = g_application_run (G_APPLICATION (launcher), argc, argv);

  if (!ret)
    ret = ges_launcher_get_exit_status (launcher);

  g_object_unref (launcher);
  ges_deinit ();
  gst_deinit ();

  return ret;
}

int
main (int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main ((GstMainFunc) real_main, argc, argv, NULL);
#else
  return real_main (argc, argv);
#endif
}

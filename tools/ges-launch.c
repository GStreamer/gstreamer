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

static void
_print_all_commands (void)
{
  /* Yeah I know very fancy */
  g_print ("Available ges-launch-1.0 commands:\n\n");
  g_print ("  %-9s %-11s %-15s %-10s\n\n", "+clip", "+effect", "+test-clip",
      "set-");
  g_print ("See ges-launch-1.0 help <command> or ges-launch-1.0 help <guide> "
      "to read about a specific command or a given guide\n");
}

static void
_check_command_help (int argc, gchar ** argv)
{
/**
 *   gchar *page = NULL;
 *
 *     if (argc == 2)
 *       page = g_strdup ("ges-launch-1.0");
 *     else if (!g_strcmp0 (argv[2], "all"))
 */

  if (!g_strcmp0 (argv[1], "help")) {
    _print_all_commands ();
    exit (0);
  }

/*     else
 *       page = g_strconcat ("ges-launch-1.0", "-", argv[2], NULL);
 *
 *     if (page) {
 *       execlp ("man", "man", page, NULL);
 *       g_free (page);
 *     }
 *
 *     an error is raised by execlp it will be displayed in the terminal
 *     exit (0);
 *   }
 */
}

int
main (int argc, gchar ** argv)
{
  GESLauncher *launcher;
  gint ret;

  _check_command_help (argc, argv);
  setlocale (LC_ALL, "");

  launcher = ges_launcher_new ();

  ret = g_application_run (G_APPLICATION (launcher), argc, argv);
  if (ret)
    return ret;
  return ges_launcher_get_exit_status (launcher);
}

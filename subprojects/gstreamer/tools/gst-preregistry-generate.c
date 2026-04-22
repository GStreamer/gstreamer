/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000 Wim Taymans <wtay@chello.be>
 *               2004 Thomas Vander Stichele <thomas@apestaart.org>
 *               2026 Jan Schmidt <jan@centricular.com>
 *
 * gst-preregistry-generate.c: tool to generate a registry cache
 * file in a given plugin directory, containing only the plugins
 * in that directory.
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

#include <glib.h>
#include <gst/gst_private.h>
#include <gst/gst.h>

#include <locale.h>             /* for LC_ALL */
#include <string.h>

#include "tools.h"

static int
real_main (int argc, char *argv[])
{
  gchar **plugin_paths = NULL;

  GError *err = NULL;

  GOptionEntry options[] = {
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &plugin_paths,
        "Plugin paths to scan"},
    GST_TOOLS_GOPTION_VERSION,
    {NULL}
  };

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

#ifndef GST_DISABLE_REGISTRY
  _gst_disable_registry_cache = TRUE;

#endif
  g_unsetenv ("GST_TRACERS");
  g_unsetenv ("GST_PLUGIN_PATH_1_0");
  g_unsetenv ("GST_PLUGIN_SYSTEM_PATH_1_0");
  g_unsetenv ("GST_PLUGIN_SYSTEM_PATH");
  g_setenv ("GST_REGISTRY_DISABLE", "yes", TRUE);

  GOptionContext *ctx = g_option_context_new ("PATHS-TO-SCAN");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
#ifdef G_OS_WIN32
  if (!g_option_context_parse_strv (ctx, &argv, &err))
#else
  if (!g_option_context_parse (ctx, &argc, &argv, &err))
#endif
  {
    if (err)
      gst_printerr ("Error initializing: %s\n", GST_STR_NULL (err->message));
    else
      gst_printerr ("Error initializing: Unknown error!\n");
    g_clear_error (&err);
    g_option_context_free (ctx);
    exit (1);
  }
  g_option_context_free (ctx);

  gst_tools_print_version ();

  /* Now, output the registries into the target directories */
  if (plugin_paths == NULL) {
    gst_printerr ("Error: Please supply at least one path to scan\n");
    return -1;
  }

  _priv_gst_registry_create_static_caches ((const gchar **) plugin_paths, &err);
  if (err != NULL) {
    gst_printerr ("Error creating cache(s): %s\n", GST_STR_NULL (err->message));
    g_clear_error (&err);
    return -2;
  }
  g_strfreev (plugin_paths);

  return 0;
}

int
main (int argc, char *argv[])
{
  int ret;

#ifdef G_OS_WIN32
  argv = g_win32_get_command_line ();
#endif

#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  ret = gst_macos_main ((GstMainFunc) real_main, argc, argv, NULL);
#else
  ret = real_main (argc, argv);
#endif

#ifdef G_OS_WIN32
  g_strfreev (argv);
#endif

  return ret;
}

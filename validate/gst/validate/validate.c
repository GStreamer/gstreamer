/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * validate.c - Validate generic functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
/**
 * SECTION:validate
 * @short_description: Initialize GstValidate
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* For g_stat () */
#include <glib/gstdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "validate.h"
#include "gst-validate-internal.h"

GST_DEBUG_CATEGORY (gstvalidate_debug);

static gboolean
gst_validate_scan_path_for_plugins (const gchar * path)
{
  GDir *dir;
  const gchar *dirent;
  gchar *filename;
  GstPlugin *plugin;
  gboolean changed = FALSE;

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return FALSE;

  while ((dirent = g_dir_read_name (dir))) {
    GStatBuf file_status;

    filename = g_build_filename (path, dirent, NULL);
    if (g_stat (filename, &file_status) < 0) {
      /* Plugin will be removed from cache after the scan completes if it
       * is still marked 'cached' */
      g_free (filename);
      continue;
    }

    if (!(file_status.st_mode & S_IFREG)) {
      g_free (filename);
      continue;
    }
    if (!g_str_has_suffix (dirent, G_MODULE_SUFFIX)) {
      GST_TRACE ("extension is not recognized as module file, ignoring file %s",
          filename);
      g_free (filename);
      continue;
    }

    plugin = gst_plugin_load_file (filename, NULL);
    if (plugin) {
      GST_DEBUG ("Plugin %s loaded", filename);
      gst_object_unref (plugin);
    }

    g_free (filename);
  }

  g_dir_close (dir);

  return changed;

}


static void
gst_validate_init_plugins (void)
{
  const gchar *plugin_path;

  plugin_path = g_getenv ("GST_VALIDATE_PLUGIN_PATH");
  if (plugin_path) {
    char **list;
    int i;

    GST_DEBUG ("GST_VALIDATE_PLUGIN_PATH set to %s", plugin_path);
    list = g_strsplit (plugin_path, G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; list[i]; i++) {
      gst_validate_scan_path_for_plugins (list[i]);
    }
    g_strfreev (list);
  } else {
    GST_DEBUG ("GST_VALIDATE_PLUGIN_PATH not set");
  }

  if (plugin_path == NULL) {
    char *home_plugins;

    /* plugins in the user's home directory take precedence over
     * system-installed ones */
    home_plugins = g_build_filename (g_get_user_data_dir (),
        "gstreamer-" GST_API_VERSION, "plugins", NULL);

    GST_DEBUG ("scanning home plugins %s", home_plugins);
    gst_validate_scan_path_for_plugins (home_plugins);
    g_free (home_plugins);

    /* add the main (installed) library path */

#ifdef G_OS_WIN32
    {
      char *base_dir;
      char *dir;

      base_dir =
          g_win32_get_package_installation_directory_of_module
          (_priv_gst_dll_handle);

      dir = g_build_filename (base_dir,
#ifdef _DEBUG
          "debug"
#endif
          "lib", "gstreamer-" GST_API_VERSION, NULL);
      GST_DEBUG ("scanning DLL dir %s", dir);

      gst_validate_scan_path_for_plugins (dir);

      g_free (dir);
      g_free (base_dir);
    }
#else
    gst_validate_scan_path_for_plugins (PLUGINDIR);
#endif
  }
}

/**
 * gst_validate_init:
 *
 * Initializes GstValidate, call that before any usage of GstValidate.
 * You should take care of initilizing GStreamer before calling this
 * function.
 */
void
gst_validate_init (void)
{
  GST_DEBUG_CATEGORY_INIT (gstvalidate_debug, "validate", 0,
      "Validation library");

  /* init the report system (can be called multiple times) */
  gst_validate_report_init ();

  /* Init the scenario system */
  init_scenarios ();

  /* Ensure we load overrides before any use of a monitor */
  gst_validate_override_registry_preload ();

  gst_validate_init_plugins ();
}

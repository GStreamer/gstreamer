/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gst-register.c: Plugin subsystem for loading elements, types, and libs
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <locale.h>

static gint num_features = 0;
static gint num_plugins = 0;

static void
plugin_added_func (GstRegistry * registry, GstPlugin * plugin,
    gpointer user_data)
{
  g_print ("added plugin %s with %d feature(s)\n", plugin->desc.name,
      plugin->numfeatures);

  num_features += plugin->numfeatures;
  num_plugins++;
}

static void
spawn_all_in_dir (const char *dirname)
{
  char *argv[2] = { NULL, NULL };
  GDir *dir;
  const char *file;

  /* g_print("spawning all in %s\n", dirname); */

  dir = g_dir_open (dirname, 0, NULL);
  if (dir == NULL)
    return;

  while ((file = g_dir_read_name (dir))) {
    argv[0] = g_build_filename (dirname, file, NULL);
    g_print ("running %s\n", argv[0]);
    g_spawn_sync (NULL, argv, NULL, G_SPAWN_FILE_AND_ARGV_ZERO, NULL, NULL,
        NULL, NULL, NULL, NULL);
    g_free (argv[0]);
  }
  g_dir_close (dir);
}

int
main (int argc, char *argv[])
{
  GList *registries;
  GList *path_spill = NULL;     /* used for path spill from failing registries */

  setlocale (LC_ALL, "");

  /* Init gst */
  _gst_registry_auto_load = FALSE;
  gst_init (&argc, &argv);

  registries = gst_registry_pool_list ();
  registries = g_list_reverse (registries);

  while (registries) {
    GstRegistry *registry = GST_REGISTRY (registries->data);
    GList *dir_list;
    GList *iter;
    char *dir;

    if (path_spill) {
      GList *iter;

      /* add spilled paths to this registry;
       * since they're spilled they probably weren't loaded correctly
       * so we should give a lower priority registry the chance to do them */
      for (iter = path_spill; iter; iter = iter->next) {
        g_print ("added path   %s to %s \n",
            (const char *) iter->data, registry->name);
        gst_registry_add_path (registry, (const gchar *) iter->data);
      }
      g_list_free (path_spill);
      path_spill = NULL;
    }

    g_signal_connect (G_OBJECT (registry), "plugin_added",
        G_CALLBACK (plugin_added_func), NULL);

    if (registry->flags & GST_REGISTRY_WRITABLE) {
      char *location;

      g_object_get (registry, "location", &location, NULL);
      g_print ("rebuilding %s (%s)\n", registry->name, location);
      g_free (location);
      gst_registry_rebuild (registry);
      gst_registry_save (registry);
    } else {
      g_print ("trying to load %s\n", registry->name);
      if (!gst_registry_load (registry)) {
        g_print ("error loading %s\n", registry->name);
        /* move over paths from this registry to the next one */
        path_spill = g_list_concat (path_spill,
            gst_registry_get_path_list (registry));
        g_assert (path_spill != NULL);
      }
      /* also move over paths if the registry wasn't writable
       * FIXME: we should check if the paths that were loaded from this
       registry get removed from the path_list so we only try to
       spill paths that could not be registered */
      /* Until that is done, don't spill paths when registry is not writable
         (e.g. case of user running gst-register and sysreg not writable) */

      /*
         path_spill = g_list_concat (path_spill,
         gst_registry_get_path_list (registry));
       */
    }

    dir_list = gst_registry_get_path_list (registry);
    for (iter = dir_list; iter; iter = iter->next) {
      dir =
          g_build_filename ((const char *) iter->data, "register-scripts",
          NULL);
      spawn_all_in_dir (dir);
      g_free (dir);
    }
    g_list_free (dir_list);

    registries = g_list_next (registries);
  }

  g_print ("loaded %d plugins with %d features\n", num_plugins, num_features);

  return (0);
}

/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstregistry.c: handle registry
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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "gstinfo.h"
#include "gstregistry.h"
#include "gstlog.h"
#include "gstmarshal.h"

/* Element signals and args */
enum {
  PLUGIN_ADDED,
  LAST_SIGNAL
};

static void             gst_registry_class_init           (GstRegistryClass *klass);
static void             gst_registry_init                 (GstRegistry *registry);

static GObjectClass *parent_class = NULL;
static guint gst_registry_signals[LAST_SIGNAL] = { 0 }; 

GType
gst_registry_get_type (void)
{
  static GType registry_type = 0;

  if (!registry_type) {
    static const GTypeInfo registry_info = {
      sizeof (GstRegistryClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_registry_class_init,
      NULL,
      NULL,
      sizeof (GstRegistry),
      32,
      (GInstanceInitFunc) gst_registry_init,
      NULL
    };
    registry_type = g_type_register_static (G_TYPE_OBJECT, "GstRegistry",
                                            &registry_info, G_TYPE_FLAG_ABSTRACT);
  }
  return registry_type;
}

static void
gst_registry_class_init (GstRegistryClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  gst_registry_signals[PLUGIN_ADDED] =
    g_signal_new ("plugin_added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstRegistryClass, plugin_added), NULL, NULL,
                  gst_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                  G_TYPE_POINTER);

  gobject_class->dispose = NULL;
}

static void
gst_registry_init (GstRegistry *registry)
{
  registry->priority = 0;
  registry->loaded = FALSE;
  registry->paths = NULL;
}

/**
 * gst_registry_load:
 * @registry: the registry to load
 *
 * Load the given registry
 *
 * Returns: TRUE on success.
 */
gboolean
gst_registry_load (GstRegistry *registry)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->load)
    return rclass->load (registry);

  return FALSE;
}

/**
 * gst_registry_is_loaded:
 * @registry: the registry to check
 *
 * Check if the given registry is loaded
 *
 * Returns: TRUE if loaded.
 */
gboolean
gst_registry_is_loaded (GstRegistry *registry)
{
  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  return registry->loaded;
}

/**
 * gst_registry_save:
 * @registry: the registry to save
 *
 * Save the contents of the given registry
 *
 * Returns: TRUE on success
 */
gboolean
gst_registry_save (GstRegistry *registry)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->save)
    return rclass->save (registry);

  return FALSE;
}

/**
 * gst_registry_rebuild:
 * @registry: the registry to rebuild
 *
 * Rebuild the given registry
 *
 * Returns: TRUE on success
 */
gboolean
gst_registry_rebuild (GstRegistry *registry)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->rebuild)
    return rclass->rebuild (registry);

  return FALSE;
}

/**
 * gst_registry_unload:
 * @registry: the registry to unload
 *
 * Unload the given registry
 *
 * Returns: TRUE on success
 */
gboolean
gst_registry_unload (GstRegistry *registry)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->unload)
    return rclass->unload (registry);

  return FALSE;
}

/**
 * gst_registry_add_path:
 * @registry: the registry to add the path to
 * @path: the path to add to the registry 
 *
 * Add the given path to the registry. The syntax of the
 * path is specific to the registry. If the path has already been
 * added, do nothing.
 */
void
gst_registry_add_path (GstRegistry *registry, const gchar *path)
{
  GList *l;

  g_return_if_fail (GST_IS_REGISTRY (registry));
  g_return_if_fail (path != NULL);

  l = registry->paths;
  while (l) {
    if (strcmp (l->data, path) == 0)
      return;

    l = g_list_next (l);
  }
  
  registry->paths = g_list_append (registry->paths, g_strdup (path));
}

/**
 * gst_registry_get_path_list:
 * @registry: the registry to get the pathlist of
 *
 * Get the list of paths for the given registry.
 *
 * Returns: A Glist of paths as strings. g_list_free after use.
 */
GList*
gst_registry_get_path_list (GstRegistry *registry)
{
  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);

  return g_list_copy (registry->paths);
}


static void
free_list_strings_func (gpointer data, gpointer user_data)
{
  g_free (data);
}

/**
 * gst_registry_clear_paths:
 * @registry: the registry to clear the paths of
 *
 * Clear the paths of the given registry
 */
void
gst_registry_clear_paths (GstRegistry *registry)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));

  g_list_foreach (registry->paths, free_list_strings_func, NULL);
  g_list_free (registry->paths);

  registry->paths = NULL;
}

/**
 * gst_registry_add_plugin:
 * @registry: the registry to add the plugin to
 * @plugin: the plugin to add
 *
 * Add the plugin to the registry. The plugin-added signal 
 * will be emitted.
 *
 * Returns: TRUE on success.
 */
gboolean 
gst_registry_add_plugin (GstRegistry *registry, GstPlugin *plugin)
{
  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);
  
  plugin->manager = registry;
  registry->plugins = g_list_prepend (registry->plugins, plugin);

  g_signal_emit (G_OBJECT (registry), gst_registry_signals[PLUGIN_ADDED], 0, plugin);

  return TRUE;
}

/**
 * gst_registry_remove_plugin:
 * @registry: the registry to remove the plugin from
 * @plugin: the plugin to remove
 *
 * Remove the plugin from the registry.
 */
void
gst_registry_remove_plugin (GstRegistry *registry, GstPlugin *plugin)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));

  registry->plugins = g_list_remove (registry->plugins, plugin);
}

/**
 * gst_registry_find_plugin:
 * @registry: the registry to search
 * @name: the plugin name to find
 *
 * Find the plugin with the given name in the registry.
 *
 * Returns: The plugin with the given name or NULL if the plugin was not found.
 */
GstPlugin*
gst_registry_find_plugin (GstRegistry *registry, const gchar *name)
{
  GList *plugins;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  plugins = registry->plugins;
  
  while (plugins) {
    GstPlugin *plugin = (GstPlugin *) (plugins->data);

    if (plugin->name && !strcmp (plugin->name, name))
      return plugin;
    
    plugins = g_list_next (plugins);
  }
  return NULL;
}

/**
 * gst_registry_find_feature:
 * @registry: the registry to search
 * @name: the pluginfeature name to find
 * @type: the pluginfeature type to find
 *
 * Find the pluginfeature with the given name and type in the registry.
 *
 * Returns: The pluginfeature with the given name and type or NULL 
 * if the plugin was not found.
 */
GstPluginFeature*
gst_registry_find_feature (GstRegistry *registry, const gchar *name, GType type)
{
  GstPluginFeature *feature = NULL;
  GList *plugins;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  plugins = registry->plugins;

  while (plugins) {
    GstPlugin *plugin = (GstPlugin *) (plugins->data);

    feature = gst_plugin_find_feature (plugin, name, type);
    if (feature)
      return feature;
    
    plugins = g_list_next (plugins);
  }
  return feature;
}


/**
 * gst_registry_load_plugin:
 * @registry: the registry to load the plugin from
 * @plugin: the plugin to load
 *
 * Bring the plugin from the registry into memory.
 *
 * Returns: a value indicating the result 
 */
GstRegistryReturn
gst_registry_load_plugin (GstRegistry *registry, GstPlugin *plugin)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), GST_REGISTRY_PLUGIN_LOAD_ERROR);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->load_plugin)
    return rclass->load_plugin (registry, plugin);

  return GST_REGISTRY_PLUGIN_LOAD_ERROR;
}

/**
 * gst_registry_unload_plugin:
 * @registry: the registry to unload the plugin from
 * @plugin: the plugin to unload
 *
 * Unload the plugin from the given registry.
 *
 * Returns: a value indicating the result 
 */
GstRegistryReturn
gst_registry_unload_plugin (GstRegistry *registry, GstPlugin *plugin)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), GST_REGISTRY_PLUGIN_LOAD_ERROR);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->unload_plugin)
    return rclass->unload_plugin (registry, plugin);

  return GST_REGISTRY_PLUGIN_LOAD_ERROR;
}

/**
 * gst_registry_update_plugin:
 * @registry: the registry to update
 * @plugin: the plugin to update
 *
 * Update the plugin in the given registry.
 *
 * Returns: a value indicating the result 
 */
GstRegistryReturn
gst_registry_update_plugin (GstRegistry *registry, GstPlugin *plugin)
{
  GstRegistryClass *rclass;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), GST_REGISTRY_PLUGIN_LOAD_ERROR);

  rclass = GST_REGISTRY_GET_CLASS (registry);

  if (rclass->update_plugin)
    return rclass->update_plugin (registry, plugin);

  return GST_REGISTRY_PLUGIN_LOAD_ERROR;
}


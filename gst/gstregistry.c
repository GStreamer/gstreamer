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

#define CLASS(registry)  GST_REGISTRY_CLASS (G_OBJECT_GET_CLASS (registry))

/* Element signals and args */
enum {
  PLUGIN_ADDED,
  LAST_SIGNAL
};


static GList *_gst_registry_pool = NULL;
static GList *_gst_registry_pool_plugins = NULL;

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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  if (CLASS (registry)->load)
    return CLASS (registry)->load (registry);

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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  if (CLASS (registry)->save)
    return CLASS (registry)->save (registry);

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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  if (CLASS (registry)->rebuild)
    return CLASS (registry)->rebuild (registry);

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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  if (CLASS (registry)->unload)
    return CLASS (registry)->unload (registry);

  return FALSE;
}

/**
 * gst_registry_add_path:
 * @registry: the registry to add the path to
 *
 * Add the given pathstring to the registry. The syntax of the
 * pathstring is specific to the registry.
 */
void
gst_registry_add_path (GstRegistry *registry, const gchar *path)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));
  g_return_if_fail (path != NULL);

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
  GList *walk;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  
  walk = registry->plugins;

  while (walk) {
    GstPlugin *plugin = (GstPlugin *) (walk->data);

    if (plugin->name && !strcmp (plugin->name, name))
      return plugin;
    
    walk = g_list_next (walk);
  }
  return NULL;
}

static GstPluginFeature*
gst_plugin_list_find_feature (GList *plugins, const gchar *name, GType type)
{
  GstPluginFeature *feature = NULL;

  g_return_val_if_fail (name != NULL, NULL);
  
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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);
  g_return_val_if_fail (name != NULL, NULL);
  
  return gst_plugin_list_find_feature (registry->plugins, name, type);
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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), GST_REGISTRY_PLUGIN_LOAD_ERROR);

  if (CLASS (registry)->load_plugin)
    return CLASS (registry)->load_plugin (registry, plugin);

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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), GST_REGISTRY_PLUGIN_LOAD_ERROR);

  if (CLASS (registry)->unload_plugin)
    return CLASS (registry)->unload_plugin (registry, plugin);

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
  g_return_val_if_fail (GST_IS_REGISTRY (registry), GST_REGISTRY_PLUGIN_LOAD_ERROR);

  if (CLASS (registry)->update_plugin)
    return CLASS (registry)->update_plugin (registry, plugin);

  return GST_REGISTRY_PLUGIN_LOAD_ERROR;
}

/**
 * gst_registry_pool_list:
 *
 * Get a list of all registries in the pool
 *
 * Returns: a Glist of GstRegistries, g_list_free after use.
 */
GList*
gst_registry_pool_list (void)
{
  return g_list_copy (_gst_registry_pool);
}

static gint
gst_registry_compare_func (gconstpointer a, gconstpointer b)
{
  return GST_REGISTRY (a)->priority - GST_REGISTRY (b)->priority;
}

/**
 * gst_registry_pool_add:
 * @registry: the registry to add
 * @priority: the priority of the registry
 *
 * Add the registry to the pool with the given priority.
 */
void
gst_registry_pool_add (GstRegistry *registry, guint priority)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));

  registry->priority = priority;

  _gst_registry_pool = g_list_insert_sorted (_gst_registry_pool, registry, gst_registry_compare_func);
}

/**
 * gst_registry_pool_remove:
 * @registry: the registry to remove
 *
 * Remove the registry from the pool.
 */
void
gst_registry_pool_remove (GstRegistry *registry)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));

  _gst_registry_pool = g_list_remove (_gst_registry_pool, registry);
}

/**
 * gst_registry_pool_add_plugin:
 * @plugin: the plugin to add
 *
 * Add the plugin to the global pool of plugins.
 */
void
gst_registry_pool_add_plugin (GstPlugin *plugin)
{
  _gst_registry_pool_plugins = g_list_prepend (_gst_registry_pool_plugins, plugin);
}


/**
 * gst_registry_pool_load_all:
 *
 * Load all the registries in the pool. Registries with the
 * GST_REGISTRY_DELAYED_LOADING will not be loaded.
 */
void
gst_registry_pool_load_all (void)
{
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    if (registry->flags & GST_REGISTRY_READABLE &&
        !(registry->flags & GST_REGISTRY_DELAYED_LOADING)) {
      gst_registry_load (registry);
    }
    
    walk = g_list_next (walk);
  }
}

/**
 * gst_registry_pool_plugin_list:
 *
 * Get a list of all plugins in the pool.
 * 
 * Returns: a GList of plugins, g_list_free after use.
 */
GList*
gst_registry_pool_plugin_list (void)
{
  GList *result = NULL;
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    /* FIXME only include highest priority plugins */
    result = g_list_concat (result, g_list_copy (registry->plugins));
    
    walk = g_list_next (walk);
  }
  
  return result;
}

/**
 * gst_registry_pool_feature_list:
 * @type: the type of the features to list.
 *
 * Get a list of all pluginfeatures of the given type in the pool.
 * 
 * Returns: a GList of pluginfeatures, g_list_free after use.
 */
GList*
gst_registry_pool_feature_list (GType type)
{
  GList *result = NULL;
  GList *plugins = gst_registry_pool_plugin_list ();

  while (plugins) {
    GstPlugin *plugin = GST_PLUGIN (plugins->data);
    GList *features = plugin->features;
      
    while (features) {
      GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);

      if (type == 0 || G_OBJECT_TYPE (feature) == type) {
        result = g_list_prepend (result, feature);
      }
      features = g_list_next (features);
    }
    plugins = g_list_next (plugins);
  }
  result = g_list_reverse (result);
  
  return result;
}

/**
 * gst_registry_pool_find_plugin:
 * @name: the name of the plugin to find
 *
 * Get the named plugin from the registry pool
 * 
 * Returns: The plugin with the given name or NULL if the plugin 
 * was not found.
 */
GstPlugin*
gst_registry_pool_find_plugin (const gchar *name)
{
  GstPlugin *result = NULL;
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    /* FIXME only include highest priority plugins */
    result = gst_registry_find_plugin (registry, name);
    if (result)
      return result;
    
    walk = g_list_next (walk);
  }
  return NULL;
}

/**
 * gst_registry_pool_find_feature:
 * @name: the name of the pluginfeature to find
 * @type: the type of the pluginfeature to find
 *
 * Get the pluginfeature with the given name and type from the pool of
 * registries.
 * 
 * Returns: A pluginfeature with the given name and type or NULL if the feature
 * was not found.
 */
GstPluginFeature*
gst_registry_pool_find_feature (const gchar *name, GType type)
{
  GstPluginFeature *result = NULL;
  GList *walk;
  
  result = gst_plugin_list_find_feature (_gst_registry_pool_plugins, name, type);
  if (result)
    return result;
  
  walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    /* FIXME only include highest priority plugins */
    result = gst_registry_find_feature (registry, name, type);
    if (result)
      return result;
    
    walk = g_list_next (walk);
  }
  return NULL;
}

/**
 * gst_registry_pool_get_prefered:
 * @flags: The flags for the prefered registry
 *
 * Get the prefered registry with the given flags
 * 
 * Returns: The registry with the flags.
 */
GstRegistry*
gst_registry_pool_get_prefered (GstRegistryFlags flags)
{
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    if (registry->flags & flags)
      return registry;
    
    walk = g_list_next (walk);
  }
  return NULL;
}



static gchar *gst_registry_option = NULL;

/* save the registry specified as an option */
void
gst_registry_option_set (const gchar *registry)
{
  gst_registry_option = g_strdup (registry);
  return;
}

/* decide if we're going to use the global registry or not 
 * - if root, use global
 * - if not root :
 *   - if user can write to global, use global
 *   - else use local
 */
gboolean
gst_registry_use_global (void)
{
  /* struct stat reg_stat; */
  FILE *reg;
  
  if (getuid () == 0) return TRUE; 	/* root always uses global */

  /* check if we can write to the global registry somehow */
  reg = fopen (GLOBAL_REGISTRY_FILE, "a");
  if (reg == NULL) { return FALSE; }
  else
  {
    /* we can write to it, do so for kicks */
    fclose (reg);
  }
  
  /* we can write to it, so now see if we can write in the dir as well */ 
  if (access (GLOBAL_REGISTRY_DIR, W_OK) == 0) return TRUE;

  return FALSE;
}

/* get the data that tells us where we can write the registry
 * Allocate, fill in the GstRegistryWrite struct according to 
 * current situation, and return it */
GstRegistryWrite *
gst_registry_write_get ()
{
  GstRegistryWrite *gst_reg = g_malloc (sizeof (GstRegistryWrite));
  
  /* if a registry is specified on command line, use that one */
  if (gst_registry_option)
  {
    /* FIXME: maybe parse the dir from file ? */
    gst_reg->dir = NULL;
    gst_reg->file = gst_registry_option;
    /* we cannot use the temp dir since the move needs to be on same device */
    gst_reg->tmp_file = g_strdup_printf ("%s.tmp", gst_registry_option);
  }
  else if (g_getenv ("GST_REGISTRY"))
  {
    gst_reg->dir = NULL;
    gst_reg->file = g_strdup (g_getenv ("GST_REGISTRY"));
    gst_reg->tmp_file = g_strdup_printf ("%s.tmp", g_getenv ("GST_REGISTRY"));
  }
  else
  {
    if (gst_registry_use_global ())
    {
      gst_reg->dir      = g_strdup (GLOBAL_REGISTRY_DIR);
      gst_reg->file     = g_strdup (GLOBAL_REGISTRY_FILE);
      gst_reg->tmp_file = g_strdup (GLOBAL_REGISTRY_FILE_TMP);
    }
    else
    {
      gchar *homedir = (gchar *) g_get_home_dir ();
      
      gst_reg->dir = g_strjoin ("/", homedir, LOCAL_REGISTRY_DIR, NULL);
      gst_reg->file = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE, NULL);
      gst_reg->tmp_file = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE_TMP, NULL);
    }
  } 
  return gst_reg;
}

/* fill in the GstRegistryRead struct according to current situation */
GstRegistryRead *
gst_registry_read_get ()
{
  GstRegistryRead *gst_reg = g_malloc (sizeof (GstRegistryRead));
  
  /* if a registry is specified on command line, use that one */
  if (gst_registry_option)
  {
    /* FIXME: maybe parse the dir from file ? */
    gst_reg->local_reg = NULL;
    gst_reg->global_reg = gst_registry_option;
  } 
  else if (g_getenv ("GST_REGISTRY"))
  {
    gst_reg->local_reg = NULL;
    gst_reg->global_reg = g_strdup (g_getenv ("GST_REGISTRY"));
  }
  else
  {
    gchar *homedir = (gchar *) g_get_home_dir ();
    gst_reg->local_reg = g_strjoin ("/", homedir, LOCAL_REGISTRY_FILE, NULL);
    if (g_file_test (gst_reg->local_reg, G_FILE_TEST_EXISTS) == FALSE)
    {
      /* it does not exist, so don't read from it */
      g_free (gst_reg->local_reg);
    }
    gst_reg->global_reg = g_strdup (GLOBAL_REGISTRY_FILE);
  }
  return gst_reg;
}

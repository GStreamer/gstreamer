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

#include <string.h>

#include "gstinfo.h"
#include "gstregistrypool.h"
#include "gstlog.h"

static GList *_gst_registry_pool = NULL;
static GList *_gst_registry_pool_plugins = NULL;

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

#ifndef GST_DISABLE_REGISTRY
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
#endif /* GST_DISABLE_REGISTRY */

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
#ifndef GST_DISABLE_REGISTRY
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    if (registry->flags & GST_REGISTRY_READABLE &&
        !(registry->flags & GST_REGISTRY_DELAYED_LOADING)) {
      gst_registry_load (registry);
    }
    
    walk = g_list_next (walk);
  }
#endif /* GST_DISABLE_REGISTRY */
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
#ifndef GST_DISABLE_REGISTRY
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    /* FIXME only include highest priority plugins */
    result = g_list_concat (result, g_list_copy (registry->plugins));
    
    walk = g_list_next (walk);
  }
#endif /* GST_DISABLE_REGISTRY */
  
  return g_list_concat (_gst_registry_pool_plugins, result);
}

GstFeatureFilterResult
gst_registry_pool_feature_type_filter (GstPluginFeature *feature, GType type)
{
  if (type == 0 || G_OBJECT_TYPE (feature) == type)
    return GST_FEATURE_FILTER_OK;

  return GST_FEATURE_FILTER_NOK;
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
  return gst_registry_pool_feature_filter (
		  (GstPluginFeatureFilter) gst_registry_pool_feature_type_filter, 
		  GINT_TO_POINTER (type));
}

/**
 * gst_registry_pool_feature_filter:
 * @filter: the filter to apply to the feature list
 * @user_data: data passed to the filter function
 *
 * Apply the filter function to all features and return a list
 * of those features that satisfy the filter.
 * 
 * Returns: a GList of pluginfeatures, g_list_free after use.
 */
GList*
gst_registry_pool_feature_filter (GstPluginFeatureFilter filter, gpointer user_data)
{
  GList *result = NULL;
  GList *plugins, *orig;
  gboolean done = FALSE;

  orig = plugins = gst_registry_pool_plugin_list ();

  while (plugins && !done) {
    GstPlugin *plugin = GST_PLUGIN (plugins->data);
    GList *features = plugin->features;
      
    plugins = g_list_next (plugins);

    while (features && !done) {
      GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);
      gboolean res = GST_FEATURE_FILTER_OK;

      features = g_list_next (features);

      if (filter)
        res = filter (feature, user_data);

      switch (res) {
	case GST_FEATURE_FILTER_DONE:
	  done = TRUE;
	  /* fallthrough */
	case GST_FEATURE_FILTER_OK:
          result = g_list_prepend (result, feature);
          break;
	case GST_FEATURE_FILTER_NOK:
	default:
          break;
      }
    }
  }
  result = g_list_reverse (result);

  g_list_free (orig);
  
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
  GList *walk;

  g_return_val_if_fail (name != NULL, NULL);

  walk = _gst_registry_pool_plugins;
  while (walk) {
    result = (GstPlugin *) (walk->data);

    if (result->name && !strcmp (result->name, name))
      return result;
    
    walk = g_list_next (walk);
  }
  
#ifndef GST_DISABLE_REGISTRY
  walk = _gst_registry_pool;
  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    /* FIXME only include highest priority plugins */
    result = gst_registry_find_plugin (registry, name);
    if (result)
      return result;
    
    walk = g_list_next (walk);
  }
#endif /* GST_DISABLE_REGISTRY */
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

  g_return_val_if_fail (name != NULL, NULL);
  
  walk = _gst_registry_pool_plugins;
  while (walk) {
    GstPlugin *plugin = (GstPlugin *) (walk->data);

    result = gst_plugin_find_feature (plugin, name, type);
    if (result)
      return result;

    walk = g_list_next (walk);
  }

#ifndef GST_DISABLE_REGISTRY
  walk = _gst_registry_pool;
  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    /* FIXME only include highest priority plugins */
    result = gst_registry_find_feature (registry, name, type);
    if (result)
      return result;
    
    walk = g_list_next (walk);
  }
#endif /* GST_DISABLE_REGISTRY */
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
#ifndef GST_DISABLE_REGISTRY
  GList *walk = _gst_registry_pool;

  while (walk) {
    GstRegistry *registry = GST_REGISTRY (walk->data);

    if (registry->flags & flags)
      return registry;
    
    walk = g_list_next (walk);
  }
#endif /* GST_DISABLE_REGISTRY */
  return NULL;
}


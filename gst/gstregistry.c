/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 David A. Schleef <ds@schleef.org>
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
/**
 * SECTION:gstregistry
 * @short_description: Abstract base class for management of #GstPlugin objects
 * @see_also: #GstPlugin, #GstPluginFeature
 *
 * One registry holds the metadata of a set of plugins.
 * All registries build the #GstRegistryPool.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gst_private.h"
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>


#include "gstinfo.h"
#include "gstregistry.h"
#include "gstmarshal.h"
#include "gstfilter.h"

#define GST_CAT_DEFAULT GST_CAT_REGISTRY

/*
 * Design:
 *
 * The GstRegistry object is a list of plugins and some functions
 * for dealing with them.  Plugins are matched 1-1 with a file on
 * disk, and may or may not be loaded at a given time.  There may
 * be multiple GstRegistry objects, but the "default registry" is
 * the only object that has any meaning to the core.
 *
 * The registry.xml file in 0.9 is actually a cache of plugin
 * information.  This is unlike previous versions, where the registry
 * file was the primary source of plugin information, and was created
 * by the gst-register command.
 *
 * In 0.9, the primary source, at all times, of plugin information
 * is each plugin file itself.  Thus, if an application wants
 * information about a particular plugin, or wants to search for
 * a feature that satisfies given criteria, the primary means of
 * doing so is to load every plugin and look at the resulting
 * information that is gathered in the default registry.  Clearly,
 * this is a time consuming process, so we cache information in
 * the registry.xml file.
 *
 * On startup, plugins are searched for in the plugin search path.
 * This path can be set directly using the GST_PLUGIN_PATH
 * environment variable.  The registry file is loaded from
 * ~/.gstreamer-0.9/registry.xml or the file listed in the
 * GST_REGISTRY env var.  The only reason to change the registry
 * location is for testing.
 *
 * For each plugin that is found in the plugin search path, there
 * could be 3 possibilities for cached information:
 *  - the cache may not contain information about a given file.
 *  - the cache may have stale information.
 *  - the cache may have current information.
 * In the first two cases, the plugin is loaded and the cache
 * updated.  In addition to these cases, the cache may have entries
 * for plugins that are not relevant to the current process.  These
 * are marked as not available to the current process.  If the
 * cache is updated for whatever reason, it is marked dirty.
 *
 * A dirty cache is written out at the end of initialization.  Each
 * entry is checked to make sure the information is minimally valid.
 * If not, the entry is simply dropped.
 *
 * Implementation notes:
 *
 * The "cache" and "default registry" are different concepts and
 * can represent different sets of plugins.  For various reasons,
 * at init time, the cache is stored in the default registry, and
 * plugins not relevant to the current process are marked with the
 * GST_PLUGIN_FLAG_CACHED bit.  These plugins are removed at the
 * end of intitialization.
 *
 */

/* Element signals and args */
enum
{
  PLUGIN_ADDED,
  FEATURE_ADDED,
  LAST_SIGNAL
};

static void gst_registry_class_init (GstRegistryClass * klass);
static void gst_registry_init (GstRegistry * registry);

static guint gst_registry_signals[LAST_SIGNAL] = { 0 };

static GstPluginFeature *gst_registry_lookup_feature_locked (GstRegistry *
    registry, const char *name);
static GstPlugin *gst_registry_lookup_locked (GstRegistry * registry,
    const char *filename);

G_DEFINE_TYPE (GstRegistry, gst_registry, GST_TYPE_OBJECT);

static void
gst_registry_class_init (GstRegistryClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gst_registry_signals[PLUGIN_ADDED] =
      g_signal_new ("plugin-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRegistryClass, plugin_added), NULL,
      NULL, gst_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
  gst_registry_signals[FEATURE_ADDED] =
      g_signal_new ("feature-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRegistryClass, feature_added),
      NULL, NULL, gst_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

  gobject_class->dispose = NULL;
}

static void
gst_registry_init (GstRegistry * registry)
{

}

GstRegistry *
gst_registry_get_default (void)
{
  static GstRegistry *_gst_registry_default;

  if (!_gst_registry_default) {
    _gst_registry_default = g_object_new (GST_TYPE_REGISTRY, NULL);
  }
  return _gst_registry_default;
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
gst_registry_add_path (GstRegistry * registry, const gchar * path)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));
  g_return_if_fail (path != NULL);

  if (strlen (path) == 0) {
    GST_INFO ("Ignoring empty plugin path");
    return;
  }

  GST_LOCK (registry);
  if (g_list_find_custom (registry->paths, path, (GCompareFunc) strcmp)) {
    g_warning ("path %s already added to registry", path);
    GST_UNLOCK (registry);
    return;
  }

  GST_INFO ("Adding plugin path: \"%s\"", path);
  registry->paths = g_list_append (registry->paths, g_strdup (path));
  GST_UNLOCK (registry);
}

/**
 * gst_registry_get_path_list:
 * @registry: the registry to get the pathlist of
 *
 * Get the list of paths for the given registry.
 *
 * Returns: A Glist of paths as strings. g_list_free after use.
 */
GList *
gst_registry_get_path_list (GstRegistry * registry)
{
  GList *list;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);

  GST_LOCK (registry);
  /* We don't need to copy the strings, because they won't be deleted
   * as long as the GstRegistry is around */
  list = g_list_copy (registry->paths);
  GST_UNLOCK (registry);

  return list;
}


/**
 * gst_registry_add_plugin:
 * @registry: the registry to add the plugin to
 * @plugin: the plugin to add
 *
 * Add the plugin to the registry. The plugin-added signal will be emitted.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_registry_add_plugin (GstRegistry * registry, GstPlugin * plugin)
{
  GstPlugin *existing_plugin;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);

  GST_LOCK (registry);
  existing_plugin = gst_registry_lookup_locked (registry, plugin->filename);
  if (existing_plugin) {
    GST_DEBUG ("Replacing existing plugin %p for filename \"%s\"",
        existing_plugin, plugin->filename);
    registry->plugins = g_list_remove (registry->plugins, existing_plugin);
    gst_object_unref (existing_plugin);
  }

  GST_DEBUG ("Adding plugin %p for filename \"%s\"", plugin, plugin->filename);

  registry->plugins = g_list_prepend (registry->plugins, plugin);

  gst_object_ref (plugin);
  gst_object_sink (plugin);
  GST_UNLOCK (registry);

  GST_DEBUG ("emitting plugin-added for filename %s", plugin->filename);
  g_signal_emit (G_OBJECT (registry), gst_registry_signals[PLUGIN_ADDED], 0,
      plugin);

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
gst_registry_remove_plugin (GstRegistry * registry, GstPlugin * plugin)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));

  GST_LOCK (registry);
  registry->plugins = g_list_remove (registry->plugins, plugin);
  GST_UNLOCK (registry);
  gst_object_unref (plugin);
}

/**
 * gst_registry_add_feature:
 * @registry: the registry to add the plugin to
 * @feature: the feature to add
 *
 * Add the feature to the registry. The feature-added signal will be emitted.
 *
 * Returns: TRUE on success.
 */
gboolean
gst_registry_add_feature (GstRegistry * registry, GstPluginFeature * feature)
{
  GstPluginFeature *existing_feature;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), FALSE);
  g_return_val_if_fail (GST_IS_PLUGIN_FEATURE (feature), FALSE);
  g_return_val_if_fail (feature->name != NULL, FALSE);
  g_return_val_if_fail (feature->plugin_name != NULL, FALSE);

  GST_LOCK (registry);
  existing_feature = gst_registry_lookup_feature_locked (registry,
      feature->name);
  if (existing_feature) {
    GST_DEBUG ("Replacing existing feature %p (%s)",
        existing_feature, feature->name);
    registry->features = g_list_remove (registry->features, existing_feature);
    gst_object_unref (existing_feature);
  }

  GST_DEBUG ("Adding feature %p (%s)", feature, feature->name);

  registry->features = g_list_prepend (registry->features, feature);

  gst_object_ref (feature);
  gst_object_sink (feature);
  GST_UNLOCK (registry);

  GST_DEBUG ("emitting feature-added for %s", feature->name);
  g_signal_emit (G_OBJECT (registry), gst_registry_signals[FEATURE_ADDED], 0,
      feature);

  return TRUE;
}

/**
 * gst_registry_remove_feature:
 * @registry: the registry to remove the feature from
 * @feature: the feature to remove
 *
 * Remove the feature from the registry.
 */
void
gst_registry_remove_feature (GstRegistry * registry, GstPluginFeature * feature)
{
  g_return_if_fail (GST_IS_REGISTRY (registry));

  GST_LOCK (registry);
  registry->features = g_list_remove (registry->features, feature);
  GST_UNLOCK (registry);
  gst_object_unref (feature);
}

/**
 * gst_registry_plugin_filter:
 * @registry: registry to query
 * @filter: the filter to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugins in the registry and returns a GList with
 * the results. If the first flag is set, only the first match is
 * returned (as a list with a single object).
 *
 * Returns: a GList of plugins, gst_plugin_list_free after use.
 */
GList *
gst_registry_plugin_filter (GstRegistry * registry,
    GstPluginFilter filter, gboolean first, gpointer user_data)
{
  GList *list;
  GList *g;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);

  GST_LOCK (registry);
  list = gst_filter_run (registry->plugins, (GstFilterFunc) filter, first,
      user_data);
  for (g = list; g; g = g->next) {
    gst_object_ref (GST_PLUGIN (g->data));
  }
  GST_UNLOCK (registry);

  return list;
}

/**
 * gst_registry_feature_filter:
 * @registry: registry to query
 * @filter: the filter to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all features of the plugins in the registry
 * and returns a GList with the results.
 * If the first flag is set, only the first match is
 * returned (as a list with a single object).
 *
 * Returns: a GList of plugin features, gst_plugin_feature_list_free after use.
 */
GList *
gst_registry_feature_filter (GstRegistry * registry,
    GstPluginFeatureFilter filter, gboolean first, gpointer user_data)
{
  GList *list;
  GList *g;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);

  GST_LOCK (registry);
  list = gst_filter_run (registry->features, (GstFilterFunc) filter, first,
      user_data);
  for (g = list; g; g = g->next) {
    gst_object_ref (GST_PLUGIN_FEATURE (g->data));
  }
  GST_UNLOCK (registry);

  return list;
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
GstPlugin *
gst_registry_find_plugin (GstRegistry * registry, const gchar * name)
{
  GList *walk;
  GstPlugin *result = NULL;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  walk = gst_registry_plugin_filter (registry,
      (GstPluginFilter) gst_plugin_name_filter, TRUE, (gpointer) name);
  if (walk) {
    result = GST_PLUGIN (walk->data);

    gst_object_ref (result);
    gst_plugin_list_free (walk);
  }

  return result;
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
GstPluginFeature *
gst_registry_find_feature (GstRegistry * registry, const gchar * name,
    GType type)
{
  GstPluginFeature *feature = NULL;
  GList *walk;
  GstTypeNameData data;

  g_return_val_if_fail (GST_IS_REGISTRY (registry), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  data.name = name;
  data.type = type;

  walk = gst_registry_feature_filter (registry,
      (GstPluginFeatureFilter) gst_plugin_feature_type_name_filter,
      TRUE, &data);

  if (walk) {
    feature = GST_PLUGIN_FEATURE (walk->data);

    gst_object_ref (feature);
    gst_plugin_feature_list_free (walk);
  }

  return feature;
}

GList *
gst_registry_get_feature_list (GstRegistry * registry, GType type)
{
  GstTypeNameData data;

  data.type = type;
  data.name = NULL;

  return gst_registry_feature_filter (registry,
      (GstPluginFeatureFilter) gst_plugin_feature_type_name_filter,
      FALSE, &data);
}

GList *
gst_registry_get_plugin_list (GstRegistry * registry)
{
  GList *list;
  GList *g;

  GST_LOCK (registry);
  list = g_list_copy (registry->plugins);
  for (g = list; g; g = g->next) {
    gst_object_ref (GST_PLUGIN (g->data));
  }
  GST_UNLOCK (registry);

  return list;
}

static GstPluginFeature *
gst_registry_lookup_feature_locked (GstRegistry * registry, const char *name)
{
  GList *g;
  GstPluginFeature *feature;

  if (name == NULL)
    return NULL;

  for (g = registry->features; g; g = g_list_next (g)) {
    feature = GST_PLUGIN_FEATURE (g->data);
    if (feature->name && strcmp (name, feature->name) == 0) {
      return feature;
    }
  }

  return NULL;
}

GstPluginFeature *
gst_registry_lookup_feature (GstRegistry * registry, const char *name)
{
  GstPluginFeature *feature;

  GST_LOCK (registry);
  feature = gst_registry_lookup_feature_locked (registry, name);
  if (feature)
    gst_object_ref (feature);
  GST_UNLOCK (registry);

  return feature;
}

static GstPlugin *
gst_registry_lookup_locked (GstRegistry * registry, const char *filename)
{
  GList *g;
  GstPlugin *plugin;

  if (filename == NULL)
    return NULL;

  for (g = registry->plugins; g; g = g_list_next (g)) {
    plugin = GST_PLUGIN (g->data);
    if (plugin->filename && strcmp (filename, plugin->filename) == 0) {
      return plugin;
    }
  }

  return NULL;
}

GstPlugin *
gst_registry_lookup (GstRegistry * registry, const char *filename)
{
  GstPlugin *plugin;

  GST_LOCK (registry);
  plugin = gst_registry_lookup_locked (registry, filename);
  if (plugin)
    gst_object_ref (plugin);
  GST_UNLOCK (registry);

  return plugin;
}

static void
gst_registry_scan_path_level (GstRegistry * registry, const gchar * path,
    int level)
{
  GDir *dir;
  const gchar *dirent;
  gchar *filename;
  GstPlugin *plugin;
  GstPlugin *newplugin;

  dir = g_dir_open (path, 0, NULL);
  if (!dir)
    return;

  while ((dirent = g_dir_read_name (dir))) {
    filename = g_strjoin ("/", path, dirent, NULL);

    GST_DEBUG ("examining file: %s", filename);

    if (g_file_test (filename, G_FILE_TEST_IS_DIR)) {
      if (level > 0) {
        GST_DEBUG ("found directory, recursing");
        gst_registry_scan_path_level (registry, filename, level - 1);
      } else {
        GST_DEBUG ("found directory, but recursion level is too deep");
      }
      g_free (filename);
      continue;
    }
    if (!g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
      GST_DEBUG ("not a regular file, ignoring");
      g_free (filename);
      continue;
    }
    if (!g_str_has_suffix (filename, ".so") &&
        !g_str_has_suffix (filename, ".dll") &&
        !g_str_has_suffix (filename, ".dynlib")) {
      GST_DEBUG ("extension is not recognized as module file, ignoring");
      g_free (filename);
      continue;
    }

    plugin = gst_registry_lookup (registry, filename);
    if (plugin) {
      struct stat file_status;

      if (stat (filename, &file_status)) {
        /* FIXME remove from cache */
        g_free (filename);
        continue;
      }

      if (plugin->file_mtime == file_status.st_mtime &&
          plugin->file_size == file_status.st_size) {
        GST_DEBUG ("file %s cached", filename);
        plugin->flags &= ~GST_PLUGIN_FLAG_CACHED;
      } else {
        GST_DEBUG ("cached info for %s is stale", filename);
        GST_DEBUG ("mtime %ld != %ld or size %" G_GSIZE_FORMAT " != %"
            G_GSIZE_FORMAT, plugin->file_mtime, file_status.st_mtime,
            plugin->file_size, file_status.st_size);
        gst_registry_remove_plugin (gst_registry_get_default (), plugin);
        newplugin = gst_plugin_load_file (filename, NULL);
        if (newplugin)
          gst_object_unref (newplugin);
      }
      gst_object_unref (plugin);

    } else {
      newplugin = gst_plugin_load_file (filename, NULL);
      if (newplugin)
        gst_object_unref (newplugin);
    }

    g_free (filename);
  }

  g_dir_close (dir);
}

/**
 * gst_registry_scan_path:
 * @registry: the registry to add the path to
 * @path: the path to add to the registry
 *
 * Add the given path to the registry. The syntax of the
 * path is specific to the registry. If the path has already been
 * added, do nothing.
 */
void
gst_registry_scan_path (GstRegistry * registry, const gchar * path)
{
  gst_registry_scan_path_level (registry, path, 10);
}

void
_gst_registry_remove_cache_plugins (GstRegistry * registry)
{
  GList *g;
  GList *g_next;
  GstPlugin *plugin;

  g = registry->plugins;
  while (g) {
    g_next = g->next;
    plugin = g->data;
    if (plugin->flags & GST_PLUGIN_FLAG_CACHED) {
      registry->plugins = g_list_remove (registry->plugins, plugin);
      //gst_object_unref (plugin);
    }
    g = g_next;
  }
}


static gboolean
_gst_plugin_feature_filter_plugin_name (GstPluginFeature * feature,
    gpointer user_data)
{
  return (strcmp (feature->plugin_name, (gchar *) user_data) == 0);
}

GList *
gst_registry_get_feature_list_by_plugin (GstRegistry * registry,
    const gchar * name)
{
  return gst_registry_feature_filter (registry,
      _gst_plugin_feature_filter_plugin_name, FALSE, (gpointer) name);
}

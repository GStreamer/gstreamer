/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstplugin.c: Plugin subsystem for loading elements, types, and libs
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

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "gst_private.h"
#include "gstplugin.h"
#include "gstversion.h"
#include "gstregistry.h"
#include "config.h"

static GModule *main_module;

GList *_gst_plugin_static = NULL;

static void 		gst_plugin_register_statics 	(GModule *module);
static GstPlugin* 	gst_plugin_register_func 	(GstPluginDesc *desc, GstPlugin *plugin, 
							 GModule *module);
GQuark 
gst_plugin_error_quark (void)
{
  static GQuark quark = 0;
  if (!quark)
    quark = g_quark_from_static_string ("gst_plugin_error");
  return quark;
}

void
_gst_plugin_initialize (void)
{
  main_module =  g_module_open (NULL, G_MODULE_BIND_LAZY);
  gst_plugin_register_statics (main_module);
}

void
_gst_plugin_register_static (GstPluginDesc *desc)
{
  _gst_plugin_static = g_list_prepend (_gst_plugin_static, desc);
}

static void
gst_plugin_register_statics (GModule *module)
{
  GList *walk = _gst_plugin_static;

  while (walk) {
    GstPluginDesc *desc = (GstPluginDesc *) walk->data;
    GstPlugin *plugin;

    plugin = g_new0 (GstPlugin, 1);
    plugin->filename = NULL;
    plugin->module = NULL;
    plugin = gst_plugin_register_func (desc, plugin, module);

    if (plugin) {
      plugin->module = module;
      gst_registry_pool_add_plugin (plugin);
    }
    
    walk = g_list_next (walk);
  }
}

static gboolean
gst_plugin_check_version (gint major, gint minor)
{
  /* return NULL if the major and minor version numbers are not compatible */
  /* with ours. */
  if (major != GST_VERSION_MAJOR || minor != GST_VERSION_MINOR) 
    return FALSE;

  return TRUE;
}

static GstPlugin*
gst_plugin_register_func (GstPluginDesc *desc, GstPlugin *plugin, GModule *module)
{
  if (!gst_plugin_check_version (desc->major_version, desc->minor_version)) {
    GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" has incompatible version, not loading",
       plugin->filename);
    return NULL;
  }

  g_free (plugin->name);
  plugin->name = g_strdup(desc->name);

  if (!((desc->plugin_init) (module, plugin))) {
    GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" failed to initialise",
       plugin->filename);
    return NULL;
  }
  GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" initialised", plugin->filename);

  return plugin;
}

GstPlugin*
gst_plugin_new (const gchar *filename)
{
  GstPlugin *plugin = g_new0 (GstPlugin, 1);
  plugin->filename = g_strdup (filename);

  return plugin;
}

/**
 * gst_plugin_load_plugin:
 * @plugin: The plugin to load
 * @error: Pointer to a NULL-valued GError.
 *
 * Load the given plugin.
 *
 * Returns: whether or not the plugin loaded. Sets @error as appropriate.
 */
gboolean
gst_plugin_load_plugin (GstPlugin *plugin, GError **error)
{
  GModule *module;
  GstPluginDesc *desc;
  struct stat file_status;
  gchar *filename;

  g_return_val_if_fail (plugin != NULL, FALSE);

  if (plugin->module) 
    return TRUE;

  filename = plugin->filename;

  GST_DEBUG (GST_CAT_PLUGIN_LOADING, "attempt to load plugin \"%s\"", filename);

  if (g_module_supported () == FALSE) {
    g_set_error (error,
                 GST_PLUGIN_ERROR,
                 GST_PLUGIN_ERROR_MODULE,
                 "Dynamic loading not supported");
    return FALSE;
  }

  if (stat (filename, &file_status)) {
    g_set_error (error,
                 GST_PLUGIN_ERROR,
                 GST_PLUGIN_ERROR_MODULE,
                 "Problem opening file %s (plugin %s)\n",
                 filename, plugin->name); 
    return FALSE;
  }

  module = g_module_open (filename, G_MODULE_BIND_LAZY);

  if (module != NULL) {
    if (g_module_symbol (module, "plugin_desc", (gpointer *)&desc)) {
      GST_DEBUG (GST_CAT_PLUGIN_LOADING, "plugin \"%s\" loaded, called entry function...", filename);

      plugin->filename = g_strdup (filename);
      plugin = gst_plugin_register_func (desc, plugin, module);

      if (plugin != NULL) {
        GST_INFO (GST_CAT_PLUGIN_LOADING, "plugin \"%s\" loaded", plugin->filename);
        plugin->module = module;
        return TRUE;
      }
    }
    else {
      g_set_error (error,
                   GST_PLUGIN_ERROR,
                   GST_PLUGIN_ERROR_MODULE,
                   "Could not find plugin_desc in \"%s\"",
                   filename);
    }
    return FALSE;
  } 
  else {
    g_set_error (error,
                 GST_PLUGIN_ERROR,
                 GST_PLUGIN_ERROR_MODULE,
                 "Error loading plugin %s, reason: %s\n",
                 filename, g_module_error());
  }

  return FALSE;
}


/**
 * gst_plugin_unload_plugin:
 * @plugin: The plugin to unload
 *
 * Unload the given plugin.
 *
 * Returns: whether or not the plugin unloaded
 */
gboolean
gst_plugin_unload_plugin (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, FALSE);

  if (!plugin->module) 
    return TRUE;

  if (g_module_close (plugin->module)) {
    plugin->module = NULL;
    GST_INFO (GST_CAT_PLUGIN_LOADING, "plugin \"%s\" unloaded", plugin->filename);
    return TRUE;
  }
  else {
    GST_INFO (GST_CAT_PLUGIN_LOADING, "failed to unload plugin \"%s\"", plugin->filename);
    return FALSE;
  }
}

/**
 * gst_plugin_get_name:
 * @plugin: plugin to get the name of
 *
 * Get the short name of the plugin
 *
 * Returns: the name of the plugin
 */
const gchar*
gst_plugin_get_name (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->name;
}

/**
 * gst_plugin_set_name:
 * @plugin: plugin to set name of
 * @name: new name
 *
 * Sets the name (should be short) of the plugin.
 */
void
gst_plugin_set_name (GstPlugin *plugin, const gchar *name)
{
  g_return_if_fail (plugin != NULL);

  if (plugin->name)
    g_free (plugin->name);

  plugin->name = g_strdup (name);
}

/**
 * gst_plugin_set_longname:
 * @plugin: plugin to set long name of
 * @longname: new long name
 *
 * Sets the long name (should be descriptive) of the plugin.
 */
void
gst_plugin_set_longname (GstPlugin *plugin, const gchar *longname)
{
  g_return_if_fail(plugin != NULL);

  if (plugin->longname)
    g_free(plugin->longname);

  plugin->longname = g_strdup(longname);
}

/**
 * gst_plugin_get_longname:
 * @plugin: plugin to get long name of
 *
 * Get the long descriptive name of the plugin
 *
 * Returns: the long name of the plugin
 */
const gchar*
gst_plugin_get_longname (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->longname;
}

/**
 * gst_plugin_get_filename:
 * @plugin: plugin to get the filename of
 *
 * get the filename of the plugin
 *
 * Returns: the filename of the plugin
 */
const gchar*
gst_plugin_get_filename (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->filename;
}

/**
 * gst_plugin_is_loaded:
 * @plugin: plugin to query
 *
 * queries if the plugin is loaded into memory
 *
 * Returns: TRUE is loaded, FALSE otherwise
 */
gboolean
gst_plugin_is_loaded (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, FALSE);

  return (plugin->module != NULL);
}

GstPluginFeature*
gst_plugin_find_feature (GstPlugin *plugin, const gchar *name, GType type)
{
  GList *features = plugin->features;

  g_return_val_if_fail (name != NULL, NULL);

  while (features) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);

    if (!strcmp(GST_PLUGIN_FEATURE_NAME (feature), name) && G_OBJECT_TYPE (feature) == type) {
      return GST_PLUGIN_FEATURE (feature);
    }

    features = g_list_next (features);
  }
  return NULL;
}

/**
 * gst_plugin_add_feature:
 * @plugin: plugin to add feature to
 * @feature: feature to add
 *
 * Add feature to the list of those provided by the plugin.
 * There is a separate namespace for each plugin feature type.
 * See #gst_plugin_get_feature_list
 */
void
gst_plugin_add_feature (GstPlugin *plugin, GstPluginFeature *feature)
{
  GstPluginFeature *oldfeature;

  g_return_if_fail (plugin != NULL);
  g_return_if_fail (GST_IS_PLUGIN_FEATURE (feature));
  g_return_if_fail (feature != NULL);

  oldfeature = gst_plugin_find_feature (plugin, 
		  GST_PLUGIN_FEATURE_NAME (feature), G_OBJECT_TYPE (feature));

  if (!oldfeature) {
    feature->manager = plugin;
    plugin->features = g_list_prepend (plugin->features, feature);
    plugin->numfeatures++;
  }
}

/**
 * gst_plugin_get_feature_list:
 * @plugin: the plugin to get the features from
 *
 * get a list of all the features that this plugin provides
 *
 * Returns: a GList of features
 */
GList*
gst_plugin_get_feature_list (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->features;
}

/**
 * gst_plugin_load:
 * @name: name of plugin to load
 *
 * Load the named plugin.  
 *
 * Returns: whether the plugin was loaded or not
 */
gboolean
gst_plugin_load (const gchar *name)
{
  GstPlugin *plugin;

  plugin = gst_registry_pool_find_plugin (name);
  if (plugin)
    return gst_plugin_load_plugin (plugin, NULL);

  return FALSE;
}

/**
 * gst_library_load:
 * @name: name of library to load
 *
 * Load the named library.  Name should be given as
 * &quot;liblibrary.so&quot;.
 *
 * Returns: whether the library was loaded or not
 */
gboolean
gst_library_load (const gchar *name)
{
  gboolean res;

  /* for now this is the same */
  res = gst_plugin_load(name);

  return res;
}


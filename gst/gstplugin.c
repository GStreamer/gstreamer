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
#include "gstregistrypool.h"
#include "gstlog.h"
#include "config.h"
#include "gstfilter.h"

static GModule *main_module = NULL;
static GList *_gst_plugin_static = NULL;

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

/* this function can be called in the GCC constructor extension, before
 * the _gst_plugin_initialize() was called. In that case, we store the 
 * plugin description in a list to initialize it when we open the main
 * module later on.
 * When the main module is known, we can register the plugin right away.
 * */
void
_gst_plugin_register_static (GstPluginDesc *desc)
{
  if (main_module == NULL) {
    _gst_plugin_static = g_list_prepend (_gst_plugin_static, desc);
  }
  else {
    GstPlugin *plugin;

    plugin = g_new0 (GstPlugin, 1);
    plugin->filename = NULL;
    plugin->module = NULL;
    plugin = gst_plugin_register_func (desc, plugin, main_module);

    if (plugin) {
      plugin->module = main_module;
      gst_registry_pool_add_plugin (plugin);
    }
  }
}

void
_gst_plugin_initialize (void)
{
  main_module =  g_module_open (NULL, G_MODULE_BIND_LAZY);

  /* now register all static plugins */
  g_list_foreach (_gst_plugin_static, (GFunc) _gst_plugin_register_static, NULL);
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
  GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" initialised", GST_STR_NULL (plugin->filename));

  return plugin;
}

/**
 * gst_plugin_new:
 * @filename: The filename of the plugin
 *
 * Creates a plugin from the given filename
 *
 * Returns: A new GstPlugin object
 */
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
      else {
	/* plugin == NULL */
        g_set_error (error,
                     GST_PLUGIN_ERROR,
                     GST_PLUGIN_ERROR_MODULE,
                     "gst_plugin_register_func failed for plugin \"%s\"",
                     filename);
        return FALSE;
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
    return FALSE;
  }
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

/**
 * gst_plugin_feature_list:
 * @plugin: plugin to query
 * @filter: the filter to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugin features and returns a GList with
 * the results. If the first flag is set, only the first match is 
 * returned (as a list with a single object).
 *
 * Returns: a GList of features, g_list_free after use.
 */
GList*
gst_plugin_feature_filter (GstPlugin *plugin,
		           GstPluginFeatureFilter filter,
			   gboolean first,
			   gpointer user_data)
{
  return gst_filter_run (plugin->features, (GstFilterFunc) filter, first, user_data);
}

typedef struct
{ 
  GstPluginFeatureFilter filter;
  gboolean               first;
  gpointer               user_data;
  GList                 *result;
} FeatureFilterData;

static gboolean
_feature_filter (GstPlugin *plugin, gpointer user_data)
{
  GList *result;
  FeatureFilterData *data = (FeatureFilterData *) user_data;

  result = gst_plugin_feature_filter (plugin, data->filter, data->first, data->user_data);
  if (result) {
    data->result = g_list_concat (data->result, result);
    return TRUE;
  }
  return FALSE;
}

/**
 * gst_plugin_list_feature_list:
 * @list: a list of plugins to query
 * @filter: the filter to use
 * @first: only return first match
 * @user_data: user data passed to the filter function
 *
 * Runs a filter against all plugin features of the plugins in the given
 * list and returns a GList with the results. 
 * If the first flag is set, only the first match is 
 * returned (as a list with a single object).
 *
 * Returns: a GList of features, g_list_free after use.
 */
GList*
gst_plugin_list_feature_filter  (GList *list, 
		                 GstPluginFeatureFilter filter,
			         gboolean first,
			         gpointer user_data)
{
  FeatureFilterData data;
  GList *result;

  data.filter = filter;
  data.first = first;
  data.user_data = user_data;
  data.result = NULL;

  result = gst_filter_run (list, (GstFilterFunc) _feature_filter, first, &data);
  g_list_free (result);

  return data.result;
}

/**
 * gst_plugin_name_filter:
 * @plugin: the plugin to check
 * @name: the name of the plugin
 *
 * A standard filterthat returns TRUE when the plugin is of the
 * given name.
 *
 * Returns: TRUE if the plugin is of the given name.
 */
gboolean
gst_plugin_name_filter (GstPlugin *plugin, const gchar *name)
{
  return (plugin->name && !strcmp (plugin->name, name));
}

/**
 * gst_plugin_find_feature:
 * @plugin: plugin to get the feature from
 * @name: The name of the feature to find
 * @type: The type of the feature to find
 *
 * Find a feature of the given name and type in the given plugin.
 *
 * Returns: a GstPluginFeature or NULL if the feature was not found.
 */
GstPluginFeature*
gst_plugin_find_feature (GstPlugin *plugin, const gchar *name, GType type)
{
  GList *walk;
  GstPluginFeature *result = NULL;
  GstTypeNameData data;

  g_return_val_if_fail (name != NULL, NULL);

  data.type = type;
  data.name = name;
  
  walk = gst_filter_run (plugin->features, 
		         (GstFilterFunc) gst_plugin_feature_type_name_filter, TRUE,
		         &data);

  if (walk) 
    result = GST_PLUGIN_FEATURE (walk->data);

  return result;
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
 * Returns: a GList of features, use g_list_free to free the list.
 */
GList*
gst_plugin_get_feature_list (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return g_list_copy (plugin->features);
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
  GError *error = NULL;

  plugin = gst_registry_pool_find_plugin (name);
  if (plugin) {
    gboolean result = gst_plugin_load_plugin (plugin, &error);
    if (error) {
      GST_DEBUG (GST_CAT_PLUGIN_LOADING, "load_plugin error: %s\n",
	         error->message);
      g_error_free (error);
    }
    return result;
  }

  GST_DEBUG (GST_CAT_PLUGIN_LOADING, "Could not find %s in registry pool",
             name);

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
  res = gst_plugin_load (name);

  return res;
}

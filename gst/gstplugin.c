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
#include <signal.h>

#include "gst_private.h"

#include "gstplugin.h"
#include "gstversion.h"
#include "gstregistrypool.h"
#include "gstinfo.h"
#include "config.h"
#include "gstfilter.h"


#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT GST_CAT_PLUGIN_LOADING
#else
#define GST_CAT_DEFAULT 0
#endif

static GModule *main_module = NULL;
static GList *_gst_plugin_static = NULL;

/* static variables for segfault handling of plugin loading */
static char *_gst_plugin_fault_handler_filename = NULL;
extern gboolean *_gst_disable_segtrap; /* see gst.c */
static gboolean *_gst_plugin_fault_handler_is_setup = FALSE;

/* list of valid licenses.
 * One of these must be specified or the plugin won't be loaded 
 * Contact gstreamer-devel@lists.sourceforge.net if your license should be 
 * added.
 *
 * GPL: http://www.gnu.org/copyleft/gpl.html
 * LGPL: http://www.gnu.org/copyleft/lesser.html
 * QPL: http://www.trolltech.com/licenses/qpl.html
 */
static gchar *valid_licenses[] = {
  "LGPL",			/* GNU Lesser General Public License */
  "GPL",			/* GNU General Public License */
  "QPL",			/* Trolltech Qt Public License */
  "GPL/QPL",			/* Combi-license of GPL + QPL */
  GST_LICENSE_UNKNOWN,		/* some other license */
  NULL
};

static void		gst_plugin_desc_copy		(GstPluginDesc *dest, 
							 const GstPluginDesc *src);

static GstPlugin *	gst_plugin_register_func 	(GstPlugin *plugin, 
							 GModule *module,
							 GstPluginDesc *desc);

static GstPlugin *
gst_plugin_copy (GstPlugin *plugin)
{
  return g_memdup(plugin, sizeof(*plugin));
}

GType
gst_plugin_get_type (void)
{
  static GType plugin_type;

  if (plugin_type == 0) {
    plugin_type = g_boxed_type_register_static ("GstPlugin",
        (GBoxedCopyFunc) gst_plugin_copy, g_free);
  }

  return plugin_type;
}

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
    if (GST_CAT_DEFAULT) GST_LOG ("queueing static plugin \"%s\" for loading later on", desc->name);
    _gst_plugin_static = g_list_prepend (_gst_plugin_static, desc);
  }
  else {
    GstPlugin *plugin;

    if (GST_CAT_DEFAULT) GST_LOG ("attempting to load static plugin \"%s\" now...", desc->name);
    plugin = g_new0 (GstPlugin, 1);
    if (gst_plugin_register_func (plugin, main_module, desc)) {
      if (GST_CAT_DEFAULT) GST_INFO ("loaded static plugin \"%s\"", desc->name);
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

/* this function could be extended to check if the plugin license matches the 
 * applications license (would require the app to register its license somehow).
 * We'll wait for someone who's interested in it to code it :)
 */
static gboolean
gst_plugin_check_license (const gchar *license)
{
  gchar **check_license = valid_licenses;

  g_assert (check_license);
  
  while (*check_license) {
    if (strcmp (license, *check_license) == 0)
      return TRUE;
    check_license++;
  }
  return FALSE;
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
gst_plugin_register_func (GstPlugin *plugin, GModule *module, GstPluginDesc *desc)
{
  g_assert (plugin->module == NULL);

  if (!gst_plugin_check_version (desc->major_version, desc->minor_version)) {
    if (GST_CAT_DEFAULT) GST_INFO ("plugin \"%s\" has incompatible version, not loading",
       plugin->filename);
    return FALSE;
  }

  if (!desc->license || !desc->description || !desc->package ||
      !desc->origin) {
    if (GST_CAT_DEFAULT) GST_INFO ("plugin \"%s\" has incorrect GstPluginDesc, not loading",
       plugin->filename);
    return FALSE;
  }
      
  if (!gst_plugin_check_license (desc->license)) {
    if (GST_CAT_DEFAULT) GST_INFO ("plugin \"%s\" has invalid license \"%s\", not loading",
       plugin->filename, desc->license);
    return FALSE;
  }
  
  gst_plugin_desc_copy (&plugin->desc, desc);
  plugin->module = module;

  if (!((desc->plugin_init) (plugin))) {
    if (GST_CAT_DEFAULT) GST_INFO ("plugin \"%s\" failed to initialise", plugin->filename);
    plugin->module = NULL;
    return FALSE;
  }
  
  if (GST_CAT_DEFAULT) GST_DEBUG ("plugin \"%s\" initialised", GST_STR_NULL (plugin->filename));

  return plugin;
}

/*
 * _gst_plugin_fault_handler_restore:
 * segfault handler restorer
 */
static void
_gst_plugin_fault_handler_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGSEGV, &action, NULL);
}

/*
 * _gst_plugin_fault_handler_sighandler:
 * segfault handler implementation
 */
static void
_gst_plugin_fault_handler_sighandler (int signum)
{
  /* We need to restore the fault handler or we'll keep getting it */
  _gst_plugin_fault_handler_restore ();

  switch (signum)
  {
    case SIGSEGV:
      g_print ("\nERROR: ");
      g_print ("Caught a segmentation fault while loading plugin file:\n");
      g_print ("%s\n\n", _gst_plugin_fault_handler_filename);
      g_print ("Please either:\n");
      g_print ("- remove it and restart.\n");
      g_print ("- run with --gst-disable-segtrap and debug.\n");
      exit (-1);
      break;
    default:
      g_print ("Caught unhandled signal on plugin loading\n");
      break;
  }
}

/*
 * _gst_plugin_fault_handler_setup:
 * sets up the segfault handler
 */
static void
_gst_plugin_fault_handler_setup (void)
{
  struct sigaction action;

  /* if asked to leave segfaults alone, just return */
  if (_gst_disable_segtrap) return;

  if (_gst_plugin_fault_handler_is_setup) return;

  memset (&action, 0, sizeof (action));
  action.sa_handler = _gst_plugin_fault_handler_sighandler;

  sigaction (SIGSEGV, &action, NULL);
}

static void
_gst_plugin_fault_handler_setup ();

/**
 * gst_plugin_load_file:
 * @plugin: The plugin to load
 * @error: Pointer to a NULL-valued GError.
 *
 * Load the given plugin.
 *
 * Returns: a new GstPlugin or NULL, if an error occurred.
 */
GstPlugin *
gst_plugin_load_file (const gchar *filename, GError **error)
{
  GstPlugin *plugin;
  GModule *module;
  GstPluginDesc *desc;
  struct stat file_status;
  gboolean free_plugin;

  g_return_val_if_fail (filename != NULL, NULL);

  GST_CAT_DEBUG (GST_CAT_PLUGIN_LOADING, "attempt to load plugin \"%s\"", filename);

  if (g_module_supported () == FALSE) {
    g_set_error (error,
                 GST_PLUGIN_ERROR,
                 GST_PLUGIN_ERROR_MODULE,
                 "Dynamic loading not supported");
    return NULL;
  }

  if (stat (filename, &file_status)) {
    g_set_error (error,
                 GST_PLUGIN_ERROR,
                 GST_PLUGIN_ERROR_MODULE,
                 "Problem opening file %s\n",
                 filename);
    return NULL;
  }

  module = g_module_open (filename, G_MODULE_BIND_LAZY);

  if (module != NULL) {
    gpointer ptr;

    if (g_module_symbol (module, "gst_plugin_desc", &ptr)) {
      desc = (GstPluginDesc *) ptr;

      plugin = gst_registry_pool_find_plugin (desc->name);
      if (!plugin) {
	free_plugin = TRUE;
	plugin = g_new0 (GstPlugin, 1);
	plugin->filename = g_strdup (filename);
	GST_DEBUG ("created new GstPlugin %p for file \"%s\"", plugin, filename);
      } else {
	free_plugin = FALSE;
	if (gst_plugin_is_loaded (plugin)) {
	  if (strcmp (plugin->filename, filename) != 0) {
	    GST_WARNING ("plugin %p from file \"%s\" with same name %s is already "
			 "loaded, aborting loading of \"%s\"", 
			 plugin, plugin->filename, plugin->desc.name, filename);
	    g_set_error (error,
			 GST_PLUGIN_ERROR,
			 GST_PLUGIN_ERROR_NAME_MISMATCH,
			 "already a plugin with name \"%s\" loaded",
			 desc->name);
	    if (free_plugin) g_free (plugin);
	    return NULL;
	  }
	  GST_LOG ("Plugin %p for file \"%s\" already loaded, returning it now", plugin, filename);
	  return plugin;
	}
      }
      GST_LOG ("Plugin %p for file \"%s\" prepared, calling entry function...", plugin, filename);

      if (g_module_symbol (module, "plugin_init", &ptr)) {
        g_print ("plugin %p from file \"%s\" exports a symbol named plugin_init\n",
                     plugin, plugin->filename);
        g_set_error (error,
                     GST_PLUGIN_ERROR,
                     GST_PLUGIN_ERROR_NAME_MISMATCH,
                     "plugin \"%s\" exports a symbol named plugin_init",
                     desc->name);
      }

      /* this is where we load the actual .so, so let's trap SIGSEGV */
      _gst_plugin_fault_handler_setup ();
      _gst_plugin_fault_handler_filename = plugin->filename;

      if (gst_plugin_register_func (plugin, module, desc)) {
        /* remove signal handler */
        _gst_plugin_fault_handler_restore ();
        _gst_plugin_fault_handler_filename = NULL;
        GST_INFO ("plugin \"%s\" loaded", plugin->filename);
        return plugin;
      } else {
        /* remove signal handler */
        _gst_plugin_fault_handler_restore ();
        GST_DEBUG ("gst_plugin_register_func failed for plugin \"%s\"", filename);
	/* plugin == NULL */
        g_set_error (error,
                     GST_PLUGIN_ERROR,
                     GST_PLUGIN_ERROR_MODULE,
                     "gst_plugin_register_func failed for plugin \"%s\"",
                     filename);
	if (free_plugin) g_free (plugin);
        return NULL;
      }
    } else {
      GST_DEBUG ("Could not find plugin entry point in \"%s\"", filename);
      g_set_error (error,
                   GST_PLUGIN_ERROR,
                   GST_PLUGIN_ERROR_MODULE,
                   "Could not find plugin entry point in \"%s\"",
                   filename);
    }
    return NULL;
  } else {
    GST_DEBUG ("Error loading plugin %s, reason: %s\n", filename, g_module_error());
    g_set_error (error,
                 GST_PLUGIN_ERROR,
                 GST_PLUGIN_ERROR_MODULE,
                 "Error loading plugin %s, reason: %s\n",
                 filename, g_module_error());
    return NULL;
  }
}

static void
gst_plugin_desc_copy (GstPluginDesc *dest, const GstPluginDesc *src)
{
  dest->major_version = src->major_version;
  dest->minor_version = src->minor_version;
  g_free (dest->name);
  dest->name = g_strdup (src->name);
  g_free (dest->description);
  dest->description = g_strdup (src->description);
  dest->plugin_init = src->plugin_init;
  dest->plugin_exit = src->plugin_exit;
  g_free (dest->version);
  dest->version = g_strdup (src->version);
  g_free (dest->license);
  dest->license = g_strdup (src->license);
  g_free (dest->package);
  dest->package = g_strdup (src->package);
  g_free (dest->origin);
  dest->origin = g_strdup (src->origin);
}
#if 0
/* unused */
static void
gst_plugin_desc_free (GstPluginDesc *desc)
{
  g_free (desc->name);
  g_free (desc->description);
  g_free (desc->version);
  g_free (desc->license);
  g_free (desc->package);
  g_free (desc->origin);

  memset (desc, 0, sizeof (GstPluginDesc));
}
#endif
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
    GST_CAT_INFO (GST_CAT_PLUGIN_LOADING, "plugin \"%s\" unloaded", plugin->filename);
    return TRUE;
  }
  else {
    GST_CAT_INFO (GST_CAT_PLUGIN_LOADING, "failed to unload plugin \"%s\"", plugin->filename);
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

  return plugin->desc.name;
}

/**
 * gst_plugin_get_description:
 * @plugin: plugin to get long name of
 *
 * Get the long descriptive name of the plugin
 *
 * Returns: the long name of the plugin
 */
G_CONST_RETURN gchar*
gst_plugin_get_description (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.description;
}

/**
 * gst_plugin_get_filename:
 * @plugin: plugin to get the filename of
 *
 * get the filename of the plugin
 *
 * Returns: the filename of the plugin
 */
G_CONST_RETURN gchar*
gst_plugin_get_filename (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->filename;
}
/**
 * gst_plugin_get_license:
 * @plugin: plugin to get the license of
 *
 * get the license of the plugin
 *
 * Returns: the license of the plugin
 */
G_CONST_RETURN gchar*
gst_plugin_get_license (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.license;
}
/**
 * gst_plugin_get_package:
 * @plugin: plugin to get the package of
 *
 * get the package the plugin belongs to.
 *
 * Returns: the package of the plugin
 */
G_CONST_RETURN gchar*
gst_plugin_get_package (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.package;
}
/**
 * gst_plugin_get_origin:
 * @plugin: plugin to get the origin of
 *
 * get the URL where the plugin comes from
 *
 * Returns: the origin of the plugin
 */
G_CONST_RETURN gchar*
gst_plugin_get_origin (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, NULL);

  return plugin->desc.origin;
}
/**
 * gst_plugin_get_module:
 * @plugin: plugin to query
 *
 * Gets the #GModule of the plugin. If the plugin isn't loaded yet, NULL is 
 * returned.
 *
 * Returns: module belonging to the plugin or NULL if the plugin isn't 
 *          loaded yet.
 */
GModule *
gst_plugin_get_module (GstPlugin *plugin)
{
  g_return_val_if_fail (plugin != NULL, FALSE);

  return plugin->module;
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
 * A standard filter that returns TRUE when the plugin is of the
 * given name.
 *
 * Returns: TRUE if the plugin is of the given name.
 */
gboolean
gst_plugin_name_filter (GstPlugin *plugin, const gchar *name)
{
  return (plugin->desc.name && !strcmp (plugin->desc.name, name));
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
    gst_plugin_load_file (plugin->filename, &error);
    if (error) {
      GST_WARNING ("load_plugin error: %s\n", error->message);
      g_error_free (error);
      return FALSE;
    }
    return TRUE;;
  }

  GST_DEBUG ("Could not find %s in registry pool", name);
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

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
#include "config.h"

static GModule *main_module;

GList *_gst_plugin_static = NULL;

/* global list of plugins and its sequence number */
GList *_gst_plugins = NULL;
gint _gst_plugins_seqno = 0;
/* list of paths to check for plugins */
GList *_gst_plugin_paths = NULL;

GList *_gst_libraries = NULL;
gint _gst_libraries_seqno = 0;

/* whether or not to spew library load issues */
gboolean _gst_plugin_spew = FALSE;

/* whether or not to warn if registry needs rebuild (gstreamer-register sets
 * this to false.) */
gboolean _gst_warn_old_registry = TRUE;

#ifndef GST_DISABLE_REGISTRY
static gboolean 	plugin_times_older_than		(time_t regtime);
static time_t 		get_time			(const char * path);
#endif
static void 		gst_plugin_register_statics 	(GModule *module);
static GstPlugin* 	gst_plugin_register_func 	(GstPluginDesc *desc, GstPlugin *plugin, 
							 GModule *module);

void
_gst_plugin_initialize (void)
{
#ifndef GST_DISABLE_REGISTRY
  xmlDocPtr doc;
#endif

  main_module =  g_module_open (NULL, G_MODULE_BIND_LAZY);
  gst_plugin_register_statics (main_module);

  /* add the main (installed) library path */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths, PLUGINS_DIR);

  /* if this is set, we add build-directory paths to the list */
#ifdef PLUGINS_USE_BUILDDIR
  /* the catch-all plugins directory */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_BUILDDIR "/plugins");
  /* the libreary directory */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_BUILDDIR "/libs");
  /* location libgstelements.so */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_BUILDDIR "/gst/elements");
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_BUILDDIR "/gst/types");
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_BUILDDIR "/gst/autoplug");
#endif /* PLUGINS_USE_BUILDDIR */

#ifndef GST_DISABLE_REGISTRY
  doc = xmlParseFile (GST_CONFIG_DIR"/reg.xml");

  if (!doc || 
      !doc->xmlRootNode ||
      doc->xmlRootNode->name == 0 ||
      strcmp (doc->xmlRootNode->name, "GST-PluginRegistry") ||
      !plugin_times_older_than(get_time(GST_CONFIG_DIR"/reg.xml"))) {
    if (_gst_warn_old_registry)
	g_warning ("gstplugin: registry needs rebuild: run gstreamer-register\n");
    gst_plugin_load_all ();
    //gst_plugin_unload_all ();
    return;
  }
  gst_plugin_load_thyself (doc->xmlRootNode);

  xmlFreeDoc (doc);
#endif // GST_DISABLE_REGISTRY
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
      _gst_plugins = g_list_prepend (_gst_plugins, plugin);
      _gst_plugins_seqno++;
      plugin->module = module;
    }
    
    walk = g_list_next (walk);
  }
}

/**
 * gst_plugin_add_path:
 * @path: the directory to add to the search path
 *
 * Add a directory to the path searched for plugins.
 */
void
gst_plugin_add_path (const gchar *path)
{
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,g_strdup(path));
}

#ifndef GST_DISABLE_REGISTRY
static time_t
get_time(const char * path)
{
  struct stat statbuf;
  if (stat(path, &statbuf)) return 0;
  if (statbuf.st_mtime > statbuf.st_ctime) return statbuf.st_mtime;
  return statbuf.st_ctime;
}

static gboolean
plugin_times_older_than_recurse(gchar *path, time_t regtime)
{
  DIR *dir;
  struct dirent *dirent;
  gchar *pluginname;

  time_t pathtime = get_time(path);

  if (pathtime > regtime) {
    GST_INFO (GST_CAT_PLUGIN_LOADING,
	       "time for %s was %ld; more recent than registry time of %ld\n",
	       path, (long)pathtime, (long)regtime);
    return FALSE;
  }

  dir = opendir(path);
  if (dir) {
    while ((dirent = readdir(dir))) {
      /* don't want to recurse in place or backwards */
      if (strcmp(dirent->d_name,".") && strcmp(dirent->d_name,"..")) {
	pluginname = g_strjoin("/",path,dirent->d_name,NULL);
	if (!plugin_times_older_than_recurse(pluginname , regtime)) {
          g_free (pluginname);
          closedir(dir);
          return FALSE;
        }
        g_free (pluginname);
      }
    }
    closedir(dir);
  }
  return TRUE;
}

static gboolean
plugin_times_older_than(time_t regtime)
{
  // return true iff regtime is more recent than the times of all the files
  // in the plugin dirs.
  GList *path;
  path = _gst_plugin_paths;
  while (path != NULL) {
    GST_DEBUG (GST_CAT_PLUGIN_LOADING,
	       "comparing plugin times from %s with %ld\n",
	       (gchar *)path->data, (long) regtime);
    if(!plugin_times_older_than_recurse(path->data, regtime))
	return FALSE;
    path = g_list_next(path);
  }
  return TRUE;
}
#endif

static gboolean
gst_plugin_load_recurse (gchar *directory, gchar *name)
{
  DIR *dir;
  struct dirent *dirent;
  gboolean loaded = FALSE;
  gchar *dirname;

  //g_print("recursive load of '%s' in '%s'\n", name, directory);
  dir = opendir(directory);
  if (dir) {
    while ((dirent = readdir(dir))) {
      /* don't want to recurse in place or backwards */
      if (strcmp(dirent->d_name,".") && strcmp(dirent->d_name,"..")) {
        dirname = g_strjoin("/",directory,dirent->d_name,NULL);
        loaded = gst_plugin_load_recurse(dirname,name);
        g_free(dirname);
	if (loaded && name) {
          closedir(dir);
          return TRUE;
        }
      }
    }
    closedir(dir);
  } else {
    if (strstr(directory,".so")) {
      gchar *temp;
      if (name) {
        if ((temp = strstr(directory,name)) &&
            (!strcmp(temp,name))) {
          loaded = gst_plugin_load_absolute(directory);
        }
      } else if ((temp = strstr(directory,".so")) &&
                 (!strcmp(temp,".so"))) {
        loaded = gst_plugin_load_absolute(directory);
      }
    }
  }
  return loaded;
}

/**
 * gst_plugin_load_all:
 *
 * Load all plugins in the path.
 */
void
gst_plugin_load_all (void)
{
  GList *path;

  path = _gst_plugin_paths;
  while (path != NULL) {
    GST_INFO (GST_CAT_PLUGIN_LOADING,"loading plugins from %s",(gchar *)path->data);
    gst_plugin_load_recurse(path->data,NULL);
    path = g_list_next(path);
  }
  GST_INFO (GST_CAT_PLUGIN_LOADING,"loaded %d plugins", _gst_plugins_seqno);
}

void
gst_plugin_unload_all (void)
{
  GList *walk = _gst_plugins;

  while (walk) {
    GstPlugin *plugin = (GstPlugin *) walk->data;

    GST_INFO (GST_CAT_PLUGIN_LOADING, "unloading plugin %s", plugin->name);
    if (plugin->module) {
      GList *features = gst_plugin_get_feature_list (plugin);

      while (features) {
        GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);

	GST_INFO (GST_CAT_PLUGIN_LOADING, "unloading feature %s", GST_OBJECT_NAME (feature));
	gst_plugin_feature_unload_thyself (feature);

	features = g_list_next (features);
      }
      if (g_module_close (plugin->module)) {
        plugin->module = NULL;
      }
      else {
	g_warning ("error closing module");
      }
    }
    
    walk = g_list_next (walk);
  }
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
  GList *libraries = _gst_libraries;

  while (libraries) {
    if (!strcmp((gchar *)libraries->data, name)) return TRUE;

    libraries = g_list_next(libraries);
  }

  // for now this is the same
  res = gst_plugin_load(name);

  if (res) {
    _gst_libraries = g_list_prepend(_gst_libraries, g_strdup (name));
  }

  return res;
}

/**
 * gst_plugin_load:
 * @name: name of plugin to load
 *
 * Load the named plugin.  Name should be given as
 * &quot;libplugin.so&quot;.
 *
 * Returns: whether the plugin was loaded or not
 */
gboolean
gst_plugin_load (const gchar *name)
{
  GList *path;
  gchar *libspath;
  gchar *pluginname;
  GstPlugin *plugin;
  
  g_return_val_if_fail (name != NULL, FALSE);

  plugin = gst_plugin_find (name);
  
  if (plugin && plugin->module) 
    return TRUE;

  path = _gst_plugin_paths;
  while (path != NULL) {
    pluginname = g_module_build_path(path->data,name);
    if (gst_plugin_load_absolute(pluginname)) {
      g_free(pluginname);
      return TRUE;
    }
    g_free(pluginname);
    libspath = g_strconcat(path->data,"/.libs",NULL);
    //g_print("trying to load '%s'\n",g_module_build_path(libspath,name));
    pluginname = g_module_build_path(libspath,name);
    g_free(libspath);
    if (gst_plugin_load_absolute(pluginname)) {
      g_free(pluginname);
      return TRUE;
    }
    g_free(pluginname);
    //g_print("trying to load '%s' from '%s'\n",name,path->data);
    pluginname = g_module_build_path("",name);
    if (gst_plugin_load_recurse(path->data,pluginname)) {
      g_free(pluginname);
      return TRUE;
    }
    g_free(pluginname);
    path = g_list_next(path);
  }
  return FALSE;
}

/**
 * gst_plugin_load:
 * @name: name of plugin to load
 *
 * Load the named plugin.  Name should be given as
 * &quot;libplugin.so&quot;.
 *
 * Returns: whether the plugin was loaded or not
 */
gboolean
gst_plugin_load_absolute (const gchar *filename)
{
  GstPlugin *plugin = NULL;
  GList *plugins = _gst_plugins;

  g_return_val_if_fail (filename != NULL, FALSE);

  GST_INFO (GST_CAT_PLUGIN_LOADING, "plugin \"%s\" absolute loading", filename);

  while (plugins) {
    GstPlugin *testplugin = (GstPlugin *)plugins->data;

    if (testplugin->filename) {
      if (!strcmp (testplugin->filename, filename)) {
	plugin = testplugin;
	break;
      }
    }
    plugins = g_list_next (plugins);
  }
  if (!plugin) {
    plugin = g_new0 (GstPlugin, 1);
    plugin->filename = g_strdup (filename);
    _gst_plugins = g_list_prepend (_gst_plugins, plugin);
    _gst_plugins_seqno++;
  }

  return gst_plugin_load_plugin (plugin);
}

static gboolean
gst_plugin_check_version (gint major, gint minor)
{
  // return NULL if the major and minor version numbers are not compatible
  // with ours.
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
    g_free(plugin);
    return NULL;
  }

  plugin->name = g_strdup(desc->name);

  if (!((desc->plugin_init) (module, plugin))) {
    GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" failed to initialise",
       plugin->filename);
    g_free(plugin);
    return NULL;
  }
  
  return plugin;
}

/**
 * gst_plugin_load_absolute:
 * @name: name of plugin to load
 *
 * Returns: whether or not the plugin loaded
 */
gboolean
gst_plugin_load_plugin (GstPlugin *plugin)
{
  GModule *module;
  GstPluginDesc *desc;
  struct stat file_status;
  gchar *filename;

  g_return_val_if_fail (plugin != NULL, FALSE);

  if (plugin->module) 
    return TRUE;

  filename = plugin->filename;

  GST_INFO (GST_CAT_PLUGIN_LOADING, "plugin \"%s\" loading", filename);

  if (g_module_supported () == FALSE) {
    g_warning("gstplugin: wow, you built this on a platform without dynamic loading???\n");
    return FALSE;
  }

  if (stat (filename, &file_status)) {
    //g_print("problem opening file %s\n",filename);
    return FALSE;
  }

  module = g_module_open (filename, G_MODULE_BIND_LAZY);

  if (module != NULL) {
    if (g_module_symbol (module, "plugin_desc", (gpointer *)&desc)) {
      GST_INFO (GST_CAT_PLUGIN_LOADING,"loading plugin \"%s\"...", filename);

      plugin->filename = g_strdup (filename);
      plugin = gst_plugin_register_func (desc, plugin, module);

      if (plugin != NULL) {
        GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" loaded",
             plugin->filename);
        plugin->module = module;
        return TRUE;
      }
    }
    return TRUE;
  } else if (_gst_plugin_spew) {
    // FIXME this should be some standard gst mechanism!!!
    g_printerr ("error loading plugin %s, reason: %s\n", filename, g_module_error());
  }
  else {
    GST_INFO (GST_CAT_PLUGIN_LOADING, "error loading plugin %s, reason: %s\n", filename, g_module_error());
  }

  return FALSE;
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


/**
 * gst_plugin_find:
 * @name: name of plugin to find
 *
 * Search the list of registered plugins for one of the given name
 *
 * Returns: pointer to the #GstPlugin if found, NULL otherwise
 */
GstPlugin*
gst_plugin_find (const gchar *name)
{
  GList *plugins = _gst_plugins;

  g_return_val_if_fail (name != NULL, NULL);

  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;

    if (plugin->name) {
      if (!strcmp (plugin->name, name)) {
        return plugin;
      }
    }
    plugins = g_list_next (plugins);
  }
  return NULL;
}

static GstPluginFeature*
gst_plugin_find_feature_func (GstPlugin *plugin, const gchar *name, GType type)
{
  GList *features = plugin->features;

  g_return_val_if_fail (name != NULL, NULL);

  while (features) {
    GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);


    if (!strcmp(GST_OBJECT_NAME (feature), name) && G_OBJECT_TYPE (feature) == type)
      return  GST_PLUGIN_FEATURE (feature);

    features = g_list_next (features);
  }

  return NULL;
}

GstPluginFeature*
gst_plugin_find_feature (const gchar *name, GType type)
{
  GList *plugins;

  plugins = _gst_plugins;
  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
    GstPluginFeature *feature;

    feature = gst_plugin_find_feature_func (plugin, name, type);
    if (feature)
      return feature;
    
    plugins = g_list_next(plugins);
  }

  return NULL;
}

/**
 * gst_plugin_add_feature:
 * @plugin: plugin to add feature to
 * @feature: feature to add
 *
 * Add feature to the list of those provided by the plugin.
 */
void
gst_plugin_add_feature (GstPlugin *plugin, GstPluginFeature *feature)
{
  GstPluginFeature *oldfeature;

  g_return_if_fail (plugin != NULL);
  g_return_if_fail (GST_IS_PLUGIN_FEATURE (feature));
  g_return_if_fail (feature != NULL);

  oldfeature = gst_plugin_find_feature_func (plugin, GST_OBJECT_NAME (feature), G_OBJECT_TYPE (feature));

  if (!oldfeature) {
    feature->manager = plugin;
    plugin->features = g_list_prepend (plugin->features, feature);
    plugin->numfeatures++;
  }
}

/**
 * gst_plugin_get_list:
 *
 * get the currently loaded plugins
 *
 * Returns; a GList of GstPlugin elements
 */
GList*
gst_plugin_get_list (void)
{
  return g_list_copy (_gst_plugins);
}

#ifndef GST_DISABLE_REGISTRY
/**
 * gst_plugin_save_thyself:
 * @parent: the parent node to save the plugin to
 *
 * saves the plugin into an XML representation
 *
 * Returns: the new XML node
 */
xmlNodePtr
gst_plugin_save_thyself (xmlNodePtr parent)
{
  xmlNodePtr tree, subtree;
  GList *plugins = NULL;

  plugins = _gst_plugins;
  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
    GList *features;

    plugins = g_list_next (plugins);

    if (!plugin->name) 
      continue;
    
    tree = xmlNewChild (parent, NULL, "plugin", NULL);
    xmlNewChild (tree, NULL, "name", plugin->name);
    xmlNewChild (tree, NULL, "longname", plugin->longname);
    xmlNewChild (tree, NULL, "filename", plugin->filename);

    features = plugin->features;
    while (features) {
      GstPluginFeature *feature = GST_PLUGIN_FEATURE (features->data);

      subtree = xmlNewChild(tree, NULL, "feature", NULL);
      xmlNewProp (subtree, "typename", g_type_name (G_OBJECT_TYPE (feature)));

      gst_object_save_thyself (GST_OBJECT (feature), subtree);

      features = g_list_next (features);
    }
  }
  return parent;
}

/**
 * gst_plugin_load_thyself:
 * @parent: the parent node to load the plugin from
 *
 * load the plugin from an XML representation
 */
void
gst_plugin_load_thyself (xmlNodePtr parent)
{
  xmlNodePtr kinderen;
  gint featurecount = 0;
  gchar *pluginname;

  kinderen = parent->xmlChildrenNode; // Dutch invasion :-)
  while (kinderen) {
    if (!strcmp (kinderen->name, "plugin")) {
      xmlNodePtr field = kinderen->xmlChildrenNode;
      GstPlugin *plugin = g_new0 (GstPlugin, 1);

      plugin->numfeatures = 0;
      plugin->features = NULL;
      plugin->module = NULL;

      while (field) {
	if (!strcmp (field->name, "name")) {
          pluginname = xmlNodeGetContent (field);
	  if (gst_plugin_find (pluginname)) {
            g_free (pluginname);
            g_free (plugin);
	    plugin = NULL;
            break;
	  } else {
	    plugin->name = pluginname;
	  }
	}
	else if (!strcmp (field->name, "longname")) {
	  plugin->longname = xmlNodeGetContent (field);
	}
	else if (!strcmp (field->name, "filename")) {
	  plugin->filename = xmlNodeGetContent (field);
	}
	else if (!strcmp (field->name, "feature")) {
	  GstPluginFeature *feature;
	  gchar *prop;
	  
	  prop = xmlGetProp (field, "typename");
	  feature = GST_PLUGIN_FEATURE (g_object_new (g_type_from_name (prop), NULL));
	  
	  if (feature) {
	    gst_object_restore_thyself (GST_OBJECT (feature), field);
	    gst_plugin_add_feature (plugin, feature);
	    featurecount++;
	  }
	}

	field = field->next;
      }

      if (plugin) {
        _gst_plugins = g_list_prepend (_gst_plugins, plugin);
        _gst_plugins_seqno++;
      }
    }

    kinderen = kinderen->next;
  }
  GST_INFO (GST_CAT_PLUGIN_LOADING, "added %d features ", featurecount);
}
#endif // GST_DISABLE_REGISTRY


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

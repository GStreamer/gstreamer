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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstplugin.h"


//#undef PLUGINS_USE_SRCDIR

/* list of loaded modules and its sequence number */
GList *_gst_modules;
gint _gst_modules_seqno;
/* global list of plugins and its sequence number */
GList *_gst_plugins;
gint _gst_plugins_seqno;
gint _gst_plugin_elementfactories = 0;
gint _gst_plugin_types = 0;
/* list of paths to check for plugins */
GList *_gst_plugin_paths;

GList *_gst_libraries;
gint _gst_libraries_seqno;

/* whether or not to spew library load issues */
gboolean _gst_plugin_spew = FALSE;


void 
_gst_plugin_initialize (void) 
{
  xmlDocPtr doc;
  _gst_modules = NULL;
  _gst_modules_seqno = 0;
  _gst_plugins = NULL;
  _gst_plugins_seqno = 0;
  _gst_plugin_paths = NULL;
  _gst_libraries = NULL;
  _gst_libraries_seqno = 0;


  /* if this is set, we add build-directory paths to the list */
#ifdef PLUGINS_USE_SRCDIR
  /* the catch-all plugins directory */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_SRCDIR "/plugins");
  /* the libreary directory */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_SRCDIR "/libs");
  /* location libgstelements.so */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_SRCDIR "/gst/elements");
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths,
                                      PLUGINS_SRCDIR "/gst/types");
#else
  /* add the main (installed) library path */
  _gst_plugin_paths = g_list_prepend (_gst_plugin_paths, PLUGINS_DIR);
#endif /* PLUGINS_USE_SRCDIR */

  doc = xmlParseFile ("/etc/gstreamer/reg.xml");

  if (!doc || strcmp (doc->root->name, "GST-PluginRegistry")) {
    g_warning ("gstplugin: registry needs rebuild\n");
    gst_plugin_load_all ();
    return;
  }
  gst_plugin_load_thyself (doc->root);
}

static gboolean 
gst_plugin_load_recurse (gchar *directory, gchar *name) 
{
  DIR *dir;
  struct dirent *dirent;
  gboolean loaded = FALSE;

  //g_print("recursive load of '%s' in '%s'\n", name, directory);
  dir = opendir(directory);
  if (dir) {
    while ((dirent = readdir(dir))) {
      /* don't want to recurse in place or backwards */
      if (strcmp(dirent->d_name,".") && strcmp(dirent->d_name,"..")) {
        loaded = gst_plugin_load_recurse(g_strjoin("/",directory,dirent->d_name,
                                              NULL),name);
	if (loaded && name) return TRUE;
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
gst_plugin_load_all(void) 
{
  GList *path;

  path = _gst_plugin_paths;
  while (path != NULL) {
    gst_plugin_load_recurse(path->data,NULL);
    path = g_list_next(path);
  }
  GST_INFO (GST_CAT_PLUGIN_LOADING,"loaded %d plugins with %d elements and %d types",
       _gst_plugins_seqno,_gst_plugin_elementfactories,_gst_plugin_types);
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
gst_library_load (gchar *name) 
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
    _gst_libraries = g_list_prepend(_gst_libraries, name);
  }

  return res;
}

static void 
gst_plugin_remove (GstPlugin *plugin) 
{
  GList *factories;

  factories = plugin->elements;
  while (factories) {
    gst_elementfactory_destroy ((GstElementFactory*)(factories->data));
    factories = g_list_next(factories);
  }
  
  _gst_plugins = g_list_remove(_gst_plugins, plugin);

  // don't free the stuct because someone can have a handle to it
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
gst_plugin_load (gchar *name) 
{
  GList *path;
  gchar *libspath;
  GstPlugin *plugin;

  //g_print("attempting to load plugin '%s'\n",name);

  plugin = gst_plugin_find (name);

  if (plugin && plugin->loaded) return TRUE;

  path = _gst_plugin_paths;
  while (path != NULL) {
    if (gst_plugin_load_absolute(g_module_build_path(path->data,name)))
      return TRUE;
    libspath = g_strconcat(path->data,"/.libs",NULL);
    //g_print("trying to load '%s'\n",g_module_build_path(libspath,name));
    if (gst_plugin_load_absolute(g_module_build_path(libspath,name))) {
      g_free(libspath);
      return TRUE;
    }
    g_free(libspath);
    //g_print("trying to load '%s' from '%s'\n",name,path->data);
    if (gst_plugin_load_recurse(path->data,g_module_build_path("",name))) {
      return TRUE;
    }
    path = g_list_next(path);
  }
  return FALSE;
}

/**
 * gst_plugin_load_absolute:
 * @name: name of plugin to load
 *
 * Returns: whether or not the plugin loaded
 */
gboolean 
gst_plugin_load_absolute (gchar *name) 
{
  GModule *module;
  GstPluginInitFunc initfunc;
  GstPlugin *plugin;
  struct stat file_status;

  if (g_module_supported() == FALSE) {
    g_warning("gstplugin: wow, you built this on a platform without dynamic loading???\n");
    return FALSE;
  }

  if (stat(name,&file_status)) {
//    g_print("problem opening file %s\n",name);
    return FALSE;
  }

  module = g_module_open(name,G_MODULE_BIND_LAZY);
  if (module != NULL) {
    if (g_module_symbol(module,"plugin_init",(gpointer *)&initfunc)) {
      if ((plugin = (initfunc)(module))) {
        GST_INFO (GST_CAT_PLUGIN_LOADING,"plugin \"%s\" loaded: %d elements, %d types",
             plugin->name,plugin->numelements,plugin->numtypes);
        plugin->filename = g_strdup(name);
        plugin->loaded = TRUE;
        _gst_modules = g_list_prepend(_gst_modules,module);
        _gst_modules_seqno++;
        _gst_plugins = g_list_prepend(_gst_plugins,plugin);
        _gst_plugins_seqno++;
        _gst_plugin_elementfactories += plugin->numelements;
        _gst_plugin_types += plugin->numtypes;
        return TRUE;
      }
    }
    return TRUE;
  } else if (_gst_plugin_spew) {
    gst_info("error loading plugin: %s, reason: %s\n", name, g_module_error());
  }

  return FALSE;
}

/**
 * gst_plugin_new:
 * @name: name of new plugin
 *
 * Create a new plugin with given name.
 *
 * Returns: new plugin
 */
GstPlugin*
gst_plugin_new (gchar *name) 
{
  GstPlugin *plugin;

  // return NULL if the plugin is allready loaded
  plugin = gst_plugin_find (name);
  if (plugin) return NULL;

  plugin = (GstPlugin *)g_malloc(sizeof(GstPlugin));

  plugin->name = g_strdup(name);
  plugin->longname = NULL;
  plugin->elements = NULL;
  plugin->numelements = 0;
  plugin->types = NULL;
  plugin->numtypes = 0;
  plugin->loaded = TRUE;

  return plugin;
}

/**
 * gst_plugin_set_longname:
 * @plugin: plugin to set long name of
 * @longname: new long name
 *
 * Sets the long name (should be descriptive) of the plugin.
 */
void 
gst_plugin_set_longname (GstPlugin *plugin, gchar *longname) 
{
  g_return_if_fail(plugin != NULL);

  if (plugin->longname) g_free(plugin->longname);
  plugin->longname = g_strdup(longname);
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

  g_return_val_if_fail(name != NULL, NULL);

  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
//    g_print("plugin name is '%s'\n",plugin->name);
    if (plugin->name) {
      if (!strcmp(plugin->name,name)) {
        return plugin;
      }
    }
    plugins = g_list_next(plugins);
  }
  return NULL;
}

/** 
 * gst_plugin_find_elementfactory:
 * @name: name of elementfactory to find
 *
 * Find a registered elementfactory by name.
 *
 * Returns: @GstElementFactory if found, NULL if not
 */
GstElementFactory*
gst_plugin_find_elementfactory (gchar *name) 
{
  GList *plugins, *factories;
  GstElementFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  plugins = _gst_plugins;
  while (plugins) {
    factories = ((GstPlugin *)(plugins->data))->elements;
    while (factories) {
      factory = (GstElementFactory*)(factories->data);
      if (!strcmp(factory->name, name))
        return (GstElementFactory*)(factory);
      factories = g_list_next(factories);
    }
    plugins = g_list_next(plugins);
  }

  return NULL;
}

/** 
 * gst_plugin_load_elementfactory:
 * @name: name of elementfactory to load
 *
 * Load a registered elementfactory by name.
 *
 * Returns: @GstElementFactory if loaded, NULL if not
 */
GstElementFactory*
gst_plugin_load_elementfactory (gchar *name) 
{
  GList *plugins, *factories;
  GstElementFactory *factory = NULL;
  GstPlugin *plugin;

  g_return_val_if_fail(name != NULL, NULL);

  plugins = _gst_plugins;
  while (plugins) {
    plugin = (GstPlugin *)plugins->data;
    factories = plugin->elements;
    
    while (factories) {
      factory = (GstElementFactory*)(factories->data);
      
      if (!strcmp(factory->name,name)) {
	if (!plugin->loaded) {
          gchar *filename = g_strdup (plugin->filename);
	  gchar *pluginname = g_strdup (plugin->name);
	  
          GST_INFO (GST_CAT_PLUGIN_LOADING,"loaded elementfactory %s from plugin %s",name,plugin->name);
	  gst_plugin_remove(plugin);
	  if (!gst_plugin_load_absolute(filename)) {
	    GST_DEBUG (0,"gstplugin: error loading element factory %s from plugin %s\n", name, pluginname);
	  }
	  g_free (pluginname);
	  g_free (filename);
	}
	factory = gst_plugin_find_elementfactory(name);
        return factory;
      }
      factories = g_list_next(factories);
    }
    plugins = g_list_next(plugins);
  }

  return factory;
}

/** 
 * gst_plugin_load_typefactory:
 * @mime: name of typefactory to load
 *
 * Load a registered typefactory by mime type.
 */
void 
gst_plugin_load_typefactory (gchar *mime) 
{
  GList *plugins, *factories;
  GstTypeFactory *factory;
  GstPlugin *plugin;

  g_return_if_fail (mime != NULL);

  plugins = g_list_copy (_gst_plugins);
  while (plugins) {
    plugin = (GstPlugin *)plugins->data;
    factories = g_list_copy (plugin->types);
    
    while (factories) {
      factory = (GstTypeFactory*)(factories->data);
      
      if (!strcmp(factory->mime,mime)) {
	if (!plugin->loaded) {
          gchar *filename = g_strdup (plugin->filename);
	  gchar *pluginname = g_strdup (plugin->name);
	  
          GST_INFO (GST_CAT_PLUGIN_LOADING,"loading type factory for \"%s\" from plugin %s",mime,plugin->name);
	  plugin->loaded = TRUE;
	  gst_plugin_remove(plugin);
	  if (!gst_plugin_load_absolute(filename)) {
	    GST_DEBUG (0,"gstplugin: error loading type factory \"%s\" from plugin %s\n", mime, pluginname);
	  }
	  g_free (filename);
	  g_free (pluginname);
	}
	//return;
      }
      factories = g_list_next(factories);
    }

    g_list_free (factories);
    plugins = g_list_next(plugins);
  }
  g_list_free (plugins);

  return;
}

/**
 * gst_plugin_add_factory:
 * @plugin: plugin to add factory to
 * @factory: factory to add
 *
 * Add factory to the list of those provided by the plugin.
 */
void 
gst_plugin_add_factory (GstPlugin *plugin, GstElementFactory *factory) 
{
  g_return_if_fail (plugin != NULL);
  g_return_if_fail (factory != NULL);

//  g_print("adding factory to plugin\n");
  plugin->elements = g_list_prepend (plugin->elements, factory);
  plugin->numelements++;
}

/**
 * gst_plugin_add_type:
 * @plugin: plugin to add type to
 * @factory: the typefactory to add
 *
 * Add a typefactory to the list of those provided by the plugin.
 */
void 
gst_plugin_add_type (GstPlugin *plugin, GstTypeFactory *factory) 
{
  g_return_if_fail (plugin != NULL);
  g_return_if_fail (factory != NULL);

//  g_print("adding factory to plugin\n");
  plugin->types = g_list_prepend (plugin->types, factory);
  plugin->numtypes++;
  gst_type_register (factory);
}

/**
 * gst_plugin_get_list:
 *
 * get the currently loaded plugins
 *
 * Returns; a GList of GstPlugin elements
 */
GList*
gst_plugin_get_list(void) 
{
  return _gst_plugins;
}

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
  GList *plugins = NULL, *elements = NULL, *types = NULL;

  plugins = gst_plugin_get_list();
  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
    tree = xmlNewChild(parent,NULL,"plugin",NULL);
    xmlNewChild(tree,NULL,"name",plugin->name);
    xmlNewChild(tree,NULL,"longname",plugin->longname);
    xmlNewChild(tree,NULL,"filename",plugin->filename);
    types = plugin->types;
    while (types) {
      GstTypeFactory *factory = (GstTypeFactory *)types->data;
      subtree = xmlNewChild(tree,NULL,"type",NULL);

      gst_typefactory_save_thyself(factory, subtree);

      types = g_list_next(types);
    }
    elements = plugin->elements;
    while (elements) {
      GstElementFactory *factory = (GstElementFactory *)elements->data;
      subtree = xmlNewChild(tree,NULL,"element",NULL);

      gst_elementfactory_save_thyself(factory, subtree);

      elements = g_list_next(elements);
    }
    plugins = g_list_next(plugins);
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
  gint elementcount = 0;
  gint typecount = 0;
  
  kinderen = parent->childs; // Dutch invasion :-)
  while (kinderen) {
    if (!strcmp(kinderen->name, "plugin")) {
      xmlNodePtr field = kinderen->childs;
      GstPlugin *plugin = g_new0 (GstPlugin, 1);
      plugin->elements = NULL;
      plugin->types = NULL;
      plugin->loaded = FALSE;

      while (field) {
	if (!strcmp(field->name, "name")) {
	  if (gst_plugin_find(xmlNodeGetContent(field))) {
            g_free(plugin);
	    plugin = NULL;
            break;
	  }
	  else {
	    plugin->name = g_strdup(xmlNodeGetContent(field));
	  }
	}
	else if (!strcmp(field->name, "longname")) {
	  plugin->longname = g_strdup(xmlNodeGetContent(field));
	}
	else if (!strcmp(field->name, "filename")) {
	  plugin->filename = g_strdup(xmlNodeGetContent(field));
	}
	else if (!strcmp(field->name, "element")) {
	  GstElementFactory *factory = gst_elementfactory_load_thyself(field);
	  gst_plugin_add_factory (plugin, factory);
	  elementcount++;
	}
	else if (!strcmp(field->name, "type")) {
	  GstTypeFactory *factory = gst_typefactory_load_thyself(field);
	  gst_plugin_add_type (plugin, factory);
	  elementcount++;
	  typecount++;
	}

	field = field->next;
      }

      if (plugin) {
        _gst_plugins = g_list_prepend(_gst_plugins, plugin);
      }
    }

    kinderen = kinderen->next;
  }
  GST_INFO (GST_CAT_PLUGIN_LOADING,"added %d registered factories and %d types",elementcount,typecount);
}


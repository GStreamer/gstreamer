/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <string.h>

#include <gst/gstplugin.h>

#include "config.h"

#undef PLUGINS_USE_SRCDIR

/* list of loaded modules and its sequence number */
GList *_gst_modules;
gint _gst_modules_seqno;
/* global list of plugins and its sequence number */
GList *_gst_plugins;
gint _gst_plugins_seqno;
/* list of paths to check for plugins */
GList *_gst_plugin_paths;

GList *_gst_libraries;
gint _gst_libraries_seqno;

/* whether or not to spew library load issues */
gboolean _gst_plugin_spew = FALSE;


void _gst_plugin_initialize() {
  xmlDocPtr doc;
  _gst_modules = NULL;
  _gst_modules_seqno = 0;
  _gst_plugins = NULL;
  _gst_plugins_seqno = 0;
  _gst_plugin_paths = NULL;
  _gst_libraries = NULL;
  _gst_libraries_seqno = 0;

  /* add the main (installed) library path */
  _gst_plugin_paths = g_list_append(_gst_plugin_paths,PLUGINS_DIR);

  /* if this is set, we add build-directory paths to the list */
#ifdef PLUGINS_USE_SRCDIR
  /* the catch-all plugins directory */
  _gst_plugin_paths = g_list_append(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/plugins");
  /* the libreary directory */
  _gst_plugin_paths = g_list_append(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/libs");
  /* location libgstelements.so */
  _gst_plugin_paths = g_list_append(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/gst/elements");
  _gst_plugin_paths = g_list_append(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/gst/types");
#endif /* PLUGINS_USE_SRCDIR */

  doc = xmlParseFile("/etc/gstreamer/reg.xml");

  if (!doc || strcmp(doc->root->name, "GST-PluginRegistry")) {
    g_print("gstplugin: registry needs rebuild\n");
    gst_plugin_load_all();
    return;
  }
  gst_plugin_load_thyself(doc->root);
}

static gboolean gst_plugin_load_recurse(gchar *directory,gchar *name) {
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
          return loaded;
        }
      } else if ((temp = strstr(directory,".so")) &&
                 (!strcmp(temp,".so"))) {
        loaded = gst_plugin_load_absolute(directory);
        //return loaded;
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
void gst_plugin_load_all() {
  GList *path;

  path = _gst_plugin_paths;
  while (path != NULL) {
    gst_plugin_load_recurse(path->data,NULL);
    path = g_list_next(path);
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
gboolean gst_library_load(gchar *name) {
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

/**
 * gst_plugin_load:
 * @name: name of plugin to load
 *
 * Load the named plugin.  Name should be given as
 * &quot;libplugin.so&quot;.
 *
 * Returns: whether the plugin was loaded or not
 */
gboolean gst_plugin_load(gchar *name) {
  GList *path;
  gchar *libspath;

  //g_print("attempting to load plugin '%s'\n",name);

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
gboolean gst_plugin_load_absolute(gchar *name) {
  GModule *module;
  GstPluginInitFunc initfunc;
  GstPlugin *plugin;
  GList *plugins;


  if (g_module_supported() == FALSE) {
    g_print("gstplugin: wow, you built this on a platform without dynamic loading???\n");
    return FALSE;
  }

  plugins = _gst_plugins;

  while (plugins) {
    plugin = (GstPlugin *)plugins->data;

    if (!strcmp(plugin->filename, name) && plugin->loaded) {
      _gst_plugins = g_list_append(_gst_plugins,plugin);
      return TRUE;
    }
    plugins = g_list_next(plugins);
  }
  //g_print("trying to absolute load '%s\n",name);

  module = g_module_open(name,G_MODULE_BIND_LAZY);
  if (module != NULL) {
    if (g_module_symbol(module,"plugin_init",(gpointer *)&initfunc)) {
      if ((plugin = (initfunc)(module))) {
        GList *factories;
        g_print("gstplugin: plugin %s loaded\n", plugin->name);
        plugin->filename = g_strdup(name);
        plugin->loaded = TRUE;
        _gst_modules = g_list_append(_gst_modules,module);
        _gst_modules_seqno++;
        _gst_plugins = g_list_append(_gst_plugins,plugin);
        _gst_plugins_seqno++;
        factories = plugin->elements;
        while (factories) {
          gst_elementfactory_register((GstElementFactory*)(factories->data));
          factories = g_list_next(factories);
        }
        return TRUE;
      }
    }
    return TRUE;
  } else if (_gst_plugin_spew) {
//    if (strstr(g_module_error(),"No such") == NULL)
      gst_info("error loading plugin: %s\n",g_module_error());
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
GstPlugin *gst_plugin_new(gchar *name) {
  GstPlugin *plugin = (GstPlugin *)g_malloc(sizeof(GstPlugin));

  plugin->name = g_strdup(name);
  plugin->longname = NULL;
  plugin->types = NULL;
  plugin->elements = NULL;
  plugin->loaded = FALSE;

  return plugin;
}

/**
 * gst_plugin_set_longname:
 * @plugin: plugin to set long name of
 * @longname: new long name
 *
 * Sets the long name (should be descriptive) of the plugin.
 */
void gst_plugin_set_longname(GstPlugin *plugin,gchar *longname) {
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
GstPlugin *gst_plugin_find(const gchar *name) {
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
GstElementFactory *gst_plugin_find_elementfactory(gchar *name) {
  GList *plugins, *factories;
  GstElementFactory *factory;

  g_return_val_if_fail(name != NULL, NULL);

  plugins = _gst_plugins;
  while (plugins) {
    factories = ((GstPlugin *)(plugins->data))->elements;
    while (factories) {
      factory = (GstElementFactory*)(factories->data);
      if (!strcmp(factory->name,name))
        return (GstElementFactory*)(factory);
      factories = g_list_next(factories);
    }
    plugins = g_list_next(plugins);
  }

  return NULL;
}

GstElementFactory *gst_plugin_load_elementfactory(gchar *name) {
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
	  g_print("gstplugin: loading element factory %s from plugin %s\n", name, plugin->name);
	  _gst_plugins = g_list_remove(_gst_plugins, plugin);
	  if (!gst_plugin_load_absolute(plugin->filename)) {
	    g_print("gstplugin: error loading element factory %s from plugin %s\n", name, plugin->name);
	  }
	  factory = gst_plugin_find_elementfactory(factory->name);
	}
        return factory;
      }
      factories = g_list_next(factories);
    }
    plugins = g_list_next(plugins);
  }

  return factory;
}

void gst_plugin_load_typefactory(gchar *mime) {
  GList *plugins, *factories;
  GstTypeFactory *factory;
  GstPlugin *plugin;

  g_return_if_fail(mime != NULL);

  plugins = _gst_plugins;
  while (plugins) {
    plugin = (GstPlugin *)plugins->data;
    factories = plugin->types;
    while (factories) {
      factory = (GstTypeFactory*)(factories->data);
      if (!strcmp(factory->mime,mime)) {
	if (!plugin->loaded) {
	  g_print("gstplugin: loading type factory for \"%s\" from plugin %s\n", mime, plugin->name);
	  _gst_plugins = g_list_remove(_gst_plugins, plugin);
	  if (!gst_plugin_load_absolute(plugin->filename)) {
	    g_print("gstplugin: error loading type factory \"%s\" from plugin %s\n", mime, plugin->name);
	  }
	}
	return;
      }
      factories = g_list_next(factories);
    }
    plugins = g_list_next(plugins);
  }

  return;
}

/**
 * gst_plugin_add_factory:
 * @plugin: plugin to add factory to
 * @factory: factory to add
 *
 * Add factory to the list of those provided by the element.
 */
void gst_plugin_add_factory(GstPlugin *plugin,GstElementFactory *factory) {
  g_return_if_fail(plugin != NULL);
  g_return_if_fail(factory != NULL);

//  g_print("adding factory to plugin\n");
  plugin->elements = g_list_append(plugin->elements,factory);
}

void gst_plugin_add_type(GstPlugin *plugin,GstTypeFactory *factory) {
  g_return_if_fail(plugin != NULL);
  g_return_if_fail(factory != NULL);

//  g_print("adding factory to plugin\n");
  plugin->types = g_list_append(plugin->types,factory);
}

/**
 * gst_plugin_get_list:
 *
 * get the currently loaded plugins
 *
 * Returns; a GList of GstPlugin elements
 */
GList *gst_plugin_get_list() {
  return _gst_plugins;
}

xmlNodePtr gst_plugin_save_thyself(xmlNodePtr parent) {
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

void gst_plugin_load_thyself(xmlNodePtr parent) {
  xmlNodePtr kinderen;   
  gint elementcount = 0;
  gint typecount = 0;
  
  kinderen = parent->childs; // Dutch invasion :-)
  while (kinderen) {
    if (!strcmp(kinderen->name, "plugin")) {
      xmlNodePtr field = kinderen->childs;
      GstPlugin *plugin = (GstPlugin *)g_malloc(sizeof(GstPlugin));
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
	  plugin->elements = g_list_prepend(plugin->elements, factory);
	  elementcount++;
	}
	else if (!strcmp(field->name, "type")) {
	  GstTypeFactory *factory = gst_typefactory_load_thyself(field);
	  guint16 typeid = gst_type_find_by_mime(factory->mime);
	  if (!typeid) {
	    typeid = gst_type_register(factory);
	  }
	  plugin->types = g_list_prepend(plugin->types, factory);
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
  g_print("gstplugin: added %d registered factories and %d types\n", elementcount, typecount);
}


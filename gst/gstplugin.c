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


/* list of loaded modules and its sequence number */
GList *_gst_modules;
gint _gst_modules_seqno;
/* global list of plugins and its sequence number */
GList *_gst_plugins;
gint _gst_plugins_seqno;
/* list of paths to check for plugins */
GList *_gst_plugin_paths;

/* whether or not to spew library load issues */
gboolean _gst_plugin_spew = FALSE;


void _gst_plugin_initialize() {
  _gst_modules = NULL;
  _gst_modules_seqno = 0;
  _gst_plugins = NULL;
  _gst_plugins_seqno = 0;
  _gst_plugin_paths = NULL;

  /* add the main (installed) library path */
  _gst_plugin_paths = g_list_prepend(_gst_plugin_paths,PLUGINS_DIR);

  /* if this is set, we add build-directory paths to the list */
#ifdef PLUGINS_USE_SRCDIR
  /* the catch-all plugins directory */
  _gst_plugin_paths = g_list_prepend(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/plugins");
  /* the libreary directory */
  _gst_plugin_paths = g_list_prepend(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/libs");
  /* location libgstelements.so */
  _gst_plugin_paths = g_list_prepend(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/gst/elements");
  _gst_plugin_paths = g_list_prepend(_gst_plugin_paths,
                                     PLUGINS_SRCDIR "/gst/types");
#endif /* PLUGINS_USE_SRCDIR */
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
  // for now this is the same
  return gst_plugin_load(name);
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

//  g_print("attempting to load plugin '%s'\n",name);

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

  //g_print("trying to absolute load '%s\n",name);

  if (g_module_supported() == FALSE) {
    g_print("wow, you built this on a platform without dynamic loading???\n");
    return FALSE;
  }

  module = g_module_open(name,G_MODULE_BIND_LAZY);
  if (module != NULL) {
    if (g_module_symbol(module,"plugin_init",(gpointer *)&initfunc)) {
      if ((plugin = (initfunc)(module))) {
        GList *factories;
        plugin->filename = g_strdup(name);
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
GstPlugin *gst_plugin_find(gchar *name) {
  GList *plugins = _gst_plugins;

  g_return_val_if_fail(name != NULL, NULL);

  while (plugins) {
    GstPlugin *plugin = (GstPlugin *)plugins->data;
//    g_print("plugin name is '%s'\n",plugin->name);
    if (plugin->name) {
      if (!strcmp(plugin->name,name))
        return plugin;
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
      if (!strcmp(gst_element_get_name(GST_ELEMENT(factory)),name))
        return (GstElementFactory*)(factory);
      factories = g_list_next(factories);
    }
    plugins = g_list_next(plugins);
  }

  return NULL;
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

GList *gst_plugin_get_list() {
  return _gst_plugins;
}

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/* list of paths to check for plugins */
GList *_plugin_paths;

/* whether or not to spew library load issues */
gboolean _plugin_spew = FALSE;


void plugin_initialize() {
  _plugin_paths = NULL;

  /* add the main (installed) library path */
  _plugin_paths = g_list_prepend(_plugin_paths,PLUGINS_DIR);

  /* if this is set, we add build-directory paths to the list */
#ifdef PLUGINS_USE_SRCDIR
  _plugin_paths = g_list_prepend(_plugin_paths,PLUGINS_SRCDIR);
#endif /* PLUGINS_USE_SRCDIR */
}

static GModule *plugin_load_recurse(gchar *directory,gchar *name) {
  DIR *dir;
  struct dirent *dirent;
  GModule *mod;

  dir = opendir(directory);
  if (dir) {
    while (dirent = readdir(dir)) {
      /* don't want to recurse in place or backwards */
      if (strcmp(dirent->d_name,".") && strcmp(dirent->d_name,"..")) {
        mod = plugin_load_recurse(g_strjoin("/",directory,dirent->d_name,
                                            NULL),name);
        if (mod != NULL) {
          closedir(dir);
          return mod;
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
          mod = plugin_load_absolute(directory);
          if (mod != NULL) return mod;
        }
      } else if ((temp = strstr(directory,".so")) &&
                 (!strcmp(temp,".so"))) {
        mod = plugin_load_absolute(directory);
        if (mod != NULL) return mod;
      }
    }
  }
  return NULL;
}

/**
 * plugin_load_all:
 *
 * Load all plugins in the path.
 */
void plugin_load_all() {
  GList *path;

  path = _plugin_paths;
  while (path != NULL) {
    plugin_load_recurse(path->data,NULL);
    path = g_list_next(path);
  }
}

/**
 * plugin_load:
 * @name: name of plugin to load
 *
 * Load the named plugin.  Name should be given as
 * &quot;libplugin.so&quot;.
 *
 * Returns: whether the plugin was loaded or not
 */
GModule *plugin_load(gchar *name) {
  GList *path;
  gchar *libspath;
  GModule *mod;

//  g_print("attempting to load plugin '%s'\n",name);

  path = _plugin_paths;
  while (path != NULL) {
    mod = plugin_load_absolute(g_module_build_path(path->data,name));
    if (mod != NULL) return mod;
    libspath = g_strconcat(path->data,"/.libs",NULL);
//    g_print("trying to load '%s'\n",g_module_build_path(libspath,name));
    mod = plugin_load_absolute(g_module_build_path(libspath,name));
    if (mod != NULL) {
      g_free(libspath);
      return mod;
    }
    g_free(libspath);
//    g_print("trying to load '%s' from '%s'\n",name,path->data);
    mod = plugin_load_recurse(path->data,name);
    if (mod != NULL) return mod;
    path = g_list_next(path);
  }
  return NULL;
}

/**
 * plugin_load_absolute:
 * @name: name of plugin to load
 *
 * Returns: whether or not the plugin loaded
 */
GModule *plugin_load_absolute(gchar *name) {
  GModule *mod;

//  g_print("trying to load '%s\n",name);

  if (g_module_supported() == FALSE) {
    g_print("wow, you built this on a platform without dynamic loading???\n");
    return;
  }

  mod = g_module_open(name,0);
  if (mod != NULL) {
    return mod;
  } else if (_gst_plugin_spew) {
//    if (strstr(g_module_error(),"No such") == NULL)
      g_print("error loading plugin: %s\n",g_module_error());
  }

  return NULL;
}

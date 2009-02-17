/* gst-python
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
 *               2005 Benjamin Otte <otte@gnome.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <pygobject.h>
#include <gst/gst.h>

PyTypeObject *_PyGstElement_Type;
#define PyGstElement_Type (*_PyGstElement_Type)


GST_DEBUG_CATEGORY_STATIC (pyplugindebug);
#define GST_CAT_DEFAULT pyplugindebug

#define GST_ORIGIN "http://gstreamer.freedesktop.org"

static PyObject *element;

static inline gboolean
np_init_pygobject (void)
{
  PyObject *gobject = PyImport_ImportModule ("gobject");
  gboolean res = TRUE;

  if (gobject != NULL) {
    PyObject *mdict = PyModule_GetDict (gobject);
    PyObject *cobject = PyDict_GetItemString (mdict, "_PyGObject_API");
    if (PyCObject_Check (cobject)) {
      _PyGObject_API =
          (struct _PyGObject_Functions *) PyCObject_AsVoidPtr (cobject);
    } else {
      PyErr_SetString (PyExc_RuntimeError,
          "could not find _PyGObject_API object");
      PyErr_Print ();
      res = FALSE;
      goto beach;
    }
    if (!(PyObject_CallMethod (gobject, "threads_init", NULL, NULL))) {
      PyErr_SetString (PyExc_RuntimeError, "Could not initialize threads");
      PyErr_Print ();
      res = FALSE;
      goto beach;
    }
  } else {
    PyErr_Print ();
    GST_WARNING ("could not import gobject");
    res = FALSE;
  }

beach:
  return res;
}

static gboolean
gst_python_plugin_load_file (GstPlugin * plugin, const char *name)
{
  PyObject *main_module, *main_locals;
  PyObject *elementfactory;
  int pos = 0;
  GType gtype;
  PyObject *module;
  const gchar *facname;
  guint rank;
  PyObject *class;

  GST_DEBUG ("loading plugin %s", name);

  main_module = PyImport_AddModule ("__main__");
  if (main_module == NULL) {
    GST_WARNING ("Could not get __main__, ignoring plugin %s", name);
    PyErr_Print ();
    PyErr_Clear ();
    return FALSE;
  }

  main_locals = PyModule_GetDict (main_module);
  module =
      PyImport_ImportModuleEx ((char *) name, main_locals, main_locals, NULL);
  if (!module) {
    GST_DEBUG ("Could not load module, ignoring plugin %s", name);
    PyErr_Print ();
    PyErr_Clear ();
    return FALSE;
  }

  /* Get __gstelementfactory__ from file */
  elementfactory = PyObject_GetAttrString (module, "__gstelementfactory__");
  if (!elementfactory) {
    GST_DEBUG ("python file doesn't contain __gstelementfactory__");
    PyErr_Clear ();
    return FALSE;
  }

  /* parse tuple : name, rank, gst.ElementClass */
  if (!PyArg_ParseTuple (elementfactory, "sIO", &facname, &rank, &class)) {
    GST_WARNING ("__gstelementfactory__ isn't correctly formatted");
    PyErr_Print ();
    PyErr_Clear ();
    Py_DECREF (elementfactory);
    return FALSE;
  }

  if (!PyType_Check (class)
      || !(PyObject_IsSubclass (class, (PyObject *) & PyGstElement_Type))) {
    GST_WARNING ("the class provided isn't a subclass of gst.Element");
    PyErr_Print ();
    PyErr_Clear ();
    Py_DECREF (elementfactory);
    Py_DECREF (class);
    return FALSE;
  }

  GST_LOG ("Valid plugin");
  Py_DECREF (elementfactory);

  return gst_element_register (plugin, facname, rank,
      pyg_type_from_object (class));
}

static gboolean
gst_python_load_directory (GstPlugin * plugin, gchar * path)
{
  GST_LOG ("Checking for python plugins in %s", path);
  GDir *dir;
  const gchar *file;
  GError *error = NULL;
  gboolean ret = TRUE;

  dir = g_dir_open (path, 0, &error);
  if (!dir) {
    /*retval should probably be depending on error, but since we ignore it... */
    GST_WARNING ("Couldn't open Python plugin dir: %s", error->message);
    g_error_free (error);
    return FALSE;
  }
  while ((file = g_dir_read_name (dir))) {
    /* FIXME : go down in subdirectories */
    if (g_str_has_suffix (file, ".py")) {
      gsize len = strlen (file) - 3;
      gchar *name = g_strndup (file, len);
      ret &= gst_python_plugin_load_file (plugin, name);
      g_free (name);
    }
  }
  return TRUE;
}

static gboolean
gst_python_plugin_load (GstPlugin * plugin)
{
  PyObject *sys_path;
  const gchar *plugin_path;
  gboolean ret = TRUE;

  sys_path = PySys_GetObject ("path");

  /* Mimic the order in which the registry is checked in core */

  /* 1. check env_variable GST_PLUGIN_PATH */
  plugin_path = g_getenv ("GST_PLUGIN_PATH");
  if (plugin_path) {
    char **list;
    int i;

    GST_DEBUG ("GST_PLUGIN_PATH set to %s", plugin_path);
    list = g_strsplit (plugin_path, G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; list[i]; i++) {
      gchar *sysdir = g_build_filename (list[i], "python", NULL);
      PyList_Insert (sys_path, 0, PyString_FromString (sysdir));
      gst_python_load_directory (plugin, sysdir);
      g_free (sysdir);
    }

    g_strfreev (list);
  }

  /* 2. Check for GST_PLUGIN_SYSTEM_PATH */
  plugin_path = g_getenv ("GST_PLUGIN_SYSTEM_PATH");
  if (plugin_path == NULL) {
    char *home_plugins;

    /* 2.a. Scan user and system-wide plugin directory */
    GST_DEBUG ("GST_PLUGIN_SYSTEM_PATH not set");

    /* plugins in the user's home directory take precedence over
     * system-installed ones */
    home_plugins = g_build_filename (g_get_home_dir (),
        ".gstreamer-" GST_MAJORMINOR, "plugins", "python", NULL);
    PyList_Insert (sys_path, 0, PyString_FromString (home_plugins));
    gst_python_load_directory (plugin, home_plugins);
    g_free (home_plugins);

    /* add the main (installed) library path */
    PyList_Insert (sys_path, 0, PyString_FromString (PLUGINDIR "/python"));
    gst_python_load_directory (plugin, PLUGINDIR "/python");
  } else {
    gchar **list;
    gint i;

    /* 2.b. Scan GST_PLUGIN_SYSTEM_PATH */
    GST_DEBUG ("GST_PLUGIN_SYSTEM_PATH set to %s", plugin_path, plugin_path);
    list = g_strsplit (plugin_path, G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; list[i]; i++) {
      gchar *sysdir;

      sysdir = g_build_filename (list[i], "python", NULL);

      PyList_Insert (sys_path, 0, PyString_FromString (sysdir));
      gst_python_load_directory (plugin, sysdir);
      g_free (sysdir);
    }
    g_strfreev (list);
  }


  return ret;
}

/** 
 *pygst_require:
 * @version: the version required
 *
 * Checks if the pygst/gst python modules are available.
 * Requests the specified version.
 *
 * Returns: the gst-python module, or NULL if there was an error.
 */

static PyObject *
pygst_require (gchar * version)
{
  PyObject *pygst, *gst;
  PyObject *require;

  if (!(pygst = PyImport_ImportModule ("pygst"))) {
    GST_ERROR ("the pygst module is not available!");
    goto error;
  }

  if (!(PyObject_CallMethod (pygst, "require", "s", version))) {
    GST_ERROR ("the required version, %s, of gst-python is not available!");
    Py_DECREF (pygst);
    goto error;
  }

  if (!(gst = PyImport_ImportModule ("gst"))) {
    GST_ERROR ("couldn't import the gst module");
    Py_DECREF (pygst);
    goto error;
  }
#define IMPORT(x, y) \
    _PyGst##x##_Type = (PyTypeObject *)PyObject_GetAttrString(gst, y); \
	if (_PyGst##x##_Type == NULL) { \
		PyErr_Print(); \
		return NULL; \
	}
  IMPORT (Element, "Element");

  return gst;

error:
  {
    PyErr_Print ();
    PyErr_Clear ();
    return NULL;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  PyGILState_STATE state;
  PyObject *gst, *dict, *pyplugin;
  gboolean we_initialized = FALSE;
  GModule *libpython;
  gpointer has_python = NULL;

  GST_DEBUG_CATEGORY_INIT (pyplugindebug, "pyplugin", 0,
      "Python plugin loader");

  gst_plugin_add_dependency_simple (plugin,
      "HOME/.gstreamer-0.10/plugins/python:GST_PLUGIN_SYSTEM_PATH/python:GST_PLUGIN_PATH/python",
      NULL, NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  GST_LOG ("Checking to see if libpython is already loaded");
  g_module_symbol (g_module_open (NULL, G_MODULE_BIND_LOCAL), "Py_None",
      &has_python);
  if (has_python) {
    GST_LOG ("libpython is already loaded");
  } else {
    GST_LOG ("loading libpython");
    libpython =
        g_module_open (PY_LIB_LOC "/libpython" PYTHON_VERSION "."
        G_MODULE_SUFFIX, 0);
    if (!libpython) {
      GST_WARNING ("Couldn't g_module_open libpython. Reason: %s",
          g_module_error ());
      return FALSE;
    }
  }

  if (!Py_IsInitialized ()) {
    GST_LOG ("python wasn't initialized");
    /* set the correct plugin for registering stuff */
    Py_Initialize ();
    we_initialized = TRUE;
  } else {
    GST_LOG ("python was already initialized");
    state = pyg_gil_state_ensure ();
  }

  GST_LOG ("initializing pygobject");
  if (!np_init_pygobject ()) {
    GST_WARNING ("pygobject initialization failed");
    return FALSE;
  }

  if (!(gst = pygst_require ("0.10"))) {
    return FALSE;
  }

  if (we_initialized) {
    pyplugin = pygobject_new (G_OBJECT (plugin));
    if (!pyplugin || PyModule_AddObject (gst, "__plugin__", pyplugin) != 0) {
      g_warning ("Couldn't set plugin");
      Py_DECREF (pyplugin);
    }
  }

  dict = PyModule_GetDict (gst);
  if (!dict) {
    GST_ERROR ("no dict?!");
    return FALSE;
  }

  gst_python_plugin_load (plugin);

  if (we_initialized) {
    /* We need to release the GIL since we're going back to C land */
    PyEval_SaveThread ();
  } else
    pyg_gil_state_release (state);
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "python",
    "loader for plugins written in python",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_ORIGIN)

/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gesformatter
 * @title: GESFormatter
 * @short_description: Timeline saving and loading.
 *
 **/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "ges-formatter.h"
#include "ges-internal.h"
#include "ges.h"
#ifndef DISABLE_XPTV
#include "ges-pitivi-formatter.h"
#endif

#ifdef HAS_PYTHON
#include <Python.h>
#include "ges-resources.h"

/*
 * We need to call dlopen() directly on macOS to workaround a macOS runtime
 * linker bug. When there are nested dlopen() calls and the second dlopen() is
 * called from another library (such as gmodule), @loader_path is resolved as
 * @executable_path and RPATHs are read from the executable (gst-plugin-scanner)
 * instead of the library itself (libgstges.dylib). This doesn't happen if the
 * second dlopen() call is directly in the source code of the library.
 * Previously seen at:
 * https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/1171#note_2290789
 */
#ifdef G_OS_WIN32
#include <gmodule.h>
#define ges_module_open(fname) g_module_open(fname,0)
#define ges_module_error g_module_error
#define ges_module_symbol(module,name,symbol) g_module_symbol(module,name,symbol)
#else
#include <dlfcn.h>
#define ges_module_open(fname) dlopen(fname,RTLD_NOW | RTLD_GLOBAL)
#define ges_module_error dlerror
static inline gboolean
ges_module_symbol (gpointer handle, const char *name, gpointer * symbol)
{
  *symbol = dlsym (handle, name);
  return *symbol != NULL;
}
#endif
#endif /* HAS_PYTHON */

GST_DEBUG_CATEGORY_STATIC (ges_formatter_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_formatter_debug
static gboolean initialized = FALSE;

/* TODO Add a GCancellable somewhere in the API */
static void ges_extractable_interface_init (GESExtractableInterface * iface);

struct _GESFormatterPrivate
{
  gpointer nothing;
};

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESFormatter, ges_formatter,
    G_TYPE_INITIALLY_UNOWNED, G_ADD_PRIVATE (GESFormatter)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

static void ges_formatter_dispose (GObject * object);
static gboolean default_can_load_uri (GESFormatter * dummy_instance,
    const gchar * uri, GError ** error);

/* GESExtractable implementation */
static gchar *
extractable_check_id (GType type, const gchar * id)
{
  GESFormatterClass *class;

  if (id)
    return g_strdup (id);

  class = g_type_class_peek (type);
  return g_strdup (class->name);
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  GESAsset *asset;

  if (!(asset = ges_extractable_get_asset (self)))
    return NULL;

  return g_strdup (ges_asset_get_id (asset));
}

static gboolean
_register_metas (GESExtractableInterface * iface, GObjectClass * class,
    GESAsset * asset)
{
  GESFormatterClass *fclass = GES_FORMATTER_CLASS (class);
  GESMetaContainer *container = GES_META_CONTAINER (asset);

  ges_meta_container_register_meta_string (container, GES_META_READABLE,
      GES_META_FORMATTER_NAME, fclass->name);
  ges_meta_container_register_meta_string (container, GES_META_READABLE,
      GES_META_DESCRIPTION, fclass->description);
  ges_meta_container_register_meta_string (container, GES_META_READABLE,
      GES_META_FORMATTER_MIMETYPE, fclass->mimetype);
  ges_meta_container_register_meta_string (container, GES_META_READABLE,
      GES_META_FORMATTER_EXTENSION, fclass->extension);
  ges_meta_container_register_meta_double (container, GES_META_READABLE,
      GES_META_FORMATTER_VERSION, fclass->version);
  ges_meta_container_register_meta_uint (container, GES_META_READABLE,
      GES_META_FORMATTER_RANK, fclass->rank);
  ges_meta_container_register_meta_string (container, GES_META_READ_WRITE,
      GES_META_FORMAT_VERSION, NULL);

  /* We are leaking the metadata but we don't really have choice here
   * as calling ges_init() after deinit() is allowed.
   */

  return TRUE;
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_id = extractable_get_id;
  iface->asset_type = GES_TYPE_ASSET;
  iface->register_metas = _register_metas;
}

static void
ges_formatter_class_init (GESFormatterClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ges_formatter_dispose;

  klass->can_load_uri = default_can_load_uri;
  klass->load_from_uri = NULL;
  klass->save_to_uri = NULL;

  /* We set dummy  metas */
  klass->name = g_strdup ("base-formatter");
  klass->extension = g_strdup ("noextension");
  klass->description = g_strdup ("Formatter base class, you should give"
      " a name to your formatter");
  klass->mimetype = g_strdup ("No mimetype");
  klass->version = 0.0;
  klass->rank = GST_RANK_NONE;
}

static void
ges_formatter_init (GESFormatter * object)
{
  object->priv = ges_formatter_get_instance_private (object);
  object->project = NULL;
}

static void
ges_formatter_dispose (GObject * object)
{
  ges_formatter_set_project (GES_FORMATTER (object), NULL);

  G_OBJECT_CLASS (ges_formatter_parent_class)->dispose (object);
}

static gboolean
default_can_load_uri (GESFormatter * dummy_instance, const gchar * uri,
    GError ** error)
{
  GST_DEBUG ("%s: no 'can_load_uri' vmethod implementation",
      G_OBJECT_TYPE_NAME (dummy_instance));

  return FALSE;
}

static gchar *
_get_extension (const gchar * uri)
{
  gchar *result;
  gsize len;
  gint find;

  GST_DEBUG ("finding extension of %s", uri);

  if (uri == NULL)
    goto no_uri;

  /* find the extension on the uri, this is everything after a '.' */
  len = strlen (uri);
  find = len - 1;

  while (find >= 0) {
    if (uri[find] == '.')
      break;
    find--;
  }
  if (find < 0)
    goto no_extension;

  result = g_strdup (&uri[find + 1]);

  GST_DEBUG ("found extension %s", result);

  return result;

  /* ERRORS */
no_uri:
  {
    GST_WARNING ("could not parse the peer uri");
    return NULL;
  }
no_extension:
  {
    GST_WARNING ("could not find uri extension in %s", uri);
    return NULL;
  }
}

/**
 * ges_formatter_can_load_uri:
 * @uri: a #gchar * pointing to the URI
 * @error: A #GError that will be set in case of error
 *
 * Checks if there is a #GESFormatter available which can load a #GESTimeline
 * from the given URI.
 *
 * Returns: TRUE if there is a #GESFormatter that can support the given uri
 * or FALSE if not.
 */

gboolean
ges_formatter_can_load_uri (const gchar * uri, GError ** error)
{
  gboolean ret = FALSE;
  gchar *extension;
  GList *formatter_assets, *tmp;
  GESFormatterClass *class = NULL;

  if (!(gst_uri_is_valid (uri))) {
    GST_ERROR ("Invalid uri!");
    return FALSE;
  }

  extension = _get_extension (uri);

  formatter_assets = ges_list_assets (GES_TYPE_FORMATTER);
  for (tmp = formatter_assets; tmp; tmp = tmp->next) {
    GESAsset *asset = GES_ASSET (tmp->data);
    GESFormatter *dummy_instance;
    gchar **valid_exts =
        g_strsplit (ges_meta_container_get_string (GES_META_CONTAINER (asset),
            GES_META_FORMATTER_EXTENSION), ",", -1);
    gint i;

    if (extension) {
      gboolean found = FALSE;

      for (i = 0; valid_exts[i]; i++) {
        if (!g_strcmp0 (extension, valid_exts[i])) {
          found = TRUE;
          break;
        }
      }

      if (!found)
        goto next;
    }

    class = g_type_class_ref (ges_asset_get_extractable_type (asset));
    dummy_instance =
        g_object_ref_sink (g_object_new (ges_asset_get_extractable_type (asset),
            NULL));
    if (class->can_load_uri (dummy_instance, uri, error)) {
      g_type_class_unref (class);
      gst_object_unref (dummy_instance);
      ret = TRUE;
      break;
    }
    g_type_class_unref (class);
    gst_object_unref (dummy_instance);
  next:
    g_strfreev (valid_exts);
  }
  g_free (extension);

  g_list_free (formatter_assets);
  return ret;
}

/**
 * ges_formatter_can_save_uri:
 * @uri: a #gchar * pointing to a URI
 * @error: A #GError that will be set in case of error
 *
 * Returns TRUE if there is a #GESFormatter available which can save a
 * #GESTimeline to the given URI.
 *
 * Returns: TRUE if the given @uri is supported, else FALSE.
 */

gboolean
ges_formatter_can_save_uri (const gchar * uri, GError ** error)
{
  GFile *file = NULL;
  GFile *dir = NULL;
  gboolean ret = TRUE;
  GFileInfo *info = NULL;

  if (!(gst_uri_is_valid (uri))) {
    GST_ERROR ("%s invalid uri!", uri);
    return FALSE;
  }

  if (!(gst_uri_has_protocol (uri, "file"))) {
    gchar *proto = gst_uri_get_protocol (uri);
    GST_ERROR ("Unsupported protocol '%s'", proto);
    g_free (proto);
    return FALSE;
  }

  /* Check if URI or parent directory is writeable */
  file = g_file_new_for_uri (uri);
  if (g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL)
      == G_FILE_TYPE_DIRECTORY) {
    dir = g_object_ref (file);
  } else {
    dir = g_file_get_parent (file);

    if (dir == NULL)
      goto error;
  }

  info = g_file_query_info (dir, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE,
      G_FILE_QUERY_INFO_NONE, NULL, error);

  if (error && *error != NULL) {
    GST_ERROR ("Unable to write to directory: %s", (*error)->message);

    goto error;
  } else {
    gboolean writeable = g_file_info_get_attribute_boolean (info,
        G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
    if (!writeable) {
      GST_ERROR ("Unable to write to directory");
      goto error;
    }
  }

done:
  if (file)
    g_object_unref (file);

  if (dir)
    g_object_unref (dir);

  if (info)
    g_object_unref (info);

  /* TODO: implement file format registry */
  /* TODO: search through the registry and chose a GESFormatter class that can
   * handle the URI.*/

  return ret;
error:
  ret = FALSE;
  goto done;
}

/**
 * ges_formatter_load_from_uri:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * @error: A #GError that will be set in case of error
 *
 * Load data from the given URI into timeline.
 *
 * Returns: TRUE if the timeline data was successfully loaded from the URI,
 * else FALSE.
 *
 * Deprecated: 1.18: Use @ges_timeline_load_from_uri
 */

gboolean
ges_formatter_load_from_uri (GESFormatter * formatter,
    GESTimeline * timeline, const gchar * uri, GError ** error)
{
  gboolean ret = FALSE;
  GESFormatterClass *klass = GES_FORMATTER_GET_CLASS (formatter);

  g_return_val_if_fail (GES_IS_FORMATTER (formatter), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE (timeline), FALSE);

  if (klass->load_from_uri) {
    formatter->timeline = timeline;
    ret = klass->load_from_uri (formatter, timeline, uri, error);
  }

  return ret;
}

/**
 * ges_formatter_save_to_uri:
 * @formatter: a #GESFormatter
 * @timeline: a #GESTimeline
 * @uri: a #gchar * pointing to a URI
 * @overwrite: %TRUE to overwrite file if it exists
 * @error: A #GError that will be set in case of error
 *
 * Save data from timeline to the given URI.
 *
 * Returns: TRUE if the timeline data was successfully saved to the URI
 * else FALSE.
 *
 * Deprecated: 1.18: Use @ges_timeline_save_to_uri
 */

gboolean
ges_formatter_save_to_uri (GESFormatter * formatter, GESTimeline *
    timeline, const gchar * uri, gboolean overwrite, GError ** error)
{
  GError *lerr = NULL;
  gboolean ret = FALSE;
  GESFormatterClass *klass = GES_FORMATTER_GET_CLASS (formatter);

  GST_DEBUG_OBJECT (formatter, "Saving %" GST_PTR_FORMAT " to %s",
      timeline, uri);
  if (klass->save_to_uri)
    ret = klass->save_to_uri (formatter, timeline, uri, overwrite, &lerr);
  else
    GST_ERROR_OBJECT (formatter, "save_to_uri not implemented!");

  if (lerr) {
    GST_WARNING_OBJECT (formatter, "%" GST_PTR_FORMAT
        " not saved to %s error: %s", timeline, uri, lerr->message);
    g_propagate_error (error, lerr);
  } else
    GST_INFO_OBJECT (formatter, "%" GST_PTR_FORMAT
        " saved to %s", timeline, uri);

  return ret;
}

/**
 * ges_formatter_get_default:
 *
 * Get the default #GESAsset to use as formatter. It will return
 * the asset for the #GESFormatter that has the highest @rank
 *
 * Returns: (transfer none): The #GESAsset for the formatter with highest @rank
 */
GESAsset *
ges_formatter_get_default (void)
{
  GList *assets, *tmp;
  GESAsset *ret = NULL;
  guint tmprank, rank = GST_RANK_NONE;

  assets = ges_list_assets (GES_TYPE_FORMATTER);
  for (tmp = assets; tmp; tmp = tmp->next) {
    tmprank = GST_RANK_NONE;
    ges_meta_container_get_uint (GES_META_CONTAINER (tmp->data),
        GES_META_FORMATTER_RANK, &tmprank);

    if (tmprank > rank) {
      rank = tmprank;
      ret = GES_ASSET (tmp->data);
    }
  }
  g_list_free (assets);

  return ret;
}

/**
 * ges_formatter_class_register_metas:
 * @klass: The class to register metas on
 * @name: The name of the formatter
 * @description: The formatter description
 * @extensions: A list of coma separated file extensions handled
 * by the formatter. The order of the extensions should match the
 * list of the structures inside @caps
 * @caps: The caps the formatter handled, they should match what
 * gstreamer typefind mechanism will report for the files the formatter
 * handles.
 * @version: The version of the formatter
 * @rank: The rank of the formatter
 */
void
ges_formatter_class_register_metas (GESFormatterClass * klass,
    const gchar * name, const gchar * description, const gchar * extensions,
    const gchar * caps, gdouble version, GstRank rank)
{
  g_return_if_fail (klass->name);
  klass->name = g_strdup (name);
  klass->description = g_strdup (description);
  klass->extension = g_strdup (extensions);
  klass->mimetype = g_strdup (caps);
  klass->version = version;
  klass->rank = rank;

  if (g_atomic_int_get (&initialized)
      && g_type_class_peek (G_OBJECT_CLASS_TYPE (klass)))
    gst_object_unref (ges_asset_request (G_OBJECT_CLASS_TYPE (klass), NULL,
            NULL));
}

/* Main Formatter methods */

/*< protected >*/
void
ges_formatter_set_project (GESFormatter * formatter, GESProject * project)
{
  formatter->project = project;
}

GESProject *
ges_formatter_get_project (GESFormatter * formatter)
{
  return formatter->project;
}

static void
_list_formatters (GType * formatters, guint n_formatters)
{
  GType *tmptypes, type;
  guint tmp_n_types, i;

  for (i = 0; i < n_formatters; i++) {
    type = formatters[i];
    tmptypes = g_type_children (type, &tmp_n_types);
    if (tmp_n_types) {
      /* Recurse as g_type_children does not */
      _list_formatters (tmptypes, tmp_n_types);
    }
    g_free (tmptypes);

    if (G_TYPE_IS_ABSTRACT (type)) {
      GST_DEBUG ("%s is abstract, not using", g_type_name (type));
    } else {
      gst_object_unref (ges_asset_request (type, NULL, NULL));
    }
  }
}

static void
load_python_formatters (void)
{
#ifdef HAS_PYTHON
  PyGILState_STATE state = 0;
  PyObject *main_module, *main_locals;
  GError *err = NULL;
  GResource *resource = ges_get_resource ();
  GBytes *bytes =
      g_resource_lookup_data (resource, "/ges/python/gesotioformatter.py",
      G_RESOURCE_LOOKUP_FLAGS_NONE, &err);
  PyObject *code = NULL, *res = NULL;
  gboolean we_initialized = FALSE;
  gpointer has_python = NULL;

  GST_LOG ("Checking to see if libpython is already loaded");
  if (ges_module_symbol (ges_module_open (NULL),
          "_Py_NoneStruct", &has_python) && has_python) {
    GST_LOG ("libpython is already loaded");
  } else {
    GST_LOG ("loading libpython by name: %s", PY_LIB_FNAME);
    if (!ges_module_open (PY_LIB_FNAME)) {
      GST_ERROR ("Couldn't load libpython. Reason: %s", ges_module_error ());
      return;
    }
  }

  if (!Py_IsInitialized ()) {
    GST_LOG ("python wasn't already initialized");
    /* set the correct plugin for registering stuff */
    Py_Initialize ();
    we_initialized = TRUE;
  } else {
    GST_LOG ("python was already initialized");
    state = PyGILState_Ensure ();
  }

  if (!bytes) {
    GST_DEBUG ("Could not load gesotioformatter: %s\n", err->message);

    g_clear_error (&err);

    goto done;
  }

  main_module = PyImport_AddModule ("__main__");
  if (main_module == NULL) {
    GST_WARNING ("Could not add main module");
    PyErr_Print ();
    PyErr_Clear ();
    goto done;
  }

  main_locals = PyModule_GetDict (main_module);
  /* Compiling the code ourself so it has a proper filename */
  code =
      Py_CompileString (g_bytes_get_data (bytes, NULL), "gesotioformatter.py",
      Py_file_input);
  if (PyErr_Occurred ()) {
    PyErr_Print ();
    PyErr_Clear ();
    goto done;
  }
  res = PyEval_EvalCode ((gpointer) code, main_locals, main_locals);
  Py_XDECREF (code);
  Py_XDECREF (res);
  if (PyErr_Occurred ()) {
    PyObject *exception_backtrace;
    PyObject *exception_type;
    PyObject *exception_value, *exception_value_repr, *exception_value_str;

    PyErr_Fetch (&exception_type, &exception_value, &exception_backtrace);
    PyErr_NormalizeException (&exception_type, &exception_value,
        &exception_backtrace);

    exception_value_repr = PyObject_Repr (exception_value);
    exception_value_str =
        PyUnicode_AsEncodedString (exception_value_repr, "utf-8", "Error ~");
    GST_INFO ("Could not load OpenTimelineIO formatter: %s",
        PyBytes_AS_STRING (exception_value_str));

    Py_XDECREF (exception_type);
    Py_XDECREF (exception_value);
    Py_XDECREF (exception_backtrace);

    Py_XDECREF (exception_value_repr);
    Py_XDECREF (exception_value_str);
    PyErr_Clear ();
  }

done:
  if (bytes)
    g_bytes_unref (bytes);

  if (we_initialized) {
    PyEval_SaveThread ();
  } else {
    PyGILState_Release (state);
  }
#endif /* HAS_PYTHON */
}

void
_init_formatter_assets (void)
{
  GType *formatters;
  guint n_formatters;
  static gsize init_debug = 0;

  if (g_once_init_enter (&init_debug)) {

    GST_DEBUG_CATEGORY_INIT (ges_formatter_debug, "gesformatter",
        GST_DEBUG_FG_YELLOW, "ges formatter");
    g_once_init_leave (&init_debug, TRUE);
  }

  if (g_atomic_int_compare_and_exchange (&initialized, FALSE, TRUE)) {
    /* register formatter types with the system */
#ifndef DISABLE_XPTV
    g_type_class_ref (GES_TYPE_PITIVI_FORMATTER);
#endif
    g_type_class_ref (GES_TYPE_COMMAND_LINE_FORMATTER);
    g_type_class_ref (GES_TYPE_XML_FORMATTER);

    load_python_formatters ();

    formatters = g_type_children (GES_TYPE_FORMATTER, &n_formatters);
    _list_formatters (formatters, n_formatters);
    g_free (formatters);
  }
}

void
_deinit_formatter_assets (void)
{
  if (g_atomic_int_compare_and_exchange (&initialized, TRUE, FALSE)) {

#ifndef DISABLE_XPTV
    g_type_class_unref (g_type_class_peek (GES_TYPE_PITIVI_FORMATTER));
#endif

    g_type_class_unref (g_type_class_peek (GES_TYPE_COMMAND_LINE_FORMATTER));
    g_type_class_unref (g_type_class_peek (GES_TYPE_XML_FORMATTER));
  }
}

static gint
_sort_formatters (GESAsset * asset, GESAsset * asset1)
{
  GESFormatterClass *class =
      g_type_class_peek (ges_asset_get_extractable_type (asset));
  GESFormatterClass *class1 =
      g_type_class_peek (ges_asset_get_extractable_type (asset1));

  /* We want the highest ranks to be first! */
  if (class->rank > class1->rank)
    return -1;
  else if (class->rank < class1->rank)
    return 1;

  return 0;
}

GESAsset *
_find_formatter_asset_for_id (const gchar * id)
{
  GESFormatterClass *class = NULL;
  GList *formatter_assets, *tmp;
  GESAsset *asset = NULL;

  formatter_assets = g_list_sort (ges_list_assets (GES_TYPE_FORMATTER),
      (GCompareFunc) _sort_formatters);
  for (tmp = formatter_assets; tmp; tmp = tmp->next) {
    GESFormatter *dummy_instance;

    asset = GES_ASSET (tmp->data);
    class = g_type_class_ref (ges_asset_get_extractable_type (asset));
    dummy_instance =
        g_object_ref_sink (g_object_new (ges_asset_get_extractable_type (asset),
            NULL));
    if (class->can_load_uri (dummy_instance, id, NULL)) {
      g_type_class_unref (class);
      asset = gst_object_ref (asset);
      gst_object_unref (dummy_instance);
      break;
    }

    asset = NULL;
    g_type_class_unref (class);
    gst_object_unref (dummy_instance);
  }

  g_list_free (formatter_assets);

  return asset;
}

/**
 * ges_find_formatter_for_uri:
 *
 * Get the best formatter for @uri. It tries to find a formatter
 * compatible with @uri extension, if none is found, it returns the default
 * formatter asset.
 *
 * Returns: (transfer none): The #GESAsset for the best formatter to save to @uri
 *
 * Since: 1.18
 */
GESAsset *
ges_find_formatter_for_uri (const gchar * uri)
{
  GList *formatter_assets, *tmp;
  GESAsset *asset = NULL;

  gchar *extension = _get_extension (uri);
  if (!extension)
    return ges_formatter_get_default ();

  formatter_assets = g_list_sort (ges_list_assets (GES_TYPE_FORMATTER),
      (GCompareFunc) _sort_formatters);

  for (tmp = formatter_assets; tmp; tmp = tmp->next) {
    gint i;
    gchar **valid_exts =
        g_strsplit (ges_meta_container_get_string (GES_META_CONTAINER
            (tmp->data),
            GES_META_FORMATTER_EXTENSION), ",", -1);

    for (i = 0; valid_exts[i]; i++) {
      if (!g_strcmp0 (extension, valid_exts[i])) {
        asset = GES_ASSET (tmp->data);
        break;
      }
    }

    g_strfreev (valid_exts);
    if (asset)
      break;
  }
  g_free (extension);
  g_list_free (formatter_assets);

  if (asset) {
    GST_INFO_OBJECT (asset, "Using for URI %s", uri);
    return asset;
  }

  return ges_formatter_get_default ();
}

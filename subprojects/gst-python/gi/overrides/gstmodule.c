/* -*- Mode: C; ; c-file-style: "k&r"; c-basic-offset: 4 -*- */
/* gst-python
 * Copyright (C) 2002 David I. Lehn
 * Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: David I. Lehn <dlehn@users.sourceforge.net>
 */

#include "gst/gstcaps.h"
#include "gst/gstminiobject.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <Python.h>
#include <pygobject.h>
#include <gst/gst.h>

#include <locale.h>

#define URI_HANDLER_PROTOCOLS_QUARK g_quark_from_static_string("__gst__uri_handler_protocols")
#define URI_HANDLER_URITYPE_QUARK g_quark_from_static_string("__gst__uri_handler_uritype")

#define PYGLIB_MODULE_START(symbol, modname)	        \
    static struct PyModuleDef _##symbol##module = {     \
    PyModuleDef_HEAD_INIT,                              \
    modname,                                            \
    NULL,                                               \
    -1,                                                 \
    symbol##_functions,                                 \
    NULL,                                               \
    NULL,                                               \
    NULL,                                               \
    NULL                                                \
};                                                      \
PyMODINIT_FUNC PyInit_##symbol(void);                   \
PyMODINIT_FUNC PyInit_##symbol(void)                    \
{                                                       \
    PyObject *module;                                   \
    module = PyModule_Create(&_##symbol##module);
#define PYGLIB_MODULE_END return module; }

GST_DEBUG_CATEGORY_STATIC (python_debug);
GST_DEBUG_CATEGORY_STATIC (pygst_debug);
#define GST_CAT_DEFAULT pygst_debug

static PyObject *
gi_gst_get_type (const gchar * type_name)
{
  PyObject *module, *dict;

  module = PyImport_ImportModule ("gi.repository.Gst");

  if (module == NULL) {
    PyErr_SetString (PyExc_KeyError,
        "Could not get module for gi.repository.Gst");
    return NULL;
  }

  dict = PyModule_GetDict (module);
  Py_DECREF (module);

  /* For some reason we need this intermediary step */
  module = PyMapping_GetItemString (dict, "_overrides_module");
  if (module == NULL) {
    PyErr_SetString (PyExc_KeyError,
        "Could not get module for _overrides_module");
    return NULL;
  }

  dict = PyModule_GetDict (module);
  return PyMapping_GetItemString (dict, type_name);
}

static PyObject *
gi_gst_fraction_from_value (const GValue * value)
{
  PyObject *fraction_type, *args, *fraction;
  gint numerator, denominator;

  numerator = gst_value_get_fraction_numerator (value);
  denominator = gst_value_get_fraction_denominator (value);

  fraction_type = gi_gst_get_type ("Fraction");

  args = Py_BuildValue ("(ii)", numerator, denominator);
  fraction = PyObject_Call (fraction_type, args, NULL);
  Py_DECREF (args);

  return fraction;
}

static int
gi_gst_fraction_to_value (GValue * value, PyObject * object)
{
  glong numerator, denominator;
  PyObject *numerator_obj, *denominator_obj, *is_integer;

  numerator_obj = PyObject_GetAttrString (object, "num");
  if (numerator_obj == NULL)
    goto fail;

  is_integer = PyObject_CallMethod (numerator_obj, "is_integer", NULL);
  if (is_integer != Py_True) {
    PyErr_Format (PyExc_TypeError,
        "numerator %f is not an integer.", PyFloat_AsDouble (numerator_obj));
    Py_DECREF (is_integer);
    goto fail;
  }
  Py_DECREF (is_integer);

  numerator = PyFloat_AsDouble (numerator_obj);
  if (numerator < -G_MAXINT || numerator > G_MAXINT) {
    PyErr_Format (PyExc_ValueError,
        "numerator %" G_GINT64_FORMAT " is out of bound. [-%d - %d]",
        numerator, G_MAXINT, G_MAXINT);
    goto fail;
  }

  denominator_obj = PyObject_GetAttrString (object, "denom");
  if (denominator_obj == NULL)
    goto fail;

  is_integer = PyObject_CallMethod (denominator_obj, "is_integer", NULL);
  if (is_integer != Py_True) {
    PyErr_Format (PyExc_TypeError,
        "denominator %f is not an integer.",
        PyFloat_AsDouble (denominator_obj));
    Py_DECREF (is_integer);
    goto fail;
  }
  Py_DECREF (is_integer);

  denominator = PyFloat_AsDouble (denominator_obj);
  if (denominator == 0) {
    PyErr_SetString (PyExc_ValueError, "denominator is 0.");
    goto fail;
  }

  if (denominator < -G_MAXINT || denominator > G_MAXINT) {
    PyErr_Format (PyExc_ValueError,
        "denominator %" G_GINT64_FORMAT " is out of bound. [-%d - %d]",
        denominator, G_MAXINT, G_MAXINT);
    goto fail;
  }

  gst_value_set_fraction (value, numerator, denominator);

  return 0;

fail:
  return -1;
}

static PyObject *
gi_gst_int_range_from_value (const GValue * value)
{
  gint min, max, step;
  PyObject *int_range_type, *int_range, *range;

  min = gst_value_get_int_range_min (value);
  max = gst_value_get_int_range_max (value);
  step = gst_value_get_int_range_step (value);

  int_range_type = gi_gst_get_type ("IntRange");
  range = PyObject_CallFunction ((PyObject *) & PyRange_Type, "iii",
      min, max, step);
  int_range = PyObject_CallFunction (int_range_type, "O", range);

  Py_DECREF (int_range_type);
  Py_DECREF (range);

  return int_range;
}

static int
gi_gst_int_range_to_value (GValue * value, PyObject * object)
{
  PyObject *range, *min, *max, *step;

  range = PyObject_GetAttrString (object, "range");
  if (range == NULL)
    goto fail;

  min = PyObject_GetAttrString (range, "start");
  if (min == NULL)
    goto fail;

  max = PyObject_GetAttrString (range, "stop");
  if (max == NULL)
    goto fail;

  step = PyObject_GetAttrString (range, "step");
  if (step == NULL)
    goto fail;

  gst_value_set_int_range_step (value, PyLong_AsLong (min),
      PyLong_AsLong (max), PyLong_AsLong (step));

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError,
      "Object is not compatible with Gst.IntRange");
  return -1;
}

static PyObject *
gi_gst_int64_range_from_value (const GValue * value)
{
  gint64 min, max, step;
  PyObject *int64_range_type, *int64_range, *range;

  min = gst_value_get_int64_range_min (value);
  max = gst_value_get_int64_range_max (value);
  step = gst_value_get_int64_range_step (value);

  range = PyObject_CallFunction ((PyObject *) & PyRange_Type, "LLL",
      min, max, step);
  int64_range_type = gi_gst_get_type ("Int64Range");
  int64_range = PyObject_CallFunction (int64_range_type, "O", range);

  Py_DECREF (int64_range_type);
  Py_DECREF (range);

  return int64_range;
}

static int
gi_gst_int64_range_to_value (GValue * value, PyObject * object)
{
  PyObject *range, *min, *max, *step;

  range = PyObject_GetAttrString (object, "range");
  if (range == NULL)
    goto fail;

  min = PyObject_GetAttrString (range, "start");
  if (min == NULL)
    goto fail;

  max = PyObject_GetAttrString (range, "stop");
  if (max == NULL)
    goto fail;

  step = PyObject_GetAttrString (range, "step");
  if (step == NULL)
    goto fail;

  gst_value_set_int64_range_step (value, PyLong_AsLongLong (min),
      PyLong_AsLongLong (max), PyLong_AsLongLong (step));

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError,
      "Object is not compatible with Gst.Int64Range");
  return -1;
}

static PyObject *
gi_gst_double_range_from_value (const GValue * value)
{
  PyObject *double_range_type, *double_range;
  gdouble min, max;

  min = gst_value_get_double_range_min (value);
  max = gst_value_get_double_range_max (value);

  double_range_type = gi_gst_get_type ("DoubleRange");
  double_range = PyObject_CallFunction (double_range_type, "dd", min, max);

  Py_DECREF (double_range_type);

  return double_range;
}

static int
gi_gst_double_range_to_value (GValue * value, PyObject * object)
{
  PyObject *min, *max;

  min = PyObject_GetAttrString (object, "start");
  if (min == NULL)
    goto fail;

  max = PyObject_GetAttrString (object, "stop");
  if (max == NULL)
    goto fail;

  gst_value_set_double_range (value, PyFloat_AsDouble (min),
      PyFloat_AsDouble (max));

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError,
      "Object is not compatible with Gst.DoubleRange");
  return -1;
}

static PyObject *
gi_gst_fraction_range_from_value (const GValue * value)
{
  PyObject *min, *max, *fraction_range_type, *fraction_range;
  const GValue *fraction;

  fraction = gst_value_get_fraction_range_min (value);
  min = gi_gst_fraction_from_value (fraction);

  fraction = gst_value_get_fraction_range_max (value);
  max = gi_gst_fraction_from_value (fraction);

  fraction_range_type = gi_gst_get_type ("FractionRange");
  fraction_range = PyObject_CallFunction (fraction_range_type, "NN", min, max);

  Py_DECREF (fraction_range_type);

  return fraction_range;
}

static int
gi_gst_fraction_range_to_value (GValue * value, PyObject * object)
{
  PyObject *min, *max;
  GValue vmin = G_VALUE_INIT, vmax = G_VALUE_INIT;

  min = PyObject_GetAttrString (object, "start");
  if (min == NULL)
    goto fail;

  max = PyObject_GetAttrString (object, "stop");
  if (max == NULL)
    goto fail;

  g_value_init (&vmin, GST_TYPE_FRACTION);
  if (gi_gst_fraction_to_value (&vmin, min) < 0)
    goto fail;

  g_value_init (&vmax, GST_TYPE_FRACTION);
  if (gi_gst_fraction_to_value (&vmax, max) < 0) {
    g_value_unset (&vmin);
    goto fail;
  }

  gst_value_set_fraction_range (value, &vmin, &vmax);
  g_value_unset (&vmin);
  g_value_unset (&vmax);

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError,
      "Object is not compatible with Gst.FractionRange");
  return -1;
}

static PyObject *
gi_gst_array_from_value (const GValue * value)
{
  PyObject *list, *array_type, *array;
  gint i;

  list = PyList_New (gst_value_array_get_size (value));

  for (i = 0; i < gst_value_array_get_size (value); i++) {
    const GValue *v = gst_value_array_get_value (value, i);
    PyList_SET_ITEM (list, i, pyg_value_as_pyobject (v, TRUE));
  }

  array_type = gi_gst_get_type ("ValueArray");
  array = PyObject_CallFunction (array_type, "N", list);

  Py_DECREF (array_type);

  return array;
}

static int
gi_gst_array_to_value (GValue * value, PyObject * object)
{
  gint len, i;

  len = PySequence_Length (object);

  for (i = 0; i < len; i++) {
    GValue v = G_VALUE_INIT;
    GType type;
    PyObject *item;

    item = PySequence_GetItem (object, i);

    if (item == Py_None)
      type = G_TYPE_POINTER;
    else
      type = pyg_type_from_object ((PyObject *) Py_TYPE (item));

    if (type == G_TYPE_NONE) {
      Py_DECREF (item);
      goto fail;
    }

    g_value_init (&v, type);

    if (pyg_value_from_pyobject (&v, item) < 0) {
      Py_DECREF (item);
      goto fail;
    }

    gst_value_array_append_and_take_value (value, &v);
    Py_DECREF (item);
  }

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError,
      "Object is not compatible with Gst.ValueArray");
  return -1;
}

static PyObject *
gi_gst_bitmask_from_value (const GValue * value)
{
  PyObject *val, *bitmask_type;

  bitmask_type = gi_gst_get_type ("Bitmask");
  val = PyObject_CallFunction (bitmask_type, "L",
      gst_value_get_bitmask (value));
  Py_DECREF (bitmask_type);

  return val;
}

static int
gi_gst_bitmask_to_value (GValue * value, PyObject * object)
{
  PyObject *v = PyObject_GetAttrString (object, "v");
  if (v == NULL)
    goto fail;

  gst_value_set_bitmask (value, PyLong_AsLong (v));

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError, "Object is not compatible with Gst.Bitmask");
  return -1;
}

static PyObject *
gi_gst_list_from_value (const GValue * value)
{
  PyObject *list, *value_list_type, *value_list;
  gint i;

  list = PyList_New (gst_value_list_get_size (value));

  for (i = 0; i < gst_value_list_get_size (value); i++) {
    const GValue *v = gst_value_list_get_value (value, i);
    PyList_SET_ITEM (list, i, pyg_value_as_pyobject (v, TRUE));
  }

  value_list_type = gi_gst_get_type ("ValueList");
  value_list = PyObject_CallFunction (value_list_type, "N", list);

  Py_DECREF (value_list_type);

  return value_list;
}

static int
gi_gst_list_to_value (GValue * value, PyObject * object)
{
  gint len, i;

  len = PySequence_Length (object);

  for (i = 0; i < len; i++) {
    GValue v = G_VALUE_INIT;
    GType type;
    PyObject *item;

    item = PySequence_GetItem (object, i);

    if (item == Py_None)
      type = G_TYPE_POINTER;
    else
      type = pyg_type_from_object ((PyObject *) Py_TYPE (item));

    if (type == G_TYPE_NONE) {
      Py_DECREF (item);
      goto fail;
    }

    g_value_init (&v, type);

    if (pyg_value_from_pyobject (&v, item) < 0) {
      Py_DECREF (item);
      goto fail;
    }

    gst_value_list_append_and_take_value (value, &v);
    Py_DECREF (item);
  }

  return 0;

fail:
  PyErr_SetString (PyExc_KeyError,
      "Object is not compatible with Gst.ValueList");
  return -1;
}

static void
gi_gst_register_types (PyObject * d)
{
  pyg_register_gtype_custom (GST_TYPE_FRACTION,
      gi_gst_fraction_from_value, gi_gst_fraction_to_value);
  pyg_register_gtype_custom (GST_TYPE_INT_RANGE,
      gi_gst_int_range_from_value, gi_gst_int_range_to_value);
  pyg_register_gtype_custom (GST_TYPE_INT64_RANGE,
      gi_gst_int64_range_from_value, gi_gst_int64_range_to_value);
  pyg_register_gtype_custom (GST_TYPE_DOUBLE_RANGE,
      gi_gst_double_range_from_value, gi_gst_double_range_to_value);
  pyg_register_gtype_custom (GST_TYPE_FRACTION_RANGE,
      gi_gst_fraction_range_from_value, gi_gst_fraction_range_to_value);
  pyg_register_gtype_custom (GST_TYPE_ARRAY,
      gi_gst_array_from_value, gi_gst_array_to_value);
  pyg_register_gtype_custom (GST_TYPE_LIST,
      gi_gst_list_from_value, gi_gst_list_to_value);
#if 0
  /* TODO */
  pyg_register_gtype_custom (GST_TYPE_DATE_TIME,
      gi_gst_date_time_from_value, gi_gst_date_time_to_value);
  pyg_register_gtype_custom (GST_TYPE_FLAG_SET,
      gi_gst_flag_set_from_value, gi_gst_flag_set_to_value);
#endif
  pyg_register_gtype_custom (GST_TYPE_BITMASK,
      gi_gst_bitmask_from_value, gi_gst_bitmask_to_value);
}

static int
add_templates (gpointer gclass, PyObject * templates)
{
  if (PyTuple_Check (templates)) {
    gint i, len;
    PyGObject *templ;

    len = PyTuple_Size (templates);
    if (len == 0)
      return 0;

    for (i = 0; i < len; i++) {
      templ = (PyGObject *) PyTuple_GetItem (templates, i);

      if (!pygobject_check (templ, &PyGObject_Type)) {
        PyObject *repr = PyObject_Repr ((PyObject *) templ);
#if PY_VERSION_HEX < 0x03000000
        PyErr_Format (PyExc_TypeError, "expected GObject but got %s",
            PyString_AsString (repr));
#else
        PyErr_Format (PyExc_TypeError, "expected GObject but got %s",
            PyUnicode_AsUTF8 (repr));
#endif
        Py_DECREF (repr);

        return -1;
      } else if (!GST_IS_PAD_TEMPLATE (pygobject_get (templ))) {
        gchar *error =
            g_strdup_printf
            ("entries for __gsttemplates__ must be of type GstPadTemplate (%s)",
            G_OBJECT_TYPE_NAME (pygobject_get (templ)));
        PyErr_SetString (PyExc_TypeError, error);
        g_free (error);

        return -1;
      }
    }

    for (i = 0; i < len; i++) {
      templ = (PyGObject *) PyTuple_GetItem (templates, i);
      gst_element_class_add_pad_template (gclass,
          GST_PAD_TEMPLATE (templ->obj));
    }
    return 0;

  } else if (!pygobject_check (templates, &PyGObject_Type) ||
      GST_IS_PAD_TEMPLATE (pygobject_get (templates)) == FALSE) {
    PyErr_SetString (PyExc_TypeError,
        "entry for __gsttemplates__ must be of type GstPadTemplate");

    return -1;
  }

  gst_element_class_add_pad_template (gclass,
      GST_PAD_TEMPLATE (pygobject_get (templates)));

  return 0;
}

static int
_pygst_element_set_metadata (gpointer gclass, PyObject * metadata)
{

  const gchar *longname, *classification, *description, *author;

  if (!PyTuple_Check (metadata)) {
    PyErr_SetString (PyExc_TypeError, "__gstmetadata__ must be a tuple");
    return -1;
  }
  if (PyTuple_Size (metadata) != 4) {
    PyErr_SetString (PyExc_TypeError,
        "__gstmetadata__ must contain 4 elements");
    return -1;
  }
  if (!PyArg_ParseTuple (metadata, "ssss", &longname, &classification,
          &description, &author)) {
    PyErr_SetString (PyExc_TypeError, "__gstmetadata__ must contain 4 strings");
    return -1;
  }
  GST_DEBUG
      ("setting metadata on gclass %p from __gstmetadata__, longname %s",
      gclass, longname);

  gst_element_class_set_metadata (gclass, longname, classification,
      description, author);
  return 0;
}

static int
_pygst_element_init (gpointer gclass, PyTypeObject * pyclass)
{
  PyObject *templates, *metadata;

  GST_DEBUG ("_pygst_element_init for gclass %p", gclass);
  templates = PyDict_GetItemString (pyclass->tp_dict, "__gsttemplates__");
  if (templates) {
    if (add_templates (gclass, templates) != 0)
      return -1;
  } else {
    PyErr_Clear ();
  }
  metadata = PyDict_GetItemString (pyclass->tp_dict, "__gstmetadata__");
  if (metadata) {
    if (_pygst_element_set_metadata (gclass, metadata) != 0)
      return -1;
    PyDict_DelItemString (pyclass->tp_dict, "__gstmetadata__");
  } else {
    PyErr_Clear ();
  }

  return 0;
}

#include <frameobject.h>

static PyObject *
pygst_debug_log (PyObject * pyobject, PyObject * string, GstDebugLevel level,
    gboolean isgstobject)
{
#ifndef GST_DISABLE_GST_DEBUG
  gchar *str;
  gchar *function;
  gchar *filename;
  int lineno;
  PyFrameObject *frame;
  GObject *object = NULL;

  if (!PyArg_ParseTuple (string, "s:gst.debug_log", &str)) {
    PyErr_SetString (PyExc_TypeError, "Need a string!");
    return NULL;
  }

  frame = PyEval_GetFrame ();
  {
#if PY_VERSION_HEX < 0x030a0000
    PyCodeObject *code = frame->f_code;
#else
    PyCodeObject *code = PyFrame_GetCode (frame);
#endif
    PyObject *utf8;
    const gchar *utf8_str;

    utf8 = PyUnicode_AsUTF8String (code->co_name);
    utf8_str = PyBytes_AS_STRING (utf8);

    function = g_strdup (utf8_str);
    Py_DECREF (utf8);

    utf8 = PyUnicode_AsUTF8String (code->co_filename);
    utf8_str = PyBytes_AS_STRING (utf8);

    filename = g_strdup (utf8_str);
    Py_DECREF (utf8);
#if PY_VERSION_HEX < 0x030a0000
    lineno = PyCode_Addr2Line (frame->f_code, frame->f_lasti);
#else
    lineno = PyFrame_GetLineNumber (frame);
#endif

#if PY_VERSION_HEX >= 0x030a0000
    Py_DECREF (code);
#endif
  }


  /* gst_debug_log : category, level, file, function, line, object, format, va_list */
  if (isgstobject)
    object = G_OBJECT (pygobject_get (pyobject));
  gst_debug_log (python_debug, level, filename, function, lineno, object,
      "%s", str);
  if (function)
    g_free (function);
  if (filename)
    g_free (filename);
#endif
  Py_INCREF (Py_None);
  return Py_None;
}

static PyObject *
_wrap_gst_trace (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_TRACE, FALSE);
}

static PyObject *
_wrap_gst_log (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_LOG, FALSE);
}

static PyObject *
_wrap_gst_debug (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_DEBUG, FALSE);
}

static PyObject *
_wrap_gst_info (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_INFO, FALSE);
}

static PyObject *
_wrap_gst_warning (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_WARNING, FALSE);
}

static PyObject *
_wrap_gst_error (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_ERROR, FALSE);
}

static PyObject *
_wrap_gst_fixme (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_FIXME, FALSE);
}

static PyObject *
_wrap_gst_memdump (PyObject * whatever, PyObject * string)
{
  return pygst_debug_log (whatever, string, GST_LEVEL_MEMDUMP, FALSE);
}

static PyObject *
_remap (GstMapInfo * mapinfo, PyObject * py_mapinfo)
{
  PyObject *success = NULL;
  PyObject *py_cmapinfo = NULL;
  PyObject *py_mview = NULL;
  PyObject *py_memory = NULL;
  PyObject *py_flags = NULL;
  PyObject *py_size = NULL;
  PyObject *py_maxsize = NULL;

  /* Fill and encapsulating the mapinfo pointer */
  py_cmapinfo = PyCapsule_New (mapinfo, "__cmapinfo", NULL);
  if (!py_cmapinfo
      || PyObject_SetAttrString (py_mapinfo, "__cmapinfo", py_cmapinfo))
    goto err;

  /* Fill and create memoryview with compatible flags */
  int flags;
  flags = (mapinfo->flags & GST_MAP_WRITE) ? PyBUF_WRITE : PyBUF_READ;
  py_mview =
      PyMemoryView_FromMemory ((char *) mapinfo->data, mapinfo->size, flags);
  if (!py_mview || PyObject_SetAttrString (py_mapinfo, "data", py_mview))
    goto err;

  /* Fill and box GstMemory into a Gst.Memory */
  py_memory = pyg_boxed_new (_gst_memory_type, mapinfo->memory, FALSE, FALSE);
  if (!py_memory || PyObject_SetAttrString (py_mapinfo, "memory", py_memory))
    goto err;

  /* Fill out Gst.MapInfo with values corresponding to GstMapInfo */
  py_flags = Py_BuildValue ("i", mapinfo->flags);
  if (!py_flags || PyObject_SetAttrString (py_mapinfo, "flags", py_flags))
    goto err;

  py_size = Py_BuildValue ("i", mapinfo->size);
  if (!py_size || PyObject_SetAttrString (py_mapinfo, "size", py_size))
    goto err;

  py_maxsize = Py_BuildValue ("i", mapinfo->maxsize);
  if (!py_maxsize || PyObject_SetAttrString (py_mapinfo, "maxsize", py_maxsize))
    goto err;

  Py_INCREF (Py_True);
  success = Py_True;
  goto end;

err:
  GST_ERROR ("Could not map the Gst.MapInfo PyObject with GstMapInfo");
  if (py_mview)
    PyObject_CallMethod (py_mview, "release", NULL);

end:
  Py_XDECREF (py_cmapinfo);
  Py_XDECREF (py_mview);
  Py_XDECREF (py_memory);
  Py_XDECREF (py_flags);
  Py_XDECREF (py_size);
  Py_XDECREF (py_maxsize);
  return success;
}

static PyObject *
_unmap (GstMapInfo ** mapinfo, PyObject * py_mapinfo)
{
  PyObject *py_cmapinfo = NULL, *py_mview = NULL, *success = NULL;

  if (!PyObject_HasAttrString (py_mapinfo, "__cmapinfo"))
    goto done;

  /* Extract attributes from Gst.MapInfo */
  py_mview = PyObject_GetAttrString (py_mapinfo, "data");
  if (!py_mview)
    goto err;

  /* Call the memoryview.release() Python method, there is no C API */
  if (!PyObject_CallMethod (py_mview, "release", NULL))
    goto err;

  py_cmapinfo = PyObject_GetAttrString (py_mapinfo, "__cmapinfo");
  if (!py_cmapinfo)
    goto err;

  /* Reconstruct GstMapInfo from Gst.MapInfo contents */
  *mapinfo = PyCapsule_GetPointer (py_cmapinfo, "__cmapinfo");
  if (!*mapinfo)
    goto err;

  if (PyObject_DelAttrString (py_mapinfo, "__cmapinfo") == -1)
    goto err;

done:
  Py_INCREF (Py_True);
  success = Py_True;
  goto end;

err:
  GST_ERROR ("Could not unmap the GstMapInfo from Gst.MapInfo PyObject");
  Py_INCREF (Py_False);
  success = Py_False;

end:
  Py_XDECREF (py_mview);
  Py_XDECREF (py_cmapinfo);
  return success;
}

static PyObject *
_gst_memory_override_map (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_memory_type;
  PyObject *py_memory, *py_mapinfo, *success;
  int flags;
  GstMemory *memory;
  GstMapInfo *mapinfo;
  _Bool ok;

  /* Look up Gst.memory, Gst.MapInfo, and Gst.MapFlags parameters */
  gst_memory_type = pygobject_lookup_class (_gst_memory_type);
  if (!PyArg_ParseTuple (args, "O!Oi", gst_memory_type, &py_memory,
          &py_mapinfo, &flags))
    return NULL;

  /* Since Python does only support r/o or r/w it has to be changed to either */
  flags = (flags & GST_MAP_WRITE) ? GST_MAP_READWRITE : GST_MAP_READ;

  /* Extract GstMemory from Gst.Memory parameter */
  memory = GST_MEMORY_CAST (pygobject_get (py_memory));

  /* Map the memory, fill out GstMapInfo */
  mapinfo = g_new0 (GstMapInfo, 1);
  ok = gst_memory_map (memory, mapinfo, flags);
  if (!ok) {
    g_free (mapinfo);
    goto err;
  }

  success = _remap (mapinfo, py_mapinfo);
  if (!success) {
    gst_memory_unmap (memory, mapinfo);
    g_free (mapinfo);
  }
  return success;

err:
  Py_INCREF (Py_False);
  return Py_False;
}

static PyObject *
_gst_mini_object_make_writable (PyObject * self, PyObject * args)
{
  PyObject *py_miniobj, *res;
  GstMiniObject *mini_object;

  py_miniobj = PyTuple_GetItem (args, 0);
  if (py_miniobj == NULL) {
    PyErr_SetString (PyExc_TypeError, "Expected a PyGObject");
    return NULL;
  }

  /* Extract GstCaps from Gst.Caps parameter */
  mini_object = GST_MINI_OBJECT (pygobject_get (py_miniobj));
  if (!gst_mini_object_is_writable (mini_object)) {
    GstMiniObject *writable_obj = gst_mini_object_copy (mini_object);

    GST_DEBUG ("Copied miniobject %p to writable miniobject %p", mini_object,
        writable_obj);

    // Drop our reference to the original miniobject
    gst_mini_object_unref (mini_object);
    pyg_boxed_set_ptr (py_miniobj, writable_obj);
    Py_INCREF (Py_True);
    res = Py_True;
  } else {
    Py_INCREF (Py_False);
    res = Py_False;
  }

  return res;
}

static PyObject *
_gst_mini_object_flags (PyObject * self, PyObject * args)
{
  PyObject *py_miniobj;
  GstMiniObject *mini_object;

  py_miniobj = PyTuple_GetItem (args, 0);
  if (py_miniobj == NULL) {
    PyErr_SetString (PyExc_TypeError, "Expected a PyGObject");
    return NULL;
  }

  mini_object = GST_MINI_OBJECT (pygobject_get (py_miniobj));
  return pyg_flags_from_gtype (gst_mini_object_flags_get_type (),
      mini_object->flags);
}


static PyObject *
_gst_mini_object_set_flags (PyObject * self, PyObject * args)
{
  PyObject *py_miniobj, *py_flags;
  GstMiniObject *mini_object;

  py_miniobj = PyTuple_GetItem (args, 0);
  if (py_miniobj == NULL) {
    PyErr_SetString (PyExc_TypeError, "Expected a PyGObject");
    return NULL;
  }

  mini_object = GST_MINI_OBJECT (pygobject_get (py_miniobj));
  if (!gst_mini_object_is_writable (mini_object)) {

    PyObject *gstmodule = PyImport_ImportModule ("gi.repository.Gst");
    PyObject *exc_class =
        PyObject_GetAttrString (gstmodule, "NotWritableMiniObject");
    PyObject *args_tuple =
        PyUnicode_FromFormat ("Trying to set flags on immutable `%s`",
        g_type_name (GST_MINI_OBJECT_TYPE (mini_object)));
    PyObject *exc_instance = PyObject_Call (exc_class, args_tuple, NULL);

    PyErr_SetObject (exc_class, exc_instance);
    Py_DECREF (exc_instance);

    return NULL;
  }

  py_flags = PyTuple_GetItem (args, 1);
  if (py_flags == NULL) {
    PyErr_SetString (PyExc_TypeError, "Expected a flags");
    return NULL;
  }

  GstMiniObjectFlags flags;
  pyg_flags_get_value (gst_mini_object_flags_get_type (), py_flags, &flags);

  mini_object->flags = flags;

  Py_RETURN_NONE;
}

static PyObject *
_gst_mini_object_is_writable (PyObject * self, PyObject * args)
{
  PyObject *py_miniobj, *res;
  GstMiniObject *mini_object;

  py_miniobj = PyTuple_GetItem (args, 0);
  if (py_miniobj == NULL) {
    PyErr_SetString (PyExc_TypeError, "Expected a PyGObject");
    return NULL;
  }

  mini_object = GST_MINI_OBJECT (pygobject_get (py_miniobj));
  if (gst_mini_object_is_writable (mini_object)) {
    Py_INCREF (Py_True);
    res = Py_True;
  } else {
    Py_INCREF (Py_False);
    res = Py_False;
  }

  return res;
}

static PyObject *
_gst_caps_get_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_caps_type;
  PyObject *py_caps, *py_structure;
  GstCaps *caps;
  gint idx;

  /* Look up Gst.Caps and Gst.Structure parameters */
  gst_caps_type = pygobject_lookup_class (_gst_caps_type);
  if (!PyArg_ParseTuple (args, "O!i", gst_caps_type, &py_caps, &idx))
    return NULL;

  caps = GST_CAPS (pygobject_get (py_caps));

  py_structure = pyg_boxed_new (_gst_structure_type,
      gst_caps_get_structure (caps, idx), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_caps_writable_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_caps_type;
  PyObject *py_caps, *py_structure;
  GstCaps *caps;
  gint idx;

  /* Look up Gst.Caps and Gst.Structure parameters */
  gst_caps_type = pygobject_lookup_class (_gst_caps_type);
  if (!PyArg_ParseTuple (args, "O!i", gst_caps_type, &py_caps, &idx))
    return NULL;

  /* Extract GstCaps from Gst.Caps parameter */
  caps = GST_CAPS (pygobject_get (py_caps));
  if (!gst_caps_is_writable (caps)) {
    PyObject *gstmodule = PyImport_ImportModule ("gi.repository.Gst");
    PyObject *exc_class = PyObject_GetAttrString (gstmodule, "NotWritableCaps");
    PyObject *args_tuple =
        PyUnicode_FromFormat ("Trying to write structure on immutable `%s`",
        g_type_name (GST_MINI_OBJECT_TYPE (caps)));
    PyObject *exc_instance = PyObject_Call (exc_class, args_tuple, NULL);

    PyErr_SetObject (exc_class, exc_instance);
    Py_DECREF (exc_instance);

    return NULL;
  }

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      gst_caps_get_structure (caps, idx), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_query_get_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_query_type;
  PyObject *py_query, *py_structure;
  GstQuery *query;

  /* Look up Gst.Query and Gst.Structure parameters */
  gst_query_type = pygobject_lookup_class (_gst_query_type);
  if (!PyArg_ParseTuple (args, "O!", gst_query_type, &py_query))
    return NULL;

  /* Extract GstQuery from Gst.Query parameter */
  query = GST_QUERY (pygobject_get (py_query));

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      (GstStructure *) gst_query_get_structure (query), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_query_writable_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_query_type;
  PyObject *py_query, *py_structure;
  GstQuery *query;

  /* Look up Gst.Query and Gst.Structure parameters */
  gst_query_type = pygobject_lookup_class (_gst_query_type);
  if (!PyArg_ParseTuple (args, "O!", gst_query_type, &py_query))
    return NULL;

  /* Extract GstQuery from Gst.Query parameter */
  query = GST_QUERY (pygobject_get (py_query));
  if (!gst_query_is_writable (query)) {
    PyObject *gstmodule = PyImport_ImportModule ("gi.repository.Gst");
    PyObject *exc_class =
        PyObject_GetAttrString (gstmodule, "NotWritableQuery");
    PyObject *args_tuple = Py_BuildValue ("(s)",
        "Trying to get writable structure from immutable query");
    PyObject *exc_instance = PyObject_Call (exc_class, args_tuple, NULL);

    PyErr_SetObject (exc_class, exc_instance);
    Py_DECREF (exc_instance);

    return NULL;
  }

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      gst_query_writable_structure (query), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_event_get_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_event_type;
  PyObject *py_event, *py_structure;
  GstEvent *event;

  /* Look up Gst.Event and Gst.Structure parameters */
  gst_event_type = pygobject_lookup_class (_gst_event_type);
  if (!PyArg_ParseTuple (args, "O!", gst_event_type, &py_event))
    return NULL;

  /* Extract GstEvent from Gst.Event parameter */
  event = GST_EVENT (pygobject_get (py_event));

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      (GstStructure *) gst_event_get_structure (event), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_event_writable_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_event_type;
  PyObject *py_event, *py_structure;
  GstEvent *event;

  /* Look up Gst.Event and Gst.Structure parameters */
  gst_event_type = pygobject_lookup_class (_gst_event_type);
  if (!PyArg_ParseTuple (args, "O!", gst_event_type, &py_event))
    return NULL;

  /* Extract GstEvent from Gst.Event parameter */
  event = GST_EVENT (pygobject_get (py_event));
  if (!gst_event_is_writable (event)) {
    PyObject *gstmodule = PyImport_ImportModule ("gi.repository.Gst");
    PyObject *exc_class =
        PyObject_GetAttrString (gstmodule, "NotWritableEvent");
    PyObject *args_tuple = Py_BuildValue ("(s)",
        "Trying to get writable structure from immutable event");
    PyObject *exc_instance = PyObject_Call (exc_class, args_tuple, NULL);

    PyErr_SetObject (exc_class, exc_instance);
    Py_DECREF (exc_instance);

    return NULL;
  }

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      gst_event_writable_structure (event), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_context_get_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_context_type;
  PyObject *py_context, *py_structure;
  GstContext *context;

  /* Look up Gst.Context and Gst.Structure parameters */
  gst_context_type = pygobject_lookup_class (_gst_context_type);
  if (!PyArg_ParseTuple (args, "O!", gst_context_type, &py_context))
    return NULL;

  /* Extract GstContext from Gst.Context parameter */
  context = GST_CONTEXT (pygobject_get (py_context));

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      (GstStructure *) gst_context_get_structure (context), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_context_writable_structure (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_context_type;
  PyObject *py_context, *py_structure;
  GstContext *context;

  /* Look up Gst.Context and Gst.Structure parameters */
  gst_context_type = pygobject_lookup_class (_gst_context_type);
  if (!PyArg_ParseTuple (args, "O!", gst_context_type, &py_context))
    return NULL;

  context = GST_CONTEXT (pygobject_get (py_context));
  if (!gst_context_is_writable (context)) {
    PyObject *gstmodule = PyImport_ImportModule ("gi.repository.Gst");
    PyObject *exc_class =
        PyObject_GetAttrString (gstmodule, "NotWritableContext");
    PyObject *args_tuple = Py_BuildValue ("(s)",
        "Trying to get writable structure from immutable context");
    PyObject *exc_instance = PyObject_Call (exc_class, args_tuple, NULL);

    PyErr_SetObject (exc_class, exc_instance);
    Py_DECREF (exc_instance);

    return NULL;
  }

  /* Get the structure at the given index */
  py_structure = pyg_boxed_new (_gst_structure_type,
      gst_context_writable_structure (context), FALSE, FALSE);

  return py_structure;
}

static PyObject *
_gst_memory_override_unmap (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_memory_type;
  PyObject *py_memory, *py_mapinfo, *success;
  GstMemory *memory;
  GstMapInfo *mapinfo = NULL;

  /* Look up Gst.Buffer and Gst.Mapinfo parameters */
  gst_memory_type = pygobject_lookup_class (_gst_memory_type);
  if (!PyArg_ParseTuple (args, "O!O", gst_memory_type, &py_memory, &py_mapinfo)) {
    PyErr_BadArgument ();
    return NULL;
  }

  success = _unmap (&mapinfo, py_mapinfo);
  if (PyBool_Check (success) && mapinfo) {
    /* Extract GstBuffer from Gst.Buffer parameter */
    memory = GST_MEMORY_CAST (pygobject_get (py_memory));

    /* Unmap the buffer, using reconstructed GstMapInfo */
    gst_memory_unmap (memory, mapinfo);
    g_free (mapinfo);
  }

  return success;
}


static PyObject *
_gst_get_object_ptr (PyObject * self, PyObject * args)
{
  PyObject *first_arg;

  first_arg = PyTuple_GetItem (args, 0);
  gpointer ptr = pygobject_get (first_arg);

  if (ptr == NULL) {
    PyErr_SetString (PyExc_TypeError, "Expected a PyGObject");
    return NULL;
  }

  return PyLong_FromVoidPtr (ptr);
}

static PyObject *
_gst_buffer_override_map_range (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer, *py_mapinfo, *success;
  int flags, range;
  unsigned int idx;
  GstBuffer *buffer;
  GstMapInfo *mapinfo;
  _Bool ok;

  /* Look up Gst.Buffer, Gst.MapInfo, idx, range, and Gst.MapFlags parameters */
  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!OIii", gst_buffer_type, &py_buffer,
          &py_mapinfo, &idx, &range, &flags))
    goto err;

  /* Since Python does only support r/o or r/w it has to be changed to either */
  flags = (flags & GST_MAP_WRITE) ? GST_MAP_READWRITE : GST_MAP_READ;

  /* Extract GstBuffer from Gst.Buffer parameter */
  buffer = GST_BUFFER (pygobject_get (py_buffer));

  /* Map the buffer, fill out GstMapInfo */
  mapinfo = g_new0 (GstMapInfo, 1);
  ok = gst_buffer_map_range (buffer, idx, range, mapinfo, flags);
  if (!ok) {
    g_free (mapinfo);
    goto err;
  }

  success = _remap (mapinfo, py_mapinfo);
  if (!success) {
    gst_buffer_unmap (buffer, mapinfo);
    g_free (mapinfo);
  }
  return success;

err:
  Py_INCREF (Py_False);
  return Py_False;
}

static GstBuffer *
get_buffer_from_args (PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer;

  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!", gst_buffer_type, &py_buffer)) {
    PyErr_BadArgument ();
    return NULL;
  }

  return GST_BUFFER (pygobject_get (py_buffer));
}

#define DECLARE_BUFFER(args) \
  GstBuffer *buffer = get_buffer_from_args(args); \
  if (!buffer) return NULL;

static PyObject *
_gst_buffer_get_dts (PyObject * self, PyObject * args)
{
  DECLARE_BUFFER (args);

  return PyLong_FromUnsignedLong (buffer->dts);
}

static gboolean
mini_object_check_writability (GstMiniObject * mini_object, const gchar * field)
{
  if (!gst_mini_object_is_writable (mini_object)) {

    PyObject *gstmodule = PyImport_ImportModule ("gi.repository.Gst");
    PyObject *exc_class =
        PyObject_GetAttrString (gstmodule, "NotWritableMiniObject");
    PyObject *args_tuple =
        PyUnicode_FromFormat ("Trying to set %s on immutable `%s`",
        field, g_type_name (GST_MINI_OBJECT_TYPE (mini_object)));
    PyObject *exc_instance = PyObject_Call (exc_class, args_tuple, NULL);

    PyErr_SetObject (exc_class, exc_instance);
    Py_DECREF (exc_instance);

    return FALSE;
  }

  return TRUE;
}


static PyObject *
_gst_buffer_set_dts (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer;
  GstClockTime dts;
  GstBuffer *buffer;

  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!K", gst_buffer_type, &py_buffer, &dts)) {
    PyErr_BadArgument ();
    return NULL;
  }

  buffer = GST_BUFFER (pygobject_get (py_buffer));
  if (!mini_object_check_writability (GST_MINI_OBJECT (buffer), "dts"))
    return NULL;

  buffer->dts = dts;

  Py_RETURN_NONE;
}

static PyObject *
_gst_buffer_get_pts (PyObject * self, PyObject * args)
{
  DECLARE_BUFFER (args);

  return PyLong_FromUnsignedLong (buffer->pts);
}

static PyObject *
_gst_buffer_set_pts (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer;
  GstClockTime pts;
  GstBuffer *buffer;

  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!K", gst_buffer_type, &py_buffer, &pts)) {
    PyErr_BadArgument ();
    return NULL;
  }

  buffer = GST_BUFFER (pygobject_get (py_buffer));
  if (!mini_object_check_writability (GST_MINI_OBJECT (buffer), "pts"))
    return NULL;

  buffer->pts = pts;

  Py_RETURN_NONE;
}

static PyObject *
_gst_buffer_get_duration (PyObject * self, PyObject * args)
{
  DECLARE_BUFFER (args);

  return PyLong_FromUnsignedLong (buffer->duration);
}

static PyObject *
_gst_buffer_set_duration (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer;
  GstClockTime duration;
  GstBuffer *buffer;

  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!K", gst_buffer_type, &py_buffer, &duration)) {
    PyErr_BadArgument ();
    return NULL;
  }

  buffer = GST_BUFFER (pygobject_get (py_buffer));
  if (!mini_object_check_writability (GST_MINI_OBJECT (buffer), "duration"))
    return NULL;

  buffer->duration = duration;

  Py_RETURN_NONE;
}

static PyObject *
_gst_buffer_get_offset (PyObject * self, PyObject * args)
{
  DECLARE_BUFFER (args);

  return PyLong_FromUnsignedLong (buffer->offset);
}

static PyObject *
_gst_buffer_set_offset (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer;
  GstClockTime offset;
  GstBuffer *buffer;

  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!K", gst_buffer_type, &py_buffer, &offset)) {
    PyErr_BadArgument ();
    return NULL;
  }

  buffer = GST_BUFFER (pygobject_get (py_buffer));
  if (!mini_object_check_writability (GST_MINI_OBJECT (buffer), "offset"))
    return NULL;

  buffer->offset = offset;

  Py_RETURN_NONE;
}

static PyObject *
_gst_buffer_get_offset_end (PyObject * self, PyObject * args)
{
  DECLARE_BUFFER (args);

  return PyLong_FromUnsignedLong (buffer->offset_end);
}

static PyObject *
_gst_buffer_set_offset_end (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer;
  GstClockTime offset_end;
  GstBuffer *buffer;

  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!K", gst_buffer_type, &py_buffer, &offset_end)) {
    PyErr_BadArgument ();
    return NULL;
  }

  buffer = GST_BUFFER (pygobject_get (py_buffer));
  if (!mini_object_check_writability (GST_MINI_OBJECT (buffer), "offset_end"))
    return NULL;

  buffer->offset_end = offset_end;

  Py_RETURN_NONE;
}

static PyObject *
_gst_buffer_override_map (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buffer_type;
  PyObject *py_buffer, *py_mapinfo, *success;
  int flags;
  GstBuffer *buffer;
  GstMapInfo *mapinfo;
  _Bool ok;

  /* Look up Gst.Buffer, Gst.MapInfo, and Gst.MapFlags parameters */
  gst_buffer_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!Oi", gst_buffer_type, &py_buffer, &py_mapinfo,
          &flags)) {
    PyErr_BadArgument ();
    return NULL;
  }

  /* Since Python does only support r/o or r/w it has to be changed to either */
  flags = (flags & GST_MAP_WRITE) ? GST_MAP_READWRITE : GST_MAP_READ;

  /* Extract GstBuffer from Gst.Buffer parameter */
  buffer = GST_BUFFER (pygobject_get (py_buffer));

  /* Map the buffer, fill out GstMapInfo */
  mapinfo = g_new0 (GstMapInfo, 1);
  ok = gst_buffer_map (buffer, mapinfo, flags);
  if (!ok) {
    g_free (mapinfo);
    goto err;
  }

  success = _remap (mapinfo, py_mapinfo);
  if (!success) {
    gst_buffer_unmap (buffer, mapinfo);
    g_free (mapinfo);
  }
  return success;

err:
  Py_INCREF (Py_False);
  return Py_False;
}

static PyObject *
_gst_buffer_override_unmap (PyObject * self, PyObject * args)
{
  PyTypeObject *gst_buf_type;
  PyObject *py_buffer, *py_mapinfo, *success;
  GstBuffer *buffer;
  GstMapInfo *mapinfo = NULL;

  /* Look up Gst.Buffer and Gst.Mapinfo parameters */
  gst_buf_type = pygobject_lookup_class (_gst_buffer_type);
  if (!PyArg_ParseTuple (args, "O!O", gst_buf_type, &py_buffer, &py_mapinfo)) {
    PyErr_BadArgument ();
    return NULL;
  }

  success = _unmap (&mapinfo, py_mapinfo);
  if (PyBool_Check (success) && mapinfo) {
    /* Extract GstBuffer from Gst.Buffer parameter */
    buffer = GST_BUFFER (pygobject_get (py_buffer));

    /* Unmap the buffer, using reconstructed GstMapInfo */
    gst_buffer_unmap (buffer, mapinfo);
    g_free (mapinfo);
  }

  return success;
}

/* *INDENT-OFF* */
static PyMethodDef _gi_gst_functions[] = {
  {"trace", (PyCFunction) _wrap_gst_trace, METH_VARARGS, NULL},
  {"log", (PyCFunction) _wrap_gst_log, METH_VARARGS, NULL},
  {"debug", (PyCFunction) _wrap_gst_debug, METH_VARARGS, NULL},
  {"info", (PyCFunction) _wrap_gst_info, METH_VARARGS, NULL},
  {"warning", (PyCFunction) _wrap_gst_warning, METH_VARARGS, NULL},
  {"error", (PyCFunction) _wrap_gst_error, METH_VARARGS, NULL},
  {"fixme", (PyCFunction) _wrap_gst_fixme, METH_VARARGS, NULL},
  {"memdump", (PyCFunction) _wrap_gst_memdump, METH_VARARGS, NULL},
  {"buffer_override_map_range", (PyCFunction) _gst_buffer_override_map_range, METH_VARARGS, NULL},
  {"buffer_override_map", (PyCFunction) _gst_buffer_override_map, METH_VARARGS, NULL},
  {"buffer_override_unmap", (PyCFunction) _gst_buffer_override_unmap, METH_VARARGS, NULL},
  {"memory_override_map", (PyCFunction) _gst_memory_override_map, METH_VARARGS, NULL},
  {"memory_override_unmap", (PyCFunction) _gst_memory_override_unmap, METH_VARARGS, NULL},

  {"caps_get_structure", (PyCFunction) _gst_caps_get_structure, METH_VARARGS, NULL},
  {"caps_writable_structure", (PyCFunction) _gst_caps_writable_structure, METH_VARARGS, NULL},

  {"mini_object_make_writable", (PyCFunction) _gst_mini_object_make_writable, METH_VARARGS, NULL},
  {"mini_object_is_writable", (PyCFunction) _gst_mini_object_is_writable, METH_VARARGS, NULL},
  {"mini_object_flags", (PyCFunction) _gst_mini_object_flags, METH_VARARGS, NULL},
  {"mini_object_set_flags", (PyCFunction) _gst_mini_object_set_flags, METH_VARARGS, NULL},

  {"event_get_structure", (PyCFunction) _gst_event_get_structure, METH_VARARGS, NULL},
  {"event_writable_structure", (PyCFunction) _gst_event_writable_structure, METH_VARARGS, NULL},

  {"query_get_structure", (PyCFunction) _gst_query_get_structure, METH_VARARGS, NULL},
  {"query_writable_structure", (PyCFunction) _gst_query_writable_structure, METH_VARARGS, NULL},

  {"context_get_structure", (PyCFunction) _gst_context_get_structure, METH_VARARGS, NULL},
  {"context_writable_structure", (PyCFunction) _gst_context_writable_structure, METH_VARARGS, NULL},

  {"buffer_get_pts", (PyCFunction) _gst_buffer_get_pts, METH_VARARGS, NULL},
  {"buffer_set_pts", (PyCFunction) _gst_buffer_set_pts, METH_VARARGS, NULL},
  {"buffer_get_duration", (PyCFunction) _gst_buffer_get_duration, METH_VARARGS, NULL},
  {"buffer_set_duration", (PyCFunction) _gst_buffer_set_duration, METH_VARARGS, NULL},
  {"buffer_get_dts", (PyCFunction) _gst_buffer_get_dts, METH_VARARGS, NULL},
  {"buffer_set_dts", (PyCFunction) _gst_buffer_set_dts, METH_VARARGS, NULL},
  {"buffer_get_offset", (PyCFunction) _gst_buffer_get_offset, METH_VARARGS, NULL},
  {"buffer_set_offset", (PyCFunction) _gst_buffer_set_offset, METH_VARARGS, NULL},
  {"buffer_get_offset_end", (PyCFunction) _gst_buffer_get_offset_end, METH_VARARGS, NULL},
  {"buffer_set_offset_end", (PyCFunction) _gst_buffer_set_offset_end, METH_VARARGS, NULL},

  {"_get_object_ptr", (PyCFunction) _gst_get_object_ptr, METH_VARARGS, NULL},
  {NULL, NULL, 0, NULL}
};
/* *INDENT-ON* */

static const gchar *const *
py_uri_handler_get_protocols (GType type)
{
  /* FIXME: Ideally we should be able to free the list of protocols on
   * deinitialization */
  return g_type_get_qdata (type, URI_HANDLER_PROTOCOLS_QUARK);
}

static GstURIType
py_uri_handler_get_type (GType type)
{
  return GPOINTER_TO_INT (g_type_get_qdata (type, URI_HANDLER_URITYPE_QUARK));
}

static const GStrv
py_uri_handler_get_protocols_from_pyobject (PyObject * protocols)
{
  GStrv res = NULL;

  if (PyTuple_Check (protocols)) {
    gint i, len;

    len = PyTuple_Size (protocols);
    if (len == 0) {
      PyErr_Format (PyExc_TypeError,
          "Empty tuple for GstUriHandler.__protocols__");
      goto err;
    }

    res = g_malloc0 ((len + 1) * sizeof (gchar *));
    for (i = 0; i < len; i++) {
      PyObject *protocol = (PyObject *) PyTuple_GetItem (protocols, i);

      if (!PyUnicode_Check (protocol)) {
        Py_DECREF (protocol);
        goto err;
      }

      res[i] = g_strdup (PyUnicode_AsUTF8 (protocol));
    }
  } else {
    PyErr_Format (PyExc_TypeError,
        "invalid type for GstUriHandler.__protocols__" " Should be a tuple");
    goto err;
  }

  return res;

err:
  Py_DECREF (protocols);
  g_strfreev (res);
  return FALSE;
}

static void
uri_handler_iface_init (GstURIHandlerInterface * iface, PyTypeObject * pytype)
{
  gint uritype;
  GStrv protocols;
  PyObject *pyprotocols = pytype ? PyObject_GetAttrString ((PyObject *) pytype,
      "__protocols__") : NULL;
  PyObject *pyuritype = pytype ? PyObject_GetAttrString ((PyObject *) pytype,
      "__uritype__") : NULL;
  GType gtype = pyg_type_from_object ((PyObject *) pytype);

  if (pyprotocols == NULL) {
    PyErr_Format (PyExc_KeyError, "__protocols__ missing in %s",
        pytype->tp_name);
    goto done;
  }

  if (pyuritype == NULL) {
    PyErr_Format (PyExc_KeyError, "__pyuritype__ missing in %s",
        pytype->tp_name);
    goto done;
  }

  protocols = py_uri_handler_get_protocols_from_pyobject (pyprotocols);
  if (!protocols)
    goto done;

  if (pyg_enum_get_value (GST_TYPE_URI_TYPE, pyuritype, &uritype) < 0) {
    PyErr_SetString (PyExc_TypeError,
        "entry for __uritype__ must be of type GstURIType");
    goto done;
  }

  iface->get_protocols = py_uri_handler_get_protocols;
  g_type_set_qdata (gtype, URI_HANDLER_PROTOCOLS_QUARK, protocols);

  iface->get_type = py_uri_handler_get_type;
  g_type_set_qdata (gtype, URI_HANDLER_URITYPE_QUARK,
      GINT_TO_POINTER (uritype));

done:
  if (pyprotocols)
    Py_DECREF (pyprotocols);
  if (pyuritype)
    Py_DECREF (pyuritype);
}

static const GInterfaceInfo GstURIHandlerInterfaceInfo = {
  (GInterfaceInitFunc) uri_handler_iface_init,
  NULL,
  NULL
};

PYGLIB_MODULE_START (_gi_gst, "_gi_gst")
{
  PyObject *d;

  /* gst should have been initialized already */

  /* Initialize debugging category */
  GST_DEBUG_CATEGORY_INIT (pygst_debug, "pygst", 0,
      "GStreamer python bindings");
  GST_DEBUG_CATEGORY_INIT (python_debug, "python", GST_DEBUG_FG_GREEN,
      "python code using gst-python");

  pygobject_init (3, 0, 0);

  d = PyModule_GetDict (module);
  gi_gst_register_types (d);
  pyg_register_class_init (GST_TYPE_ELEMENT, _pygst_element_init);
  pyg_register_interface_info (GST_TYPE_URI_HANDLER,
      &GstURIHandlerInterfaceInfo);
}

PYGLIB_MODULE_END;

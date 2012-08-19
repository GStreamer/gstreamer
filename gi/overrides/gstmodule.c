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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David I. Lehn <dlehn@users.sourceforge.net>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <Python.h>
#include <pygobject.h>
#include <gst/gst.h>

#include <locale.h>

#define PYGLIB_MODULE_START(symbol, modname)            \
DL_EXPORT(void) init##symbol(void)          \
{                                                       \
    PyObject *module;                                   \
    module = Py_InitModule(modname, symbol##_functions);
#define PYGLIB_MODULE_END }

static PyObject *
gi_gst_fraction_from_value (const GValue * value)
{
  PyObject *module, *dict, *fraction_type, *args, *fraction;
  gint numerator, denominator;

  numerator = gst_value_get_fraction_numerator (value);
  denominator = gst_value_get_fraction_denominator (value);

  module = PyImport_ImportModule ("gi.repository.Gst");
  dict = PyModule_GetDict (module);
  /* For some reson we need this intermediary step */
  module = PyMapping_GetItemString (dict, "_overrides_module");
  dict = PyModule_GetDict (module);
  fraction_type = PyMapping_GetItemString (dict, "Fraction");

  args = Py_BuildValue ("(ii)", numerator, denominator);
  fraction = PyObject_Call (fraction_type, args, NULL);
  Py_DECREF (args);
  Py_DECREF (fraction_type);
  Py_DECREF (module);

  return fraction;
}

static int
gi_gst_fraction_to_value (GValue * value, PyObject * object)
{
  PyObject *numerator, *denominator;

  numerator = PyObject_GetAttrString (object, "num");
  if (numerator == NULL)
    goto fail;

  denominator = PyObject_GetAttrString (object, "denom");
  if (denominator == NULL)
    goto fail;

  gst_value_set_fraction (value,
      PyLong_AsLong (numerator), PyLong_AsLong (denominator));

  return 0;

fail:
  return -1;
}

void
gi_gst_register_types (PyObject * d)
{
  pyg_register_gtype_custom (GST_TYPE_FRACTION,
      gi_gst_fraction_from_value, gi_gst_fraction_to_value);
}

static int
add_templates (gpointer gclass, PyObject * templates)
{
  gint i, len;
  PyGObject *templ;

  if (PyTuple_Check (templates)) {

    len = PyTuple_Size (templates);
    if (len == 0)
      return 0;

    for (i = 0; i < len; i++) {
      templ = (PyGObject *) PyTuple_GetItem (templates, i);
      if (GST_IS_PAD_TEMPLATE (pygobject_get (templ)) == FALSE) {
        PyErr_SetString (PyExc_TypeError,
            "entries for __gsttemplates__ must be of type GstPadTemplate");
        return -1;
      }
    }

    for (i = 0; i < len; i++) {
      templ = (PyGObject *) PyTuple_GetItem (templates, i);
      gst_element_class_add_pad_template (gclass,
          GST_PAD_TEMPLATE (templ->obj));
    }
    return 0;

  }

  if (GST_IS_PAD_TEMPLATE (pygobject_get (templ)) == FALSE) {
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

static PyMethodDef _gi_gst_functions[] = { {0,} };

PYGLIB_MODULE_START (_gi_gst, "_gi_gst")
{
  PyObject *d;

  pygobject_init (3, 0, 0);

  d = PyModule_GetDict (module);
  gi_gst_register_types (d);
  pyg_register_class_init (GST_TYPE_ELEMENT, _pygst_element_init);
}

PYGLIB_MODULE_END;

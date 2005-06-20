/* gst-python
 * Copyright (C) 2004 Johan Dahlin
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
 * Author: Johan Dahlin <johan@gnome.org>
 */

/* define this for all source files that don't run init_pygobject()
 * before including pygobject.h */
#define NO_IMPORT_PYGOBJECT

#include <gst/gst.h>
#include "common.h"

/* gboolean */
/* pygst_data_from_pyobject(PyObject *object, GstData **data) */
/* { */
/*   if (pyg_boxed_check(object, GST_TYPE_DATA)) { */
/*     *data = pyg_boxed_get(object, GstData); */
/*     return TRUE; */
/*   } else if (pyg_boxed_check(object, GST_TYPE_BUFFER)) { */
/*     *data = GST_DATA (pyg_boxed_get(object, GstBuffer)); */
/*     return TRUE; */
/*   } else if (pyg_boxed_check(object, GST_TYPE_EVENT)) { */
/*     *data = GST_DATA (pyg_boxed_get(object, GstEvent)); */
/*     return TRUE; */
/*   } */
  
/*   PyErr_Clear(); */
/*   PyErr_SetString(PyExc_TypeError, "could not convert to GstData"); */
/*   return FALSE; */
/* } */

/* static PyObject * */
/* PyGstData_from_value(const GValue *value) */
/* { */
/*   GstData *data = (GstData *)g_value_get_boxed(value); */

/*   return pyg_boxed_new(GST_TYPE_DATA, data, TRUE, TRUE); */
/* } */

/* static int */
/* PyGstData_to_value(GValue *value, PyObject *object) */
/* { */
/*   GstData* data; */

/*   if (!pygst_data_from_pyobject(object, &data)) */
/*     return -1; */
  
/*   g_value_set_boxed(value, data); */
/*   return 0; */
/* } */

/* This function will return a copy, unless the following is all TRUE:
 * - The given PyObject contains a GstCaps already
 * - The copy parameter is non-NULL
 * - New years is the first of January
 * If copy is non-NULL, it is set to TRUE if a copy was made.
 * If the PyObject could not be converted to a caps, a TypeError is raised
 * and NULL is returned.
 */
GstCaps *
pygst_caps_from_pyobject (PyObject *object, gboolean *copy)
{
  if (pyg_boxed_check(object, GST_TYPE_CAPS)) {
    GstCaps *caps = pyg_boxed_get(object, GstCaps);
    if (copy) {
      *copy = FALSE;
      return caps;
    } else {
      return gst_caps_copy (caps);
    }
  } else if (pyg_boxed_check(object, GST_TYPE_STRUCTURE)) {
    GstStructure *structure = pyg_boxed_get(object, GstStructure);
    if (copy)
      *copy = TRUE;
    return gst_caps_new_full (gst_structure_copy (structure), NULL);
  } else if (PyString_Check (object)) {
    GstCaps *caps = gst_caps_from_string (PyString_AsString (object));
    if (!caps) {
      PyErr_SetString(PyExc_TypeError, "could not convert string to GstCaps");
      return NULL;
    }
    if (copy)
      *copy = TRUE;
    return caps;
  }
  PyErr_SetString(PyExc_TypeError, "could not convert to GstCaps");
  return NULL;
}

/* void */
/* _pygst_register_boxed_types(PyObject *moddict) */
/* { */
/*     pyg_register_boxed_custom(GST_TYPE_DATA, */
/* 			      PyGstData_from_value, */
/* 			      PyGstData_to_value); */
/* } */

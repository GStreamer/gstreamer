/* gst-python
 * Copyright (C) 2004 Andy Wingo
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
 * Author: Andy Wingo <wingo@pobox.com>
 */

/* define this for all source files that don't run init_pygobject()
 * before including pygobject.h */
#define NO_IMPORT_PYGOBJECT

#include "pygstvalue.h"


static PyObject *gstvalue_class = NULL;
static PyObject *gstfourcc_class = NULL;
static PyObject *gstintrange_class = NULL;
static PyObject *gstdoublerange_class = NULL;
static PyObject *gstfraction_class = NULL;
static PyObject *gstfractionrange_class = NULL;

/**
 * pygst_value_as_pyobject:
 * @value: the GValue object.
 * @copy_boxed: true if boxed values should be copied.
 *
 * This function creates/returns a Python wrapper object that
 * represents the GValue passed as an argument.
 *
 * Returns: a PyObject representing the value.
 */
PyObject *
pygst_value_as_pyobject(const GValue *value, gboolean copy_boxed)
{
  PyObject *ret = pyg_value_as_pyobject(value, copy_boxed);
  if (!ret) {
    PyErr_Clear();
    if (GST_VALUE_HOLDS_FOURCC (value)) {
      gchar str[5];
      g_snprintf (str, 5, "%"GST_FOURCC_FORMAT,
                  GST_FOURCC_ARGS (gst_value_get_fourcc (value)));
      ret = PyObject_Call (gstfourcc_class, Py_BuildValue ("(s)", str), NULL);
    } else if (GST_VALUE_HOLDS_INT_RANGE (value)) {
      ret = PyObject_Call
        (gstintrange_class,
         Py_BuildValue ("ii",
                        gst_value_get_int_range_min (value),
                        gst_value_get_int_range_max (value)),
         NULL);
    } else if (GST_VALUE_HOLDS_DOUBLE_RANGE (value)) {
      ret = PyObject_Call
        (gstdoublerange_class,
         Py_BuildValue ("dd",
                        gst_value_get_double_range_min (value),
                        gst_value_get_double_range_max (value)),
         NULL);
    } else if (GST_VALUE_HOLDS_LIST (value)) {
      int i, len;
      len = gst_value_list_get_size (value);
      ret = PyList_New (len);
      for (i=0; i<len; i++) {
        PyList_SetItem (ret, i,
                        pygst_value_as_pyobject
                        (gst_value_list_get_value (value, i), copy_boxed));
      }
    } else if (GST_VALUE_HOLDS_ARRAY (value)) {
      int i, len;
      len = gst_value_array_get_size (value);
      ret = PyTuple_New (len);
      for (i=0; i<len; i++) {
        PyTuple_SetItem (ret, i,
                         pygst_value_as_pyobject
                         (gst_value_array_get_value (value, i), copy_boxed));
      }
    } else if (GST_VALUE_HOLDS_FRACTION (value)) {
      ret = PyObject_Call
        (gstfraction_class,
         Py_BuildValue ("ii",
                        gst_value_get_fraction_numerator (value),
                        gst_value_get_fraction_denominator (value)),
         NULL);
    } else if (GST_VALUE_HOLDS_FRACTION_RANGE (value)) {
      const GValue	*min, *max;
      min = gst_value_get_fraction_range_min (value);
      max = gst_value_get_fraction_range_max (value);
      ret = PyObject_Call
	(gstfractionrange_class,
	 Py_BuildValue ("OO",
			pygst_value_as_pyobject (min, copy_boxed),
			pygst_value_as_pyobject (max, copy_boxed)),
	 NULL);
    } else {
      gchar buf[256];
      g_snprintf (buf, 256, "unknown type: %s", g_type_name (G_VALUE_TYPE (value)));
      PyErr_SetString(PyExc_TypeError, buf);
    }
  }
  return ret;
}

#define VALUE_TYPE_CHECK(v, t) \
G_STMT_START{\
if (!G_VALUE_HOLDS (v, t)) {\
  gchar errbuf[256];\
  g_snprintf (errbuf, 256, "Could not convert %s to %s",\
              g_type_name (t), g_type_name (G_VALUE_TYPE (v)));\
  PyErr_SetString (PyExc_TypeError, errbuf);\
  return -1;\
}}G_STMT_END

gboolean
pygst_value_init_for_pyobject (GValue *value, PyObject *obj) 
{
  GType t;
  
  if (!(t = pyg_type_from_object ((PyObject*)obj->ob_type))) {
    if (PyObject_IsInstance (obj, gstvalue_class)) {
      PyErr_Clear ();
      if (PyObject_IsInstance (obj, gstfourcc_class))
        t = GST_TYPE_FOURCC;
      else if (PyObject_IsInstance (obj, gstintrange_class))
        t = GST_TYPE_INT_RANGE;
      else if (PyObject_IsInstance (obj, gstdoublerange_class))
        t = GST_TYPE_DOUBLE_RANGE;
      else if (PyObject_IsInstance (obj, gstfraction_class))
        t = GST_TYPE_FRACTION;
      else if (PyObject_IsInstance (obj, gstfractionrange_class))
	t = GST_TYPE_FRACTION_RANGE;
      else {
        PyErr_SetString(PyExc_TypeError, "Unexpected gst.Value instance");
        return FALSE;
      }
    } else if (PyTuple_Check (obj)) {
      PyErr_Clear ();
      t = GST_TYPE_ARRAY;
    } else if (PyList_Check (obj)) {
      PyErr_Clear ();
      t = GST_TYPE_LIST;
    } else {
      /* pyg_type_from_object already set the error */
      return FALSE;
    }
  }
  g_value_init (value, t);
  return TRUE;
}

/**
 * pygst_value_from_pyobject:
 * @value: the GValue object to store the converted value in.
 * @obj: the Python object to convert.
 *
 * This function converts a Python object and stores the result in a
 * GValue.  The GValue must be initialised in advance with
 * g_value_init().  If the Python object can't be converted to the
 * type of the GValue, then an error is returned.
 *
 * Returns: 0 on success, -1 on error.
 */
int
pygst_value_from_pyobject (GValue *value, PyObject *obj)
{
  GType f = g_type_fundamental (G_VALUE_TYPE (value));
  
  /* work around a bug in pygtk whereby pyg_value_from_pyobject claims success
     for unknown fundamental types without actually doing anything */
  if (f < G_TYPE_MAKE_FUNDAMENTAL (G_TYPE_RESERVED_USER_FIRST)
      && pyg_value_from_pyobject (value, obj) == 0) {
    return 0;
  } else if (PyObject_IsInstance (obj, gstvalue_class)) {
    PyErr_Clear ();

    if (PyObject_IsInstance (obj, gstfourcc_class)) {
      PyObject *pystr;
      gchar *str;
      VALUE_TYPE_CHECK (value, GST_TYPE_FOURCC);
      if (!(pystr = PyObject_GetAttrString (obj, "fourcc")))
        return -1;
      if (!(str = PyString_AsString (pystr)))
        return -1;
      g_assert (strlen (str) == 4);
      gst_value_set_fourcc (value, GST_STR_FOURCC (str));
    } else if (PyObject_IsInstance (obj, gstintrange_class)) {
      PyObject *pyval;
      long low, high;
      VALUE_TYPE_CHECK (value, GST_TYPE_INT_RANGE);
      if (!(pyval = PyObject_GetAttrString (obj, "low")))
        return -1;
      low = PyInt_AsLong (pyval);
      g_assert (G_MININT <= low && low <= G_MAXINT);
      if (!(pyval = PyObject_GetAttrString (obj, "high")))
        return -1;
      high = PyInt_AsLong (pyval);
      g_assert (G_MININT <= high && high <= G_MAXINT);
      gst_value_set_int_range (value, (int)low, (int)high);
    } else if (PyObject_IsInstance (obj, gstdoublerange_class)) {
      PyObject *pyval;
      double low, high;
      VALUE_TYPE_CHECK (value, GST_TYPE_DOUBLE_RANGE);
      if (!(pyval = PyObject_GetAttrString (obj, "low")))
        return -1;
      low = PyFloat_AsDouble (pyval);
      if (!(pyval = PyObject_GetAttrString (obj, "high")))
        return -1;
      high = PyFloat_AsDouble (pyval);
      gst_value_set_double_range (value, low, high);
    } else if (PyObject_IsInstance (obj, gstfraction_class)) {
      PyObject *pyval;
      long num, denom;
      VALUE_TYPE_CHECK (value, GST_TYPE_FRACTION);
      if (!(pyval = PyObject_GetAttrString (obj, "num")))
        return -1;
      num = PyInt_AsLong (pyval);
      g_assert (G_MININT <= num && num <= G_MAXINT);
      if (!(pyval = PyObject_GetAttrString (obj, "denom")))
        return -1;
      denom = PyInt_AsLong (pyval);
      g_assert (G_MININT <= denom && denom <= G_MAXINT);
      gst_value_set_fraction (value, (int)num, (int)denom);
    } else if (PyObject_IsInstance (obj, gstfractionrange_class)) {
      GValue	low = {0, };
      GValue	high = {0, };
      PyObject	*pylow, *pyhigh;

      VALUE_TYPE_CHECK (value, GST_TYPE_FRACTION_RANGE);
      if (!(pylow = PyObject_GetAttrString (obj, "low")))
	return -1;
      if (!pygst_value_init_for_pyobject (&low, pylow))
	return -1;
      if (pygst_value_from_pyobject (&low, pylow) != 0)
	return -1;
      
      if (!(pyhigh = PyObject_GetAttrString (obj, "high")))
	return -1;
      if (!pygst_value_init_for_pyobject (&high, pyhigh))
	return -1;
      if (pygst_value_from_pyobject (&high, pyhigh) != 0)
	return -1;

      gst_value_set_fraction_range (value, &low, &high);
    } else {
      gchar buf[256];
      gchar *str = PyString_AsString (PyObject_Repr(obj));
      g_snprintf(buf, 256, "Unknown gst.Value type: %s", str);
      PyErr_SetString(PyExc_TypeError, buf);
      return -1;
    }
    return 0;
  } else if (PyTuple_Check (obj)) {
    gint i, len;
    PyErr_Clear ();
    VALUE_TYPE_CHECK (value, GST_TYPE_ARRAY);
    len = PyTuple_Size (obj);
    for (i = 0; i < len; i++) {
      PyObject *o;
      GValue new = {0,};
      o = PyTuple_GetItem (obj, i);
      if (!pygst_value_init_for_pyobject (&new, o))
        return -1;
      if (pygst_value_from_pyobject (&new, o) != 0) {
        g_value_unset (&new);
        return -1;
      }
      gst_value_array_append_value (value, &new);
      g_value_unset (&new);
    }
    return 0;
  } else if (PyList_Check (obj)) {
    gint i, len;
    PyErr_Clear ();
    VALUE_TYPE_CHECK (value, GST_TYPE_LIST);
    len = PyList_Size (obj);
    for (i = 0; i < len; i++) {
      PyObject *o;
      GValue new = {0,};
      o = PyList_GetItem (obj, i);
      if (!pygst_value_init_for_pyobject (&new, o))
        return -1;
      if (pygst_value_from_pyobject (&new, o) != 0) {
        g_value_unset (&new);
        return -1;
      }
      gst_value_list_append_value (value, &new);
      g_value_unset (&new);
    }
    return 0;
  } else {
    return -1;
  }
}

#define NULL_CHECK(o) if (!o) goto err

gboolean
pygst_value_init(void)
{
  PyObject *module, *dict;
  
  if ((module = PyImport_ImportModule("gst")) == NULL)
    return FALSE;
    
  dict = PyModule_GetDict (module);

  gstvalue_class = (PyObject*)PyDict_GetItemString (dict, "Value");
  NULL_CHECK (gstvalue_class);
  gstfourcc_class = (PyObject*)PyDict_GetItemString (dict, "Fourcc");
  NULL_CHECK (gstfourcc_class);
  gstintrange_class = (PyObject*)PyDict_GetItemString (dict, "IntRange");
  NULL_CHECK (gstintrange_class);
  gstdoublerange_class = (PyObject*)PyDict_GetItemString (dict, "DoubleRange");
  NULL_CHECK (gstdoublerange_class);
  gstfraction_class = (PyObject*)PyDict_GetItemString (dict, "Fraction");
  NULL_CHECK (gstfraction_class);
  gstfractionrange_class = (PyObject*)PyDict_GetItemString (dict, "FractionRange");
  NULL_CHECK (gstfractionrange_class);
  return TRUE;

err:
  PyErr_SetString (PyExc_ImportError,
                   "Failed to get GstValue classes from gst module");
  return FALSE;
}

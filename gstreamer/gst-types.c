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

#include <gst/gst.h>
#include <pygobject.h>

gboolean
pygst_data_from_pyobject(PyObject *object, GstData **data)
{
  g_return_val_if_fail(*data != NULL, FALSE);

  if (pyg_boxed_check(object, GST_TYPE_DATA)) {
    *data = pyg_boxed_get(object, GstData);
    return TRUE;
  } else if (pyg_boxed_check(object, GST_TYPE_BUFFER)) {
    *data = GST_DATA (pyg_boxed_get(object, GstBuffer));
    return TRUE;
  } else if (pyg_boxed_check(object, GST_TYPE_EVENT)) {
    *data = GST_DATA (pyg_boxed_get(object, GstEvent));
    return TRUE;
  }
  
  PyErr_Clear();
  PyErr_SetString(PyExc_TypeError, "could not convert to GstData");
  return FALSE;
}

static PyObject *
PyGstData_from_value(const GValue *value)
{
  GstData *data = (GstData *)g_value_get_boxed(value);

  return pyg_boxed_new(GST_TYPE_DATA, data, TRUE, TRUE);
}

static int
PyGstData_to_value(GValue *value, PyObject *object)
{
  GstData* data;

  if (!pygst_data_from_pyobject(object, &data))
    return -1;

  g_value_set_boxed(value, &data);
  return 0;
}

/* We have to set ob_type here because stupid win32 does not allow you
 * to use variables from another dll in a global variable initialisation.
 */
void
_pygst_register_boxed_types(PyObject *moddict)
{
    pyg_register_boxed_custom(GST_TYPE_DATA,
			      PyGstData_from_value,
			      PyGstData_to_value);
}

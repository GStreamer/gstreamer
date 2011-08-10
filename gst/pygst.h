/* -*- Mode: C; ; c-file-style: "python" -*- */
/* gst-python
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#ifndef _PYGST_H_
#define _PYGST_H_

#include <Python.h>

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>

#include "common.h"

G_BEGIN_DECLS

struct _PyGst_Functions {
  GstCaps*	(*caps_from_pyobject) (PyObject *object, gboolean *copy);
  PyObject*	(*iterator_new) (GstIterator *iter);
  PyObject*     (*miniobject_new) (GstMiniObject *obj);
};

#define pygstminiobject_get(v) (((PyGstMiniObject *)(v))->obj)
#define pygstminiobject_check(v,base) (PyObject_TypeCheck(v,base))

typedef struct {
    PyObject_HEAD
    GstMiniObject *obj;
    PyObject *inst_dict; /* the instance dictionary -- must be last */
    PyObject *weakreflist; /* list of weak references */
} PyGstMiniObject;

#ifndef _INSIDE_PYGST_

#if defined(NO_IMPORT_PYGOBJECT)
extern struct _PyGst_Functions *_PyGst_API;
#else
struct _PyGst_Functions *_PyGst_API;
#endif

#define pygst_caps_from_pyobject (_PyGst_API->caps_from_pyobject)
#define pygst_iterator_new (_PyGst_API->iterator_new)
#define pygstminiobject_new (_PyGst_API->miniobject_new)

static inline PyObject *
pygst_init(void)
{
  PyObject *gstobject, *cobject;

  gstobject = PyImport_ImportModule("gst._gst");
  if (!gstobject) {
    if (PyErr_Occurred())
      {
	PyObject *type, *value, *traceback;
	PyObject *py_orig_exc;
	PyErr_Fetch(&type, &value, &traceback);
	py_orig_exc = PyObject_Repr(value);
	Py_XDECREF(type);
	Py_XDECREF(value);
	Py_XDECREF(traceback);
	PyErr_Format(PyExc_ImportError,
		     "could not import gst (error was: %s)",
		     PyString_AsString(py_orig_exc));
	Py_DECREF(py_orig_exc);
      } else {
      PyErr_SetString(PyExc_ImportError,
		      "could not import gst (no error given)");
    }
    return NULL;
  }

  cobject = PyObject_GetAttrString(gstobject, "_PyGst_API");
  if (!cobject) {
    PyErr_SetString(PyExc_ImportError,
		    "could not import gst (getting _PyGst_API)");
    return NULL;
  }
  _PyGst_API = (struct _PyGst_Functions *) PyCObject_AsVoidPtr(cobject);

  return gstobject;
}

#endif	/* _INSIDE_PYGST_ */

G_END_DECLS

#endif /* !_PYGST_H_ */

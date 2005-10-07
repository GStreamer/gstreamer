/* -*- Mode: C; c-basic-offset: 4 -*- */
/* gst-python
 * Copyright (C) 2005 Johan Dahlin
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

#include <gst/gstiterator.h>
#include <Python.h>
#include <pygobject.h>
#include "pygstobject.h"

typedef struct {
    PyObject_HEAD
    GstIterator *iter;
} PyGstIterator;

static void
pygst_iterator_dealloc(PyGstIterator *self)
{
    gst_iterator_free(self->iter);
    PyObject_Del((PyObject*) self);
}

static PyObject *
pygst_iterator_iter_next(PyGstIterator *self)
{
    gpointer element;
    PyObject *retval = NULL;
    GstIteratorResult result;
    
    result = gst_iterator_next(self->iter, &element);
    switch (result)
	{
	case GST_ITERATOR_DONE:
	    PyErr_SetNone(PyExc_StopIteration);
	    break;
	case GST_ITERATOR_OK:
	    if (g_type_is_a(self->iter->type, G_TYPE_OBJECT))
		retval = pygstobject_new(G_OBJECT(element));
	    else {
		PyErr_Format(PyExc_TypeError, "Unsupported child type: %s",
			     g_type_name(self->iter->type));
	    }
	    break;
	case GST_ITERATOR_RESYNC:
	    PyErr_SetString(PyExc_TypeError, "Resync");
	    break;
	case GST_ITERATOR_ERROR:
	    PyErr_SetString(PyExc_TypeError, "Error");
	    break;
	default:
	    g_assert_not_reached();
	    break;
	}
    
    return retval;
}

PyTypeObject PyGstIterator_Type = {
    PyObject_HEAD_INIT(NULL)
    0,					/* ob_size */
    "gst.Iterator",		        /* tp_name */
    sizeof(PyGstIterator),	        /* tp_basicsize */
    0,					/* tp_itemsize */
    (destructor)pygst_iterator_dealloc,	/* tp_dealloc */
    0,					/* tp_print */
    0,					/* tp_getattr */
    0,					/* tp_setattr */
    0,					/* tp_compare */
    0,					/* tp_repr */
    0,					/* tp_as_number */
    0,					/* tp_as_sequence */
    0,		       			/* tp_as_mapping */
    0,					/* tp_hash */
    0,					/* tp_call */
    0,					/* tp_str */
    0,					/* tp_getattro */
    0,					/* tp_setattro */
    0,					/* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,			/* tp_flags */
    "GstIterator wrapper",	        /* tp_doc */
    0,					/* tp_traverse */
    0,					/* tp_clear */
    0,					/* tp_richcompare */
    0,					/* tp_weaklistoffset */
    PyObject_SelfIter,	                /* tp_iter */
    (iternextfunc)pygst_iterator_iter_next, /* tp_iternext */
};

PyObject*
pygst_iterator_new(GstIterator *iter)
{
    PyGstIterator *self;
    self = PyObject_NEW(PyGstIterator, &PyGstIterator_Type);
    self->iter = iter;
    return (PyObject *) self;
}

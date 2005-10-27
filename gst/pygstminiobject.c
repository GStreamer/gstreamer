/* -*- Mode: C; c-basic-offset: 4 -*-
 * pygtk- Python bindings for the GTK toolkit.
 * Copyright (C) 1998-2003  James Henstridge
 *
 *   pygobject.c: wrapper for the GObject type.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "pygstminiobject.h"
#include <gst/gst.h>

static const gchar *pygstminiobject_class_id     = "PyGstMiniObject::class";
static GQuark       pygstminiobject_class_key    = 0;
/* static const gchar *pygstminiobject_wrapper_id   = "PyGstMiniObject::wrapper"; */
/* static GQuark       pygstminiobject_wrapper_key  = 0; */

static void pygstminiobject_dealloc(PyGstMiniObject *self);
/* static int  pygstminiobject_traverse(PyGstMiniObject *self, visitproc visit, void *arg); */
/* static int  pygstminiobject_clear(PyGstMiniObject *self); */

GST_DEBUG_CATEGORY_EXTERN (pygst_debug);
#define GST_CAT_DEFAULT pygst_debug

/**
 * pygstminiobject_lookup_class:
 * @gtype: the GType of the GstMiniObject subclass.
 *
 * This function looks up the wrapper class used to represent
 * instances of a GstMiniObject represented by @gtype.  If no wrapper class
 * or interface has been registered for the given GType, then a new
 * type will be created.
 *
 * Returns: The wrapper class for the GstMiniObject or NULL if the
 *          GType has no registered type and a new type couldn't be created
 */
PyTypeObject *
pygstminiobject_lookup_class(GType gtype)
{
    PyTypeObject *py_type = NULL;
    GType	ctype = gtype;

    while (!py_type && ctype) {
	py_type = g_type_get_qdata(ctype, pygstminiobject_class_key);
	ctype = g_type_parent(ctype);
    }
    if (!ctype)
	g_error ("Couldn't find a good base type!!");
    
    return py_type;
}

/**
 * pygstminiobject_register_class:
 * @dict: the module dictionary.  A reference to the type will be stored here.
 * @type_name: not used ?
 * @gtype: the GType of the Gstminiobject subclass.
 * @type: the Python type object for this wrapper.
 * @bases: a tuple of Python type objects that are the bases of this type.
 *
 * This function is used to register a Python type as the wrapper for
 * a particular Gstminiobject subclass.  It will also insert a reference to
 * the wrapper class into the module dictionary passed as a reference,
 * which simplifies initialisation.
 */
void
pygstminiobject_register_class(PyObject *dict, const gchar *type_name,
			 GType gtype, PyTypeObject *type,
			 PyObject *bases)
{
    PyObject *o;
    const char *class_name, *s;

    if (!pygstminiobject_class_key)
	pygstminiobject_class_key = g_quark_from_static_string(pygstminiobject_class_id);

    class_name = type->tp_name;
    s = strrchr(class_name, '.');
    if (s != NULL)
	class_name = s + 1;
    
    type->ob_type = &PyType_Type;
    type->tp_alloc = PyType_GenericAlloc;
    type->tp_new = PyType_GenericNew;
    if (bases) {
	type->tp_bases = bases;
	type->tp_base = (PyTypeObject *)PyTuple_GetItem(bases, 0);
    }

    if (PyType_Ready(type) < 0) {
	g_warning ("couldn't make the type `%s' ready", type->tp_name);
	return;
    }

    if (gtype) {
	o = pyg_type_wrapper_new(gtype);
	PyDict_SetItemString(type->tp_dict, "__gtype__", o);
	Py_DECREF(o);

	/* stash a pointer to the python class with the GType */
	Py_INCREF(type);
	g_type_set_qdata(gtype, pygstminiobject_class_key, type);
    }

    PyDict_SetItemString(dict, (char *)class_name, (PyObject *)type);
}

void
pygstminiobject_register_wrapper (PyObject *self)
{
}


/**
 * pygstminiobject_new:
 * @obj: a GstMiniObject instance.
 *
 * This function gets a reference to a wrapper for the given GstMiniObject
 * instance.  A new wrapper will always be created.
 *
 * Returns: a reference to the wrapper for the GstMiniObject.
 */
PyObject *
pygstminiobject_new (GstMiniObject *obj)
{
    PyGstMiniObject *self = NULL;
    PyGILState_STATE state;
    PyTypeObject *tp = NULL;

    if (obj == NULL) {
	Py_INCREF (Py_None);
	return Py_None;
    }

    /* since mini objects cannot notify us when they get destroyed, we
     * can't use a global hash to map GMO to PyO, and have to create a new
     * Python object every time we see it */
    tp = pygstminiobject_lookup_class (G_OBJECT_TYPE (obj));
    GST_DEBUG ("have to create wrapper for object %p", obj);
    if (!tp)
        g_warning ("Couldn't get class for type object : %p", obj);
    if (tp->tp_flags & Py_TPFLAGS_HEAPTYPE) {
        GST_INFO ("Increment refcount %p", tp);
        Py_INCREF (tp);
    }
    state = pyg_gil_state_ensure();
    self = PyObject_New (PyGstMiniObject, tp);
    pyg_gil_state_release(state);

    if (self == NULL)
        return NULL;
    self->obj = gst_mini_object_ref (obj);

    self->inst_dict = NULL;
    self->weakreflist = NULL;

    GST_DEBUG ("created Python object %p for GstMiniObject %p [ref:%d]",
        self, obj, GST_MINI_OBJECT_REFCOUNT_VALUE (obj));
    return (PyObject *) self;
}

static void
pygstminiobject_dealloc(PyGstMiniObject *self)
{
    g_return_if_fail (self != NULL);

    PyGILState_STATE state;

    GST_DEBUG ("At the beginning %p", self);
    state = pyg_gil_state_ensure();

    if (self->obj) {
        GST_DEBUG ("PyO %p unreffing GstMiniObject %p [ref:%d]", self,
             self->obj, GST_MINI_OBJECT_REFCOUNT_VALUE (self->obj));
	gst_mini_object_unref(self->obj);
	GST_DEBUG ("setting self %p -> obj to NULL", self);
	self->obj = NULL;
    }

    if (self->inst_dict) {
	Py_DECREF(self->inst_dict);
	self->inst_dict = NULL;
    }

    self->ob_type->tp_free((PyObject *) self);
    pyg_gil_state_release(state);
    GST_DEBUG ("At the end %p", self);
}

static int
pygstminiobject_compare(PyGstMiniObject *self, PyGstMiniObject *v)
{
    if (self->obj == v->obj) return 0;
    if (self->obj > v->obj)  return -1;
    return 1;
}

static long
pygstminiobject_hash(PyGstMiniObject *self)
{
    return (long)self->obj;
}

static PyObject *
pygstminiobject_repr(PyGstMiniObject *self)
{
    gchar buf[256];

    g_snprintf(buf, sizeof(buf),
	       "<%s mini-object (%s) at 0x%lx>",
	       self->ob_type->tp_name,
	       self->obj ? G_OBJECT_TYPE_NAME(self->obj) : "uninitialized",
	       (long)self);
    return PyString_FromString(buf);
}


static void
pygstminiobject_free(PyObject *op)
{
    PyObject_FREE(op);
}


/* ---------------- PyGstMiniObject methods ----------------- */

static int
pygstminiobject_init(PyGstMiniObject *self, PyObject *args, PyObject *kwargs)
{
    GType object_type;
    GstMiniObjectClass *class;

    if (!PyArg_ParseTuple(args, ":GstMiniObject.__init__", &object_type))
	return -1;

    object_type = pyg_type_from_object((PyObject *)self);
    if (!object_type)
	return -1;

    if (G_TYPE_IS_ABSTRACT(object_type)) {
	PyErr_Format(PyExc_TypeError, "cannot create instance of abstract "
		     "(non-instantiable) type `%s'", g_type_name(object_type));
	return -1;
    }

    if ((class = g_type_class_ref (object_type)) == NULL) {
	PyErr_SetString(PyExc_TypeError,
			"could not get a reference to type class");
	return -1;
    }

    self->obj = gst_mini_object_new(object_type);
    if (self->obj == NULL)
	PyErr_SetString (PyExc_RuntimeError, "could not create object");
	   
    g_type_class_unref(class);
    
    return (self->obj) ? 0 : -1;
}

static PyObject *
pygstminiobject__gstminiobject_init__(PyGstMiniObject *self, PyObject *args, PyObject *kwargs)
{
    if (pygstminiobject_init(self, args, kwargs) < 0)
	return NULL;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
pygstminiobject_copy(PyGstMiniObject *self, PyObject *args)
{
    return pygstminiobject_new(gst_mini_object_copy(self->obj));
}

static PyMethodDef pygstminiobject_methods[] = {
    { "__gstminiobject_init__", (PyCFunction)pygstminiobject__gstminiobject_init__,
      METH_VARARGS|METH_KEYWORDS },
    { "copy", (PyCFunction)pygstminiobject_copy, METH_VARARGS, "Copies the miniobject"},
    { NULL, NULL, 0 }
};

static PyObject *
pygstminiobject_get_dict(PyGstMiniObject *self, void *closure)
{
    if (self->inst_dict == NULL) {
	self->inst_dict = PyDict_New();
	if (self->inst_dict == NULL)
	    return NULL;
    }
    Py_INCREF(self->inst_dict);
    return self->inst_dict;
}

static PyObject *
pygstminiobject_get_refcount(PyGstMiniObject *self, void *closure)
{
    return PyInt_FromLong(GST_MINI_OBJECT_REFCOUNT_VALUE(self->obj));
}

static PyObject *
pygstminiobject_get_flags(PyGstMiniObject *self, void *closure)
{
    return PyInt_FromLong(GST_MINI_OBJECT_FLAGS(self->obj));
}

static PyGetSetDef pygstminiobject_getsets[] = {
    { "__dict__", (getter)pygstminiobject_get_dict, (setter)0 },
    { "__grefcount__", (getter)pygstminiobject_get_refcount, (setter)0, },
    { "flags", (getter)pygstminiobject_get_flags, (setter)0, },
    { NULL, 0, 0 }
};

PyTypeObject PyGstMiniObject_Type = {
    PyObject_HEAD_INIT(NULL)
    0,					/* ob_size */
    "gst.MiniObject",			/* tp_name */
    sizeof(PyGstMiniObject),			/* tp_basicsize */
    0,					/* tp_itemsize */
    /* methods */
    (destructor)pygstminiobject_dealloc,	/* tp_dealloc */
    (printfunc)0,			/* tp_print */
    (getattrfunc)0,			/* tp_getattr */
    (setattrfunc)0,			/* tp_setattr */
    (cmpfunc)pygstminiobject_compare,		/* tp_compare */
    (reprfunc)pygstminiobject_repr,		/* tp_repr */
    0,					/* tp_as_number */
    0,					/* tp_as_sequence */
    0,					/* tp_as_mapping */
    (hashfunc)pygstminiobject_hash,		/* tp_hash */
    (ternaryfunc)0,			/* tp_call */
    (reprfunc)0,			/* tp_str */
    (getattrofunc)0,			/* tp_getattro */
    (setattrofunc)0,			/* tp_setattro */
    0,					/* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,		/* tp_flags */
    NULL, /* Documentation string */
    (traverseproc)0,	/* tp_traverse */
    (inquiry)0,		/* tp_clear */
    (richcmpfunc)0,			/* tp_richcompare */
    offsetof(PyGstMiniObject, weakreflist),	/* tp_weaklistoffset */
    (getiterfunc)0,			/* tp_iter */
    (iternextfunc)0,			/* tp_iternext */
    pygstminiobject_methods,			/* tp_methods */
    0,					/* tp_members */
    pygstminiobject_getsets,			/* tp_getset */
    (PyTypeObject *)0,			/* tp_base */
    (PyObject *)0,			/* tp_dict */
    0,					/* tp_descr_get */
    0,					/* tp_descr_set */
    offsetof(PyGstMiniObject, inst_dict),	/* tp_dictoffset */
    (initproc)pygstminiobject_init,		/* tp_init */
    (allocfunc)0,			/* tp_alloc */
    (newfunc)0,				/* tp_new */
    (freefunc)pygstminiobject_free,		/* tp_free */
    (inquiry)0,				/* tp_is_gc */
    (PyObject *)0,			/* tp_bases */
};


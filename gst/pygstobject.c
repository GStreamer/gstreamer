/* -*- Mode: C; ; c-file-style: "k&r"; c-basic-offset: 4 -*- */
/* gst-python
 * Copyright (C) 2002 David I. Lehn
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

#include "pygstobject.h"

#include <locale.h>

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <pygobject.h>
#include <gst/gst.h>
#include <gst/gstversion.h>

GST_DEBUG_CATEGORY_EXTERN (pygst_debug);
#define GST_CAT_DEFAULT pygst_debug

/* we reuse the same string for our quark so we get the same qdata;
 * it might be worth it to use our own to shake out all instances
 * were GObject-only calls are being used where we should be using
 * gst_object_  */
static const gchar *pygobject_wrapper_id   = "PyGObject::wrapper";
static GQuark       pygobject_wrapper_key  = 0;

/* only use on GstObject */
void
pygstobject_sink(GObject *object)
{
     g_assert (GST_IS_OBJECT (object));

     if (GST_OBJECT_IS_FLOATING(object)) {
          gst_object_ref(GST_OBJECT(object));
          gst_object_sink(GST_OBJECT(object));
     }
}

/* functions used by the code generator we can call on both
 * GstObject and non-GstObject GObjects
 */

/* to be called instead of pygobject_new */
PyObject *
pygstobject_new(GObject *obj)
{
    PyGObject *self = NULL;

    if (!GST_IS_OBJECT (obj))
        return pygobject_new (obj);

    GST_DEBUG_OBJECT (obj, "wrapping GstObject");

    if (!pygobject_wrapper_key)
        pygobject_wrapper_key = g_quark_from_static_string(pygobject_wrapper_id);

    if (obj == NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    /* we already have a wrapper for this object -- return it. */
    self = (PyGObject *)g_object_get_qdata(obj, pygobject_wrapper_key);
    if (self != NULL) {
        Py_INCREF(self);
    } else {
        /* create wrapper */
        PyTypeObject *tp = pygobject_lookup_class(G_OBJECT_TYPE(obj));
        /* need to bump type refcount if created with
           pygobject_new_with_interfaces(). fixes bug #141042 */
        if (tp->tp_flags & Py_TPFLAGS_HEAPTYPE)
            Py_INCREF(tp);
        self = PyObject_GC_New(PyGObject, tp);
        if (self == NULL)
            return NULL;
        pyg_begin_allow_threads;
        self->obj = gst_object_ref(obj);
        pyg_end_allow_threads;
        pygstobject_sink(self->obj);

        self->inst_dict = NULL;
        self->weakreflist = NULL;
        self->closures = NULL;
        /* save wrapper pointer so we can access it later */
        Py_INCREF(self);
        g_object_set_qdata_full(obj, pygobject_wrapper_key, self,
                                pyg_destroy_notify);

        PyObject_GC_Track((PyObject *)self);
    }
    GST_DEBUG_OBJECT (obj, "wrapped GstObject %p as PyObject %p", obj, self);

    return (PyObject *)self;
}

/* to be called instead of g_object_unref */
void
pygst_object_unref(GObject *obj)
{
    if (GST_IS_OBJECT (obj)) {
        GST_DEBUG_OBJECT (obj, "unreffing GstObject %p", obj);
        gst_object_unref (obj);
    } else
        g_object_unref (obj);
}

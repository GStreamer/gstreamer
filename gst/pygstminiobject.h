/* -*- Mode: C; c-basic-offset: 4 -*- */

#ifndef _PYGSTMINIOBJECT_H_
#define _PYGSTMINIOBJECT_H_

#include <Python.h>

#include <glib.h>
#include <glib-object.h>

#include "common.h"

G_BEGIN_DECLS

/* Work around bugs in PyGILState api fixed in 2.4.0a4 */
#if PY_VERSION_HEX < 0x020400A4
#define PYGIL_API_IS_BUGGY TRUE
#else
#define PYGIL_API_IS_BUGGY FALSE
#endif

typedef struct {
    PyObject_HEAD
    GstMiniObject *obj;
    PyObject *inst_dict; /* the instance dictionary -- must be last */
    PyObject *weakreflist; /* list of weak references */
} PyGstMiniObject;

PyObject *
pygstminiobject_new(GstMiniObject *obj);

#define pygstminiobject_get(v) (((PyGstMiniObject *)(v))->obj)
#define pygstminiobject_check(v,base) (PyObject_TypeCheck(v,base))

void
pygstminiobject_register_class(PyObject *dict, const gchar *type_name,
			       GType gtype, PyTypeObject *type,
			       PyObject *bases);

#ifndef _INSIDE_PYGSTMINIOBJECT_

struct _PyGObject_Functions *_PyGObject_API;

extern PyTypeObject PyGstMiniObject_Type;

#define init_pygstminiobject() { \
    PyObject *gstminiobject = PyImport_ImportModule("gstminiobject"); \
    if (gstminiobject != NULL) { \
        PyObject *mdict = PyModule_GetDict(gstminiobject); \
        PyObject *cobject = PyDict_GetItemString(mdict, "_PyGstMiniObject_API"); \
        if (PyCObject_Check(cobject)) \
            _PyGstMiniObject_API = (struct _PyGstMiniObject_Functions *)PyCObject_AsVoidPtr(cobject); \
        else { \
            PyErr_SetString(PyExc_RuntimeError, \
                            "could not find _PyGstMiniObject_API object"); \
	    return; \
        } \
    } else { \
        PyErr_SetString(PyExc_ImportError, \
                        "could not import gst"); \
        return; \
    } \
}

#endif /* !_INSIDE_PYGSTMINIOBJECT_ */

G_END_DECLS

#endif /* !_PYGSTMINIOBJECT_H_ */

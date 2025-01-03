/* gst-python
 * Copyright (C) 2024 Collabora Ltd
 *  Author: Daniel Morin <daniel.morin@dmohub.org>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <Python.h>
#include <pygobject.h>
#include <gst/gst.h>
#include <gst/analytics/analytics.h>

#include <locale.h>

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

typedef struct
{
  PyObject_HEAD PyObject *py_module;
  PyObject *py_rmeta;
  GstAnalyticsRelationMeta *rmeta;
  gpointer state;
  gboolean ended;
} _GstAnalyticsRelationMetaIterator;

static PyObject *
_gst_analytics_relation_meta_iterator_new (PyTypeObject * type, PyObject * args,
    PyObject * kwds)
{
  PyObject *py_rmeta;
  PyObject *py_module;
  _GstAnalyticsRelationMetaIterator *self;
  self = (_GstAnalyticsRelationMetaIterator *) type->tp_alloc (type, 0);
  if (self != NULL) {

    if (!PyArg_ParseTuple (args, "OO", &py_module, &py_rmeta)) {
      Py_DECREF (self);
      return NULL;
    }

    self->py_module = py_module;
    self->py_rmeta = py_rmeta;
    self->state = NULL;
    Py_INCREF (py_module);
    Py_INCREF (py_rmeta);
    self->rmeta = (GstAnalyticsRelationMeta *) (pygobject_get (py_rmeta));
    self->ended = FALSE;
  }

  return (PyObject *) self;
}

static void
_gst_analytics_relation_meta_iterator_dtor (_GstAnalyticsRelationMetaIterator *
    self)
{
  Py_DECREF (self->py_rmeta);
  Py_DECREF (self->py_module);
  Py_TYPE (self)->tp_free ((PyObject *) self);
}

static PyObject *
_gst_analytics_relation_meta_iterator_next (_GstAnalyticsRelationMetaIterator *
    self)
{
  GstAnalyticsMtd mtd;
  PyObject *py_mtd_id;
  PyObject *py_mtd_type;
  PyObject *py_args;
  PyObject *py_func;
  PyObject *py_result = NULL;

  if (self->ended || !gst_analytics_relation_meta_iterate (self->rmeta,
          &self->state, GST_ANALYTICS_MTD_TYPE_ANY, &mtd)) {

    self->ended = TRUE;
    return NULL;
  }

  py_mtd_type = PyLong_FromUnsignedLong (gst_analytics_mtd_get_mtd_type (&mtd));
  py_mtd_id = PyLong_FromUnsignedLong (mtd.id);
  py_args = PyTuple_Pack (3, py_mtd_type, self->py_rmeta, py_mtd_id);

  py_func = PyObject_GetAttrString (self->py_module, "_get_mtd");

  if (py_func) {
    py_result = PyObject_Call (py_func, py_args, NULL);
    Py_DECREF (py_func);
  }

  Py_DECREF (py_args);
  Py_DECREF (py_mtd_id);
  Py_DECREF (py_mtd_type);

  return py_result;
}

static PyMethodDef _gi_gst_analytics_functions[] = {
  {NULL, NULL, 0, NULL}
};

static PyTypeObject GstAnalyticsRelationMetaIteratorType = {
  PyVarObject_HEAD_INIT (NULL, 0)
      .tp_name = "_gi_gst_analytics.AnalyticsRelationMetaIterator",
  .tp_doc = "Iterator for Gst.AnalyticsRelationMeta",
  .tp_basicsize = sizeof (_GstAnalyticsRelationMetaIterator),
  .tp_itemsize = 0,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = _gst_analytics_relation_meta_iterator_new,
  .tp_iter = (getiterfunc) PyObject_SelfIter,
  .tp_iternext = (iternextfunc) _gst_analytics_relation_meta_iterator_next,
  .tp_dealloc = (destructor) _gst_analytics_relation_meta_iterator_dtor
};

PYGLIB_MODULE_START (_gi_gst_analytics, "_gi_gst_analytics")
{
  pygobject_init (3, 0, 0);

  if (PyType_Ready (&GstAnalyticsRelationMetaIteratorType) < 0)
    return NULL;

  Py_INCREF (&GstAnalyticsRelationMetaIteratorType);
  if (PyModule_AddObject (module, "AnalyticsRelationMetaIterator", (PyObject *)
          & GstAnalyticsRelationMetaIteratorType) < 0) {
    Py_DECREF (&GstAnalyticsRelationMetaIteratorType);
    Py_DECREF (module);
    return NULL;
  }
}

PYGLIB_MODULE_END;

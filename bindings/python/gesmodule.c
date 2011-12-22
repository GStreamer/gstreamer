/* -*- Mode: C; c-basic-offset: 4 -*- */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <Python.h>
#include <pygobject.h>
#include <pyglib.h>
#include <gst/pygst.h>

/* include any extra headers needed here */

void pyges_register_classes (PyObject * d);
extern PyMethodDef pyges_functions[];
void pyges_add_constants (PyObject * module, const gchar * strip_prefix);
DL_EXPORT (void)
initges (void);

DL_EXPORT (void)
initges (void)
{
  PyObject *m, *d;

  /* perform any initialisation required by the library here */

  m = Py_InitModule ("ges", pyges_functions);
  d = PyModule_GetDict (m);

  init_pygobject ();
  pygst_init ();

  pygst_init ();
  pyges_register_classes (d);
  pyges_add_constants (m, "GES_");

  /* add anything else to the module dictionary (such as constants) */

  if (PyErr_Occurred ())
    Py_FatalError ("could not initialise module ges");
}

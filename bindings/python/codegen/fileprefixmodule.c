/* -*- Mode: C; c-basic-offset: 4 -*- */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <Python.h>
#include <pygtk.h>

/* include any extra headers needed here */

void prefix_register_classes(PyObject *d);
extern PyMethodDef prefix_functions[];

DL_EXPORT(void)
initmodule(void)
{
    PyObject *m, *d;

    /* perform any initialisation required by the library here */

    m = Py_InitModule("module", prefix_functions);
    d = PyModule_GetDict(m);

    init_pygtk();

    prefix_register_classes(d);

    /* add anything else to the module dictionary (such as constants) */

    if (PyErr_Occurred())
        Py_FatalError("could not initialise module module");
}

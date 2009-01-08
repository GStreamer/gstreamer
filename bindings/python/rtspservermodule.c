/* -*- Mode: C; c-basic-offset: 4 -*- */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#include <Python.h>
#include <pygobject.h>

/* include any extra headers needed here */

void pygst_rtsp_server_register_classes(PyObject *d);
extern PyMethodDef pygst_rtsp_server_functions[];

DL_EXPORT(void)
initrtspserver(void)
{
    PyObject *m, *d;

    /* perform any initialisation required by the library here */

    m = Py_InitModule("rtspserver", pygst_rtsp_server_functions);
    d = PyModule_GetDict(m);

    init_pygobject();

    pygst_rtsp_server_register_classes(d);

    /* add anything else to the module dictionary (such as constants) */

    if (PyErr_Occurred())
        Py_FatalError("could not initialise module rtspserver");
}

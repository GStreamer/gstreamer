/* -*- Mode: C; c-basic-offset: 4 -*- */
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

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <pygobject.h>
#include <gst/gst.h>

void pygstreamer_register_classes (PyObject *d);
void pygstreamer_add_constants(PyObject *module, const gchar *strip_prefix);
		
extern PyMethodDef pygstreamer_functions[];

DL_EXPORT(void)
init_gstreamer (void)
{
	PyObject *m, *d;
	PyObject *av;
        int argc, i;
        char **argv;

	init_pygobject ();

        /* pull in arguments */
        av = PySys_GetObject ("argv");
        if (av != NULL) {
            argc = PyList_Size (av);
            argv = g_new (char *, argc);
            for (i = 0; i < argc; i++)
                argv[i] = g_strdup (PyString_AsString (PyList_GetItem (av, i)));
        } else {
                argc = 0;
                argv = NULL;
        }
                                                                                    
        if (!gst_init_check (&argc, &argv)) {
            if (argv != NULL) {
                for (i = 0; i < argc; i++)
                    g_free (argv[i]);
                g_free (argv);
            }
            PyErr_SetString (PyExc_RuntimeError, "can't initialize module gstreamer");
        }
        if (argv != NULL) {
            PySys_SetArgv (argc, argv);
            for (i = 0; i < argc; i++)
                g_free (argv[i]);
            g_free (argv);
        }

	m = Py_InitModule ("_gstreamer", pygstreamer_functions);
	d = PyModule_GetDict (m);

	pygstreamer_register_classes (d);
	pygstreamer_add_constants (m, "GST_");
	
	if (PyErr_Occurred ()) {
		Py_FatalError ("can't initialize module gstreamer");
	}
}

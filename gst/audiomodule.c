/* -*- Mode: C; c-basic-offset: 4 -*- */
/* gst-python
 * Copyright (C) 2008 Edward Hervey <edward.hervey@collabora.co.uk>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <pygobject.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstbaseaudiosink.h>
#include <gst/audio/gstbaseaudiosrc.h>

void pyaudio_register_classes (PyObject *d);
void pyaudio_add_constants(PyObject *module, const gchar *strip_prefix);
		
extern PyMethodDef pyaudio_functions[];

GST_DEBUG_CATEGORY (pygst_debug); /* for python code */

DL_EXPORT(void)
initaudio (void)
{
	PyObject *m, *d;

	init_pygobject ();

	m = Py_InitModule ("audio", pyaudio_functions);
	d = PyModule_GetDict (m);

	pyaudio_register_classes (d);
	pyaudio_add_constants (m, "GST_");
	
	if (PyErr_Occurred ()) {
	    PyErr_Print ();
	    Py_FatalError ("can't initialize module gst.audio");
	}
}

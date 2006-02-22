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

#include "pygstminiobject.h"
#include "pygstexception.h"

#include <locale.h>

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <pygobject.h>
#include <gst/gst.h>
#include <gst/gstversion.h>

void pygst_register_classes (PyObject *d);
void pygst_add_constants(PyObject *module, const gchar *strip_prefix);
void _pygst_register_boxed_types(PyObject *moddict);
		
extern PyMethodDef pygst_functions[];

GST_DEBUG_CATEGORY (pygst_debug);  /* for bindings code */
GST_DEBUG_CATEGORY (python_debug); /* for python code */

/* copied from pygtk to register GType */
#define REGISTER_TYPE(d, type, name) \
    type.ob_type = &PyType_Type; \
    type.tp_alloc = PyType_GenericAlloc; \
    type.tp_new = PyType_GenericNew; \
    if (PyType_Ready(&type)) \
        return; \
    PyDict_SetItemString(d, name, (PyObject *)&type);

#define REGISTER_GTYPE(d, type, name, gtype) \
    REGISTER_TYPE(d, type, name); \
    PyDict_SetItemString(type.tp_dict, "__gtype__", \
                         o=pyg_type_wrapper_new(gtype)); \
    Py_DECREF(o);


/* This is a timeout that gets added to the mainloop to handle SIGINT (Ctrl-C)
 * Other signals get handled at some other point where transition from
 * C -> Python is being made.
 */
static gboolean
python_do_pending_calls(gpointer data)
{
    PyGILState_STATE state;

    if (PyOS_InterruptOccurred()) {
	 state = pyg_gil_state_ensure();
	 PyErr_SetNone(PyExc_KeyboardInterrupt);
	 pyg_gil_state_release(state);
    }

    return TRUE;
}


static PyObject*
pygstminiobject_from_gvalue(const GValue *value)
{
     GstMiniObject	*miniobj;

     if ((miniobj = gst_value_get_mini_object (value)) == NULL)
	  return NULL;
     return pygstminiobject_new(miniobj);
}

static int
pygstminiobject_to_gvalue(GValue *value, PyObject *obj)
{
     PyGstMiniObject	*self = (PyGstMiniObject*) obj;

     gst_value_set_mini_object(value, self->obj);
     return 0;
}

DL_EXPORT(void)
init_gst (void)
{
     PyObject *m, *d;
     PyObject *av, *tuple;
     int argc, i;
     guint major, minor, micro, nano;
     char **argv;
     GError	*error = NULL;

     init_pygobject ();

     /* pull in arguments */
     av = PySys_GetObject ("argv");
     if (av != NULL) {
	  argc = PyList_Size (av);
	  argv = g_new (char *, argc);
	  for (i = 0; i < argc; i++)
	       argv[i] = g_strdup (PyString_AsString (PyList_GetItem (av, i)));
     } else {
          /* gst_init_check does not like argc == 0 */
	  argc = 1;
	  argv = g_new (char *, argc);
	  argv[0] = g_strdup("");
     }
     if (!gst_init_check (&argc, &argv, &error)) {
          gchar *errstr;
          
	  if (argv != NULL) {
	       for (i = 0; i < argc; i++)
                    g_free (argv[i]);
	       g_free (argv);
	  }
          errstr = g_strdup_printf ("can't initialize module gst: %s",
              GST_STR_NULL (error->message));
	  PyErr_SetString (PyExc_RuntimeError, errstr);
          g_free (errstr);
	  g_error_free (error);
	  setlocale(LC_NUMERIC, "C");
	  return;
     }
     
     setlocale(LC_NUMERIC, "C");
     if (argv != NULL) {
	  PySys_SetArgv (argc, argv);
	  for (i = 0; i < argc; i++)
	       g_free (argv[i]);
	  g_free (argv);
     }

     /* Initialize debugging category */
     GST_DEBUG_CATEGORY_INIT (pygst_debug, "pygst", 0, "GStreamer python bindings");
     GST_DEBUG_CATEGORY_INIT (python_debug, "python", 
         GST_DEBUG_FG_GREEN, "python code using gst-python");

/*      _pygst_register_boxed_types (NULL); */
     pygobject_register_sinkfunc(GST_TYPE_OBJECT, pygstobject_sink);

     m = Py_InitModule ("_gst", pygst_functions);
     d = PyModule_GetDict (m);

     /* gst version */
     gst_version(&major, &minor, &micro, &nano);
     tuple = Py_BuildValue("(iii)", major, minor, micro);
     PyDict_SetItemString(d, "gst_version", tuple);    
     Py_DECREF(tuple);
     
     /* gst-python version */
     tuple = Py_BuildValue ("(iii)", PYGST_MAJOR_VERSION, PYGST_MINOR_VERSION,
			    PYGST_MICRO_VERSION);
     PyDict_SetItemString(d, "pygst_version", tuple);
     Py_DECREF(tuple);

     /* clock stuff */
     PyModule_AddIntConstant(m, "SECOND", GST_SECOND);
     PyModule_AddIntConstant(m, "MSECOND", GST_MSECOND);
     PyModule_AddIntConstant(m, "NSECOND", GST_NSECOND);

     PyModule_AddObject(m, "CLOCK_TIME_NONE", PyLong_FromUnsignedLongLong(GST_CLOCK_TIME_NONE));

     pygst_exceptions_register_classes (d);
     
     REGISTER_TYPE(d, PyGstIterator_Type, "Iterator");


     pygstminiobject_register_class(d, "GstMiniObject", GST_TYPE_MINI_OBJECT,
				    &PyGstMiniObject_Type, NULL);
     pyg_register_boxed_custom(GST_TYPE_MINI_OBJECT,
			       pygstminiobject_from_gvalue,
			       pygstminiobject_to_gvalue);
     
     pygst_register_classes (d);
     pygst_add_constants (m, "GST_");

     /* make our types available */
     PyModule_AddObject (m, "TYPE_ELEMENT_FACTORY",
                         pyg_type_wrapper_new(GST_TYPE_ELEMENT_FACTORY));
     PyModule_AddObject (m, "TYPE_INDEX_FACTORY",
                         pyg_type_wrapper_new(GST_TYPE_INDEX_FACTORY));
     PyModule_AddObject (m, "TYPE_TYPE_FIND_FACTORY",
                         pyg_type_wrapper_new(GST_TYPE_TYPE_FIND_FACTORY));

     /* GStreamer core tags */
     PyModule_AddStringConstant (m, "TAG_TITLE", GST_TAG_TITLE);
     PyModule_AddStringConstant (m, "TAG_ARTIST", GST_TAG_ARTIST);
     PyModule_AddStringConstant (m, "TAG_ALBUM", GST_TAG_ALBUM);
     PyModule_AddStringConstant (m, "TAG_DATE", GST_TAG_DATE);
     PyModule_AddStringConstant (m, "TAG_GENRE", GST_TAG_GENRE);
     PyModule_AddStringConstant (m, "TAG_COMMENT", GST_TAG_COMMENT);
     PyModule_AddStringConstant (m, "TAG_TRACK_NUMBER", GST_TAG_TRACK_NUMBER);
     PyModule_AddStringConstant (m, "TAG_TRACK_COUNT", GST_TAG_TRACK_COUNT);
     PyModule_AddStringConstant (m, "TAG_ALBUM_VOLUME_NUMBER", GST_TAG_ALBUM_VOLUME_NUMBER);
     PyModule_AddStringConstant (m, "TAG_ALBUM_VOLUME_COUNT", GST_TAG_ALBUM_VOLUME_COUNT);
     PyModule_AddStringConstant (m, "TAG_LOCATION", GST_TAG_LOCATION);
     PyModule_AddStringConstant (m, "TAG_DESCRIPTION", GST_TAG_DESCRIPTION);
     PyModule_AddStringConstant (m, "TAG_VERSION", GST_TAG_VERSION);
     PyModule_AddStringConstant (m, "TAG_ISRC", GST_TAG_ISRC);
     PyModule_AddStringConstant (m, "TAG_ORGANIZATION", GST_TAG_ORGANIZATION);
     PyModule_AddStringConstant (m, "TAG_COPYRIGHT", GST_TAG_COPYRIGHT);
     PyModule_AddStringConstant (m, "TAG_CONTACT", GST_TAG_CONTACT);
     PyModule_AddStringConstant (m, "TAG_LICENSE", GST_TAG_LICENSE);
     PyModule_AddStringConstant (m, "TAG_PERFORMER", GST_TAG_PERFORMER);
     PyModule_AddStringConstant (m, "TAG_DURATION", GST_TAG_DURATION);
     PyModule_AddStringConstant (m, "TAG_CODEC", GST_TAG_CODEC);
     PyModule_AddStringConstant (m, "TAG_VIDEO_CODEC", GST_TAG_VIDEO_CODEC);
     PyModule_AddStringConstant (m, "TAG_AUDIO_CODEC", GST_TAG_AUDIO_CODEC);
     PyModule_AddStringConstant (m, "TAG_BITRATE", GST_TAG_BITRATE);
     PyModule_AddStringConstant (m, "TAG_NOMINAL_BITRATE", GST_TAG_NOMINAL_BITRATE);
     PyModule_AddStringConstant (m, "TAG_MINIMUM_BITRATE", GST_TAG_MINIMUM_BITRATE);
     PyModule_AddStringConstant (m, "TAG_MAXIMUM_BITRATE", GST_TAG_MAXIMUM_BITRATE);
     PyModule_AddStringConstant (m, "TAG_SERIAL", GST_TAG_SERIAL);
     PyModule_AddStringConstant (m, "TAG_ENCODER", GST_TAG_ENCODER);
     PyModule_AddStringConstant (m, "TAG_ENCODER_VERSION", GST_TAG_ENCODER_VERSION);
     PyModule_AddStringConstant (m, "TAG_TRACK_GAIN", GST_TAG_TRACK_GAIN);
     PyModule_AddStringConstant (m, "TAG_TRACK_PEAK", GST_TAG_TRACK_PEAK);
     PyModule_AddStringConstant (m, "TAG_ALBUM_GAIN", GST_TAG_ALBUM_GAIN);
     PyModule_AddStringConstant (m, "TAG_ALBUM_PEAK", GST_TAG_ALBUM_PEAK);
     PyModule_AddStringConstant (m, "TAG_LANGUAGE_CODE", GST_TAG_LANGUAGE_CODE);

     g_timeout_add_full (0, 100, python_do_pending_calls, NULL, NULL);

     atexit(gst_deinit);
     
     if (PyErr_Occurred ()) {
	  Py_FatalError ("can't initialize module gst");
     }
}

/*
 * pygstexception.c - gst-python exceptions
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <Python.h>
#include "structmember.h"

PyObject *PyGstExc_LinkError = NULL;
PyObject *PyGstExc_AddError = NULL;
PyObject *PyGstExc_QueryError = NULL;
PyObject *PyGstExc_RemoveError = NULL;
PyObject *PyGstExc_PluginNotFoundError = NULL;


static PyObject *
call_exception_init(PyObject *args)
{
    PyObject *parent_init = NULL;
    PyObject *res = NULL;

    /* get Exception.__init__ */
    parent_init = PyObject_GetAttrString(PyExc_Exception, "__init__");
    if (parent_init == NULL)
        goto exception;
   
    /* call Exception.__init__. This will set self.args */
    res = PyObject_CallObject(parent_init, args);
    if (res == NULL)
        goto exception;
    
    Py_DECREF(parent_init);
    
    return res;

exception:
    Py_XDECREF(parent_init);
    Py_XDECREF(res);
    
    return NULL;
}

static int
add_method(PyObject *klass, PyObject *dict, PyMethodDef *method) {
    PyObject *module = NULL;
    PyObject *func = NULL;
    PyObject *meth = NULL;

    module = PyString_FromString("gst");
    if (module == NULL)
        goto exception;
  
    func = PyCFunction_NewEx(method, NULL, module);
    if (func == NULL)
        goto exception;
    Py_DECREF(module);
  
    meth = PyMethod_New(func, NULL, klass);
    if (meth == NULL)
        goto exception;
    Py_DECREF(func);
  
    if (PyDict_SetItemString(dict, method->ml_name, meth) < 0)
        goto exception;
    Py_DECREF(meth);
  
    return 0;
 
exception:
    Py_XDECREF(module);
    Py_XDECREF(func);
    Py_XDECREF(meth);

    return -1;
}

static PyObject *
link_error_init(PyObject *self, PyObject *args)
{
    PyObject *err_type = NULL;
    int status;
   
    if (!PyArg_ParseTuple(args, "O|O:__init__", &self, &err_type))
        return NULL;
   
    if (err_type == NULL)
        err_type = Py_None;
    Py_INCREF(err_type);
    
    /* set self.error */
    status = PyObject_SetAttrString(self, "error", err_type);
    Py_DECREF(err_type);
    if (status < 0)
        return NULL;
    
    return call_exception_init(args);
}

static PyObject *
plugin_not_found_error_init(PyObject *self, PyObject *args)
{
    PyObject *plugin_name = NULL;
    int status;

    if (!PyArg_ParseTuple(args, "O|O:__init__", &self, &plugin_name))
        return NULL;

    if (plugin_name == NULL)
        plugin_name = Py_None;
    Py_INCREF(plugin_name);
    
    /* set self.name */
    status = PyObject_SetAttrString(self, "name", plugin_name);
    Py_DECREF(plugin_name);
    if (status < 0)
        return NULL;

    return call_exception_init(args);
}

static PyMethodDef link_error_init_method = {"__init__",
    link_error_init, METH_VARARGS
};

static PyMethodDef plugin_not_found_error_init_method = {"__init__",
    plugin_not_found_error_init, METH_VARARGS
};

void
pygst_exceptions_register_classes(PyObject *d)
{
    PyObject *dict = NULL;
   
    /* register gst.LinkError */ 
    dict = PyDict_New();
    if (dict == NULL)
        goto exception;
   
    PyGstExc_LinkError = PyErr_NewException("gst.LinkError", 
            PyExc_Exception, dict);
    if (PyGstExc_LinkError == NULL)
        goto exception;
    
    if (add_method(PyGstExc_LinkError, dict, &link_error_init_method) < 0)
        goto exception;
    
    Py_DECREF(dict);
    
    if (PyDict_SetItemString(d, "LinkError", PyGstExc_LinkError) < 0)
        goto exception;
   
    Py_DECREF(PyGstExc_LinkError);
    
    /* register gst.AddError */
    PyGstExc_AddError = PyErr_NewException("gst.AddError",
            PyExc_Exception, NULL);
    if (PyGstExc_AddError == NULL)
        goto exception;
    
    if (PyDict_SetItemString(d, "AddError", PyGstExc_AddError) < 0)
        goto exception;

    Py_DECREF(PyGstExc_AddError);
    
    /* register gst.RemoveError */
    PyGstExc_RemoveError = PyErr_NewException("gst.RemoveError",
            PyExc_Exception, NULL);
    if (PyGstExc_RemoveError == NULL)
        goto exception;
    
    if (PyDict_SetItemString(d, "RemoveError", PyGstExc_RemoveError) < 0)
        goto exception;
    
    Py_DECREF(PyGstExc_RemoveError);

    /* register gst.QueryError */
    PyGstExc_QueryError = PyErr_NewException("gst.QueryError",
            PyExc_Exception, NULL);
    if (PyGstExc_QueryError == NULL)
        goto exception;
    
    if (PyDict_SetItemString(d, "QueryError", PyGstExc_QueryError) < 0)
        goto exception;
    
    Py_DECREF(PyGstExc_QueryError);
   
   
    /* register gst.PluginNotFoundError */
    dict = PyDict_New();
    if (dict == NULL)
        goto exception;
    
    PyGstExc_PluginNotFoundError = \
        PyErr_NewException("gst.PluginNotFoundError", PyExc_Exception, dict);
    if (PyGstExc_PluginNotFoundError == NULL)
        goto exception;
    
    if (add_method(PyGstExc_PluginNotFoundError,
                dict, &plugin_not_found_error_init_method) < 0)
        goto exception;

    Py_DECREF(dict);
    
    if (PyDict_SetItemString(d, "PluginNotFoundError",
                PyGstExc_PluginNotFoundError) < 0)
        goto exception;

    Py_DECREF(PyGstExc_PluginNotFoundError);
    
    return;
    
exception:
    Py_XDECREF(dict);
    Py_XDECREF(PyGstExc_LinkError);
    Py_XDECREF(PyGstExc_AddError);
    Py_XDECREF(PyGstExc_RemoveError);
    Py_XDECREF(PyGstExc_QueryError);
    Py_XDECREF(PyGstExc_PluginNotFoundError);

    return;
}

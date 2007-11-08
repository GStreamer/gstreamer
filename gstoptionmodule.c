/* -*- Mode: C; ; c-file-style: "k&r"; c-basic-offset: 4 -*- */
/* gst-python
 * Copyright (C) 2007 Johan Dahlin
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
 * Author: Johan Dahlin <johan@gnome.org>
 */

#include <Python.h>
#include <pygobject.h>
#include <gst/gst.h>

static PyObject *
_wrap_gstoption_get_group (PyObject *self)
{
  GOptionGroup *option_group;

  option_group = gst_init_get_option_group();
  return pyg_option_group_new(option_group);
}

static PyMethodDef pygstoption_functions[] = {
    { "get_group", (PyCFunction)_wrap_gstoption_get_group, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL }
};

DL_EXPORT(void)
initgstoption (void)
{
     init_pygobject ();

     if (!g_thread_supported ())
       g_thread_init (NULL);

     Py_InitModule ("gstoption", pygstoption_functions);
}

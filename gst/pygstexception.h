/*
 * pygstexception.h - gst-python exceptions
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

#ifndef _PYGSTEXCEPTION_H_
#define _PYGSTEXCEPTION_H_

extern PyObject *PyGstExc_LinkError;
extern PyObject *PyGstExc_AddError;
extern PyObject *PyGstExc_RemoveError;
extern PyObject *PyGstExc_QueryError;
extern PyObject *PyGstExc_PluginNotFoundError;

void pygst_exceptions_register_classes(PyObject *d);

#endif /* _PYGSTEXCEPTION_H_ */

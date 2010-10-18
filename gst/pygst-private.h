/* -*- Mode: C; ; c-file-style: "python" -*- */
/* gst-python
 * Copyright (C) 2010 Edward Hervey <bilboed@bilboed.com>
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

#ifndef _PYGST_PRIVATE_H_
#define _PYGST_PRIVATE_H_

#ifdef _PYGST_H_
# error "include pygst.h or pygst-private.h but not both"
#endif

#define _INSIDE_PYGST_
#include "pygst.h"

extern PyTypeObject PyGstMiniObject_Type;

/* from gst-types.c */
GstCaps *pygst_caps_from_pyobject (PyObject *object, gboolean *copy);
PyObject* pygst_iterator_new(GstIterator *iter);

/* from pygstminiobject.c */
PyObject *
pygstminiobject_new(GstMiniObject *obj);



#endif	/* _PYGST_PRIVATE_H_ */

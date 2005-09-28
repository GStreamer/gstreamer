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

#ifndef _PYGSTOBJECT_H_
#define _PYGSTOBJECT_H_

#include "common.h"

G_BEGIN_DECLS

void pygstobject_sink(GObject *object);
PyObject * pygstobject_new(GObject *obj);
void pygst_object_unref(GObject *obj);

G_END_DECLS

#endif /* !_PYGSTOBJECT_H_ */

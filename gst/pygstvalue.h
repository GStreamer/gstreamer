/* gst-python
 * Copyright (C) 2004 Andy Wingo
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
 * Author: Andy Wingo <wingo@pobox.com>
 */

#include "common.h"
#include <gst/gst.h>


PyObject *pygst_value_as_pyobject(const GValue *value, gboolean copy_boxed);
gboolean pygst_value_init_for_pyobject (GValue *value, PyObject *obj);
int pygst_value_from_pyobject(GValue *value, PyObject *obj);
gboolean pygst_value_init(void);

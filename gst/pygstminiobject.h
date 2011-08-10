/* -*- Mode: C; c-basic-offset: 4 -*- */

#ifndef _PYGSTMINIOBJECT_H_
#define _PYGSTMINIOBJECT_H_

#include <Python.h>

#include <glib.h>
#include <glib-object.h>

#include "common.h"

G_BEGIN_DECLS

/* Work around bugs in PyGILState api fixed in 2.4.0a4 */
#undef PYGIL_API_IS_BUGGY
#if PY_VERSION_HEX < 0x020400A4
#define PYGIL_API_IS_BUGGY TRUE
#else
#define PYGIL_API_IS_BUGGY FALSE
#endif


void
pygstminiobject_register_class(PyObject *dict, const gchar *type_name,
			       GType gtype, PyTypeObject *type,
			       PyObject *bases);
void
pygstminiobject_register_wrapper(PyObject *self);

void
pygst_miniobject_init();

#ifndef _INSIDE_PYGSTMINIOBJECT_


#endif /* !_INSIDE_PYGSTMINIOBJECT_ */

G_END_DECLS

#endif /* !_PYGSTMINIOBJECT_H_ */

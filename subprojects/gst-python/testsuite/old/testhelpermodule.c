#include "pygobject.h"
#include "test-object.h"

#include <gst/gst.h>
#include <glib-object.h>

static PyObject *
_wrap_get_object (PyObject * self)
{
  GObject *obj;
  obj = g_object_new (TEST_TYPE_OBJECT, NULL);
  if (!obj) {
    return NULL;
  }

  return pygobject_new (obj);
}

static PyObject *
_wrap_emit_event (PyObject * self, PyObject * args)
{
  PyGObject *obj;
  GstEventType event_type = GST_EVENT_UNKNOWN;
  GstEvent *event;

  if (!PyArg_ParseTuple (args, "O|i", &obj, &event_type))
    return NULL;

  event = gst_event_new_custom (event_type, NULL);

  g_signal_emit_by_name (G_OBJECT (obj->obj), "event", event);

  gst_mini_object_unref (GST_MINI_OBJECT (event));

  Py_INCREF (Py_None);
  return Py_None;
}

static PyMethodDef testhelper_methods[] = {
  {"get_object", (PyCFunction) _wrap_get_object, METH_NOARGS},
  {"emit_event", (PyCFunction) _wrap_emit_event, METH_VARARGS},
  {NULL, NULL}
};

void
inittesthelper ()
{
  PyObject *m, *d;

  init_pygobject ();
  gst_init (NULL, NULL);

  m = Py_InitModule ("testhelper", testhelper_methods);

  d = PyModule_GetDict (m);
}

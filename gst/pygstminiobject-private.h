#ifndef _PYGOBJECT_PRIVATE_H_
#define _PYGOBJECT_PRIVATE_H_

#ifdef _PYGOBJECT_H_
#  error "include pygobject.h or pygobject-private.h, but not both"
#endif

#define _INSIDE_PYGOBJECT_
#include "pygobject.h"

/* from gobjectmodule.c */
extern struct _PyGObject_Functions pygobject_api_functions;
#define pyg_block_threads()   G_STMT_START { \
    if (pygobject_api_functions.block_threads != NULL)    \
      (* pygobject_api_functions.block_threads)();        \
  } G_STMT_END
#define pyg_unblock_threads() G_STMT_START { \
    if (pygobject_api_functions.unblock_threads != NULL)  \
      (* pygobject_api_functions.unblock_threads)();      \
  } G_STMT_END

#define pyg_threads_enabled (pygobject_api_functions.threads_enabled)


#define pyg_gil_state_ensure() (pygobject_api_functions.threads_enabled? (pygobject_api_functions.gil_state_ensure()) : 0)
#define pyg_gil_state_release(state) G_STMT_START {     \
    if (pygobject_api_functions.threads_enabled)                \
        pygobject_api_functions.gil_state_release(state);       \
    } G_STMT_END

#define pyg_begin_allow_threads                         \
    G_STMT_START {                                      \
        PyThreadState *_save = NULL;                    \
        if (pygobject_api_functions.threads_enabled)    \
            _save = PyEval_SaveThread();
#define pyg_end_allow_threads                           \
        if (pygobject_api_functions.threads_enabled)    \
            PyEval_RestoreThread(_save);                \
    } G_STMT_END


extern GType PY_TYPE_OBJECT;

void  pyg_destroy_notify (gpointer user_data);

/* from pygtype.h */
extern PyTypeObject PyGTypeWrapper_Type;

PyObject *pyg_type_wrapper_new (GType type);
GType     pyg_type_from_object (PyObject *obj);

gint pyg_enum_get_value  (GType enum_type, PyObject *obj, gint *val);
gint pyg_flags_get_value (GType flag_type, PyObject *obj, gint *val);
int pyg_pyobj_to_unichar_conv (PyObject* py_obj, void* ptr);

typedef PyObject *(* fromvaluefunc)(const GValue *value);
typedef int (*tovaluefunc)(GValue *value, PyObject *obj);

void      pyg_register_boxed_custom(GType boxed_type,
				    fromvaluefunc from_func,
				    tovaluefunc to_func);
int       pyg_value_from_pyobject(GValue *value, PyObject *obj);
PyObject *pyg_value_as_pyobject(const GValue *value, gboolean copy_boxed);
int       pyg_param_gvalue_from_pyobject(GValue* value,
                                         PyObject* py_obj, 
                                         const GParamSpec* pspec);
PyObject *pyg_param_gvalue_as_pyobject(const GValue* gvalue,
                                       gboolean copy_boxed, 
                                       const GParamSpec* pspec);

GClosure *pyg_closure_new(PyObject *callback, PyObject *extra_args, PyObject *swap_data);
GClosure *pyg_signal_class_closure_get(void);

PyObject *pyg_object_descr_doc_get(void);


/* from pygobject.h */
extern PyTypeObject PyGObject_Type;
extern PyTypeObject PyGInterface_Type;
extern GQuark pyginterface_type_key;

void          pygobject_register_class   (PyObject *dict,
					  const gchar *type_name,
					  GType gtype, PyTypeObject *type,
					  PyObject *bases);
void          pygobject_register_wrapper (PyObject *self);
PyObject *    pygobject_new              (GObject *obj);
PyTypeObject *pygobject_lookup_class     (GType gtype);
void          pygobject_watch_closure    (PyObject *self, GClosure *closure);
void          pygobject_register_sinkfunc(GType type,
					  void (* sinkfunc)(GObject *object));

/* from pygboxed.c */
extern PyTypeObject PyGBoxed_Type;

void       pyg_register_boxed (PyObject *dict, const gchar *class_name,
			       GType boxed_type, PyTypeObject *type);
PyObject * pyg_boxed_new      (GType boxed_type, gpointer boxed,
			       gboolean copy_boxed, gboolean own_ref);

extern PyTypeObject PyGPointer_Type;

void       pyg_register_pointer (PyObject *dict, const gchar *class_name,
				 GType pointer_type, PyTypeObject *type);
PyObject * pyg_pointer_new      (GType pointer_type, gpointer pointer);

extern char * pyg_constant_strip_prefix(gchar *name, const gchar *strip_prefix);

/* pygflags */
typedef struct {
    PyIntObject parent;
    GType gtype;
} PyGFlags;

extern PyTypeObject PyGFlags_Type;

#define PyGFlags_Check(x) (g_type_is_a(((PyGFlags*)x)->gtype, G_TYPE_FLAGS))
			   
extern PyObject * pyg_flags_add        (PyObject *   module,
					const char * typename,
					const char * strip_prefix,
					GType        gtype);
extern PyObject * pyg_flags_from_gtype (GType        gtype,
					int          value);

/* pygenum */
#define PyGEnum_Check(x) (g_type_is_a(((PyGFlags*)x)->gtype, G_TYPE_ENUM))

typedef struct {
    PyIntObject parent;
    GType gtype;
} PyGEnum;

extern PyTypeObject PyGEnum_Type;

extern PyObject * pyg_enum_add        (PyObject *   module,
				       const char * typename,
				       const char * strip_prefix,
				       GType        gtype);
extern PyObject * pyg_enum_from_gtype (GType        gtype,
				       int          value);

/* pygmainloop */

typedef struct {
    PyObject_HEAD
    GMainLoop *loop;
    GSource *signal_source;
} PyGMainLoop;

extern PyTypeObject PyGMainLoop_Type;

/* pygmaincontext */

typedef struct {
    PyObject_HEAD
    GMainContext *context;
} PyGMainContext;

extern PyTypeObject PyGMainContext_Type;

/* pygparamspec */

extern PyTypeObject PyGParamSpec_Type;
PyObject * pyg_param_spec_new (GParamSpec *pspec);



#endif

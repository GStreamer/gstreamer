#ifndef __GOBJECT_2_GTK_H__
#define __GOBJECT_2_GTK_H__

#include <gtk/gtk.h>

#define G_MAXUINT UINT_MAX
#define G_MAXULONG ULONG_MAX

#define G_E     2.7182818284590452354E0
#define G_LN2   6.9314718055994530942E-1
#define G_LN10  2.3025850929940456840E0
#define G_PI    3.14159265358979323846E0
#define G_PI_2  1.57079632679489661923E0
#define G_PI_4  0.78539816339744830962E0
#define G_SQRT2 1.4142135623730950488E0

// lists functions not in glib 1.2
GList *g_list_delete_link (GList *list, GList *llink);
GSList *g_slist_delete_link (GSList *list, GSList *llink);
  

// GObject
typedef struct _GObject GObject;
typedef struct _GObjectClass GObjectClass;

#define g_object_ref(obj)			gtk_object_ref((GtkObject *)(obj))
#define g_object_unref(obj)			gtk_object_unref((GtkObject *)(obj))

// the helper macros for type checking
#define G_TYPE_CHECK_INSTANCE_CAST		GTK_CHECK_CAST
#define G_TYPE_CHECK_INSTANCE_TYPE		GTK_CHECK_TYPE
#define G_TYPE_INSTANCE_GET_CLASS(o,t,c)        (((c*)(GTK_OBJECT(o)->klass)))
#define G_TYPE_CHECK_CLASS_CAST			GTK_CHECK_CLASS_CAST
#define G_TYPE_CHECK_CLASS_TYPE			GTK_CHECK_CLASS_TYPE
#define G_TYPE_FROM_CLASS(klass)		(((GtkObjectClass *)(klass))->type)
#define G_OBJECT_GET_CLASS(object)		(G_OBJECT(object)->klass)
#define G_OBJECT_TYPE				GTK_OBJECT_TYPE
#define G_OBJECT_CLASS_TYPE(gclass)		(gclass->type)

// types
#define G_TYPE_NONE				GTK_TYPE_NONE
#define G_TYPE_CHAR				GTK_TYPE_CHAR
#define G_TYPE_UCHAR				GTK_TYPE_UCHAR
#define G_TYPE_BOOLEAN				GTK_TYPE_BOOL
#define G_TYPE_INT				GTK_TYPE_INT
#define G_TYPE_UINT				GTK_TYPE_UINT
#define G_TYPE_LONG				GTK_TYPE_LONG
#define G_TYPE_ULONG				GTK_TYPE_ULONG
#define G_TYPE_ENUM				GTK_TYPE_ENUM
#define G_TYPE_FLAGS				GTK_TYPE_FLAGS
#define G_TYPE_FLOAT				GTK_TYPE_FLOAT
#define G_TYPE_DOUBLE				GTK_TYPE_DOUBLE
#define G_TYPE_STRING				GTK_TYPE_STRING
#define G_TYPE_POINTER				GTK_TYPE_POINTER
#define G_TYPE_BOXED				GTK_TYPE_BOXED
#define G_TYPE_PARAM				GTK_TYPE_PARAM

// marshallers
#define g_cclosure_marshal_VOID__VOID			gtk_marshal_NONE__NONE
#define g_cclosure_marshal_VOID__BOOLEAN		gtk_marshal_NONE__BOOL
#define g_cclosure_marshal_VOID__CHAR			gtk_marshal_NONE__CHAR
#define g_cclosure_marshal_VOID__UCHAR			gtk_marshal_NONE__UCHAR
#define g_cclosure_marshal_VOID__INT			gtk_marshal_NONE__INT
#define g_cclosure_marshal_VOID__UINT			gtk_marshal_NONE__UINT
#define g_cclosure_marshal_VOID__LONG			gtk_marshal_NONE__LONG
#define g_cclosure_marshal_VOID__ULONG			gtk_marshal_NONE__ULONG
#define g_cclosure_marshal_VOID__ENUM			gtk_marshal_NONE__ENUM
#define g_cclosure_marshal_VOID__FLAGS			gtk_marshal_NONE__FLAGS
#define g_cclosure_marshal_VOID__FLOAT			gtk_marshal_NONE__FLOAT
#define g_cclosure_marshal_VOID__DOUBLE			gtk_marshal_NONE__DOUBLE
#define g_cclosure_marshal_VOID__STRING			gtk_marshal_NONE__STRING
#define g_cclosure_marshal_VOID__PARAM			gtk_marshal_NONE__PARAM
#define g_cclosure_marshal_VOID__BOXED			gtk_marshal_NONE__BOXED
#define g_cclosure_marshal_VOID__POINTER		gtk_marshal_NONE__POINTER
#define g_cclosure_marshal_VOID__OBJECT			gtk_marshal_NONE__OBJECT
#define g_cclosure_marshal_STRING__OBJECT_POINTER	gtk_marshal_STRING__POINTER_POINTER
#define g_cclosure_marshal_VOID__UINT_POINTER		gtk_marshal_NONE__UINT_POINTER

#define gst_marshal_VOID__VOID			gtk_marshal_NONE__NONE
#define gst_marshal_VOID__BOOLEAN		gtk_marshal_NONE__BOOL
#define gst_marshal_VOID__INT			gtk_marshal_NONE__INT
#define gst_marshal_VOID__STRING		gtk_marshal_NONE__STRING
#define gst_marshal_VOID__POINTER		gtk_marshal_NONE__POINTER
#define gst_marshal_VOID__OBJECT		gtk_marshal_NONE__POINTER
#define gst_marshal_VOID__OBJECT_POINTER	gtk_marshal_NONE__POINTER_POINTER
#define gst_marshal_VOID__INT_INT		gtk_marshal_NONE__INT_INT

/* General macros */
#ifdef  __cplusplus
# define G_BEGIN_DECLS  extern "C" {
# define G_END_DECLS    }
#else
# define G_BEGIN_DECLS
# define G_END_DECLS
#endif

// args
//#define set_property set_arg
//#define get_property get_arg

#define g_object_get_property(obj,argname,pspec)\
G_STMT_START{ \
  (pspec)->name = (gchar*)argname;\
  gtk_object_getv ((GtkObject *)(obj),1,(pspec));\
}G_STMT_END

#define g_object_set(o,args...)		        gtk_object_set ((GtkObject *) (o), ## args)


// type system
#define GType					GtkType
#define GTypeFlags				guint
#define GClassInitFunc				GtkClassInitFunc
#define GBaseInitFunc				GtkClassInitFunc
#define GInstanceInitFunc			GtkObjectInitFunc
#define g_type_class_peek_parent(c)		gtk_type_parent_class (GTK_OBJECT_CLASS (c)->type)
#define g_type_init                             gtk_type_init
#define g_type_is_a				gtk_type_is_a
#define g_type_class_ref			gtk_type_class
#define g_type_class_unref(c)
#define g_type_name(t)				gtk_type_name(t)
#define g_type_from_name(t)			gtk_type_from_name(t)
#define g_type_parent(t)			gtk_type_parent(t)
#define GEnumValue				GtkEnumValue
#define g_enum_register_static			gtk_type_register_enum


GType g2g_object_get_type (void);

/*********************************
 * FIXME API NOT in glib2.0
 ***********************************/


// type registration
typedef struct _GTypeInfo               GTypeInfo;
struct _GTypeInfo
{
  /* interface types, classed types, instantiated types */
  guint16                class_size;

  GBaseInitFunc          base_init;
  gpointer               base_finalize;

  /* classed types, instantiated types */
  GClassInitFunc         class_init;
  gpointer               class_finalize;
  gconstpointer          class_data;
  
  /* instantiated types */
  guint16                instance_size;
  guint16                n_preallocs;
  GInstanceInitFunc      instance_init;

  /* value handling */
  const gpointer         value_table;
};

#define G_TYPE_FLAG_ABSTRACT				0

#define g_type_register_static				g2g_type_register_static
guint g2g_type_register_static (GtkType parent_type, gchar *type_name,
                              const GTypeInfo *info, guint flags);



// object creation
#define g_object_new					g2g_object_new
gpointer g2g_object_new(GtkType type,gpointer blah_varargs_stuff);

// disposal
#define g_object_run_dispose				g2g_object_run_dispose
void g2g_object_run_dispose (GObject *object);


#define G_SIGNAL_RUN_LAST				GTK_RUN_LAST
#define G_SIGNAL_RUN_FIRST				GTK_RUN_FIRST
#define G_SIGNAL_RUN_CLEANUP				0
#define G_SIGNAL_NO_RECURSE				GTK_RUN_NO_RECURSE
#define G_SIGNAL_NO_HOOKS				GTK_RUN_NO_HOOKS

#define GCallback					gpointer	// FIXME?
#define G_CALLBACK(f)					((gpointer)(f))

#define g_signal_new					g2g_signal_new
#define g_signal_handlers_destroy(x)

guint
g2g_signal_new (const gchar       *signal_name,
		GtkType            object_type,
		GtkSignalRunType   signal_flags,
		guint              function_offset,
		gpointer           accumulator,  // GSignalAccumulator   
		gpointer           accu_data,
		GtkSignalMarshaller  marshaller,
		GType              return_type,
		guint              nparams,
		...);

#define \
g_signal_emit(object,signal,detail,args...) \
gtk_signal_emit((GtkObject *)object,signal, ## args )

#define \
g_signal_connect(object,name,func,func_data) \
gtk_signal_connect((GtkObject *)object,name,func,func_data)

#define \
g_signal_emit_by_name(object,name,data,self) \
gtk_signal_emit_by_name ((GtkObject *)object,name,data,self)

#define \
g_signal_has_handler_pending(object,name,data,may_block) \
gtk_signal_handler_pending ((GtkObject *)object,name,may_block)

#define g_signal_lookup			gtk_signal_lookup
#define g_signal_handler_block(o,id)	gtk_signal_handler_block ((GtkObject *)(o), id)
#define g_signal_handler_unblock(o,id)	gtk_signal_handler_unblock ((GtkObject *)(o), id)

gint* g_signal_list_ids (GType type, guint *n_ids);

// lists
GSList*		g_slist_delete_link	(GSList *list, GSList *link);


// arguments/parameters

// first define GValue and GParamSpec
#define GValue			GtkArg
#define GParamFlags		gint
#define G_VALUE_TYPE(v)		((v)->type)
#define G_PARAM_READWRITE	GTK_ARG_READWRITE
#define G_PARAM_READABLE	GTK_ARG_READABLE
#define G_PARAM_WRITABLE	GTK_ARG_WRITABLE
#define G_OBJECT_WARN_INVALID_PROPERTY_ID(a,b,c)
typedef struct _GParamSpec GParamSpec;
struct _GParamSpec {
  gchar *name;
  gint value_type;
  gint flags;
};

#define g_value_init(value,t)			((value)->type = (t))
#define g_value_copy			gtk_arg_copy
#define g_value_unset(val)

#define g_object_class_install_property		g2g_object_class_install_property
void g2g_object_class_install_property(GObjectClass *oclass,guint property_id,GParamSpec *pspec);
#define g_object_class_find_property		g2g_object_class_find_property
GParamSpec *g2g_object_class_find_property(GObjectClass *oclass,const gchar *name);
#define g_object_class_list_properties		g2g_object_class_list_properties
GParamSpec **g2g_object_class_list_properties(GObjectClass *oclass,guint *n_properties);

#define G_IS_PARAM_SPEC_ENUM(pspec)		(GTK_FUNDAMENTAL_TYPE(pspec->value_type) == GTK_TYPE_ENUM)

#define g_param_spec_boolean			g2g_param_spec_boolean
GParamSpec *g2g_param_spec_boolean(gchar *name,gchar *nick,gchar *blurb,gboolean def,gint flags);
#define g_param_spec_int			g2g_param_spec_int
GParamSpec *g2g_param_spec_int(gchar *name,gchar *nick,gchar *blurb,gint min,gint max,gint def,gint flags);
#define g_param_spec_uint			g2g_param_spec_uint
GParamSpec *g2g_param_spec_uint(gchar *name,gchar *nick,gchar *blurb,guint min,guint max,guint def,gint flags);
#define g_param_spec_long			g2g_param_spec_long
GParamSpec *g2g_param_spec_long(gchar *name,gchar *nick,gchar *blurb,glong min,glong max,glong def,gint flags);
#define g_param_spec_ulong			g2g_param_spec_ulong
GParamSpec *g2g_param_spec_ulong(gchar *name,gchar *nick,gchar *blurb,gulong min,gulong max,gulong def,gint flags);
#define g_param_spec_float			g2g_param_spec_float
GParamSpec *g2g_param_spec_float(gchar *name,gchar *nick,gchar *blurb,float min,float max,float def,gint flags);
#define g_param_spec_double			g2g_param_spec_double
GParamSpec *g2g_param_spec_double(gchar *name,gchar *nick,gchar *blurb,double min,double max,double def,gint flags);
#define g_param_spec_enum			g2g_param_spec_enum
GParamSpec *g2g_param_spec_enum(gchar *name,gchar *nick,gchar *blurb,GType e,guint def,gint flags);
#define g_param_spec_pointer			g2g_param_spec_pointer
GParamSpec *g2g_param_spec_pointer(gchar *name,gchar *nick,gchar *blurb,gint flags);
#define g_param_spec_string			g2g_param_spec_string
GParamSpec *g2g_param_spec_string(gchar *name,gchar *nick,gchar *blurb,gchar *def,gint flags);

#define g_value_get_char(value)			GTK_VALUE_CHAR(*value)
#define g_value_set_char(value,data)		(GTK_VALUE_CHAR(*value) = (data))
#define g_value_get_uchar(value)		GTK_VALUE_UCHAR(*value)
#define g_value_set_uchar(value,data)		(GTK_VALUE_UCHAR(*value) = (data))
#define g_value_get_boolean(value)		GTK_VALUE_BOOL(*value)
#define g_value_set_boolean(value,data)		(GTK_VALUE_BOOL(*value) = (data))
#define g_value_get_enum(value)			GTK_VALUE_INT(*value)
#define g_value_set_enum(value,data)		(GTK_VALUE_INT(*value) = (data))
#define g_value_get_int(value)			GTK_VALUE_INT(*value)
#define g_value_set_int(value,data)			(GTK_VALUE_INT(*value) = (data))
#define g_value_get_uint(value)			GTK_VALUE_UINT(*value)
#define g_value_set_uint(value,data)		(GTK_VALUE_UINT(*value) = (data))
#define g_value_get_long(value)			GTK_VALUE_LONG(*value)
#define g_value_set_long(value,data)		(GTK_VALUE_LONG(*value) = (data))
#define g_value_get_ulong(value)		GTK_VALUE_ULONG(*value)
#define g_value_set_ulong(value,data)		(GTK_VALUE_ULONG(*value) = (data))
#define g_value_get_float(value)		GTK_VALUE_FLOAT(*value)
#define g_value_set_float(value,data)		(GTK_VALUE_FLOAT(*value) = (data))
#define g_value_get_double(value)		GTK_VALUE_DOUBLE(*value)
#define g_value_set_double(value,data)		(GTK_VALUE_DOUBLE(*value) = (data))
#define g_value_get_string(value)		GTK_VALUE_STRING(*value)
#define g_value_set_string(value,data)		(GTK_VALUE_STRING(*value) = (data))
#define g_value_get_pointer(value)		GTK_VALUE_POINTER(*value)
#define g_value_set_pointer(value,data)		(GTK_VALUE_POINTER(*value) = (data))


#define G_VALUE_HOLDS_CHAR(value) (((value)->type)==GTK_TYPE_CHAR)
#define G_VALUE_HOLDS_UCHAR(value) (((value)->type)==GTK_TYPE_UCHAR)
#define G_VALUE_HOLDS_BOOLEAN(value) (((value)->type)==GTK_TYPE_BOOL)
#define G_VALUE_HOLDS_INT(value) (((value)->type)==GTK_TYPE_INT)
#define G_VALUE_HOLDS_UINT(value) (((value)->type)==GTK_TYPE_UINT)
#define G_VALUE_HOLDS_LONG(value) (((value)->type)==GTK_TYPE_LONG)
#define G_VALUE_HOLDS_ULONG(value) (((value)->type)==GTK_TYPE_ULONG)
#define G_VALUE_HOLDS_FLOAT(value) (((value)->type)==GTK_TYPE_FLOAT)
#define G_VALUE_HOLDS_DOUBLE(value) (((value)->type)==GTK_TYPE_DOUBLE)
#define G_VALUE_HOLDS_STRING(value) (((value)->type)==GTK_TYPE_STRING)
#define G_VALUE_HOLDS_POINTER(value) (((value)->type)==GTK_TYPE_POINTER)

// the object itself
//#define GObject				GtkObject
//#define GObjectClass				GtkObjectClass
#define G_OBJECT(obj)				((GObject *)(obj))
#define G_OBJECT_CLASS(c)			((GObjectClass *)(c))

#define G_TYPE_OBJECT \
  (g2g_object_get_type())
//#define G_OBJECT(obj) 
//  (GTK_CHECK_CAST((obj),G_TYPE_OBJECT,GObject))
//#define G_OBJECT_CLASS(klass) 
//  (GTK_CHECK_CLASS_CAST((klass),G_TYPE_OBJECT,GObjectClass)) 
#define G_IS_OBJECT(obj) \
  (GTK_CHECK_TYPE((obj),G_TYPE_OBJECT))
#define G_IS_OBJECT_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),G_TYPE_OBJECT))

struct _GObject {
/***** THE FOLLOWING IS A VERBATIM COPY FROM GTKOBJECT *****/
  /* GtkTypeObject related fields: */
  GObjectClass *klass;
 
   
  /* 32 bits of flags. GtkObject only uses 4 of these bits and
   *  GtkWidget uses the rest. This is done because structs are
   *  aligned on 4 or 8 byte boundaries. If a new bitfield were
   *  used in GtkWidget much space would be wasted.
   */
  guint32 flags;
  
  /* reference count.
   * refer to the file docs/refcounting.txt on this issue.
   */
  guint ref_count;
  
  /* A list of keyed data pointers, used for e.g. the list of signal
   * handlers or an object's user_data.
   */
  GData *object_data;
/***** END OF COPY FROM GTKOBJECT *****/
};

struct _GObjectClass {
/***** THE FOLLOWING IS A VERBATIM COPY FROM GTKOBJECT *****/
  /* GtkTypeClass fields: */               
  GtkType type;

   
  /* The signals this object class handles. "signals" is an
   *  array of signal ID's.
   */
  guint *signals;
     
  /* The number of signals listed in "signals".
   */
  guint nsignals;
  
  /* The number of arguments per class.
   */
  guint n_args;
  GSList *construct_args;

  /* Non overridable class methods to set and get per class arguments */
  void (*set_arg) (GtkObject *object,
                   GtkArg    *arg,
                   guint      arg_id);
  void (*get_arg) (GtkObject *object,
                   GtkArg    *arg,
                   guint      arg_id);
  
  /* The functions that will end an objects life time. In one way ore
   *  another all three of them are defined for all objects. If an
   *  object class overrides one of the methods in order to perform class
   *  specific destruction then it must still invoke its superclass'
   *  implementation of the method after it is finished with its
   *  own cleanup. (See the destroy function for GtkWidget for
   *  an example of how to do this).   
   */
  void (* dispose) (GObject *object);
  void (* destroy)  (GObject *object);
 
  void (* finalize) (GObject *object);
/***** END OF COPY FROM GTKOBJECT *****/

  void (*set_property) (GObject *object, guint prop_id,
                        const GValue *value, GParamSpec *pspec);
  void (*get_property) (GObject *object, guint prop_id,
                        GValue *value, GParamSpec *pspec);
};

GType g_object_get_type (void);

#endif /* __GOBJECT_2_GTK_H__ */

/* Written by Erik Walthinsen 06-2001 */
/* Modified by Jamie Gennis 06-2001 */
#include <string.h>
#include <stdio.h>
#include "gstlog.h"
#include "gobject2gtk.h"


/* list functions not in glib 1.2 */
GList *
g_list_delete_link (GList *list, GList *llink)
{
  GList *temp = g_list_remove_link(list, llink);
  g_list_free(llink);
  return temp;
}

GSList *
g_slist_delete_link (GSList *list, GSList *llink)
{
  GSList *temp = g_slist_remove_link(list, llink);
  g_slist_free(llink);
  return temp;
}

/* string helper functions not in glib 1.2 */

gchar*
g_strcanon (gchar       *string,
	    const gchar *valid_chars,
	    gchar        substitutor)
{
  register gchar *c;
  
  g_return_val_if_fail (string != NULL, NULL);
  g_return_val_if_fail (valid_chars != NULL, NULL);
    
  for (c = string; *c; c++)
    {
      if (!strchr (valid_chars, *c))
	*c = substitutor;
    }

  return string;
}

/* GObject dummy implementation */
static void
g_object_set_arg(GtkObject *object, GtkArg *arg, guint id)
{
  ((GObjectClass *)object->klass)->set_property((GObject *)object,id,arg,NULL);
}

static void
g_object_get_arg(GtkObject *object, GtkArg *arg, guint id)
{
  ((GObjectClass *)object->klass)->get_property((GObject *)object,id,arg,NULL);
}

static void
g_object_base_class_init (GObjectClass *klass)
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*) klass;
 
  gtkobject_class->set_arg = g_object_set_arg;
  gtkobject_class->get_arg = g_object_get_arg;
}

void
g2g_object_run_dispose (GObject *object)
{
  g_return_if_fail (G_IS_OBJECT (object));
  g_return_if_fail (object->ref_count > 0);

  g_object_ref (object);
  G_OBJECT_GET_CLASS (object)->dispose (object);
  g_object_unref (object);
}

GType
g2g_object_get_type (void)
{
  static GType object_type = 0;

  if (!object_type) {
    static const GtkTypeInfo object_info = {
      "GObject",
      sizeof(GObject),
      sizeof(GObjectClass),
      (GtkClassInitFunc)NULL,
      (GtkObjectInitFunc)NULL,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)g_object_base_class_init,
    };
    object_type = gtk_type_unique(gtk_object_get_type(),&object_info);
  }
  return object_type;
} 




guint
g2g_type_register_static (GtkType parent_type, gchar *type_name,
                        const GTypeInfo *info, guint flags)
{
  GtkTypeInfo gtkinfo = {
    type_name,
    info->instance_size,
    info->class_size,
    info->class_init,
    info->instance_init,
    NULL,
    NULL,
    info->base_init,
  };
  return gtk_type_unique(parent_type,&gtkinfo);
}


gpointer
g2g_object_new(GtkType type,gpointer blah_varargs_stuff) {
  return gtk_type_new(type);
}


void
g2g_object_class_install_property(GObjectClass *oclass,guint property_id,GParamSpec *pspec)
{
  gchar *arg_fullname;
 
  arg_fullname = g_strdup_printf("%s::%s",gtk_type_name(oclass->type),pspec->name);
  /* fprintf(stderr,"installing arg \"%s\" into class \"%s\"\n",arg_fullname,""); */
  gtk_object_add_arg_type(arg_fullname,pspec->value_type,pspec->flags,property_id);
  g_free(pspec);
}

GParamSpec *
g2g_object_class_find_property(GObjectClass *class, const gchar *name)
{
  GtkArgInfo *info;
  GParamSpec *spec;

  /* fprintf(stderr,"class name is %s\n",gtk_type_name(class->type)); */

  /* the return value NULL if no error */
  if (gtk_object_arg_get_info(class->type,name,&info) != NULL) {
    return NULL;
  }
  
  spec = g_new0(GParamSpec,1);

  spec->name = (gchar *) name;
  spec->value_type = info->type;
  spec->flags = info->arg_flags;

  return spec;
}

GParamSpec **
g2g_object_class_list_properties(GObjectClass *oclass,guint *n_properties) {
  GType type = G_OBJECT_CLASS_TYPE (oclass);
  guint32 *flags;
  GtkArg *args;
  gint num_args;
  GParamSpec **params;
  int i;

  args = gtk_object_query_args (type, &flags, &num_args);
  /* FIXME: args and flags need to be freed. */

  params = g_new0(GParamSpec *,num_args);
  for (i=0;i<num_args;i++) {
    params[i] = g_new0(GParamSpec,1);
    params[i]->name = args[i].name;
    params[i]->value_type = args[i].type;
    params[i]->flags = flags[i];
  }

  *n_properties = num_args;

  return params;
}

GParamSpec *
g2g_param_spec_boolean(gchar *name,gchar *nick,gchar *blurb,gboolean def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_BOOL;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_enum(gchar *name,gchar *nick,gchar *blurb,GtkType e,guint def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = e;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_int(gchar *name,gchar *nick,gchar *blurb,gint min,gint max,gint def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_INT;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_uint(gchar *name,gchar *nick,gchar *blurb,guint min,guint max,guint def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_UINT;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_long(gchar *name,gchar *nick,gchar *blurb,glong min,glong max,glong def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_LONG;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_ulong(gchar *name,gchar *nick,gchar *blurb,gulong min,gulong max,gulong def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_ULONG;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_float(gchar *name,gchar *nick,gchar *blurb,float min,float max,float def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_FLOAT;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_double(gchar *name,gchar *nick,gchar *blurb,double min,double max,double def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_DOUBLE;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_pointer(gchar *name,gchar *nick,gchar *blurb,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_POINTER;
  spec->flags = flags;

  return spec;
}

GParamSpec *
g2g_param_spec_string(gchar *name,gchar *nick,gchar *blurb,gchar *def,gint flags) {
  GParamSpec *spec = g_new(GParamSpec,1);

  spec->name = name;
  spec->value_type = GTK_TYPE_STRING;
  spec->flags = flags;

  return spec;
}



guint
g2g_signal_new (const gchar       *name,
		GtkType            object_type,
		GtkSignalRunType   signal_flags,
		guint              function_offset,
		gpointer           accumulator,  /* GSignalAccumulator */
		gpointer           accu_data,
		GtkSignalMarshaller  marshaller,
		GType              return_val,
		guint              nparams,
		...)
{
  GtkType *params;
  guint i;
  va_list args;
  guint signal_id;

  if (strcmp (name, "destroy") == 0)
    name = "g2gdestroy";

#define MAX_SIGNAL_PARAMS		(31)		/* from gtksignal.c */
  g_return_val_if_fail (nparams < MAX_SIGNAL_PARAMS, 0);
     
  if (nparams > 0) 
    {
      params = g_new (GtkType, nparams);

      va_start (args, nparams);
                   
      for (i = 0; i < nparams; i++)
        params[i] = va_arg (args, GtkType);
  
      va_end (args);
    }           
  else
    params = NULL;
 
  signal_id = gtk_signal_newv (name,
                               signal_flags,
                               object_type,
                               function_offset,
                               marshaller,
                               return_val,
                               nparams,
                               params);
          
  g_free (params);

  /* now register it. */
  gtk_object_class_add_signals(gtk_type_class(object_type), &signal_id, 1);
    
  return signal_id;
}

gint* g_signal_list_ids (GType type, guint *n_ids)
{
  GtkObjectClass *class;

  class = gtk_type_class (type);

  *n_ids = class->nsignals;

  return class->signals;
}

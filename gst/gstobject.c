/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstobject.c: Fundamental class used for all of GStreamer
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

#include "gst_private.h"

#include "gstobject.h"
#include "gstmarshal.h"
#include "gstinfo.h"

#ifndef GST_DISABLE_TRACE
#include "gsttrace.h"
#endif

/* Object signals and args */
enum {
  PARENT_SET,
  PARENT_UNSET,
#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  OBJECT_SAVED,
#endif
  DEEP_NOTIFY,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NAME
  /* FILL ME */
};

enum {
  SO_OBJECT_LOADED,
  SO_LAST_SIGNAL
};

GType _gst_object_type = 0;
static GHashTable *object_name_counts = NULL;
G_LOCK_DEFINE_STATIC (object_name_mutex);

typedef struct _GstSignalObject GstSignalObject;
typedef struct _GstSignalObjectClass GstSignalObjectClass;

static GType		gst_signal_object_get_type	(void);
static void		gst_signal_object_class_init	(GstSignalObjectClass *klass);
static void		gst_signal_object_init		(GstSignalObject *object);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
static guint gst_signal_object_signals[SO_LAST_SIGNAL] = { 0 };
#endif

static void		gst_object_class_init		(GstObjectClass *klass);
static void		gst_object_init			(GstObject *object);
#ifndef GST_DISABLE_TRACE
static GObject *	gst_object_constructor 		(GType type, guint n_construct_properties, 
							 GObjectConstructParam *construct_params);
#endif

static void 		gst_object_set_property 	(GObject * object, guint prop_id, const GValue * value,
		                                    	 GParamSpec * pspec);
static void 		gst_object_get_property 	(GObject * object, guint prop_id, GValue * value,
		                                    	 GParamSpec * pspec);
static void 		gst_object_dispatch_properties_changed (GObject     *object,
                                       			 guint        n_pspecs,
                                       			 GParamSpec **pspecs);

static void 		gst_object_dispose 		(GObject *object);
static void 		gst_object_finalize 		(GObject *object);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
static void		gst_object_real_restore_thyself (GstObject *object, xmlNodePtr self);
#endif

static GObjectClass *parent_class = NULL;
static guint gst_object_signals[LAST_SIGNAL] = { 0 };

GType
gst_object_get_type (void)
{
  if (!_gst_object_type) {
    static const GTypeInfo object_info = {
      sizeof (GstObjectClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_object_class_init,
      NULL,
      NULL,
      sizeof (GstObject),
      32,
      (GInstanceInitFunc) gst_object_init,
      NULL
    };
    _gst_object_type = g_type_register_static (G_TYPE_OBJECT, "GstObject", &object_info, G_TYPE_FLAG_ABSTRACT);
  }
  return _gst_object_type;
}

static void
gst_object_class_init (GstObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_object_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_object_get_property);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NAME,
    g_param_spec_string ("name", "Name", "The name of the object",
                         NULL, G_PARAM_READWRITE));

  gst_object_signals[PARENT_SET] =
    g_signal_new ("parent-set", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstObjectClass, parent_set), NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
                  G_TYPE_OBJECT);
  gst_object_signals[PARENT_UNSET] =
    g_signal_new ("parent-unset", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstObjectClass, parent_unset), NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
                  G_TYPE_OBJECT);
#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  /* FIXME This should be the GType of xmlNodePtr instead of G_TYPE_POINTER */
  gst_object_signals[OBJECT_SAVED] =
    g_signal_new ("object-saved", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstObjectClass, object_saved), NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                  G_TYPE_POINTER);
  
  klass->restore_thyself = gst_object_real_restore_thyself;
#endif
  gst_object_signals[DEEP_NOTIFY] =
    g_signal_new ("deep-notify", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED | G_SIGNAL_NO_HOOKS,
                  G_STRUCT_OFFSET (GstObjectClass, deep_notify), NULL, NULL,
                  gst_marshal_VOID__OBJECT_PARAM, G_TYPE_NONE,
                  2, G_TYPE_OBJECT, G_TYPE_PARAM);

  klass->path_string_separator = "/";

  klass->signal_object = g_object_new (gst_signal_object_get_type (), NULL);

  /* see the comments at gst_object_dispatch_properties_changed */
  gobject_class->dispatch_properties_changed
	        = GST_DEBUG_FUNCPTR (gst_object_dispatch_properties_changed);

  gobject_class->dispose = gst_object_dispose;
  gobject_class->finalize = gst_object_finalize;
#ifndef GST_DISABLE_TRACE
  gobject_class->constructor = gst_object_constructor;
#endif
}

static void
gst_object_init (GstObject *object)
{
  object->lock = g_mutex_new();
  object->parent = NULL;
  object->name = NULL;

  object->flags = 0;
  GST_FLAG_SET (object, GST_FLOATING);
}

#ifndef GST_DISABLE_TRACE
static GObject *
gst_object_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_params)
{
  const gchar *name;
  GstAllocTrace *trace;
  GObject *obj = G_OBJECT_CLASS (parent_class)->constructor (type, n_construct_properties, construct_params);  

  name = g_type_name (type);

  trace = gst_alloc_trace_get (name);
  if (!trace) {
    trace = gst_alloc_trace_register (name);
  }
  gst_alloc_trace_new (trace, obj);
  
  return obj;
}
#endif
/**
 * gst_object_ref:
 * @object: GstObject to reference
 *
 * Increments the refence count on the object.
 *
 * Returns: A pointer to the object
 */
GstObject*
gst_object_ref (GstObject *object)
{
  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "ref %d->%d",
             G_OBJECT (object)->ref_count,
	     G_OBJECT (object)->ref_count + 1);

  g_object_ref (G_OBJECT (object));
  return object;
}

/**
 * gst_object_unref:
 * @object: GstObject to unreference
 *
 * Decrements the refence count on the object.  If reference count hits
 * zero, destroy the object.
 */
void
gst_object_unref (GstObject *object)
{
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (G_OBJECT (object)->ref_count > 0);

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "unref %d->%d",
             G_OBJECT (object)->ref_count,
	     G_OBJECT (object)->ref_count - 1);

  g_object_unref (G_OBJECT (object));
}

/**
 * gst_object_sink:
 * @object: GstObject to sink
 *
 * Removes floating reference on an object.  Any newly created object has
 * a refcount of 1 and is FLOATING.  This function should be used when
 * creating a new object to symbolically 'take ownership' of the object.
 * Use #gst_object_set_parent to have this done for you.
 */
void
gst_object_sink (GstObject *object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "sink");

  if (GST_OBJECT_FLOATING (object)) {
    GST_FLAG_UNSET (object, GST_FLOATING);
    gst_object_unref (object);
  }
}

/**
 * gst_object_replace:
 * @oldobj: pointer to place of old GstObject
 * @newobj: new GstObject
 *
 * Unrefs the object pointer to by oldobj, refs the newobj and
 * puts the newobj in *oldobj.
 */
void
gst_object_replace (GstObject **oldobj, GstObject *newobj)
{
  g_return_if_fail (oldobj != NULL);
  g_return_if_fail (*oldobj == NULL || GST_IS_OBJECT (*oldobj));
  g_return_if_fail (newobj == NULL || GST_IS_OBJECT (newobj));
	
  GST_CAT_LOG (GST_CAT_REFCOUNTING, "replace %s %s", 
	       *oldobj ? GST_STR_NULL (GST_OBJECT_NAME (*oldobj)) : "(NONE)", 
	       newobj ? GST_STR_NULL (GST_OBJECT_NAME (newobj)) : "(NONE)");

  if (*oldobj != newobj) {
    if (newobj)  gst_object_ref (newobj);
    if (*oldobj) gst_object_unref (*oldobj);

    *oldobj = newobj;
  }
}

static void
gst_object_dispose (GObject *object)
{
  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");
  
  GST_FLAG_SET (GST_OBJECT (object), GST_DESTROYED);
  GST_OBJECT_PARENT (object) = NULL;

  parent_class->dispose (object);
}

/* finalize is called when the object has to free its resources */
static void
gst_object_finalize (GObject *object)
{
  GstObject *gstobject = GST_OBJECT (object);

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "finalize");

  g_signal_handlers_destroy (object);

  g_free (gstobject->name);

  g_mutex_free (gstobject->lock);

#ifndef GST_DISABLE_TRACE
  {
    const gchar *name;
    GstAllocTrace *trace;
  
    name = g_type_name (G_OBJECT_TYPE (object));
    trace = gst_alloc_trace_get (name);
    g_assert (trace);
    gst_alloc_trace_free (trace, object);
  }
#endif

  parent_class->finalize (object);
}

/* Changing a GObject property of a GstObject will result in "deep_notify"
 * signals being emitted by the object itself, as well as in each parent
 * object. This is so that an application can connect a listener to the
 * top-level bin to catch property-change notifications for all contained
 * elements. */
static void
gst_object_dispatch_properties_changed (GObject     *object,
                                        guint        n_pspecs,
                                        GParamSpec **pspecs)
{
  GstObject *gst_object;
  guint i;

  /* do the standard dispatching */
  G_OBJECT_CLASS (parent_class)->dispatch_properties_changed (object, n_pspecs, pspecs);

  /* now let the parent dispatch those, too */
  gst_object = GST_OBJECT_PARENT (object);
  while (gst_object) {
    /* need own category? */
    for (i = 0; i < n_pspecs; i++) {
      GST_CAT_LOG (GST_CAT_EVENT, "deep notification from %s to %s (%s)", GST_OBJECT_NAME (object),
                 GST_OBJECT_NAME (gst_object), pspecs[i]->name);
      g_signal_emit (gst_object, gst_object_signals[DEEP_NOTIFY], g_quark_from_string (pspecs[i]->name),
                     (GstObject *) object, pspecs[i]);
    }

    gst_object = GST_OBJECT_PARENT (gst_object);
  }
}

/** 
 * gst_object_default_deep_notify:
 * @object: the #GObject that signalled the notify.
 * @orig: a #GstObject that initiated the notify.
 * @pspec: a #GParamSpec of the property.
 * @excluded_props: a set of user-specified properties to exclude or
 *  NULL to show all changes.
 *
 * Adds a default deep_notify signal callback to an
 * element. The user data should contain a pointer to an array of
 * strings that should be excluded from the notify.
 * The default handler will print the new value of the property 
 * using g_print.
 */
void
gst_object_default_deep_notify (GObject *object, GstObject *orig,
                                GParamSpec *pspec, gchar **excluded_props)
{
  GValue value = { 0, }; /* the important thing is that value.type = 0 */
  gchar *str = 0;
  gchar *name = NULL;

  if (pspec->flags & G_PARAM_READABLE) {
    /* let's not print these out for excluded properties... */
    while (excluded_props != NULL && *excluded_props != NULL) {
      if (strcmp (pspec->name, *excluded_props) == 0)
        return;
      excluded_props++;
    }
    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);

    if (G_IS_PARAM_SPEC_ENUM (pspec)) {
      GEnumValue *enum_value;
      enum_value = g_enum_get_value (G_ENUM_CLASS (g_type_class_ref (pspec->value_type)),
      g_value_get_enum (&value));

      str = g_strdup_printf ("%s (%d)", enum_value->value_nick,
                                        enum_value->value);
    }
    else {
      str = g_strdup_value_contents (&value);
    }
    name = gst_object_get_path_string (orig);
    g_print ("%s: %s = %s\n", name, pspec->name, str);
    g_free (name);
    g_free (str);
    g_value_unset (&value);
  } else {
    name = gst_object_get_path_string (orig);
    g_warning ("Parameter %s not readable in %s.",
               pspec->name, name);
    g_free (name);
  }
}

static void
gst_object_set_name_default (GstObject *object)
{
  gint count;
  gchar *name, *tmp;
  const gchar *type_name;
  
  type_name = G_OBJECT_TYPE_NAME (object);

  /* to ensure guaranteed uniqueness across threads, only one thread
   * may ever assign a name */
  G_LOCK (object_name_mutex);

  if (!object_name_counts) {
    object_name_counts = g_hash_table_new_full (g_str_hash, g_str_equal,
        g_free, NULL);
  }

  count = GPOINTER_TO_INT (g_hash_table_lookup (object_name_counts, type_name));
  g_hash_table_insert (object_name_counts, g_strdup (type_name), 
                       GINT_TO_POINTER (count + 1));
  
  G_UNLOCK (object_name_mutex);

  /* GstFooSink -> foosinkN */
  if (strncmp (type_name, "Gst", 3) == 0)
    type_name += 3;
  tmp = g_strdup_printf ("%s%d", type_name, count);
  name = g_ascii_strdown (tmp, strlen (tmp));
  g_free (tmp);
  
  gst_object_set_name (object, name);
  g_free (name);
}

/**
 * gst_object_set_name:
 * @object: GstObject to set the name of
 * @name: new name of object
 *
 * Sets the name of the object, or gives the element a guaranteed unique
 * name (if @name is NULL).
 */
void
gst_object_set_name (GstObject *object, const gchar *name)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));

  if (object->name != NULL)
    g_free (object->name);

  if (name != NULL)
    object->name = g_strdup (name);
  else
    gst_object_set_name_default (object);
}

/**
 * gst_object_get_name:
 * @object: GstObject to get the name of
 *
 * Get the name of the object.
 *
 * Returns: name of the object
 */
const gchar*
gst_object_get_name (GstObject *object)
{
  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  return object->name;
}

/**
 * gst_object_set_parent:
 * @object: GstObject to set parent of
 * @parent: new parent of object
 *
 * Sets the parent of @object. The object's reference count will be incremented,
 * and any floating reference will be removed (see gst_object_sink()).
 *
 * Causes the parent-set signal to be emitted.
 */
void
gst_object_set_parent (GstObject *object, GstObject *parent)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GST_IS_OBJECT (parent));
  g_return_if_fail (object != parent);
  g_return_if_fail (object->parent == NULL);

  gst_object_ref (object);
  gst_object_sink (object);
  object->parent = parent;

  g_signal_emit (G_OBJECT (object), gst_object_signals[PARENT_SET], 0, parent);
}

/**
 * gst_object_get_parent:
 * @object: GstObject to get parent of
 *
 * Returns the parent of @object.
 *
 * Returns: parent of the object
 */
GstObject*
gst_object_get_parent (GstObject *object)
{
  g_return_val_if_fail (object != NULL, NULL);
  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  return object->parent;
}

/**
 * gst_object_unparent:
 * @object: GstObject to unparent
 *
 * Clear the parent of @object, removing the associated reference.
 */
void
gst_object_unparent (GstObject *object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT(object));
  if (object->parent == NULL)
    return;

  GST_CAT_LOG_OBJECT (GST_CAT_REFCOUNTING, object, "unparent");
  
  g_signal_emit (G_OBJECT (object), gst_object_signals[PARENT_UNSET], 0, object->parent);

  object->parent = NULL;
  gst_object_unref (object);
}

/**
 * gst_object_check_uniqueness:
 * @list: a list of #GstObject to check through
 * @name: the name to search for
 *
 * Checks to see if there is any object named @name in @list.
 *
 * Returns: TRUE if the name does not appear in the list, FALSE if it does.
 */
gboolean
gst_object_check_uniqueness (GList *list, const gchar *name)
{
  g_return_val_if_fail (name != NULL, FALSE);

  while (list) {
    GstObject *child = GST_OBJECT (list->data);

    list = g_list_next (list);
      
    if (strcmp (GST_OBJECT_NAME (child), name) == 0) 
      return FALSE;
  }

  return TRUE;
}


#ifndef GST_DISABLE_LOADSAVE_REGISTRY
/**
 * gst_object_save_thyself:
 * @object: GstObject to save
 * @parent: The parent XML node to save the object into
 *
 * Saves the given object into the parent XML node.
 *
 * Returns: the new xmlNodePtr with the saved object
 */
xmlNodePtr
gst_object_save_thyself (GstObject *object, xmlNodePtr parent)
{
  GstObjectClass *oclass;

  g_return_val_if_fail (object != NULL, parent);
  g_return_val_if_fail (GST_IS_OBJECT (object), parent);
  g_return_val_if_fail (parent != NULL, parent);

  oclass = GST_OBJECT_GET_CLASS (object);

  if (oclass->save_thyself)
    oclass->save_thyself (object, parent);

  g_signal_emit (G_OBJECT (object), gst_object_signals[OBJECT_SAVED], 0, parent);

  return parent;
}

/**
 * gst_object_restore_thyself:
 * @object: GstObject to load into
 * @self: The XML node to load the object from
 *
 * Restores the given object with the data from the parent XML node.
 */
void
gst_object_restore_thyself (GstObject *object, xmlNodePtr self)
{
  GstObjectClass *oclass;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (self != NULL);

  oclass = GST_OBJECT_GET_CLASS (object);

  if (oclass->restore_thyself)
    oclass->restore_thyself (object, self);
}

static void
gst_object_real_restore_thyself (GstObject *object, xmlNodePtr self)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (self != NULL);
  
   gst_class_signal_emit_by_name (object, "object_loaded", self);
}
#endif /* GST_DISABLE_LOADSAVE_REGISTRY */

static void
gst_object_set_property (GObject* object, guint prop_id, 
			 const GValue* value, GParamSpec* pspec)
{
  GstObject *gstobject;
	    
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OBJECT (object));
	      
  gstobject = GST_OBJECT (object);

  switch (prop_id) {
    case ARG_NAME:
      gst_object_set_name (gstobject, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_object_get_property (GObject* object, guint prop_id, 
			 GValue* value, GParamSpec* pspec)
{
  GstObject *gstobject;
	    
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OBJECT (object));
	      
  gstobject = GST_OBJECT (object);

  switch (prop_id) {
    case ARG_NAME:
      g_value_set_string (value, (gchar*)GST_OBJECT_NAME (gstobject));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_object_get_path_string:
 * @object: GstObject to get the path from
 *
 * Generates a string describing the path of the object in
 * the object hierarchy. Only useful (or used) for debugging.
 *
 * Returns: a string describing the path of the object
 */
gchar*
gst_object_get_path_string (GstObject *object)
{
  GSList *parentage = NULL;
  GSList *parents;
  void *parent;
  gchar *prevpath, *path;
  const char *component;
  gchar *separator = "";
  gboolean free_component;

  parentage = g_slist_prepend (NULL, object);

  path = g_strdup ("");

  /* first walk the object hierarchy to build a list of the parents */
  do {
    if (GST_IS_OBJECT (object)) {
      parent = gst_object_get_parent (object);
    } else {
      parentage = g_slist_prepend (parentage, NULL);
      parent = NULL;
    }

    if (parent != NULL) {
      parentage = g_slist_prepend (parentage, parent);
    }

    object = parent;
  } while (object != NULL);

  /* then walk the parent list and print them out */
  parents = parentage;
  while (parents) {
    if (GST_IS_OBJECT (parents->data)) {
      GstObjectClass *oclass = GST_OBJECT_GET_CLASS (parents->data);

      component = gst_object_get_name (parents->data);
      separator = oclass->path_string_separator;
      free_component = FALSE;
    } else {
      component = g_strdup_printf("%p",parents->data);
      separator = "/";
      free_component = TRUE;
    }

    prevpath = path;
    path = g_strjoin (separator, prevpath, component, NULL);
    g_free(prevpath);
    if (free_component)
      g_free((gchar *)component);

    parents = g_slist_next(parents);
  }

  g_slist_free (parentage);

  return path;
}



struct _GstSignalObject {
  GObject object;
};

struct _GstSignalObjectClass {
  GObjectClass        parent_class;

  /* signals */
#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  void          (*object_loaded)           (GstSignalObject *object, GstObject *new, xmlNodePtr self);
#endif /* GST_DISABLE_LOADSAVE_REGISTRY */
};

static GType
gst_signal_object_get_type (void)
{
  static GType signal_object_type = 0;

  if (!signal_object_type) {
    static const GTypeInfo signal_object_info = {
      sizeof(GstSignalObjectClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_signal_object_class_init,
      NULL,
      NULL,
      sizeof(GstSignalObject),
      16,
      (GInstanceInitFunc)gst_signal_object_init,
      NULL
    };
    signal_object_type = g_type_register_static(G_TYPE_OBJECT, "GstSignalObject", &signal_object_info, 0);
  }
  return signal_object_type;
}

static void
gst_signal_object_class_init (GstSignalObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
  gst_signal_object_signals[SO_OBJECT_LOADED] =
    g_signal_new ("object-loaded", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstSignalObjectClass, object_loaded), NULL, NULL,
                  gst_marshal_VOID__OBJECT_POINTER, G_TYPE_NONE, 2,
                  G_TYPE_OBJECT, G_TYPE_POINTER);
#endif
}

static void
gst_signal_object_init (GstSignalObject *object)
{
}

/**
 * gst_class_signal_connect
 * @klass: the GstObjectClass to attach the signal to
 * @name: the name of the signal to attach to
 * @func: the signal function
 * @func_data: a pointer to user data
 *
 * Connect to a class signal.
 *
 * Returns: the signal id.
 */
guint
gst_class_signal_connect (GstObjectClass *klass,
			  const gchar    *name,
			  gpointer  func,
		          gpointer       func_data)
{
  return g_signal_connect (klass->signal_object, name, func, func_data);
}

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
/**
 * gst_class_signal_emit_by_name:
 * @object: the object that sends the signal
 * @name: the name of the signal to emit
 * @self: data for the signal
 *
 * emits the named class signal.
 */
void
gst_class_signal_emit_by_name (GstObject *object,
	                       const gchar *name,
	                       xmlNodePtr self)
{
  GstObjectClass *oclass;

  oclass = GST_OBJECT_GET_CLASS (object);

  g_signal_emit_by_name (oclass->signal_object, name, object, self);
}

#endif /* GST_DISABLE_LOADSAVE_REGISTRY */

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

/* Object signals and args */
enum {
  PARENT_SET,
#ifndef GST_DISABLE_LOADSAVE
  OBJECT_SAVED,
#endif
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

enum {
  SO_OBJECT_LOADED,
  SO_LAST_SIGNAL
};

typedef struct _GstSignalObject GstSignalObject;
typedef struct _GstSignalObjectClass GstSignalObjectClass;

static GType		gst_signal_object_get_type	(void);
static void		gst_signal_object_class_init	(GstSignalObjectClass *klass);
static void		gst_signal_object_init		(GstSignalObject *object);

static guint gst_signal_object_signals[SO_LAST_SIGNAL] = { 0 };

static void		gst_object_class_init		(GstObjectClass *klass);
static void		gst_object_init			(GstObject *object);

static void 		gst_object_real_destroy 	(GObject *object);
static void 		gst_object_shutdown 		(GObject *object);
static void 		gst_object_finalize 		(GObject *object);

static GObjectClass *parent_class = NULL;
static guint gst_object_signals[LAST_SIGNAL] = { 0 };

GType
gst_object_get_type (void)
{
  static GType object_type = 0;

  if (!object_type) {
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
    };
    object_type = g_type_register_static (G_TYPE_OBJECT, "GstObject", &object_info, G_TYPE_FLAG_ABSTRACT);
  }
  return object_type;
}

static void
gst_object_class_init (GstObjectClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref (G_TYPE_OBJECT);

  gst_object_signals[PARENT_SET] =
    g_signal_new("parent_set", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstObjectClass, parent_set), NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,G_TYPE_NONE,1,
                  G_TYPE_OBJECT);
#ifndef GST_DISABLE_LOADSAVE
  gst_object_signals[OBJECT_SAVED] =
    g_signal_new("object_saved", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstObjectClass, object_saved), NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,G_TYPE_NONE,1,
                  G_TYPE_POINTER);
#endif

  klass->path_string_separator = "/";
// FIXME!!!
//  klass->signal_object = g_object_new(gst_signal_object_get_type (,NULL));

//  gobject_class->shutdown = gst_object_shutdown;
//  gobject_class->destroy = gst_object_real_destroy;
  gobject_class->finalize = gst_object_finalize;
}

static void
gst_object_init (GstObject *object)
{
  object->lock = g_mutex_new();
  object->parent = NULL;

  object->flags = 0;
  GST_FLAG_SET (object, GST_FLOATING);
}

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

  GST_DEBUG (GST_CAT_REFCOUNTING, "ref '%s' %d->%d\n",GST_OBJECT_NAME(object),
             G_OBJECT(object)->ref_count,G_OBJECT(object)->ref_count+1);

  g_object_ref (G_OBJECT (object));

  return object;
}
#define gst_object_ref gst_object_ref

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

  GST_DEBUG (GST_CAT_REFCOUNTING, "unref '%s' %d->%d\n",GST_OBJECT_NAME(object),
             G_OBJECT(object)->ref_count,G_OBJECT(object)->ref_count-1);

  g_object_unref (G_OBJECT (object));
}
#define gst_object_unref gst_object_unref

/**
 * gst_object_sink:
 * @object: GstObject to sink
 *
 * Removes floating reference on an object.  Any newly created object has
 * a refcount of 1 and is FLOATING.  This function should be used when
 * creating a new object to symbolically 'take ownership of' the object.
 */
void
gst_object_sink (GstObject *object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));

  GST_DEBUG (GST_CAT_REFCOUNTING, "sink '%s'\n",GST_OBJECT_NAME(object));
  if (GST_OBJECT_FLOATING (object))
  {
    GST_FLAG_UNSET (object, GST_FLOATING);
    gst_object_unref (object);
  }
}

void
gst_object_destroy (GstObject *object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));

  GST_DEBUG (GST_CAT_REFCOUNTING, "destroy '%s'\n",GST_OBJECT_NAME(object));
  if (!GST_OBJECT_DESTROYED (object))
  {
    /* need to hold a reference count around all class method
     * invocations.
     */
    gst_object_ref (object);
//    G_OBJECT_GET_CLASS (object)->shutdown (G_OBJECT (object));
    gst_object_unref (object);
  }
}

static void
gst_object_shutdown (GObject *object)
{
  GST_DEBUG (GST_CAT_REFCOUNTING, "shutdown '%s'\n",GST_OBJECT_NAME(object));
  GST_FLAG_SET (GST_OBJECT (object), GST_DESTROYED);
//  parent_class->shutdown (object);
}

/* finilize is called when the object has to free its resources */
static void
gst_object_real_destroy (GObject *g_object)
{
  GST_DEBUG (GST_CAT_REFCOUNTING, "destroy '%s'\n",GST_OBJECT_NAME(g_object));

  GST_OBJECT_PARENT (g_object) = NULL;

// FIXME!!
//  parent_class->destroy (g_object);
}

/* finilize is called when the object has to free its resources */
static void
gst_object_finalize (GObject *object)
{
  GstObject *gstobject;

  gstobject = GST_OBJECT (object);

  GST_DEBUG (GST_CAT_REFCOUNTING, "finalize '%s'\n",GST_OBJECT_NAME(object));

  if (gstobject->name != NULL)
    g_free (gstobject->name);

  g_mutex_free (gstobject->lock);

  parent_class->finalize (object);
}

/**
 * gst_object_set_name:
 * @object: GstObject to set the name of
 * @name: new name of object
 *
 * Set the name of the object.
 */
void
gst_object_set_name (GstObject *object, const gchar *name)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (name != NULL);

  if (object->name != NULL)
    g_free (object->name);

  object->name = g_strdup (name);
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
  g_return_val_if_fail (object != NULL, NULL);
  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);

  return object->name;
}

/**
 * gst_object_set_parent:
 * @object: GstObject to set parent of
 * @parent: new parent of object
 *
 * Set the parent of the object.  The object's reference count is
 * incremented.
 * signals the parent-set signal
 */
void
gst_object_set_parent (GstObject *object, GstObject *parent)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (parent != NULL);
  g_return_if_fail (GST_IS_OBJECT (parent));
  g_return_if_fail (object != parent);

  if (object->parent != NULL) {
    GST_ERROR_OBJECT (object,object->parent, "object's parent is already set, must unparent first");
    return;
  }

  gst_object_ref (object);
  gst_object_sink (object);
  object->parent = parent;

  g_signal_emit (G_OBJECT (object), gst_object_signals[PARENT_SET], 0, parent);
}

/**
 * gst_object_get_parent:
 * @object: GstObject to get parent of
 *
 * Return the parent of the object.
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
 * Clear the parent of the object, removing the associated reference.
 */
void
gst_object_unparent (GstObject *object)
{
  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT(object));
  if (object->parent == NULL)
    return;

  object->parent = NULL;
  gst_object_unref (object);
}

/**
 * gst_object_ref:
 * @object: GstObject to reference
 *
 * Increments the refence count on the object.
 *
 * Returns: Apointer to the Object
 */
#ifndef gst_object_ref
GstObject*
gst_object_ref (GstObject *object)
{
  g_return_if_fail (object != NULL, NULL);
  g_return_if_fail (GST_IS_OBJECT (object), NULL);

//#ifdef HAVE_ATOMIC_H
//  g_return_if_fail (atomic_read (&(object->refcount)) > 0);
//  atomic_inc (&(object->refcount))
//#else
  g_return_if_fail (object->refcount > 0);
  GST_LOCK (object);
//  object->refcount++;
  g_object_ref((GObject *)object);
  GST_UNLOCK (object);
//#endif

  return object;
}
#endif /* gst_object_ref */

/**
 * gst_object_unref:
 * @object: GstObject to unreference
 *
 * Decrements the refence count on the object.  If reference count hits
 * zero, destroy the object.
 */
#ifndef gst_object_unref
void
gst_object_unref (GstObject *object)
{
  int reftest;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));

#ifdef HAVE_ATOMIC_H
  g_return_if_fail (atomic_read (&(object->refcount)) > 0);
  reftest = atomic_dec_and_test (&(object->refcount))
#else
  g_return_if_fail (object->refcount > 0);
  GST_LOCK (object);
  object->refcount--;
  reftest = (object->refcount == 0);
  GST_UNLOCK (object);
#endif

  /* if we ended up with the refcount at zero */
  if (reftest) {
    /* get the count to 1 for gtk_object_destroy() */
#ifdef HAVE_ATOMIC_H
    atomic_set (&(object->refcount),1);
#else
    object->refcount = 1;
#endif
    /* destroy it */
    gtk_object_destroy (G_OBJECT (object));
    /* drop the refcount back to zero */
#ifdef HAVE_ATOMIC_H
    atomic_set (&(object->refcount),0);
#else
    object->refcount = 0;
#endif
    /* finalize the object */
    // FIXME this is an evil hack that should be killed
// FIXMEFIXMEFIXMEFIXME
//    gtk_object_finalize(G_OBJECT(object));
  }
}
#endif /* gst_object_unref */

/**
 * gst_object_check_uniqueness:
 * @list: a list of #GstObject to check through
 * @name: the name to search for
 *
 * This function checks through the list of objects to see if the name
 * given appears in the list as the name of an object.  It returns TRUE if
 * the name does not exist in the list.
 *
 * Returns: TRUE if the name doesn't appear in the list, FALSE if it does.
 */
gboolean
gst_object_check_uniqueness (GList *list, const gchar *name)
{
  g_return_val_if_fail (name != NULL, FALSE);

  while (list) {
    GstObject *child = GST_OBJECT (list->data);

    list = g_list_next(list);
      
    if (strcmp(GST_OBJECT_NAME(child), name) == 0) 
      return FALSE;
  }

  return TRUE;
}


#ifndef GST_DISABLE_LOADSAVE
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

  oclass = (GstObjectClass *)G_OBJECT_GET_CLASS(object);
  if (oclass->save_thyself)
    oclass->save_thyself (object, parent);

  g_signal_emit (G_OBJECT (object), gst_object_signals[OBJECT_SAVED], 0, parent);

  return parent;
}

/**
 * gst_object_load_thyself:
 * @object: GstObject to load into
 * @parent: The parent XML node to save the object into
 *
 * Saves the given object into the parent XML node.
 *
 * Returns: the new xmlNodePtr with the saved object
 */
void
gst_object_restore_thyself (GstObject *object, xmlNodePtr parent)
{
  GstObjectClass *oclass;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GST_IS_OBJECT (object));
  g_return_if_fail (parent != NULL);

  oclass = (GstObjectClass *)G_OBJECT_GET_CLASS(object);
  if (oclass->restore_thyself)
    oclass->restore_thyself (object, parent);
}
#endif // GST_DISABLE_LOADSAVE

/**
 * gst_object_get_path_string:
 * @object: GstObject to get the path from
 *
 * Generates a string describing the path of the object in
 * the object hierarchy. Usefull for debugging
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

  // first walk the object hierarchy to build a list of the parents
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

  // then walk the parent list and print them out
  parents = parentage;
  while (parents) {
    if (GST_IS_OBJECT (parents->data)) {
      GstObjectClass *oclass = (GstObjectClass *)G_OBJECT_GET_CLASS(parents->data);

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
#ifndef GST_DISABLE_LOADSAVE
  void          (*object_loaded)           (GstSignalObject *object, GstObject *new, xmlNodePtr self);
#endif /* GST_DISABLE_LOADSAVE */
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

#ifndef GST_DISABLE_LOADSAVE
  gst_signal_object_signals[SO_OBJECT_LOADED] =
    g_signal_new("object_loaded", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstObjectClass, parent_set), NULL, NULL,
                  gst_marshal_VOID__OBJECT_POINTER,G_TYPE_NONE,2,
                  G_TYPE_OBJECT,G_TYPE_POINTER);
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

#ifndef GST_DISABLE_LOADSAVE
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

  oclass = (GstObjectClass *)G_OBJECT_GET_CLASS(object);

  g_signal_emit_by_name (oclass->signal_object, name, object, self);
}

#endif // GST_DISABLE_LOADSAVE

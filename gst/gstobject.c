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
#include "gstpad.h"
#include "gstelement.h"

/* Object signals and args */
enum {
  PARENT_SET,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_object_class_init(GstObjectClass *klass);
static void gst_object_init(GstObject *object);


static GtkObjectClass *parent_class = NULL;
static guint gst_object_signals[LAST_SIGNAL] = { 0 };

GtkType gst_object_get_type(void) {
  static GtkType object_type = 0;

  if (!object_type) {
    static const GtkTypeInfo object_info = {
      "GstObject",
      sizeof(GstObject),
      sizeof(GstObjectClass),
      (GtkClassInitFunc)gst_object_class_init,
      (GtkObjectInitFunc)gst_object_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    object_type = gtk_type_unique(gtk_object_get_type(),&object_info);
  }
  return object_type;
}

static void gst_object_class_init(GstObjectClass *klass) {
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gst_object_signals[PARENT_SET] =
    gtk_signal_new("parent_set",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstObjectClass,parent_set),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_OBJECT);
  gtk_object_class_add_signals(gtkobject_class,gst_object_signals,LAST_SIGNAL);
}

static void gst_object_init(GstObject *object) {
  object->lock = g_mutex_new();
#ifdef HAVE_ATOMIC_H
  atomic_set(&(object->refcount),1);
#else
  object->refcount++;
#endif
  object->parent = NULL;
}

/**
 * gst_object_new:
 *
 * Create a new, empty object.  Not very useful, should never be used.
 *
 * Returns: new object
 */
GstObject *gst_object_new() {
  return GST_OBJECT(gtk_type_new(gst_object_get_type()));
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
void gst_object_set_parent(GstObject *object,GstObject *parent) {
  g_return_if_fail(object != NULL);
  g_return_if_fail(GST_IS_OBJECT(object));
  g_return_if_fail(parent != NULL);
  g_return_if_fail(GST_IS_OBJECT(parent));
  g_return_if_fail(object != parent);

  if (object->parent != NULL) {
    ERROR_OBJECT(object,object->parent,"object's parent is already set, must unparent first");
    return;
  }

  gst_object_ref(object);
  gst_object_sink(object);
  object->parent = parent;

  gtk_signal_emit(GTK_OBJECT(object),gst_object_signals[PARENT_SET],parent);
}

/**
 * gst_object_get_parent:
 * @object: GstObject to get parent of
 *
 * Return the parent of the object.
 *
 * Returns: parent of the object
 */
GstObject *gst_object_get_parent(GstObject *object) {
  g_return_val_if_fail(object != NULL, NULL);
  g_return_val_if_fail(GST_IS_OBJECT(object), NULL);

  return object->parent;
}

/**
 * gst_object_unparent:
 * @object: GstObject to unparent
 *
 * Clear the parent of the object, removing the associated reference.
 */
void gst_object_unparent(GstObject *object) {
  g_return_if_fail(object != NULL);
  g_return_if_fail(GST_IS_OBJECT(object));
  if (object->parent == NULL)
    return;

  object->parent = NULL;
  gst_object_unref(object);
}

/**
 * gst_object_ref:
 * @object: GstObject to reference
 *
 * Increments the refence count on the object.
 */
#ifndef gst_object_ref
void gst_object_ref (GstObject *object) {
  g_return_if_fail(object != NULL);
  g_return_if_fail(GST_IS_OBJECT(object));

#ifdef HAVE_ATOMIC_H
  g_return_if_fail(atomic_read(&(object->refcount)) > 0);
  atomic_inc(&(object->refcount))
#else
  g_return_if_fail(object->refcount > 0);
  GST_LOCK(object);
  object->refcount++;
  GST_UNLOCK(object);
#endif
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
void gst_object_unref (GstObject *object) {
  int reftest;

  g_return_if_fail(object != NULL);
  g_return_if_fail(GST_IS_OBJECT(object));

#ifdef HAVE_ATOMIC_H
  g_return_if_fail(atomic_read(&(object->refcount)) > 0);
  reftest = atomic_dec_and_test(&(object->refcount))
#else
  g_return_if_fail(object->refcount > 0);
  GST_LOCK(object);
  object->refcount--;
  reftest = (object->refcount == 0);
  GST_UNLOCK(object);
#endif

  /* if we ended up with the refcount at zero */
  if (reftest) {
    /* get the count to 1 for gtk_object_destroy() */
#ifdef HAVE_ATOMIC_H
    atomic_set(&(object->refcount),1);
#else
    object->refcount = 1;
#endif
    /* destroy it */
    gtk_object_destroy(GTK_OBJECT(object));
    /* drop the refcount back to zero */
#ifdef HAVE_ATOMIC_H
    atomic_set(&(object->refcount),0);
#else
    object->refcount = 0;
#endif
    /* finalize the object */
    // FIXME this is an evil hack that should be killed
// FIXMEFIXMEFIXMEFIXME
//    gtk_object_finalize(GTK_OBJECT(object));
  }
}
#endif /* gst_object_unref */

/**
 * gst_object_sink:
 * @object: GstObject to sink
 *
 * Removes floating reference on an object.  Any newly created object has
 * a refcount of 1 and is FLOATING.  This function should be used when
 * creating a new object to symbolically 'take ownership of' the object.
 */
#ifndef gst_object_sink
void gst_object_sink(GstObject *object) {
  g_return_if_fail(object != NULL);
  g_return_if_fail(GST_IS_OBJECT(object));

  if (GTK_OBJECT_FLOATING(object)) {
    GTK_OBJECT_UNSET_FLAGS(object, GTK_FLOATING);
    gst_object_unref(object);
  }
}
#endif /* gst_object_sink */



gchar *gst_object_get_path_string(GstObject *object) {
  GSList *parentage = NULL;
  GSList *parents;
  void *parent;
  gchar *prevpath, *path = ""; 
  const char *component;
  gchar *separator = "";
  gboolean free_component;

  parentage = g_slist_prepend (NULL, object);
       
  // first walk the object hierarchy to build a list of the parents
  do {
    if (GST_IS_OBJECT(object)) {
      if (GST_IS_PAD(object)) {
        parent = GST_PAD(object)->parent;
//      } else if (GST_IS_ELEMENT(object)) {
//        parent = gst_element_get_parent(GST_ELEMENT(object));
      } else {
        parent = gst_object_get_parent (object);
      }
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
    if (GST_IS_OBJECT(parents->data)) {
      if (GST_IS_PAD(parents->data)) {
        component = gst_pad_get_name(GST_PAD(parents->data));
        separator = ".";
        free_component = FALSE;
      } else if (GST_IS_ELEMENT(parents->data)) {
        component = gst_element_get_name(GST_ELEMENT(parents->data));
        separator = "/";
        free_component = FALSE;
      } else {
//        component = g_strdup_printf("a %s",gtk_type_name(gtk_identifier_get_type(parents->data)));
        component = g_strdup_printf("unknown%p",parents->data);
        separator = "/";
        free_component = TRUE;
      }
    } else {
      component = g_strdup_printf("%p",parents->data);  
      separator = "/";
      free_component = TRUE;
    }
      
    prevpath = path;
    path = g_strjoin(separator,prevpath,component,NULL);
    g_free(prevpath);
    if (free_component)
      g_free((gchar *)component);

    parents = g_slist_next(parents);
  }
    
  g_slist_free(parentage);
              
  return path;
}


/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstobject.h: Header for base GstObject
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


#ifndef __GST_OBJECT_H__
#define __GST_OBJECT_H__

#include <gtk/gtk.h>
#include <gst/gsttrace.h>
#include <parser.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif

// FIXME
#include "gstlog.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_OBJECT \
  (gst_object_get_type())
#define GST_OBJECT(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_OBJECT,GstObject))
#define GST_OBJECT_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_OBJECT,GstObjectClass))
#define GST_IS_OBJECT(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_OBJECT))
#define GST_IS_OBJECT_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_OBJECT))

typedef struct _GstObject GstObject;
typedef struct _GstObjectClass GstObjectClass;

#define GST_OBJECT_FLAG_LAST 4

struct _GstObject {
  GtkObject object;

  gchar *name;
  /* have to have a refcount for the object */
#ifdef HAVE_ATOMIC_H
  atomic_t refcount;
#else
  int refcount;
#endif

  /* locking for all sorts of things (like the refcount) */
  GMutex *lock;

  /* this objects parent */
  GstObject *parent;
};

struct _GstObjectClass {
  GtkObjectClass	parent_class;

  gchar			*path_string_separator;
  GtkObject		*signal_object;

  /* signals */
  void		(*parent_set)		(GstObject *object, GstObject *parent);
  void		(*object_saved)		(GstObject *object, xmlNodePtr parent);

  /* functions go here */
  xmlNodePtr	(*save_thyself)		(GstObject *object, xmlNodePtr parent);
  void		(*restore_thyself)	(GstObject *object, xmlNodePtr self);
};

#define GST_OBJECT_NAME(obj)		(const gchar*)(((GstObject *)(obj))->name)
#define GST_OBJECT_PARENT(obj)		(((GstObject *)(obj))->parent)


#define GST_FLAGS(obj)			GTK_OBJECT_FLAGS(obj)
#define GST_FLAG_IS_SET(obj,flag)	(GST_FLAGS (obj) & (1<<(flag)))
#define GST_FLAG_SET(obj,flag)		G_STMT_START{ (GST_FLAGS (obj) |= (1<<(flag))); }G_STMT_END
#define GST_FLAG_UNSET(obj,flag)	G_STMT_START{ (GST_FLAGS (obj) &= ~(1<<(flag))); }G_STMT_END

/* object locking */
#define GST_LOCK(obj)		(g_mutex_lock(GST_OBJECT(obj)->lock))
#define GST_TRYLOCK(obj)	(g_mutex_trylock(GST_OBJECT(obj)->lock))
#define GST_UNLOCK(obj)		(g_mutex_unlock(GST_OBJECT(obj)->lock))
#define GST_GET_LOCK(obj)	(GST_OBJECT(obj)->lock)


/* normal GtkObject stuff */
GtkType		gst_object_get_type		(void);
GstObject*	gst_object_new			(void);

/* name routines */
void		gst_object_set_name		(GstObject *object, const gchar *name);
const gchar*	gst_object_get_name		(GstObject *object);

/* parentage routines */
void		gst_object_set_parent		(GstObject *object,GstObject *parent);
GstObject*	gst_object_get_parent		(GstObject *object);
void		gst_object_unparent		(GstObject *object);

gboolean	gst_object_check_uniqueness	(GList *list, const gchar *name);

xmlNodePtr	gst_object_save_thyself		(GstObject *object, xmlNodePtr parent);

/* refcounting */
#define		gst_object_ref(object)		gtk_object_ref(GTK_OBJECT(object));
#define		gst_object_unref(object)	gtk_object_unref(GTK_OBJECT(object));
#define		gst_object_sink(object)		gtk_object_sink(GTK_OBJECT(object));

/* destroying an object */
#define		gst_object_destroy(object)	gtk_object_destroy(GTK_OBJECT(object))

/* printing out the 'path' of the object */
gchar *		gst_object_get_path_string	(GstObject *object);

guint		gst_class_signal_connect	(GstObjectClass	*klass,
						 const gchar	*name,
						 GtkSignalFunc	func,
						 gpointer	func_data);

void		gst_class_signal_emit_by_name	(GstObject	*object,
		                                 const gchar	*name,
						 xmlNodePtr self);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_OBJECT_H__ */


/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif

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
  GtkObjectClass parent_class;

  /* signals */
  void (*parent_set) (GstObject *object,GstObject *parent);

  /* functions go here */
};


#define GST_FLAGS(obj)			GTK_OBJECT_FLAGS(obj)
#define GST_FLAG_IS_SET(obj,flag)	(GST_FLAGS (obj) & (1<<(flag)))
#define GST_FLAG_SET(obj,flag)		G_STMT_START{ (GST_FLAGS (obj) |= (1<<(flag))); }G_STMT_END
#define GST_FLAG_UNSET(obj,flag)	G_STMT_START{ (GST_FLAGS (obj) &= ~(1<<(flag))); }G_STMT_END

#define GST_LOCK(obj)		(g_mutex_lock(GST_OBJECT(obj)->lock))
#define GST_TRYLOCK(obj)	(g_mutex_trylock(GST_OBJECT(obj)->lock))
#define GST_UNLOCK(obj)		(g_mutex_unlock(GST_OBJECT(obj)->lock))


/* normal GtkObject stuff */
GtkType 	gst_object_get_type		(void);
GstObject* 	gst_object_new			(void);

/* parentage routines */
void 		gst_object_set_parent		(GstObject *object,GstObject *parent);
GstObject*	gst_object_get_parent		(GstObject *object);
void 		gst_object_unparent		(GstObject *object);

/* refcounting */
#define 	gst_object_ref(object) 		gtk_object_ref(GTK_OBJECT(object));
#define 	gst_object_unref(object) 	gtk_object_unref(GTK_OBJECT(object));
#define 	gst_object_sink(object) 	gtk_object_sink(GTK_OBJECT(object));

/* destroying an object */
#define 	gst_object_destroy(object) 	gtk_object_destroy(GTK_OBJECT(object))


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_OBJECT_H__ */     


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

#include <glib-object.h>
#include <gst/gsttrace.h>
#include <parser.h>

#include <gst/gstmarshal.h>

#include <gst/gsttypes.h>

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
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OBJECT,GstObject))
#define GST_OBJECT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OBJECT,GstObjectClass))
#define GST_IS_OBJECT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OBJECT))
#define GST_IS_OBJECT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OBJECT))

//typedef struct _GstObject GstObject;
//typedef struct _GstObjectClass GstObjectClass;
//
typedef enum
{
  GST_DESTROYED   = 0,
  GST_FLOATING,

  GST_OBJECT_FLAG_LAST   = 4,
} GstObjectFlags;

struct _GstObject {
  GObject object;

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

  guint32 flags;
};

struct _GstObjectClass {
  GObjectClass	parent_class;

  gchar			*path_string_separator;
  GObject		*signal_object;

  /* signals */
  void		(*parent_set)		(GstObject *object, GstObject *parent);
  void		(*object_saved)		(GstObject *object, xmlNodePtr parent);

  /* functions go here */
  void		(*destroy)		(GstObject *object);

  xmlNodePtr	(*save_thyself)		(GstObject *object, xmlNodePtr parent);
  void		(*restore_thyself)	(GstObject *object, xmlNodePtr self);
};

#define GST_FLAGS(obj)			(GST_OBJECT (obj)->flags)
#define GST_FLAG_IS_SET(obj,flag)	(GST_FLAGS (obj) & (1<<(flag)))
#define GST_FLAG_SET(obj,flag)		G_STMT_START{ (GST_FLAGS (obj) |= (1<<(flag))); }G_STMT_END
#define GST_FLAG_UNSET(obj,flag)	G_STMT_START{ (GST_FLAGS (obj) &= ~(1<<(flag))); }G_STMT_END

#define GST_OBJECT_NAME(obj)		(const gchar*)(((GstObject *)(obj))->name)
#define GST_OBJECT_PARENT(obj)		(((GstObject *)(obj))->parent)

#define GST_OBJECT_DESTROYED(obj)	(GST_FLAG_IS_SET (obj, GST_DESTROYED))
#define GST_OBJECT_FLOATING(obj)	(GST_FLAG_IS_SET (obj, GST_FLOATING))

/* object locking */
#define GST_LOCK(obj)		(g_mutex_lock(GST_OBJECT(obj)->lock))
#define GST_TRYLOCK(obj)	(g_mutex_trylock(GST_OBJECT(obj)->lock))
#define GST_UNLOCK(obj)		(g_mutex_unlock(GST_OBJECT(obj)->lock))
#define GST_GET_LOCK(obj)	(GST_OBJECT(obj)->lock)


/* normal GObject stuff */
GType		gst_object_get_type		(void);
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
GstObject *	gst_object_ref			(GstObject *object);		
void 		gst_object_unref		(GstObject *object);		
void 		gst_object_sink			(GstObject *object);		

/* destroying an object */
void 		gst_object_destroy		(GstObject *object);		

/* printing out the 'path' of the object */
gchar *		gst_object_get_path_string	(GstObject *object);

guint		gst_class_signal_connect	(GstObjectClass	*klass,
						 const gchar	*name,
						 gpointer	func,
						 gpointer	func_data);

void		gst_class_signal_emit_by_name	(GstObject	*object,
		                                 const gchar	*name,
						 xmlNodePtr self);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_OBJECT_H__ */


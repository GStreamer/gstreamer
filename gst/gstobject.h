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

#include <gst/gstconfig.h>

#include <glib-object.h>	/* note that this gets wrapped in __GST_OBJECT_H__ */

#include <gst/gsttypes.h>

G_BEGIN_DECLS

GST_EXPORT GType _gst_object_type;

#define GST_TYPE_OBJECT			(_gst_object_type)
#define GST_IS_OBJECT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OBJECT))
#define GST_IS_OBJECT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_OBJECT))
#define GST_OBJECT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_OBJECT, GstObjectClass))
#define GST_OBJECT(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OBJECT, GstObject))
#define GST_OBJECT_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_OBJECT, GstObjectClass))

/* make sure we don't change the object size but stil make it compile
 * without libxml */
#ifdef GST_DISABLE_LOADSAVE_REGISTRY
#define xmlNodePtr	gpointer
#endif

typedef enum
{
  GST_DESTROYED   = 0,
  GST_FLOATING,

  GST_OBJECT_FLAG_LAST   = 4
} GstObjectFlags;

struct _GstObject {
  GObject 	object;

  gchar 	*name;

  /* locking for all sorts of things */
  GMutex 	*lock;
  /* this object's parent */
  GstObject 	*parent;

  guint32 	flags;

  gpointer _gst_reserved[GST_PADDING];
};

/* signal_object is used to signal to the whole class */
struct _GstObjectClass {
  GObjectClass	parent_class;

  gchar		*path_string_separator;
  GObject	*signal_object;

  /* signals */
  void		(*parent_set)		(GstObject *object, GstObject *parent);
  void		(*parent_unset)		(GstObject *object, GstObject *parent);
  void		(*object_saved)		(GstObject *object, xmlNodePtr parent);
  void 		(*deep_notify)   	(GstObject *object, GstObject *orig, GParamSpec *pspec);

  /* functions go here */
  void		(*destroy)		(GstObject *object);

  xmlNodePtr	(*save_thyself)		(GstObject *object, xmlNodePtr parent);
  void		(*restore_thyself)	(GstObject *object, xmlNodePtr self);

  gpointer _gst_reserved[GST_PADDING];
};

#define GST_FLAGS(obj)			(GST_OBJECT (obj)->flags)
#define GST_FLAG_IS_SET(obj,flag)	(GST_FLAGS (obj) & (1<<(flag)))
#define GST_FLAG_SET(obj,flag)		G_STMT_START{ (GST_FLAGS (obj) |= (1<<(flag))); }G_STMT_END
#define GST_FLAG_UNSET(obj,flag)	G_STMT_START{ (GST_FLAGS (obj) &= ~(1<<(flag))); }G_STMT_END

#define GST_OBJECT_NAME(obj)		(const gchar*)(((GstObject *)(obj))->name)
#define GST_OBJECT_PARENT(obj)		(((GstObject *)(obj))->parent)

#define GST_OBJECT_DESTROYED(obj)	(GST_FLAG_IS_SET (obj, GST_DESTROYED))
#define GST_OBJECT_FLOATING(obj)	(GST_FLAG_IS_SET (obj, GST_FLOATING))

/* CR1: object locking - GObject 2.0 doesn't have threadsafe locking */
#define GST_LOCK(obj)			(g_mutex_lock(GST_OBJECT(obj)->lock))
#define GST_TRYLOCK(obj)		(g_mutex_trylock(GST_OBJECT(obj)->lock))
#define GST_UNLOCK(obj)			(g_mutex_unlock(GST_OBJECT(obj)->lock))
#define GST_GET_LOCK(obj)		(GST_OBJECT(obj)->lock)


/* normal GObject stuff */
GType		gst_object_get_type		(void);

/* name routines */
void		gst_object_set_name		(GstObject *object, const gchar *name);
G_CONST_RETURN gchar*
		gst_object_get_name		(GstObject *object);

/* parentage routines */
void		gst_object_set_parent		(GstObject *object, GstObject *parent);
GstObject*	gst_object_get_parent		(GstObject *object);
void		gst_object_unparent		(GstObject *object);

void            gst_object_default_deep_notify 	(GObject *object, GstObject *orig, 
		                                 GParamSpec *pspec, gchar **excluded_props);

gboolean	gst_object_check_uniqueness	(GList *list, const gchar *name);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
xmlNodePtr	gst_object_save_thyself		(GstObject *object, xmlNodePtr parent);
void		gst_object_restore_thyself	(GstObject *object, xmlNodePtr self);
#else
#pragma GCC poison gst_object_save_thyself
#pragma GCC poison gst_object_restore_thyself
#endif

/* refcounting + life cycle */
GstObject *	gst_object_ref			(GstObject *object);
void 		gst_object_unref		(GstObject *object);
void 		gst_object_sink			(GstObject *object);

/* replace object pointer */
void 		gst_object_replace		(GstObject **oldobj, GstObject *newobj);

/* printing out the 'path' of the object */
gchar *		gst_object_get_path_string	(GstObject *object);

guint		gst_class_signal_connect	(GstObjectClass	*klass,
						 const gchar	*name,
						 gpointer	 func,
						 gpointer	 func_data);

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
void		gst_class_signal_emit_by_name	(GstObject	*object,
		                                 const gchar	*name,
						 xmlNodePtr 	 self);
#else
#pragma GCC poison gst_class_signal_emit_by_name
#endif


G_END_DECLS

#endif /* __GST_OBJECT_H__ */


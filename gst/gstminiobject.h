/* GStreamer
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
 *
 * gstminiobject.h: Header for GstMiniObject
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


#ifndef __GST_MINI_OBJECT_H__
#define __GST_MINI_OBJECT_H__

#include <gst/gstconfig.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_IS_MINI_OBJECT_TYPE(obj,type)  ((obj) && GST_MINI_OBJECT_TYPE(obj) == (type))
#define GST_MINI_OBJECT_CAST(obj)          ((GstMiniObject*)(obj))
#define GST_MINI_OBJECT_CONST_CAST(obj)    ((const GstMiniObject*)(obj))
#define GST_MINI_OBJECT(obj)               (GST_MINI_OBJECT_CAST(obj))

typedef struct _GstMiniObject GstMiniObject;

/**
 * GstMiniObjectCopyFunction:
 * @obj: MiniObject to copy
 *
 * Function prototype for methods to create copies of instances.
 *
 * Returns: reference to cloned instance.
 */
typedef GstMiniObject * (*GstMiniObjectCopyFunction) (const GstMiniObject *obj);
/**
 * GstMiniObjectDisposeFunction:
 * @obj: MiniObject to dispose
 *
 * Function prototype for when a miniobject has lost its last refcount.
 * Implementation of the mini object are allowed to revive the
 * passed object by doing a gst_mini_object_ref(). If the object is not
 * revived after the dispose function, the function should return %TRUE
 * and the memory associated with the object is freed.
 *
 * Returns: %TRUE if the object should be cleaned up.
 */
typedef gboolean (*GstMiniObjectDisposeFunction) (GstMiniObject *obj);
/**
 * GstMiniObjectFreeFunction:
 * @obj: MiniObject to free
 *
 * Virtual function prototype for methods to free ressources used by
 * mini-objects.
 */
typedef void (*GstMiniObjectFreeFunction) (GstMiniObject *obj);

 /**
 * GstMiniObjectWeakNotify:
 * @data: data that was provided when the weak reference was established
 * @where_the_mini_object_was: the mini object being finalized
 * 
 * A #GstMiniObjectWeakNotify function can be added to a mini object as a
 * callback that gets triggered when the mini object is finalized. Since the
 * mini object is already being finalized when the #GstMiniObjectWeakNotify is
 * called, there's not much you could do with the object, apart from e.g. using
 * its adress as hash-index or the like.
 *
 * Since: 0.10.35
 */
typedef void (*GstMiniObjectWeakNotify) (gpointer data,
    GstMiniObject * where_the_mini_object_was);

/**
 * GST_MINI_OBJECT_FLAGS:
 * @obj: MiniObject to return flags for.
 *
 * This macro returns the entire set of flags for the mini-object.
 */
#define GST_MINI_OBJECT_TYPE(obj)  (GST_MINI_OBJECT_CAST(obj)->type)
/**
 * GST_MINI_OBJECT_FLAGS:
 * @obj: MiniObject to return flags for.
 *
 * This macro returns the entire set of flags for the mini-object.
 */
#define GST_MINI_OBJECT_FLAGS(obj)  (GST_MINI_OBJECT_CAST(obj)->flags)
/**
 * GST_MINI_OBJECT_FLAG_IS_SET:
 * @obj: MiniObject to check for flags.
 * @flag: Flag to check for
 *
 * This macro checks to see if the given flag is set.
 */
#define GST_MINI_OBJECT_FLAG_IS_SET(obj,flag)        !!(GST_MINI_OBJECT_FLAGS (obj) & (flag))
/**
 * GST_MINI_OBJECT_FLAG_SET:
 * @obj: MiniObject to set flag in.
 * @flag: Flag to set, can by any number of bits in guint32.
 *
 * This macro sets the given bits.
 */
#define GST_MINI_OBJECT_FLAG_SET(obj,flag)           (GST_MINI_OBJECT_FLAGS (obj) |= (flag))
/**
 * GST_MINI_OBJECT_FLAG_UNSET:
 * @obj: MiniObject to unset flag in.
 * @flag: Flag to set, must be a single bit in guint32.
 *
 * This macro usets the given bits.
 */
#define GST_MINI_OBJECT_FLAG_UNSET(obj,flag)         (GST_MINI_OBJECT_FLAGS (obj) &= ~(flag))

/**
 * GstMiniObjectFlags:
 * @GST_MINI_OBJECT_FLAG_LAST: first flag that can be used by subclasses.
 *
 * Flags for the mini object
 */
typedef enum
{
  /* padding */
  GST_MINI_OBJECT_FLAG_LAST = (1<<4)
} GstMiniObjectFlags;

/**
 * GST_MINI_OBJECT_REFCOUNT:
 * @obj: a #GstMiniObject
 *
 * Get access to the reference count field of the mini-object.
 */
#define GST_MINI_OBJECT_REFCOUNT(obj)           ((GST_MINI_OBJECT_CAST(obj))->refcount)
/**
 * GST_MINI_OBJECT_REFCOUNT_VALUE:
 * @obj: a #GstMiniObject
 *
 * Get the reference count value of the mini-object.
 */
#define GST_MINI_OBJECT_REFCOUNT_VALUE(obj)     (g_atomic_int_get (&(GST_MINI_OBJECT_CAST(obj))->refcount))

/**
 * GST_MINI_OBJECT_SIZE:
 * @obj: a #GstMiniObject
 *
 * Get the allocated size of @obj.
 */
#define GST_MINI_OBJECT_SIZE(obj)              ((GST_MINI_OBJECT_CAST(obj))->size)

/**
 * GstMiniObject:
 * @type: the GType of the object
 * @refcount: atomic refcount
 * @flags: extra flags.
 * @size: the size of the structure
 * @copy: a copy function
 * @dispose: a dispose function
 * @free: the free function
 *
 * Base class for refcounted lightweight objects.
 * Ref Func: gst_mini_object_ref
 * Unref Func: gst_mini_object_unref
 * Set Value Func: g_value_set_boxed
 * Get Value Func: g_value_get_boxed
 */
struct _GstMiniObject {
  GType   type;

  /*< public >*/ /* with COW */
  gint    refcount;
  guint   flags;
  gsize   size;

  GstMiniObjectCopyFunction copy;
  GstMiniObjectDisposeFunction dispose;
  GstMiniObjectFreeFunction free;

  /* < private > */
  /* Used to keep track of weak ref notifies */
  guint n_weak_refs;
  struct
  {
    GstMiniObjectWeakNotify notify;
    gpointer data;
  } *weak_refs;
};

void            gst_mini_object_init            (GstMiniObject *mini_object,
                                                 GType type, gsize size);

GstMiniObject * gst_mini_object_copy		(const GstMiniObject *mini_object);
gboolean        gst_mini_object_is_writable	(const GstMiniObject *mini_object);
GstMiniObject * gst_mini_object_make_writable	(GstMiniObject *mini_object);

/* refcounting */
GstMiniObject * gst_mini_object_ref		(GstMiniObject *mini_object);
void            gst_mini_object_unref		(GstMiniObject *mini_object);

void            gst_mini_object_weak_ref        (GstMiniObject *object,
					         GstMiniObjectWeakNotify notify,
					         gpointer data);
void            gst_mini_object_weak_unref	(GstMiniObject *object,
					         GstMiniObjectWeakNotify notify,
					         gpointer data);

gboolean        gst_mini_object_replace         (GstMiniObject **olddata, GstMiniObject *newdata);
gboolean        gst_mini_object_take            (GstMiniObject **olddata, GstMiniObject *newdata);
GstMiniObject * gst_mini_object_steal           (GstMiniObject **olddata);

#define GST_DEFINE_MINI_OBJECT_TYPE(TypeName,type_name) \
   G_DEFINE_BOXED_TYPE(TypeName,type_name,              \
       (GBoxedCopyFunc) gst_mini_object_ref,            \
       (GBoxedFreeFunc)gst_mini_object_unref)

G_END_DECLS

#endif


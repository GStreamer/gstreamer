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

#define GST_TYPE_MINI_OBJECT          (gst_mini_object_get_type())
#define GST_IS_MINI_OBJECT(obj)       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MINI_OBJECT))
#define GST_IS_MINI_OBJECT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MINI_OBJECT))
#define GST_MINI_OBJECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MINI_OBJECT, GstMiniObjectClass))
#define GST_MINI_OBJECT(obj)          (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MINI_OBJECT, GstMiniObject))
#define GST_MINI_OBJECT_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MINI_OBJECT, GstMiniObjectClass))
#define GST_MINI_OBJECT_CAST(obj)     ((GstMiniObject*)(obj))

typedef struct _GstMiniObject GstMiniObject;
typedef struct _GstMiniObjectClass GstMiniObjectClass;

typedef GstMiniObject * (*GstMiniObjectCopyFunction) (const GstMiniObject *);
typedef void (*GstMiniObjectFinalizeFunction) (GstMiniObject *);

#define GST_MINI_OBJECT_FLAGS(obj)  (GST_MINI_OBJECT(obj)->flags)
#define GST_MINI_OBJECT_FLAG_IS_SET(obj,flag)        (GST_MINI_OBJECT_FLAGS(obj) & (flag))
#define GST_MINI_OBJECT_FLAG_SET(obj,flag)           (GST_MINI_OBJECT_FLAGS (obj) |= (flag))
#define GST_MINI_OBJECT_FLAG_UNSET(obj,flag)         (GST_MINI_OBJECT_FLAGS (obj) &= ~(flag))

#define GST_VALUE_HOLDS_MINI_OBJECT(value)  (G_VALUE_HOLDS(value, GST_TYPE_MINI_OBJECT))

typedef enum
{
  GST_MINI_OBJECT_FLAG_READONLY = (1<<0),
  GST_MINI_OBJECT_FLAG_STATIC = (1<<1),
  GST_MINI_OBJECT_FLAG_LAST = (1<<4)
} GstMiniObjectFlags;

#define GST_MINI_OBJECT_REFCOUNT(obj)           ((GST_MINI_OBJECT_CAST(obj))->refcount)
#define GST_MINI_OBJECT_REFCOUNT_VALUE(obj)     (g_atomic_int_get (&(GST_MINI_OBJECT_CAST(obj))->refcount))

struct _GstMiniObject {
  GTypeInstance instance;
  gint refcount;
  guint flags;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstMiniObjectClass {
  GTypeClass type_class;

  GstMiniObjectCopyFunction copy;
  GstMiniObjectFinalizeFunction finalize;

  gpointer _gst_reserved[GST_PADDING];
};

GType gst_mini_object_get_type (void);

GstMiniObject * gst_mini_object_new (GType type);
GstMiniObject * gst_mini_object_copy (const GstMiniObject *mini_object);
gboolean gst_mini_object_is_writable (const GstMiniObject *mini_object);
GstMiniObject * gst_mini_object_make_writable (GstMiniObject *mini_object);

GstMiniObject * gst_mini_object_ref (GstMiniObject *mini_object);
void gst_mini_object_unref (GstMiniObject *mini_object);

void gst_mini_object_replace (GstMiniObject **olddata, GstMiniObject *newdata);

GParamSpec * gst_param_spec_mini_object (const char *name, const char *nick,
    const char *blurb, GType object_type, GParamFlags flags);

void gst_value_set_mini_object (GValue *value, GstMiniObject *mini_object);
void gst_value_take_mini_object (GValue *value, GstMiniObject *mini_object);
GstMiniObject * gst_value_get_mini_object (const GValue *value);


G_END_DECLS

#endif


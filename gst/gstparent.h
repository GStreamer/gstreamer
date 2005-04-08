/* GStreamer
 * Copyright (C) 2005 Stefan Kost <ensonic@users.sf.net>
 *
 * gstparent.h: interface header for multi child elements
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

#ifndef __GST_PARENT_H__
#define __GST_PARENT_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_PARENT            (gst_parent_get_type())
#define GST_PARENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PARENT, GstParent))
#define GST_PARENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PARENT, GstParentClass))
#define GST_IS_PARENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PARENT))
#define GST_IS_PARENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PARENT))
#define GST_PARENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_PARENT, GstParentClass))


typedef struct _GstParent GstParent; /* dummy object */
typedef struct _GstParentClass GstParentClass;

struct _GstParentClass {
  GTypeInterface parent;

  // methods
  GObject * (* get_child_by_name) (GstParent *parent, const gchar *name);
  GObject * (* get_child_by_index) (GstParent *parent, guint index);
  guint (* get_children_count) (GstParent *parent);
  // signals
  void (* child_added) (GstParent *parent, GObject *child);
  void (* child_removed) (GstParent *parent, GObject *child);
};

GType gst_parent_get_type(void);

GObject * gst_parent_get_child_by_name (GstParent *parent, const gchar *name);
GObject * gst_parent_get_child_by_index (GstParent *parent, guint index);
guint gst_parent_get_children_count(GstParent *parent);

void gst_parent_get_valist (GstParent *parent, const gchar *first_property_name, va_list var_args);
void gst_parent_get (GstParent *parent, const gchar *first_property_name, ...);
void gst_parent_set_valist (GstParent *parent, const gchar *first_property_name, va_list var_args);
void gst_parent_set (GstParent *parent, const gchar *first_property_name, ...);

G_END_DECLS

#endif /* __GST_PARENT_H__ */

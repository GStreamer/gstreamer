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


#ifndef __GST_EDITOR_PROPERTY_H__
#define __GST_EDITOR_PROPERTY_H__

#include <gst/gst.h>
#include <glade/glade.h>
#include "gsteditor.h"

#define GST_TYPE_EDITOR_PROPERTY \
  (gst_editor_property_get_type())
#define GST_EDITOR_PROPERTY(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_PROPERTY,GstEditorProperty))
#define GST_EDITOR_PROPERTY_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_PROPERTY,GstEditorProperty))
#define GST_IS_EDITOR_PROPERTY(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_PROPERTY))
#define GST_IS_EDITOR_PROPERTY_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_PROPERTY))

typedef struct _GstEditorProperty GstEditorProperty;
typedef struct _GstEditorPropertyClass GstEditorPropertyClass;

struct _GstEditorProperty {
  GtkObject object;

  GladeXML *xml;
  GHashTable *panels;
  gpointer *current;
};

struct _GstEditorPropertyClass {
  GtkObjectClass parent_class;

  void (*element_selected)  (GstEditorProperty *property,
                             GstEditorElement *element);
  void (*in_selection_mode) (GstEditorProperty *property,
                             GstEditorElement *element);
};

GtkType gst_editor_property_get_type();
GstEditorProperty *gst_editor_property_get();

void gst_editor_property_show(GstEditorProperty *property, GstEditorElement *element);


#endif /* __GST_EDITOR_PROPERTY_H__ */

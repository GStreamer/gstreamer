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

#include "gstfilter.h"


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_filter_class_init	(GstFilterClass *klass);
static void gst_filter_init		(GstFilter *filter);

static GstElementClass *parent_class = NULL;
//static guint gst_filter_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_filter_get_type(void) {
  static GtkType filter_type = 0;

  if (!filter_type) {
    static const GtkTypeInfo filter_info = {
      "GstFilter",
      sizeof(GstFilter),
      sizeof(GstFilterClass),
      (GtkClassInitFunc)gst_filter_class_init,
      (GtkObjectInitFunc)gst_filter_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    filter_type = gtk_type_unique(GST_TYPE_ELEMENT,&filter_info);
  }
  return filter_type;
}

static void
gst_filter_class_init (GstFilterClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);
}

static void 
gst_filter_init (GstFilter *filter) 
{
}

/**
 * gst_filter_new:
 * @name: name of new filter
 *
 * Create a new filter with given name.
 *
 * Returns: new filter
 */
GstElement*
gst_filter_new (gchar *name) 
{
  GstElement *filter = GST_ELEMENT (gtk_type_new (gst_filter_get_type()));

  gst_element_set_name (GST_ELEMENT (filter), name);

  return filter;
}

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


#include <gst/gsttypefind.h>


/* TypeFind signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_typefind_class_init(GstTypeFindClass *klass);
static void gst_typefind_init(GstTypeFind *typefind);


static GstElementClass *parent_class = NULL;
static guint gst_typefind_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_typefind_get_type(void) {
  static GtkType typefind_type = 0;

  if (!typefind_type) {
    static const GtkTypeInfo typefind_info = {
      "GstTypeFind",
      sizeof(GstTypeFind),
      sizeof(GstTypeFindClass),
      (GtkClassInitFunc)gst_typefind_class_init,
      (GtkObjectInitFunc)gst_typefind_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    typefind_type = gtk_type_unique(GST_TYPE_ELEMENT,&typefind_info);
  }
  return typefind_type;
}

static void
gst_typefind_class_init(GstTypeFindClass *klass) {
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);
}

static void gst_typefind_init(GstTypeFind *typefind) {
}

GstObject *gst_typefind_new(gchar *name) {
  GstObject *typefind = GST_OBJECT(gtk_type_new(GST_TYPE_TYPEFIND));
  gst_element_set_name(GST_ELEMENT(typefind),name);
  return typefind;
}

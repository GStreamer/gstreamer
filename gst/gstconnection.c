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

#include <gst/gstconnection.h>


/* Connection signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_connection_class_init(GstConnectionClass *klass);
static void gst_connection_init(GstConnection *connection);


static GstElementClass *parent_class = NULL;
static guint gst_connection_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_connection_get_type(void) {
  static GtkType connection_type = 0;

  if (!connection_type) {
    static const GtkTypeInfo connection_info = {
      "GstConnection",
      sizeof(GstConnection),
      sizeof(GstConnectionClass),
      (GtkClassInitFunc)gst_connection_class_init,
      (GtkObjectInitFunc)gst_connection_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    connection_type = gtk_type_unique(GST_TYPE_ELEMENT,&connection_info);
  }
  return connection_type;
}

static void
gst_connection_class_init(GstConnectionClass *klass) {
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);
}

static void gst_connection_init(GstConnection *connection) {
}

/**
 * gst_connection_new:
 * @name: name of new connection
 *
 * Create a new connection with given name.
 *
 * Returns: new connection
 */
GstElement *gst_connection_new(gchar *name) {
  GstElement *connection = GST_ELEMENT(gtk_type_new(gst_connection_get_type()));
  gst_element_set_name(GST_ELEMENT(connection),name);
  return connection;
}

void gst_connection_push(GstConnection *connection) {
  GstConnectionClass *oclass;

  g_return_if_fail(connection != NULL);
  g_return_if_fail(GST_IS_CONNECTION(connection));

  oclass = (GstConnectionClass *)(GTK_OBJECT(connection)->klass);

  g_return_if_fail(oclass->push != NULL);

  (oclass->push)(connection);
}

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


#include "gstsink.h"


/* Sink signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_sink_class_init	(GstSinkClass *klass);
static void gst_sink_init	(GstSink *sink);

static GstElementClass *parent_class = NULL;
//static guint gst_sink_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_sink_get_type (void) 
{
  static GtkType sink_type = 0;

  if (!sink_type) {
    static const GtkTypeInfo sink_info = {
      "GstSink",
      sizeof(GstSink),
      sizeof(GstSinkClass),
      (GtkClassInitFunc)gst_sink_class_init,
      (GtkObjectInitFunc)gst_sink_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    sink_type = gtk_type_unique (GST_TYPE_ELEMENT, &sink_info);
  }
  return sink_type;
}

static void
gst_sink_class_init (GstSinkClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);
}

static void 
gst_sink_init (GstSink *sink) 
{
}

/**
 * gst_sink_new:
 * @name: name of new sink
 *
 * Create a new sink with given name.
 *
 * Returns: new sink
 */

GstObject*
gst_sink_new (gchar *name) 
{
  GstObject *sink = GST_OBJECT (gtk_type_new (GST_TYPE_SINK));
  gst_element_set_name (GST_ELEMENT (sink), name);

  return sink;
}

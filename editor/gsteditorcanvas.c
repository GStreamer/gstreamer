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


#include <gnome.h>
#include <gst/gst.h>

#include "gsteditor.h"
#include "gsteditorcreate.h"

/* signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
};

static void 	gst_editor_canvas_class_init	(GstEditorCanvasClass *klass);
static void 	gst_editor_canvas_init		(GstEditorCanvas *editorcanvas);

static void 	gst_editor_canvas_set_arg	(GtkObject *object,GtkArg *arg,guint id);
static void 	gst_editor_canvas_get_arg	(GtkObject *object,GtkArg *arg,guint id);

static void 	gst_editor_canvas_realize 	(GtkWidget *widget);


//gint gst_editor_canvas_verbose_event(GtkWidget *widget,GdkEvent *event);


static GstEditorBinClass *parent_class = NULL;

GtkType 
gst_editor_canvas_get_type (void) 
{
  static GtkType editor_canvas_type = 0;

  if (!editor_canvas_type) {
    static const GtkTypeInfo editor_canvas_info = {
      "GstEditorCanvas",
      sizeof(GstEditorCanvas),
      sizeof(GstEditorCanvasClass),
      (GtkClassInitFunc)gst_editor_canvas_class_init,
      (GtkObjectInitFunc)gst_editor_canvas_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    editor_canvas_type = gtk_type_unique (gnome_canvas_get_type (), &editor_canvas_info);
  }
  return editor_canvas_type;
}

static void 
gst_editor_canvas_class_init (GstEditorCanvasClass *klass) 
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;

  object_class = (GtkObjectClass*)klass;
  widget_class = (GtkWidgetClass *)klass;

  parent_class = gtk_type_class (gnome_canvas_get_type ());

  object_class->set_arg = gst_editor_canvas_set_arg;
  object_class->get_arg = gst_editor_canvas_get_arg;

  widget_class->realize = gst_editor_canvas_realize;
}

static void 
gst_editor_canvas_init (GstEditorCanvas *editorcanvas) 
{
}

GstEditorCanvas*
gst_editor_canvas_new (void)
{
  GstEditorCanvas *editorcanvas;

  editorcanvas = GST_EDITOR_CANVAS(gtk_type_new(GST_TYPE_EDITOR_CANVAS));

  return editorcanvas;
}

GstEditorCanvas*
gst_editor_canvas_new_with_bin (GstEditorBin *bin) 
{
  GstEditorCanvas *editorcanvas;

  g_return_val_if_fail(bin != NULL, NULL);

  editorcanvas = gst_editor_canvas_new ();
  editorcanvas->bin = bin;

  GST_EDITOR_ELEMENT(bin)->parent = bin;
  GST_EDITOR_ELEMENT(bin)->canvas = editorcanvas;

  return editorcanvas;
}

static void 
gst_editor_canvas_realize (GtkWidget *widget) 
{
  GstEditorCanvas *canvas = GST_EDITOR_CANVAS (widget);

  if (GTK_WIDGET_CLASS(parent_class)->realize) {
    GTK_WIDGET_CLASS(parent_class)->realize(GTK_WIDGET(canvas));
  }

  if (canvas->bin) {
    GstEditorElementClass *element_class;
    
    GST_EDITOR_ELEMENT(canvas->bin)->group = gnome_canvas_root(GNOME_CANVAS(canvas));

    element_class = GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(canvas->bin)->klass);

    if (element_class->realize) {
      element_class->realize(GST_EDITOR_ELEMENT(canvas->bin));
    }
  }
}

static void 
gst_editor_canvas_set_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorCanvas *canvas;

  canvas = GST_EDITOR_CANVAS(object);

  switch (id) {
    default:
      g_warning("gsteditorcanvas: unknown arg!");
      break;
  }
}

static void 
gst_editor_canvas_get_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorCanvas *canvas;

  canvas = GST_EDITOR_CANVAS(object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}


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


#include <gtk/gtk.h>
#include <gnome.h>
#include <gst/gst.h>

#include "gsteditor.h"

/* signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NAME,
};

static void gst_editor_class_init(GstEditorClass *klass);
static void gst_editor_init(GstEditor *editor);

static void gst_editor_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_get_arg(GtkObject *object,GtkArg *arg,guint id);

static GtkFrame *parent_class = NULL;

GtkType gst_editor_get_type(void) {
  static GtkType editor_type = 0;

  if (!editor_type) {
    static const GtkTypeInfo editor_info = {
      "GstEditor",
      sizeof(GstEditor),
      sizeof(GstEditorClass),
      (GtkClassInitFunc)gst_editor_class_init,
      (GtkObjectInitFunc)gst_editor_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    editor_type = gtk_type_unique(gtk_frame_get_type(),&editor_info);
  }
  return editor_type;
}

static void gst_editor_class_init(GstEditorClass *klass) {
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_frame_get_type());

  gtk_object_add_arg_type("GstEditor::name",GTK_TYPE_STRING,
                          GTK_ARG_READWRITE,ARG_NAME);

  object_class->set_arg = gst_editor_set_arg;
  object_class->get_arg = gst_editor_get_arg;
}

static void gst_editor_init(GstEditor *editor) {
  /* create the pipeline */
  editor->pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(editor->pipeline != NULL);

  /* create the editor canvas */
  editor->canvas = gst_editor_canvas_new(GST_BIN(editor->pipeline),NULL);

  /* create the scrolled window */
  editor->scrollwindow = gtk_scrolled_window_new(NULL,NULL);

  /* get the canvas widget */
  editor->canvaswidget = gst_editor_canvas_get_canvas(editor->canvas);

  /* add the canvas to the scrolled window */
  gtk_container_add(GTK_CONTAINER(editor->scrollwindow),
                    editor->canvaswidget);

  /* add the scrolled window to the canvas */
  gtk_container_add(GTK_CONTAINER(editor),editor->scrollwindow);
}

/**
 * gst_editor_new:
 * name: name of editor frame
 *
 * Creates a new GstEditor composite widget with the given name.
 *
 * Returns: Freshly created GstEditor widget.
 */
GstEditor *gst_editor_new(gchar *name) {
  GstEditor *editor;

  editor = gtk_type_new(gst_editor_get_type());
  gtk_object_set(GTK_OBJECT(editor),"name",name,NULL);

  return editor;
}

static void gst_editor_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditor *editor = GST_EDITOR(object);

  switch (id) {
    case ARG_NAME:
      gtk_object_set(GTK_OBJECT(editor),"label",GTK_VALUE_STRING(*arg),NULL);
      gst_element_set_name(GST_ELEMENT(editor->pipeline),
                           GTK_VALUE_STRING(*arg));
      break;
    default:
      g_warning("gsteditor: unknown arg!\n");
      break;
  }
}

static void gst_editor_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditor *editor = GST_EDITOR(object);

  switch (id) {
    case ARG_NAME:
      GTK_VALUE_STRING(*arg) =
        gst_element_get_name(GST_ELEMENT(editor->pipeline));
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

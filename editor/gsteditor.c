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
  NAME_CHANGED,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NAME,
};

static void 	gst_editor_class_init		(GstEditorClass *klass);
static void 	gst_editor_init			(GstEditor *editor);

static void 	gst_editor_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void 	gst_editor_get_arg		(GtkObject *object, GtkArg *arg, guint id);

static GtkFrame *parent_class = NULL;
static guint gst_editor_signals[LAST_SIGNAL] = { 0 };

GtkType 
gst_editor_get_type (void) 
{
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
    editor_type = gtk_type_unique (gtk_window_get_type (), &editor_info);
  }
  return editor_type;
}

static void 
gst_editor_class_init (GstEditorClass *klass) 
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_window_get_type());

  gtk_object_add_arg_type("GstEditor::name",GTK_TYPE_STRING,
                          GTK_ARG_READWRITE,ARG_NAME);

  gst_editor_signals[NAME_CHANGED] =
    gtk_signal_new("name_changed",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorClass,name_changed),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_EDITOR);

  gtk_object_class_add_signals(object_class,gst_editor_signals,LAST_SIGNAL);

  object_class->set_arg = gst_editor_set_arg;
  object_class->get_arg = gst_editor_get_arg;
}

static void 
gst_editor_init(GstEditor *editor) 
{
}

static void 
on_name_changed (GstEditorElement *element, gpointer data) 
{
  gtk_signal_emit(GTK_OBJECT(element),gst_editor_signals[NAME_CHANGED], NULL);
}
/**
 * gst_editor_new:
 * name: name of editor frame
 *
 * Creates a new GstEditor composite widget with the given name.
 *
 * Returns: Freshly created GstEditor widget.
 */
GstEditor*
gst_editor_new (GstElement *element) 
{
  GstEditor *editor;

  g_return_val_if_fail(element != NULL, NULL);

  editor = gtk_type_new(gst_editor_get_type());
  editor->element = element;

  /* create the editor canvas */
  if (element) {
    editor->canvas = gst_editor_canvas_new_with_bin (gst_editor_bin_new (GST_BIN (element), NULL));
  }
  else {
    editor->canvas = gst_editor_canvas_new ();
  }

  /* create the scrolled window */
  editor->scrollwindow = gtk_scrolled_window_new(NULL,NULL);

  /* get the canvas widget */
  editor->canvaswidget = GTK_WIDGET (editor->canvas);

  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(editor->scrollwindow),
                    editor->canvaswidget);

  /* add the scrolled window to the canvas */
  gtk_container_add(GTK_CONTAINER(editor),editor->scrollwindow);

  gtk_widget_set_usize(GTK_WIDGET(editor),400,400);

  gtk_widget_show_all(GTK_WIDGET(editor));

  return editor;
}

const gchar*
gst_editor_get_name (GstEditor *editor) 
{
  return gst_element_get_name (GST_ELEMENT (editor->element));
}

static void 
gst_editor_set_arg(GtkObject *object, GtkArg *arg, guint id) 
{
  GstEditor *editor = GST_EDITOR(object);

  switch (id) {
    case ARG_NAME:
      gtk_object_set(GTK_OBJECT(editor),"label",GTK_VALUE_STRING(*arg),NULL);
      gst_element_set_name(GST_ELEMENT(editor->element),
                           GTK_VALUE_STRING(*arg));
      break;
    default:
      g_warning("gsteditor: unknown arg!\n");
      break;
  }
}

static void 
gst_editor_get_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstEditor *editor = GST_EDITOR(object);

  switch (id) {
    case ARG_NAME:
      GTK_VALUE_STRING(*arg) =
        (gchar *)gst_element_get_name(GST_ELEMENT(editor->element));
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

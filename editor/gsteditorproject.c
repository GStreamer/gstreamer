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

#include <glade/glade.h>
#include "gsteditorproject.h"
#include "gsteditorpalette.h"
#include "gsteditorproperty.h"
#include "gsteditorimage.h"

/* class functions */
static void gst_editor_project_class_init(GstEditorProjectClass *klass);
static void gst_editor_project_init(GstEditorProject *project);
static void gst_editor_project_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_project_get_arg(GtkObject *object,GtkArg *arg,guint id);

enum {
  ARG_0,
};

enum {
  ELEMENT_ADDED,
  ELEMENT_REMOVED,
  ELEMENT_CHANGED,
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
static guint gst_editor_project_signals[LAST_SIGNAL] = { 0 };

GtkType gst_editor_project_get_type() {
  static GtkType project_type = 0;

  if (!project_type) {
    static const GtkTypeInfo project_info = {
      "GstEditorProject",
      sizeof(GstEditorProject),
      sizeof(GstEditorProjectClass),
      (GtkClassInitFunc)gst_editor_project_class_init,
      (GtkObjectInitFunc)gst_editor_project_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    project_type = gtk_type_unique(gtk_object_get_type(),&project_info);
  }
  return project_type;
}

static void gst_editor_project_class_init(GstEditorProjectClass *klass) {
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gst_editor_project_signals[ELEMENT_ADDED] =
    gtk_signal_new("element_added",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorProjectClass,element_added),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_ELEMENT);

  gst_editor_project_signals[ELEMENT_REMOVED] =
    gtk_signal_new("element_removed",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorProjectClass,element_removed),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_ELEMENT);

  gst_editor_project_signals[ELEMENT_CHANGED] =
    gtk_signal_new("element_changed",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorProjectClass,element_changed),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_ELEMENT);

  gtk_object_class_add_signals(object_class,gst_editor_project_signals,LAST_SIGNAL);

  object_class->set_arg = gst_editor_project_set_arg;
  object_class->get_arg = gst_editor_project_get_arg;
}

static void gst_editor_project_init(GstEditorProject *project) {
  project->toplevelelements = NULL;
}

GstEditorProject *gst_editor_project_new() {
  GstEditorProject *editorproject;

  editorproject = GST_EDITOR_PROJECT(gtk_type_new(GST_TYPE_EDITOR_PROJECT));

  return editorproject;
}

GstEditorProject *gst_editor_project_new_from_file(const guchar *fname) {
  GstEditorProject *editorproject;

  editorproject = gst_editor_project_new();

  return editorproject;
}

void gst_editor_project_add_toplevel_element(GstEditorProject *project, GstElement *element) {

  g_return_if_fail(project != NULL);
  g_return_if_fail(GST_IS_EDITOR_PROJECT(project));
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

  project->toplevelelements = g_list_append(project->toplevelelements, element);

  gst_element_set_name(element, "new_element");

  gtk_signal_emit(GTK_OBJECT(project),gst_editor_project_signals[ELEMENT_ADDED], element);
}

static void gst_editor_project_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorProject *project;

  /* get the major types of this object */
  project = GST_EDITOR_PROJECT(object);

  switch (id) {
    default:
      g_warning("gsteditorproject: unknown arg!");
      break;
  }
}

static void gst_editor_project_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorProject *project;

  /* get the major types of this object */
  project = GST_EDITOR_PROJECT(object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}



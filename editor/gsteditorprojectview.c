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
static void 	gst_editor_project_view_class_init	(GstEditorProjectViewClass *klass);
static void 	gst_editor_project_view_init		(GstEditorProjectView *project_view);

static void 	gst_editor_project_view_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void 	gst_editor_project_view_get_arg		(GtkObject *object, GtkArg *arg, guint id);

enum {
  ARG_0,
};

enum {
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
//static guint gst_editor_project_view_signals[LAST_SIGNAL] = { 0 };

GtkType 
gst_editor_project_view_get_type(void) 
{
  static GtkType project_view_type = 0;

  if (!project_view_type) {
    static const GtkTypeInfo project_view_info = {
      "GstEditorProjectView",
      sizeof(GstEditorProjectView),
      sizeof(GstEditorProjectViewClass),
      (GtkClassInitFunc)gst_editor_project_view_class_init,
      (GtkObjectInitFunc)gst_editor_project_view_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    project_view_type = gtk_type_unique(gtk_object_get_type(),&project_view_info);
  }
  return project_view_type;
}

static void 
gst_editor_project_view_class_init (GstEditorProjectViewClass *klass) 
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  object_class->set_arg = gst_editor_project_view_set_arg;
  object_class->get_arg = gst_editor_project_view_get_arg;
}

static void 
gst_editor_project_view_init (GstEditorProjectView *project) 
{
}

typedef struct {
  GstEditorProjectView *view;
  GModule *symbols;
} connect_struct;
  
/* we need more control here so... */
static void 
gst_editor_project_connect_func (const gchar *handler_name,
		             GtkObject *object,
			     const gchar *signal_name,
			     const gchar *signal_data,
			     GtkObject *connect_object,
			     gboolean after,
			     gpointer user_data) 
{
  GtkSignalFunc func;
  connect_struct *data = (connect_struct *)user_data;

  if (!g_module_symbol(data->symbols, handler_name, (gpointer *)&func))
    g_warning("GstEditorProject: could not find signal handler '%s'.", handler_name);
  else {
    if (after)
      gtk_signal_connect_after(object, signal_name, func, (gpointer) data->view);
    else
      gtk_signal_connect(object, signal_name, func, (gpointer) data->view);
  }
}

static void 
gst_editor_project_element_selected (GstEditorProjectView *view, 
		                     GstElementFactory *factory, GstEditorPalette *palette) 
{
  GstElement *element;

  element = gst_elementfactory_create (factory, "new_element");

  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

  gst_editor_project_add_toplevel_element(view->project, element);
}

static void 
on_name_change (GstEditorProjectView *view, 
		GstEditorElement *element, GstEditor *editor) 
{
  gint row;
  gchar *text;
  guint8 spacing;
  GdkPixmap *pixmap;
  GdkBitmap *mask;

  row = gtk_clist_find_row_from_data(GTK_CLIST(view->list), editor);

  gtk_clist_get_pixtext(GTK_CLIST(view->list), row, 0, &text, &spacing, &pixmap, &mask);
  gtk_clist_set_pixtext(GTK_CLIST(view->list), row, 0, gst_editor_get_name(editor), 
		  spacing, pixmap, mask);
}

static void 
view_on_element_added (GstEditorProjectView *view, GstElement *element) 
{
  gchar *name;
  gint row;
  GstEditorImage *image;
  GstEditor *editor;

  image = gst_editor_image_get_for_type(GTK_OBJECT_TYPE(element));
  name = (gchar *)gst_element_get_name(element);
  row = gtk_clist_append(GTK_CLIST(view->list), &name);
  editor =  gst_editor_new(element);

  gtk_signal_connect_object(GTK_OBJECT(editor), "name_changed", on_name_change, GTK_OBJECT(view));
  gtk_clist_set_row_data(GTK_CLIST(view->list), row, editor);
  gtk_clist_set_pixtext(GTK_CLIST(view->list), row, 0, name, 3, image->pixmap, image->bitmap);
}

typedef struct {
  GtkWidget *selection;
  GstEditorProjectView *view;
} file_select;

static void 
on_save_as_file_selected (GtkWidget *button,
		          file_select *data) 
{
  GtkWidget *selector = data->selection;
  GstEditorProjectView *view = data->view;

  gchar *file_name = gtk_file_selection_get_filename (GTK_FILE_SELECTION(selector));
  gst_editor_project_save_as (view->project, file_name);

  g_free (data);
}

void 
on_save_as1_activate (GtkWidget *widget, 
		      GstEditorProjectView *view) 
{
  GtkWidget *file_selector;
  file_select *file_data = g_new0 (file_select, 1);

  file_selector = gtk_file_selection_new("Please select a file for saving.");

  file_data->selection = file_selector;
  file_data->view = view;

  gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
		     		  "clicked", GTK_SIGNAL_FUNC (on_save_as_file_selected), 
				  file_data);
     			   
  /* Ensure that the dialog box is destroyed when the user clicks a button. */
  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
     					  "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
     					  (gpointer) file_selector);
  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->cancel_button),
   					  "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
     					  (gpointer) file_selector);
	   
  /* Display that dialog */
  gtk_widget_show (file_selector);
}

static void 
on_load_file_selected (GtkWidget *button,
		          file_select *data) 
{
  GtkWidget *selector = data->selection;
  GstEditorProjectView *view = data->view;

  gchar *file_name = gtk_file_selection_get_filename (GTK_FILE_SELECTION(selector));
  gst_editor_project_load (view->project, file_name);

  g_free (data);
}

void 
on_open1_activate (GtkWidget *widget, 
	           GstEditorProjectView *view) 
{
  GtkWidget *file_selector;
  file_select *file_data = g_new0 (file_select, 1);

  file_selector = gtk_file_selection_new("Please select a file to load.");

  file_data->selection = file_selector;
  file_data->view = view;

  gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
		     		  "clicked", GTK_SIGNAL_FUNC (on_load_file_selected), 
				  file_data);
     			   
  /* Ensure that the dialog box is destroyed when the user clicks a button. */
  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
     					  "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
     					  (gpointer) file_selector);
  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->cancel_button),
   					  "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
     					  (gpointer) file_selector);
	   
  /* Display that dialog */
  gtk_widget_show (file_selector);
}

GstEditorProjectView*
gst_editor_project_view_new (GstEditorProject *project) 
{
  GstEditorProjectView *view;
  GtkWidget *main_window;
  connect_struct data;
  GModule *symbols;
  GstEditorPalette *palette;
  GList *elements;

  view = GST_EDITOR_PROJECT_VIEW(gtk_type_new(GST_TYPE_EDITOR_PROJECT_VIEW));

  view->project = project;

  symbols = g_module_open(NULL, 0);

  data.view = view;
  data.symbols = symbols;

  view->xml = glade_xml_new("editor.glade", "main_project_window");
  glade_xml_signal_autoconnect_full (view->xml, gst_editor_project_connect_func, &data);

  main_window = glade_xml_get_widget(view->xml, "main_project_window");
  gtk_widget_show(main_window);

  palette = gst_editor_palette_new();
  gtk_signal_connect_object(GTK_OBJECT(palette), "element_selected", gst_editor_project_element_selected, GTK_OBJECT(view));

  view->list = glade_xml_get_widget(view->xml, "clist1");
  gtk_clist_set_row_height(GTK_CLIST(view->list), 21);

  gst_editor_property_get();

  elements = project->toplevelelements;
  
  while (elements) {
    GstElement *element = (GstElement *)elements->data;

    view_on_element_added (view, element);

    elements = g_list_next (elements);
  }

  gtk_signal_connect_object(GTK_OBJECT(project), "element_added", view_on_element_added, GTK_OBJECT(view));

  return view;
}

static void 
gst_editor_project_view_set_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorProjectView *project_view;

  /* get the major types of this object */
  project_view = GST_EDITOR_PROJECT_VIEW(object);

  switch (id) {
    default:
      g_warning("gsteditorproject_view: unknown arg!");
      break;
  }
}

static void 
gst_editor_project_view_get_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorProjectView *project_view;

  /* get the major types of this object */
  project_view = GST_EDITOR_PROJECT_VIEW(object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}





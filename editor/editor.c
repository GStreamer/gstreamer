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
#include <libgnomeui/gnome-canvas.h>

#include <gst/gst.h>

#include "gsteditor.h"

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) {
  GtkWidget *appwindow;
  GstEditor *editor;

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
  gst_plugin_load_all();
  gnome_init("GST Graph Editor",VERSION,argc,argv);

  appwindow = gnome_app_new("gst-editor","GST Graph Editor");
  editor = gst_editor_new("pipeline");
  gtk_widget_set_usize(GTK_WIDGET(editor),250,250);
  gnome_app_set_contents(GNOME_APP(appwindow),GTK_WIDGET(editor));
  gtk_widget_show_all(appwindow);

  gtk_main();

  return(0);
}

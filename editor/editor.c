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


#include <glade/glade.h>
#include <gst/gst.h>

#include "gsteditor.h"
#include "gsteditorproject.h"
#include "config.h"

extern gboolean _gst_plugin_spew;

int 
main (int argc, char *argv[]) 
{
  GstEditorProject *project;

  bindtextdomain (PACKAGE, PACKAGE_LOCALE_DIR);
  textdomain (PACKAGE);

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
  gnome_init("GST Graph Editor",VERSION,argc,argv);
  glade_init();
  glade_gnome_init();


  if (argc > 1) {
    project = gst_editor_project_new_from_file(argv[1]);
  }
  else
    project = gst_editor_project_new();

  g_assert (project != NULL);

  gst_editor_project_view_new(project);

  gtk_main();

  return(0);
}

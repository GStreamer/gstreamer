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

#include <gst/gstextratypes.h>

GtkType 
gst_extra_get_filename_type (void) 
{
  static GtkType filename_type = 0;

  if (!filename_type) {
    static const GtkTypeInfo filename_info = {
      "GstFilename",
      0, //sizeof(GstElement),
      0, //sizeof(GstElementClass),
      (GtkClassInitFunc)NULL,
      (GtkObjectInitFunc)NULL,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    filename_type = gtk_type_unique (GTK_TYPE_STRING, &filename_info);
  }
  return filename_type;
}



/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstextratypes.c: Extra GTypes: filename type, etc.
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

#include "gst_private.h"

#include <gst/gstobject.h>
#include <gst/gstextratypes.h>

GType 
gst_extra_get_filename_type (void) 
{
  static GType filename_type = 0;

  if (!filename_type) {
    static const GTypeInfo filename_info = {
      0, /* sizeof(GstElementClass), */
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0, /* sizeof(GstElement), */
      0,
      NULL,
      NULL
    };
    filename_type = g_type_register_static (G_TYPE_STRING, "GstFilename", &filename_info, 0);
  }
  return filename_type;
}



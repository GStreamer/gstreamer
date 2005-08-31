/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsturi.c: register URI handlers
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
/**
 * SECTION:gsturitype
 * @short_description: Describes URI types
 *
 */

#include "gst_private.h"

#include "gsturi.h"
#include "gstinfo.h"

GType
gst_uri_get_uri_type (void)
{
  static GType uri_type = 0;

  if (!uri_type) {
    static const GTypeInfo uri_info = {
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0,
      0,
      NULL,
      NULL
    };
    uri_type = g_type_register_static (G_TYPE_STRING, "GstUri", &uri_info, 0);
  }
  return uri_type;
}

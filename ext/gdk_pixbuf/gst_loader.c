/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gst_loader.c: Load GStreamer videos as gdkpixbufs
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgdkanimation.h"
#include <stdio.h>

static GdkPixbufAnimation *
gst_loader_load_animation (FILE *f, GError **error)
{
  return gst_gdk_animation_new_from_file (f, error);
}
void
fill_vtable (GdkPixbufModule *module)
{
  if (gst_init_check (0, NULL)) {
    module->load_animation = gst_loader_load_animation;
  }
}
void
fill_info (GdkPixbufFormat *info)
{
  static GdkPixbufModulePattern signature[] = {
    { "RIFF    AVI ", "    xxxx    ", 100 },
    { "xx\001\272", "zz  ", 100 },
    { NULL, NULL, 0 }
  };
  
  static gchar *mime_types[] = {
    "video/avi",
    "video/mpeg",
    NULL
  };

  static gchar *extensions[] = {
    "avi",
    "mpeg", "mpe", "mpg",
    NULL
  };
  
  info->name        = "GStreamer";
  info->signature   = signature;
  info->description = "GStreamer supported video";
  info->mime_types  = mime_types;
  info->extensions  = extensions;
  info->flags       = 0;
}

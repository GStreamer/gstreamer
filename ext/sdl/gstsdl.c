/* GStreamer
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sdlvideosink.h"
#include "sdlaudiosink.h"


GST_DEBUG_CATEGORY (sdl_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{

  if (!gst_element_register (plugin, "sdlvideosink", GST_RANK_NONE,
          GST_TYPE_SDLVIDEOSINK) ||
      !gst_element_register (plugin, "sdlaudiosink", GST_RANK_NONE,
          GST_TYPE_SDLAUDIOSINK)) {
    return FALSE;
  }

  GST_DEBUG_CATEGORY_INIT (sdl_debug, "sdl", 0, "SDL elements");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    sdl,
    "SDL (Simple DirectMedia Layer) support for GStreamer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

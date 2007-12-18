/* GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
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


#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

#include <string.h>

#include <glimagesink.h>

GType gst_gl_upload_get_type (void);


static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_debug_glimage_sink, "glimagesink", 0,
      "glimagesink element");

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_MARGINAL, GST_TYPE_GLIMAGE_SINK)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "glupload",
          GST_RANK_MARGINAL, gst_gl_upload_get_type ())) {
    return FALSE;
  }
#if 0
  if (!gst_element_register (plugin, "gldownload",
          GST_RANK_MARGINAL, GST_TYPE_GL_DOWNLOAD)) {
    return FALSE;
  }
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "glimagesink",
    "OpenGL video output plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

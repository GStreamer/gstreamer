/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2015> British Broadcasting Corporation <dash@rd.bbc.co.uk>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstttmlparse.h"
#include "gstttmlrender.h"

GST_DEBUG_CATEGORY (ttmlparse_debug);
GST_DEBUG_CATEGORY (ttmlrender_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  guint rank = GST_RANK_NONE;

  /* We don't want this autoplugged by default yet for now */
  if (g_getenv ("GST_TTML_AUTOPLUG")) {
    GST_INFO_OBJECT (plugin, "Registering ttml elements with primary rank.");
    rank = GST_RANK_PRIMARY;
  }

  gst_plugin_add_dependency_simple (plugin, "GST_TTML_AUTOPLUG", NULL, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  if (!gst_element_register (plugin, "ttmlparse", rank, GST_TYPE_TTML_PARSE))
    return FALSE;
  if (!gst_element_register (plugin, "ttmlrender", rank, GST_TYPE_TTML_RENDER))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (ttmlparse_debug, "ttmlparse", 0, "TTML parser");
  GST_DEBUG_CATEGORY_INIT (ttmlrender_debug, "ttmlrender", 0, "TTML renderer");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ttmlsubs,
    "TTML subtitle handling",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

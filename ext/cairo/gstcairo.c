/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003,2004> David Schleef <ds@schleef.org>
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

#include <gsttimeoverlay.h>
#include <gsttextoverlay.h>
#include <gstcairorender.h>

#ifdef HAVE_CAIRO_GOBJECT
#include <gstcairooverlay.h>
#endif

#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY (cairo_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "cairotextoverlay", GST_RANK_NONE,
      GST_TYPE_CAIRO_TEXT_OVERLAY);
  gst_element_register (plugin, "cairotimeoverlay", GST_RANK_NONE,
      GST_TYPE_CAIRO_TIME_OVERLAY);
#ifdef HAVE_CAIRO_GOBJECT
  gst_element_register (plugin, "cairooverlay", GST_RANK_NONE,
      GST_TYPE_CAIRO_OVERLAY);
#endif
  gst_element_register (plugin, "cairorender", GST_RANK_SECONDARY,
      GST_TYPE_CAIRO_RENDER);

  GST_DEBUG_CATEGORY_INIT (cairo_debug, "cairo", 0, "Cairo elements");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "cairo",
    "Cairo-based elements", plugin_init, VERSION,
    GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

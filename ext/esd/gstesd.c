/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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
#include "esdsink.h"
#include "esdmon.h"

GST_DEBUG_CATEGORY (esd_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  if (!gst_library_load ("gstaudio"))
    return FALSE;

  ret = gst_esdsink_factory_init (plugin);
  if (ret == FALSE)
    return FALSE;

  ret = gst_esdmon_factory_init (plugin);
  if (ret == FALSE)
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (esd_debug, "esd", 0, "ESounD elements");
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "esdsink",
    "ESD Element Plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

/* GStreamer
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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstjpegdec.h"
#include "gstjpegenc.h"
#include "gstsmokeenc.h"
#include "gstsmokedec.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "jpegenc", GST_RANK_NONE,
          GST_TYPE_JPEGENC))
    return FALSE;

  if (!gst_element_register (plugin, "jpegdec", GST_RANK_PRIMARY,
          GST_TYPE_JPEGDEC))
    return FALSE;

  if (!gst_element_register (plugin, "smokeenc", GST_RANK_PRIMARY,
          GST_TYPE_SMOKEENC))
    return FALSE;

  if (!gst_element_register (plugin, "smokedec", GST_RANK_PRIMARY,
          GST_TYPE_SMOKEDEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "jpeg",
    "JPeg plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

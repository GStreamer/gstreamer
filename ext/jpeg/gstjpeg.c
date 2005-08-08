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

#include <string.h>

#include <gst/gst.h>

#include "gstjpegdec.h"

#if 0
#include "gstjpegenc.h"
#include "gstsmokeenc.h"
#include "gstsmokedec.h"

static GstStaticCaps smoke_caps = GST_STATIC_CAPS ("video/x-smoke");

#define SMOKE_CAPS (gst_static_caps_get(&smoke_caps))
static void
smoke_type_find (GstTypeFind * tf, gpointer private)
{
  guint8 *data = gst_type_find_peek (tf, 0, 6);

  if (data) {
    if (data[0] != 0x80)
      return;
    if (memcmp (&data[1], "smoke", 5) != 0)
      return;
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, SMOKE_CAPS);
  }
}
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
#if 0
  if (!gst_element_register (plugin, "jpegenc", GST_RANK_NONE,
          GST_TYPE_JPEGENC))
    return FALSE;
#endif

  if (!gst_element_register (plugin, "jpegdec", GST_RANK_PRIMARY,
          GST_TYPE_JPEG_DEC))
    return FALSE;

#if 0
  if (!gst_element_register (plugin, "smokeenc", GST_RANK_PRIMARY,
          GST_TYPE_SMOKEENC))
    return FALSE;

  if (!gst_element_register (plugin, "smokedec", GST_RANK_PRIMARY,
          GST_TYPE_SMOKEDEC))
    return FALSE;

  if (!gst_type_find_register (plugin, "video/x-smoke", GST_RANK_PRIMARY,
          smoke_type_find, NULL, SMOKE_CAPS, NULL))
    return FALSE;
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "jpeg",
    "JPeg plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

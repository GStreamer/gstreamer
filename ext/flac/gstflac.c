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
#include "config.h"
#endif

#include "gstflacenc.h"
#include "gstflacdec.h"
#include "gstflactag.h"

#include "flac_compat.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "flacenc", GST_RANK_NONE,
          GST_TYPE_FLAC_ENC))
    return FALSE;
  if (!gst_element_register (plugin, "flacdec", GST_RANK_PRIMARY,
          GST_TYPE_FLAC_DEC))
    return FALSE;
#if 0
  if (!gst_element_register (plugin, "flactag", GST_RANK_PRIMARY,
          gst_flac_tag_get_type ()))
    return FALSE;
#endif
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "flac",
    "The FLAC Lossless compressor Codec",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

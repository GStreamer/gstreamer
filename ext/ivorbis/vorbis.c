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

#include <gst/gst.h>
#include <tremor/ivorbiscodec.h>
#include "vorbisdec.h"

GST_DEBUG_CATEGORY (vorbisdec_debug);

extern GType ivorbisfile_get_type (void);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "tremor", GST_RANK_SECONDARY,
          ivorbisfile_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "ivorbisdec", GST_RANK_SECONDARY,
          gst_ivorbis_dec_get_type ()))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (vorbisdec_debug, "ivorbisdec", 0,
      "vorbis decoding element (integer decoder)");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "tremor",
    "OGG Vorbis Tremor plugins element",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

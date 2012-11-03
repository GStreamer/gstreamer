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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvorbisdec.h"

GST_DEBUG_CATEGORY (ivorbisdec_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{

  /* if tremor is around, there is probably good reason for it, so preferred */
  if (!gst_element_register (plugin, "ivorbisdec", GST_RANK_SECONDARY,
          gst_vorbis_dec_get_type ()))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (ivorbisdec_debug, "ivorbisdec", 0,
      "vorbis decoding element (integer decoder)");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ivorbisdec,
    "Vorbis Tremor decoder",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

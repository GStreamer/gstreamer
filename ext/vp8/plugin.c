/* VP8
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
 * Copyright (C) 2010 Entropy Wave Inc
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

GType gst_vp8_dec_get_type (void);
GType gst_vp8_enc_get_type (void);

GST_DEBUG_CATEGORY_EXTERN (gst_vp8dec_debug);
GST_DEBUG_CATEGORY_EXTERN (gst_vp8enc_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_vp8dec_debug, "vp8dec", 0, "VP8 Decoder");
  GST_DEBUG_CATEGORY_INIT (gst_vp8enc_debug, "vp8enc", 0, "VP8 Encoder");

  gst_element_register (plugin, "vp8dec", GST_RANK_PRIMARY,
      gst_vp8_dec_get_type ());
  gst_element_register (plugin, "vp8enc", GST_RANK_PRIMARY,
      gst_vp8_enc_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vp8",
    "VP8 plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

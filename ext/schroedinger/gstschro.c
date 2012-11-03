/* GStreamer
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
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

#include <gst/gst.h>
#include <schroedinger/schro.h>

GType gst_schro_enc_get_type (void);
GType gst_schro_dec_get_type (void);

GST_DEBUG_CATEGORY (schro_debug);
#define GST_CAT_DEFAULT schro_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  schro_init ();

  GST_DEBUG_CATEGORY_INIT (schro_debug, "schro", 0, "Schroedinger");
  gst_element_register (plugin, "schrodec", GST_RANK_PRIMARY,
      gst_schro_dec_get_type ());
  gst_element_register (plugin, "schroenc", GST_RANK_PRIMARY,
      gst_schro_enc_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    schro,
    "Schroedinger plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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
#  include "config.h"
#endif

#include <gst/gst.h>

extern GType gst_theora_dec_get_type (void);
extern GType gst_theora_enc_get_type (void);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gsttags"))
    return FALSE;

  if (!gst_element_register (plugin, "theoradec", GST_RANK_SECONDARY,
          gst_theora_dec_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "theoraenc", GST_RANK_SECONDARY,
          gst_theora_enc_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gsttheora",
    "Theora plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

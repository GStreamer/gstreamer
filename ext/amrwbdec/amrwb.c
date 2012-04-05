/* GStreamer Adaptive Multi-Rate Wide-Band (AMR-WB) Decoder plugin
 * Copyright (C) 2006 Edgard Lima <edgard.lima@indt.org.br>
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

#include "amrwbdec.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "amrwbdec",
      GST_RANK_PRIMARY, GST_TYPE_AMRWBDEC);
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    amrwbdec,
    "Adaptive Multi-Rate Wide-Band Decoder",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);

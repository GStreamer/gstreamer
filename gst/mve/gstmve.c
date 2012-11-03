/* GStreamer plugin for Interplay MVE movie files
 *
 * Copyright (C) 2006 Jens Granseuer <jensgr@gmx.net>
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
 *
 * For more information about the Interplay MVE format, visit:
 *   http://www.pcisys.net/~melanson/codecs/interplay-mve.txt
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "gstmvedemux.h"
#include "gstmvemux.h"

static gboolean
mve_plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "mvedemux", GST_RANK_PRIMARY,
      GST_TYPE_MVE_DEMUX)
      && gst_element_register (plugin, "mvemux", GST_RANK_PRIMARY,
      GST_TYPE_MVE_MUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mve,
    "Interplay MVE movie format manipulation",
    mve_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

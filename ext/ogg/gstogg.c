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

#include <gst/gst.h>

extern gboolean gst_ogg_demux_plugin_init (GstPlugin * plugin);
extern gboolean gst_ogg_mux_plugin_init (GstPlugin * plugin);

GST_DEBUG_CATEGORY (vorbisdec_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_ogg_demux_plugin_init (plugin);
  gst_ogg_mux_plugin_init (plugin);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "ogg",
    "ogg stream manipulation (info about ogg: http://xiph.org)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)

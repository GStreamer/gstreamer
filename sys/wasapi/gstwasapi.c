/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

#include "gstwasapisrc.h"
#include "gstwasapisink.h"

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  ret = gst_element_register (plugin, "wasapisrc",
      GST_RANK_NONE, GST_TYPE_WASAPI_SRC);
  if (!ret)
    return ret;

  return gst_element_register (plugin, "wasapisink",
      GST_RANK_NONE, GST_TYPE_WASAPI_SINK);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "wasapi",
    "Windows audio session API plugin",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")

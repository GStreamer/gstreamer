/* GStreamer
 * Copyright (C) 2021-2022 Jan Schmidt <jan@centricular.com>
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
#  include <config.h>
#endif

#include "dash/gstdashdemux.h"
#include "hls/gsthlsdemux.h"
#include "mss/gstmssdemux.h"
#include "../soup/gstsouploader.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

#ifndef STATIC_SOUP
  if (!gst_soup_load_library ()) {
    GST_WARNING ("Failed to load libsoup library");
    return TRUE;
  }
#endif

  ret |= GST_ELEMENT_REGISTER (hlsdemux2, plugin);
  ret |= GST_ELEMENT_REGISTER (dashdemux2, plugin);
  ret |= GST_ELEMENT_REGISTER (mssdemux2, plugin);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    adaptivedemux2,
    "Adaptive Streaming 2 plugin", plugin_init, VERSION,
    GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

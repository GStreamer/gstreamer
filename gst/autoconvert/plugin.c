/* GStreamer
 * Copyright 2010 ST-Ericsson SA 
 *  @author: Benjamin Gaignard <benjamin.gaignard@stericsson.com>
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

#include "gstautoconvert.h"
#include "gstautovideoconvert.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  ret = gst_element_register (plugin, "autoconvert",
      GST_RANK_NONE, GST_TYPE_AUTO_CONVERT);

  ret &= gst_element_register (plugin, "autovideoconvert",
      GST_RANK_NONE, GST_TYPE_AUTO_VIDEO_CONVERT);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    autoconvert,
    "Selects convertor element based on caps",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

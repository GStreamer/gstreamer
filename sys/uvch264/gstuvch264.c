/* GStreamer
 *
 * uvch264: a plugin for handling UVC compliant H264 encoding cameras
 *
 * Copyright (C) 2012 Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstuvch264_mjpgdemux.h"
#include "gstuvch264_src.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "uvch264mjpgdemux", GST_RANK_NONE,
          GST_TYPE_UVC_H264_MJPG_DEMUX))
    return FALSE;

  if (!gst_element_register (plugin, "uvch264src", GST_RANK_NONE,
          GST_TYPE_UVC_H264_SRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    uvch264,
    "UVC compliant H264 encoding cameras plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

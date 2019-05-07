/* GStreamer RIST plugin
 * Copyright (C) 2019 Net Insight AB
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#include "gstrist.h"
#include "gstroundrobin.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "ristsrc", GST_RANK_PRIMARY,
          GST_TYPE_RIST_SRC))
    return FALSE;
  if (!gst_element_register (plugin, "ristsink", GST_RANK_PRIMARY,
          GST_TYPE_RIST_SINK))
    return FALSE;
  if (!gst_element_register (plugin, "ristrtxsend", GST_RANK_NONE,
          GST_TYPE_RIST_RTX_SEND))
    return FALSE;
  if (!gst_element_register (plugin, "ristrtxreceive", GST_RANK_NONE,
          GST_TYPE_RIST_RTX_RECEIVE))
    return FALSE;
  if (!gst_element_register (plugin, "roundrobin", GST_RANK_NONE,
          GST_TYPE_ROUND_ROBIN))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rist,
    "Source and Sink for RIST TR-06-1 streaming specification",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

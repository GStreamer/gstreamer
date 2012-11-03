/* GStreamer
 * Copyright (C) <2007> Leandro Melo de Sales <leandroal@gmail.com>
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

#include "gstdccpclientsrc.h"
#include "gstdccpserversink.h"
#include "gstdccpclientsink.h"
#include "gstdccpserversrc.h"

GST_DEBUG_CATEGORY (dccp_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "dccpclientsrc", GST_RANK_NONE,
          GST_TYPE_DCCP_CLIENT_SRC))
    return FALSE;

  if (!gst_element_register (plugin, "dccpserversink", GST_RANK_NONE,
          GST_TYPE_DCCP_SERVER_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "dccpclientsink", GST_RANK_NONE,
          GST_TYPE_DCCP_CLIENT_SINK))
    return FALSE;

  if (!gst_element_register (plugin, "dccpserversrc", GST_RANK_NONE,
          GST_TYPE_DCCP_SERVER_SRC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (dccp_debug, "dccp", 0, "DCCP calls");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dccp,
    "transfer data over the network via DCCP.",
    plugin_init, VERSION, GST_LICENSE, "DCCP",
    "http://garage.maemo.org/projects/ephone")

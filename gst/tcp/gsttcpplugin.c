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

#include "gsttcpsrc.h"
#include "gsttcpsink.h"
#include "gsttcpclientsrc.h"
#include "gsttcpclientsink.h"
#include "gsttcpserversrc.h"
#include "gsttcpserversink.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstdataprotocol"))
    return FALSE;

  if (!gst_element_register (plugin, "tcpsink", GST_RANK_NONE,
          GST_TYPE_TCPSINK))
    return FALSE;

  if (!gst_element_register (plugin, "tcpsrc", GST_RANK_NONE, GST_TYPE_TCPSRC))
    return FALSE;

  if (!gst_element_register (plugin, "tcpclientsink", GST_RANK_NONE,
          GST_TYPE_TCPCLIENTSINK))
    return FALSE;
  if (!gst_element_register (plugin, "tcpclientsrc", GST_RANK_NONE,
          GST_TYPE_TCPCLIENTSRC))
    return FALSE;
  if (!gst_element_register (plugin, "tcpserversink", GST_RANK_NONE,
          GST_TYPE_TCPSERVERSINK))
    return FALSE;
  if (!gst_element_register (plugin, "tcpserversrc", GST_RANK_NONE,
          GST_TYPE_TCPSERVERSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "tcp",
    "transfer data over the network via TCP",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)

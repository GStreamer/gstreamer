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

#include <gst/netbuffer/gstnetbuffer.h>

#include "gstudpsrc.h"
#include "gstmultiudpsink.h"
#include "gstudpsink.h"
#include "gstdynudpsink.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
#ifdef G_OS_WIN32
  if (!gst_udp_net_utils_win32_wsa_startup (GST_OBJECT (plugin)))
    return FALSE;
#endif

  /* register type of the netbuffer so that we can use it from multiple threads
   * right away. Note that the plugin loading is always serialized */
  gst_netbuffer_get_type ();

  if (!gst_element_register (plugin, "udpsink", GST_RANK_NONE,
          GST_TYPE_UDPSINK))
    return FALSE;

  if (!gst_element_register (plugin, "multiudpsink", GST_RANK_NONE,
          GST_TYPE_MULTIUDPSINK))
    return FALSE;

  if (!gst_element_register (plugin, "dynudpsink", GST_RANK_NONE,
          GST_TYPE_DYNUDPSINK))
    return FALSE;

  if (!gst_element_register (plugin, "udpsrc", GST_RANK_NONE, GST_TYPE_UDPSRC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "udp",
    "transfer data via UDP",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

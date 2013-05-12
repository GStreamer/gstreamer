/* GStreamer bluez plugin
 *
 * Copyright (C) 2013 Collabora Ltd. <tim.muller@collabora.co.uk>
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
#include <config.h>
#endif

#include "gsta2dpsink.h"
#include "gstavdtpsink.h"
#include "gstavdtpsrc.h"
#include <string.h>

GST_DEBUG_CATEGORY (avdtp_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (avdtp_debug, "avdtp", 0, "avdtp utils");

  gst_element_register (plugin, "a2dpsink", GST_RANK_NONE, GST_TYPE_A2DP_SINK);

  gst_element_register (plugin, "avdtpsink",
      GST_RANK_NONE, GST_TYPE_AVDTP_SINK);

  gst_element_register (plugin, "avdtpsrc", GST_RANK_NONE, GST_TYPE_AVDTP_SRC);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    bluez,
    "Bluez-based bluetooth support",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

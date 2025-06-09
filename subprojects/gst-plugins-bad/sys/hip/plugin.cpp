/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include "gsthipdevice.h"
#include "gsthipmemorycopy.h"
#include "gsthipconvertscale.h"
#include "gsthipcompositor.h"
#include "gsthiprtc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  auto device = gst_hip_device_new (GST_HIP_VENDOR_UNKNOWN, 0);
  if (!device)
    return TRUE;

  gst_element_register (plugin,
      "hipupload", GST_RANK_NONE, GST_TYPE_HIP_UPLOAD);
  gst_element_register (plugin,
      "hipdownload", GST_RANK_NONE, GST_TYPE_HIP_DOWNLOAD);

  gboolean texture_support = FALSE;
  g_object_get (device, "texture2d-support", &texture_support, nullptr);
  if (!texture_support) {
    gst_plugin_add_status_info (plugin,
        "Texture2D not supported by HIP device");
  }

  auto have_rtc = gst_hip_rtc_load_library (GST_HIP_VENDOR_UNKNOWN);
  if (!have_rtc) {
    gst_plugin_add_status_info (plugin,
        "Couldn't find runtime kernel compiler library");
  }

  if (texture_support && have_rtc) {
    gst_element_register (plugin,
        "hipconvertscale", GST_RANK_NONE, GST_TYPE_HIP_CONVERT_SCALE);
    gst_element_register (plugin,
        "hipconvert", GST_RANK_NONE, GST_TYPE_HIP_CONVERT);
    gst_element_register (plugin,
        "hipscale", GST_RANK_NONE, GST_TYPE_HIP_SCALE);
    gst_element_register (plugin,
        "hipcompositor", GST_RANK_NONE, GST_TYPE_HIP_COMPOSITOR);
  }

  gst_clear_object (&device);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    hip,
    "HIP plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include <winapifamily.h>

#include <gst/gst.h>
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#include "gstmfvideosrc.h"
#include "gstmfdevice.h"
#endif
#include "gstmfutils.h"
#include "gstmfh264enc.h"
#include "gstmfh265enc.h"

GST_DEBUG_CATEGORY (gst_mf_debug);
GST_DEBUG_CATEGORY (gst_mf_utils_debug);
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
GST_DEBUG_CATEGORY (gst_mf_source_object_debug);
#endif
GST_DEBUG_CATEGORY (gst_mf_transform_debug);

#define GST_CAT_DEFAULT gst_mf_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  HRESULT hr;

  GST_DEBUG_CATEGORY_INIT (gst_mf_debug, "mf", 0, "media foundation");
  GST_DEBUG_CATEGORY_INIT (gst_mf_utils_debug,
      "mfutils", 0, "media foundation utility functions");
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  GST_DEBUG_CATEGORY_INIT (gst_mf_source_object_debug,
      "mfsourceobject", 0, "mfsourceobject");
#endif
  GST_DEBUG_CATEGORY_INIT (gst_mf_transform_debug,
      "mftransform", 0, "mftransform");

  hr = MFStartup (MF_VERSION, MFSTARTUP_NOSOCKET);
  if (!gst_mf_result (hr)) {
    GST_WARNING ("MFStartup failure, hr: 0x%x", hr);
    return TRUE;
  }
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  gst_element_register (plugin,
      "mfvideosrc", GST_RANK_SECONDARY, GST_TYPE_MF_VIDEO_SRC);
  gst_device_provider_register (plugin, "mfdeviceprovider",
      GST_RANK_SECONDARY, GST_TYPE_MF_DEVICE_PROVIDER);
#endif

  gst_mf_h264_enc_plugin_init (plugin, GST_RANK_SECONDARY);
  gst_mf_h265_enc_plugin_init (plugin, GST_RANK_SECONDARY);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mediafoundation,
    "Microsoft Media Foundation plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

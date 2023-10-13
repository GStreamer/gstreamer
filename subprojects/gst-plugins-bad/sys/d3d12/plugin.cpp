/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

/**
 * plugin-d3d12:
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstd3d12device.h"
#include "gstd3d12download.h"
#include "gstd3d12h264dec.h"
#include "gstd3d12h265dec.h"
#include "gstd3d12vp9dec.h"
#include "gstd3d12av1dec.h"

#include <wrl.h>
#include <d3d11_4.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY (gst_d3d12_debug);
GST_DEBUG_CATEGORY (gst_d3d12_allocator_debug);
GST_DEBUG_CATEGORY (gst_d3d12_decoder_debug);
GST_DEBUG_CATEGORY (gst_d3d12_fence_debug);
GST_DEBUG_CATEGORY (gst_d3d12_format_debug);
GST_DEBUG_CATEGORY (gst_d3d12_utils_debug);

#define GST_CAT_DEFAULT gst_d3d12_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_debug, "d3d12", 0, "d3d12");
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_allocator_debug, "d3d12allocator", 0,
      "d3d12allocator");
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_decoder_debug, "d3d12decoder", 0,
      "d3d12decoder");
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_fence_debug, "d3d12fence", 0,
      "d3d12fence");
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_format_debug, "d3d12format", 0,
      "d3d12format");
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_utils_debug,
      "d3d12utils", 0, "d3d12utils");

  /* Enumerate devices to register decoders per device and to get the highest
   * feature level */
  /* AMD seems to be supporting up to 12 cards, and 8 for NVIDIA */
  for (guint i = 0; i < 12; i++) {
    GstD3D12Device *device = nullptr;
    ID3D12Device *device_handle;
    ComPtr < ID3D12VideoDevice > video_device;
    GstD3D11Device *d3d11_device;
    HRESULT hr;
    gint64 luid;
    gboolean d3d11_interop = FALSE;

    device = gst_d3d12_device_new (i);
    if (!device)
      break;

    device_handle = gst_d3d12_device_get_device_handle (device);
    hr = device_handle->QueryInterface (IID_PPV_ARGS (&video_device));
    if (FAILED (hr)) {
      gst_object_unref (device);
      continue;
    }

    g_object_get (device, "adapter-luid", &luid, nullptr);
    d3d11_device = gst_d3d11_device_new_for_adapter_luid (luid,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT);

    /* Enable d3d11 interop only if extended NV12 shared texture feature
     * is supported by GPU */
    if (d3d11_device) {
      ID3D11Device *d3d11_handle =
          gst_d3d11_device_get_device_handle (d3d11_device);
      D3D11_FEATURE_DATA_D3D11_OPTIONS4 option4 = { FALSE };
      hr = d3d11_handle->CheckFeatureSupport (D3D11_FEATURE_D3D11_OPTIONS4,
          &option4, sizeof (D3D11_FEATURE_DATA_D3D11_OPTIONS4));
      if (SUCCEEDED (hr) && option4.ExtendedNV12SharedTextureSupported)
        d3d11_interop = TRUE;

      gst_object_unref (d3d11_device);
    }

    gst_d3d12_h264_dec_register (plugin, device, video_device.Get (),
        GST_RANK_NONE, d3d11_interop);
    gst_d3d12_h265_dec_register (plugin, device, video_device.Get (),
        GST_RANK_NONE, d3d11_interop);
    gst_d3d12_vp9_dec_register (plugin, device, video_device.Get (),
        GST_RANK_NONE, d3d11_interop);
    gst_d3d12_av1_dec_register (plugin, device, video_device.Get (),
        GST_RANK_NONE, d3d11_interop);

    gst_object_unref (device);
  }

  gst_element_register (plugin,
      "d3d12download", GST_RANK_NONE, GST_TYPE_D3D12_DOWNLOAD);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    d3d12,
    "Direct3D12 plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

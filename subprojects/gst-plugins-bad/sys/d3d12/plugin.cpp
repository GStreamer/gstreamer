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
#include <gst/d3d12/gstd3d12.h>
#include "gstd3d12pluginutils.h"
#include "gstd3d12convert.h"
#include "gstd3d12videosink.h"
#include "gstd3d12testsrc.h"
#include "gstd3d12compositor.h"
#include "gstd3d12screencapturedevice.h"
#include "gstd3d12screencapturesrc.h"
#include "gstd3d12mpeg2dec.h"
#include "gstd3d12h264dec.h"
#include "gstd3d12h264enc.h"
#include "gstd3d12h265dec.h"
#include "gstd3d12vp8dec.h"
#include "gstd3d12vp9dec.h"
#include "gstd3d12av1dec.h"
#include "gstd3d12ipcclient.h"
#include "gstd3d12ipcsrc.h"
#include "gstd3d12ipcsink.h"
#include <windows.h>
#include <versionhelpers.h>
#include <wrl.h>
#include <glib/gi18n-lib.h>

#ifdef HAVE_GST_D3D11
#include "gstd3d12memorycopy.h"
#else
#include "gstd3d12download.h"
#include "gstd3d12upload.h"
#endif

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

static void
plugin_deinit (gpointer data)
{
  gst_d3d12_ipc_client_deinit ();
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!IsWindows8OrGreater ()) {
    gst_plugin_add_status_warning (plugin,
        N_("This plugin requires at least Windows 8 or newer."));
    return TRUE;
  }

  guint sink_rank = GST_RANK_NONE;
  guint decoder_rank = GST_RANK_NONE;
  bool have_video_device = false;

  if (gst_d3d12_is_windows_10_or_greater ())
    decoder_rank = GST_RANK_PRIMARY + 2;

  /* Enumerate devices to register decoders per device and to get the highest
   * feature level */
  /* AMD seems to be supporting up to 12 cards, and 8 for NVIDIA */
  for (guint i = 0; i < 12; i++) {
    GstD3D12Device *device = nullptr;
    ID3D12Device *device_handle;
    ComPtr < ID3D12VideoDevice > video_device;
    HRESULT hr;

    device = gst_d3d12_device_new (i);
    if (!device)
      break;

    device_handle = gst_d3d12_device_get_device_handle (device);

    hr = device_handle->QueryInterface (IID_PPV_ARGS (&video_device));
    if (FAILED (hr)) {
      gst_object_unref (device);
      continue;
    }

    have_video_device = true;

    gst_d3d12_mpeg2_dec_register (plugin, device, video_device.Get (),
        decoder_rank);
    gst_d3d12_h264_dec_register (plugin, device, video_device.Get (),
        decoder_rank);
    gst_d3d12_h265_dec_register (plugin, device, video_device.Get (),
        decoder_rank);
    gst_d3d12_vp8_dec_register (plugin, device, video_device.Get (),
        decoder_rank);
    gst_d3d12_vp9_dec_register (plugin, device, video_device.Get (),
        decoder_rank);
    gst_d3d12_av1_dec_register (plugin, device, video_device.Get (),
        decoder_rank);

    gst_d3d12_h264_enc_register (plugin, device, video_device.Get (),
        GST_RANK_NONE);

    gst_object_unref (device);
  }

  if (gst_d3d12_is_windows_10_or_greater () && have_video_device)
    sink_rank = GST_RANK_PRIMARY + 1;

  gst_element_register (plugin,
      "d3d12convert", GST_RANK_NONE, GST_TYPE_D3D12_CONVERT);
  gst_element_register (plugin,
      "d3d12download", GST_RANK_NONE, GST_TYPE_D3D12_DOWNLOAD);
  gst_element_register (plugin,
      "d3d12upload", GST_RANK_NONE, GST_TYPE_D3D12_UPLOAD);
  gst_element_register (plugin,
      "d3d12videosink", sink_rank, GST_TYPE_D3D12_VIDEO_SINK);
  gst_element_register (plugin,
      "d3d12testsrc", GST_RANK_NONE, GST_TYPE_D3D12_TEST_SRC);
  gst_element_register (plugin,
      "d3d12compositor", GST_RANK_NONE, GST_TYPE_D3D12_COMPOSITOR);
  gst_element_register (plugin, "d3d12screencapturesrc", GST_RANK_NONE,
      GST_TYPE_D3D12_SCREEN_CAPTURE_SRC);
  gst_device_provider_register (plugin,
      "d3d12screencapturedeviceprovider", GST_RANK_PRIMARY,
      GST_TYPE_D3D12_SCREEN_CAPTURE_DEVICE_PROVIDER);
  gst_element_register (plugin,
      "d3d12ipcsrc", GST_RANK_NONE, GST_TYPE_D3D12_IPC_SRC);
  gst_element_register (plugin,
      "d3d12ipcsink", GST_RANK_NONE, GST_TYPE_D3D12_IPC_SINK);

  g_object_set_data_full (G_OBJECT (plugin),
      "plugin-d3d12-shutdown", (gpointer) "shutdown-data",
      (GDestroyNotify) plugin_deinit);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    d3d12,
    "Direct3D12 plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

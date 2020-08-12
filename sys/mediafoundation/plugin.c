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

#include "gstmfconfig.h"

#include <winapifamily.h>

#include <gst/gst.h>
#include "gstmfvideosrc.h"
#include "gstmfdevice.h"
#include "gstmfutils.h"
#include "gstmfh264enc.h"
#include "gstmfh265enc.h"
#include "gstmfvp9enc.h"
#include "gstmfaacenc.h"
#include "gstmfmp3enc.h"

GST_DEBUG_CATEGORY (gst_mf_debug);
GST_DEBUG_CATEGORY (gst_mf_utils_debug);
GST_DEBUG_CATEGORY (gst_mf_source_object_debug);
GST_DEBUG_CATEGORY (gst_mf_transform_debug);

#define GST_CAT_DEFAULT gst_mf_debug

static void
plugin_deinit (gpointer data)
{
  MFShutdown ();
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  HRESULT hr;
  GstRank rank = GST_RANK_SECONDARY;

  /**
   * plugin-mediafoundation:
   *
   * Since: 1.18
   */

  GST_DEBUG_CATEGORY_INIT (gst_mf_debug, "mf", 0, "media foundation");
  GST_DEBUG_CATEGORY_INIT (gst_mf_utils_debug,
      "mfutils", 0, "media foundation utility functions");
  GST_DEBUG_CATEGORY_INIT (gst_mf_source_object_debug,
      "mfsourceobject", 0, "mfsourceobject");
  GST_DEBUG_CATEGORY_INIT (gst_mf_transform_debug,
      "mftransform", 0, "mftransform");

  hr = MFStartup (MF_VERSION, MFSTARTUP_NOSOCKET);
  if (!gst_mf_result (hr)) {
    GST_WARNING ("MFStartup failure, hr: 0x%x", hr);
    return TRUE;
  }

  /* mfvideosrc should be primary rank for UWP */
#if (GST_MF_WINAPI_APP && !GST_MF_WINAPI_DESKTOP)
  rank = GST_RANK_PRIMARY + 1;
#endif

  gst_element_register (plugin, "mfvideosrc", rank, GST_TYPE_MF_VIDEO_SRC);
  gst_device_provider_register (plugin, "mfdeviceprovider",
      rank, GST_TYPE_MF_DEVICE_PROVIDER);

  gst_mf_h264_enc_plugin_init (plugin, GST_RANK_SECONDARY);
  gst_mf_h265_enc_plugin_init (plugin, GST_RANK_SECONDARY);
  gst_mf_vp9_enc_plugin_init (plugin, GST_RANK_SECONDARY);

  gst_mf_aac_enc_plugin_init (plugin, GST_RANK_SECONDARY);
  gst_mf_mp3_enc_plugin_init (plugin, GST_RANK_SECONDARY);

  /* So that call MFShutdown() when this plugin is no more used
   * (i.e., gst_deinit). Otherwise valgrind-like tools would complain
   * about un-released media foundation resources.
   *
   * NOTE: MFStartup and MFShutdown can be called multiple times, but the number
   * of each MFStartup and MFShutdown call should be identical. This rule is
   * simliar to that of CoInitialize/CoUninitialize pair */
  g_object_set_data_full (G_OBJECT (plugin),
      "plugin-mediafoundation-shutdown", "shutdown-data",
      (GDestroyNotify) plugin_deinit);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mediafoundation,
    "Microsoft Media Foundation plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

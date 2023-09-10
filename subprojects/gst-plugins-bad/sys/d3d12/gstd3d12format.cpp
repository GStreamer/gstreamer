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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12format.h"
#include "gstd3d12utils.h"
#include "gstd3d12device.h"
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_format_debug);
#define GST_CAT_DEFAULT gst_d3d12_format_debug

guint
gst_d3d12_get_format_plane_count (GstD3D12Device * device, DXGI_FORMAT format)
{
  ID3D12Device *device_handle;
  HRESULT hr;
  D3D12_FEATURE_DATA_FORMAT_INFO format_info = { format, 0 };

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), 0);

  device_handle = gst_d3d12_device_get_device_handle (device);
  hr = device_handle->CheckFeatureSupport (D3D12_FEATURE_FORMAT_INFO,
      &format_info, sizeof (D3D12_FEATURE_DATA_FORMAT_INFO));
  if (!gst_d3d12_result (hr, device))
    return 0;

  return format_info.PlaneCount;
}

GstVideoFormat
gst_d3d12_dxgi_format_to_gst (DXGI_FORMAT format)
{
  switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
      return GST_VIDEO_FORMAT_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
      return GST_VIDEO_FORMAT_RGBA;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
      return GST_VIDEO_FORMAT_RGB10A2_LE;
    case DXGI_FORMAT_AYUV:
      return GST_VIDEO_FORMAT_VUYA;
    case DXGI_FORMAT_YUY2:
      return GST_VIDEO_FORMAT_YUY2;
    case DXGI_FORMAT_Y210:
      return GST_VIDEO_FORMAT_Y210;
    case DXGI_FORMAT_Y410:
      return GST_VIDEO_FORMAT_Y410;
    case DXGI_FORMAT_NV12:
      return GST_VIDEO_FORMAT_NV12;
    case DXGI_FORMAT_P010:
      return GST_VIDEO_FORMAT_P010_10LE;
    case DXGI_FORMAT_P016:
      return GST_VIDEO_FORMAT_P016_LE;
    default:
      break;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}

gboolean
gst_d3d12_dxgi_format_to_resource_formats (DXGI_FORMAT format,
    DXGI_FORMAT resource_format[GST_VIDEO_MAX_PLANES])
{
  g_return_val_if_fail (resource_format != nullptr, FALSE);

  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    resource_format[i] = DXGI_FORMAT_UNKNOWN;

  switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
      resource_format[0] = format;
      break;
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_YUY2:
      resource_format[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
    case DXGI_FORMAT_NV12:
      resource_format[0] = DXGI_FORMAT_R8_UNORM;
      resource_format[1] = DXGI_FORMAT_R8G8_UNORM;
      break;
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      resource_format[0] = DXGI_FORMAT_R16_UNORM;
      resource_format[1] = DXGI_FORMAT_R16G16_UNORM;
      break;
    case DXGI_FORMAT_Y210:
      resource_format[0] = DXGI_FORMAT_R16G16B16A16_UNORM;
      break;
    case DXGI_FORMAT_Y410:
      resource_format[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

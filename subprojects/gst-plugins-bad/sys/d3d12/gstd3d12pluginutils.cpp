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

#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <vector>

#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>

/* *INDENT-OFF* */
using namespace DirectX;
/* *INDENT-ON* */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12pluginutils", 0, "d3d12pluginutils");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

GType
gst_d3d12_sampling_method_get_type (void)
{
  static GType type = 0;
  static const GEnumValue methods[] = {
    {GST_D3D12_SAMPLING_METHOD_NEAREST,
        "Nearest Neighbour", "nearest-neighbour"},
    {GST_D3D12_SAMPLING_METHOD_BILINEAR,
        "Bilinear", "bilinear"},
    {GST_D3D12_SAMPLING_METHOD_LINEAR_MINIFICATION,
        "Linear minification, point magnification", "linear-minification"},
    {GST_D3D12_SAMPLING_METHOD_ANISOTROPIC, "Anisotropic", "anisotropic"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12SamplingMethod", methods);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

struct SamplingMethodMap
{
  GstD3D12SamplingMethod method;
  D3D12_FILTER filter;
};

static const SamplingMethodMap sampling_method_map[] = {
  {GST_D3D12_SAMPLING_METHOD_NEAREST, D3D12_FILTER_MIN_MAG_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_BILINEAR, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_LINEAR_MINIFICATION,
      D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_ANISOTROPIC, D3D12_FILTER_ANISOTROPIC},
};

D3D12_FILTER
gst_d3d12_sampling_method_to_native (GstD3D12SamplingMethod method)
{
  for (guint i = 0; i < G_N_ELEMENTS (sampling_method_map); i++) {
    if (sampling_method_map[i].method == method)
      return sampling_method_map[i].filter;
  }

  return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

GType
gst_d3d12_msaa_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue msaa_mode[] = {
    {GST_D3D12_MSAA_DISABLED, "Disabled", "disabled"},
    {GST_D3D12_MSAA_2X, "2x MSAA", "2x"},
    {GST_D3D12_MSAA_4X, "4x MSAA", "4x"},
    {GST_D3D12_MSAA_8X, "8x MSAA", "8x"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12MSAAMode", msaa_mode);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

gboolean
gst_d3d12_need_transform (gfloat rotation_x, gfloat rotation_y,
    gfloat rotation_z, gfloat scale_x, gfloat scale_y)
{
  const gfloat min_diff = 0.00001f;

  if (!XMScalarNearEqual (rotation_x, 0.0f, min_diff) ||
      !XMScalarNearEqual (rotation_y, 0.0f, min_diff) ||
      !XMScalarNearEqual (rotation_z, 0.0f, min_diff) ||
      !XMScalarNearEqual (scale_x, 1.0f, min_diff) ||
      !XMScalarNearEqual (scale_y, 1.0f, min_diff)) {
    return TRUE;
  }

  return FALSE;
}

gboolean
gst_d3d12_is_windows_10_or_greater (void)
{
  static gboolean ret = FALSE;
  GST_D3D12_CALL_ONCE_BEGIN {
    ret = g_win32_check_windows_version (10, 0, 0, G_WIN32_OS_ANY);
  } GST_D3D12_CALL_ONCE_END;

  return ret;
}

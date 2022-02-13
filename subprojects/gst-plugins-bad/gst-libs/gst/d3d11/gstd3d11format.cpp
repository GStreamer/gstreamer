/* GStreamer
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

#include "gstd3d11format.h"
#include "gstd3d11utils.h"
#include "gstd3d11device.h"
#include "gstd3d11memory.h"
#include "gstd3d11_private.h"

#include <string.h>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("d3d11format", 0,
        "d3d11 specific formats");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

/**
 * gst_d3d11_dxgi_format_get_size:
 * @format: a DXGI_FORMAT
 * @width: a texture width
 * @height: a texture height
 * @pitch: a pitch of texture
 * @offset: offset for each plane
 * @stride: stride for each plane
 * @size: (out): required memory size for given format
 *
 * Calculate required memory size and per plane stride with
 * based on information
 *
 * Returns: %TRUE if @size can be calculated with given information
 *
 * Since: 1.20
 */
gboolean
gst_d3d11_dxgi_format_get_size (DXGI_FORMAT format, guint width, guint height,
    guint pitch, gsize offset[GST_VIDEO_MAX_PLANES],
    gint stride[GST_VIDEO_MAX_PLANES], gsize * size)
{
  g_return_val_if_fail (format != DXGI_FORMAT_UNKNOWN, FALSE);

  switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_YUY2:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
      offset[0] = 0;
      stride[0] = pitch;
      *size = pitch * height;
      break;
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      offset[0] = 0;
      stride[0] = pitch;
      offset[1] = offset[0] + stride[0] * height;
      stride[1] = pitch;
      *size = offset[1] + stride[1] * GST_ROUND_UP_2 (height / 2);
      break;
    default:
      return FALSE;
  }

  GST_LOG ("Calculated buffer size: %" G_GSIZE_FORMAT
      " (dxgi format:%d, %dx%d, Pitch %d)",
      *size, format, width, height, pitch);

  return TRUE;
}

/**
 * gst_d3d11_dxgi_format_to_gst:
 * @format: a DXGI_FORMAT
 *
 * Converts the @format to its #GstVideoFormat representation.
 *
 * Returns: a #GstVideoFormat equivalent to @format
 *
 * Since: 1.20
 */
GstVideoFormat
gst_d3d11_dxgi_format_to_gst (DXGI_FORMAT format)
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

/**
 * gst_d3d11_format_init:
 * @format: (out caller-allocates): a #GstD3D11Format
 *
 * Initialize @format with default values.
 *
 * Since: 1.22
 */
void
gst_d3d11_format_init (GstD3D11Format * format)
{
  g_return_if_fail (format != nullptr);

  memset (format, 0, sizeof (GstD3D11Format));
}

/**
 * gst_d3d11_dxgi_format_get_resource_format:
 * @format: a DXGI_FORMAT
 * @resource_format: (out caller-allocats): Resource formats for each plane
 *
 * Returns: the number of planes for @format
 *
 * Since: 1.22
 */
guint
gst_d3d11_dxgi_format_get_resource_format (DXGI_FORMAT format,
    DXGI_FORMAT resource_format[GST_VIDEO_MAX_PLANES])
{
  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    resource_format[i] = DXGI_FORMAT_UNKNOWN;

  if (format == DXGI_FORMAT_UNKNOWN)
    return 0;

  for (guint i = 0; i < GST_D3D11_N_FORMATS; i++) {
    const GstD3D11Format *fmt = &_gst_d3d11_default_format_map[i];

    if (fmt->dxgi_format == format) {
      guint n_planes = 0;

      for (n_planes = 0; n_planes < GST_VIDEO_MAX_PLANES; n_planes++) {
        if (fmt->resource_format[n_planes] == DXGI_FORMAT_UNKNOWN)
          break;

        resource_format[n_planes] = fmt->resource_format[n_planes];
      }

      return n_planes;
    }
  }

  resource_format[0] = format;
  return 1;
}

/**
 * gst_d3d11_dxgi_format_get_alignment:
 * @format: a DXGI_FORMAT
 *
 * Returns: Width and height Alignment requirement for given @format
 *
 * Since: 1.22
 */
guint
gst_d3d11_dxgi_format_get_alignment (DXGI_FORMAT format)
{
  switch (format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      return 2;
    default:
      break;
  }

  return 0;
}

const gchar *
gst_d3d11_dxgi_format_to_string (DXGI_FORMAT format)
{
#define CASE(f) \
    case DXGI_FORMAT_ ##f: \
      return G_STRINGIFY (f);

  switch (format) {
      CASE (UNKNOWN);
      CASE (R32G32B32A32_TYPELESS);
      CASE (R32G32B32A32_FLOAT);
      CASE (R32G32B32A32_UINT);
      CASE (R32G32B32A32_SINT);
      CASE (R32G32B32_TYPELESS);
      CASE (R32G32B32_FLOAT);
      CASE (R32G32B32_UINT);
      CASE (R32G32B32_SINT);
      CASE (R16G16B16A16_TYPELESS);
      CASE (R16G16B16A16_FLOAT);
      CASE (R16G16B16A16_UNORM);
      CASE (R16G16B16A16_UINT);
      CASE (R16G16B16A16_SNORM);
      CASE (R16G16B16A16_SINT);
      CASE (R32G32_TYPELESS);
      CASE (R32G32_FLOAT);
      CASE (R32G32_UINT);
      CASE (R32G32_SINT);
      CASE (R32G8X24_TYPELESS);
      CASE (D32_FLOAT_S8X24_UINT);
      CASE (R32_FLOAT_X8X24_TYPELESS);
      CASE (X32_TYPELESS_G8X24_UINT);
      CASE (R10G10B10A2_TYPELESS);
      CASE (R10G10B10A2_UNORM);
      CASE (R10G10B10A2_UINT);
      CASE (R11G11B10_FLOAT);
      CASE (R8G8B8A8_TYPELESS);
      CASE (R8G8B8A8_UNORM);
      CASE (R8G8B8A8_UNORM_SRGB);
      CASE (R8G8B8A8_UINT);
      CASE (R8G8B8A8_SNORM);
      CASE (R8G8B8A8_SINT);
      CASE (R16G16_TYPELESS);
      CASE (R16G16_FLOAT);
      CASE (R16G16_UNORM);
      CASE (R16G16_UINT);
      CASE (R16G16_SNORM);
      CASE (R16G16_SINT);
      CASE (R32_TYPELESS);
      CASE (D32_FLOAT);
      CASE (R32_FLOAT);
      CASE (R32_UINT);
      CASE (R32_SINT);
      CASE (R24G8_TYPELESS);
      CASE (D24_UNORM_S8_UINT);
      CASE (R24_UNORM_X8_TYPELESS);
      CASE (X24_TYPELESS_G8_UINT);
      CASE (R8G8_TYPELESS);
      CASE (R8G8_UNORM);
      CASE (R8G8_UINT);
      CASE (R8G8_SNORM);
      CASE (R8G8_SINT);
      CASE (R16_TYPELESS);
      CASE (R16_FLOAT);
      CASE (D16_UNORM);
      CASE (R16_UNORM);
      CASE (R16_UINT);
      CASE (R16_SNORM);
      CASE (R16_SINT);
      CASE (R8_TYPELESS);
      CASE (R8_UNORM);
      CASE (R8_UINT);
      CASE (R8_SNORM);
      CASE (R8_SINT);
      CASE (A8_UNORM);
      CASE (R1_UNORM);
      CASE (R9G9B9E5_SHAREDEXP);
      CASE (R8G8_B8G8_UNORM);
      CASE (G8R8_G8B8_UNORM);
      CASE (BC1_TYPELESS);
      CASE (BC1_UNORM);
      CASE (BC1_UNORM_SRGB);
      CASE (BC2_TYPELESS);
      CASE (BC2_UNORM);
      CASE (BC2_UNORM_SRGB);
      CASE (BC3_TYPELESS);
      CASE (BC3_UNORM);
      CASE (BC3_UNORM_SRGB);
      CASE (BC4_TYPELESS);
      CASE (BC4_UNORM);
      CASE (BC4_SNORM);
      CASE (BC5_TYPELESS);
      CASE (BC5_UNORM);
      CASE (BC5_SNORM);
      CASE (B5G6R5_UNORM);
      CASE (B5G5R5A1_UNORM);
      CASE (B8G8R8A8_UNORM);
      CASE (B8G8R8X8_UNORM);
      CASE (R10G10B10_XR_BIAS_A2_UNORM);
      CASE (B8G8R8A8_TYPELESS);
      CASE (B8G8R8A8_UNORM_SRGB);
      CASE (B8G8R8X8_TYPELESS);
      CASE (B8G8R8X8_UNORM_SRGB);
      CASE (BC6H_TYPELESS);
      CASE (BC6H_UF16);
      CASE (BC6H_SF16);
      CASE (BC7_TYPELESS);
      CASE (BC7_UNORM);
      CASE (BC7_UNORM_SRGB);
      CASE (AYUV);
      CASE (Y410);
      CASE (Y416);
      CASE (NV12);
      CASE (P010);
      CASE (P016);
    case DXGI_FORMAT_420_OPAQUE:
      return "420_OPAQUE";
      CASE (YUY2);
      CASE (Y210);
      CASE (Y216);
      CASE (NV11);
      CASE (AI44);
      CASE (IA44);
      CASE (P8);
      CASE (A8P8);
      CASE (B4G4R4A4_UNORM);
      CASE (P208);
      CASE (V208);
      CASE (V408);
    default:
      break;
  }
#undef CASE

  return "Unknown";
}

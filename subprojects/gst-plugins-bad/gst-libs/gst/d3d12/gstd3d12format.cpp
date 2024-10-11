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

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include <string.h>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12format", 0, "d3d12format");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif

/* *INDENT-OFF* */
const D3D12_FORMAT_SUPPORT1 kDefaultFormatSupport1 =
    D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE |
    D3D12_FORMAT_SUPPORT1_RENDER_TARGET;

struct FormatBuilder : public GstD3D12Format
{
  explicit FormatBuilder (const GstD3D12Format & other)
      : GstD3D12Format (other) {}

  FormatBuilder ()
  {
    format = GST_VIDEO_FORMAT_UNKNOWN;
    dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
    support1 = D3D12_FORMAT_SUPPORT1_NONE;
    support2 = D3D12_FORMAT_SUPPORT2_NONE;

    for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      resource_format[i] = DXGI_FORMAT_UNKNOWN;
      uav_format[i] = DXGI_FORMAT_UNKNOWN;
    }
  }

  FormatBuilder (
      GstVideoFormat Format,
      GstD3D12FormatFlags FormatFlags,
      D3D12_RESOURCE_DIMENSION Dimension,
      DXGI_FORMAT DxgiFormat,
      const DXGI_FORMAT ResourceFormat[GST_VIDEO_MAX_PLANES],
      D3D12_FORMAT_SUPPORT1 Support1,
      D3D12_FORMAT_SUPPORT2 Support2
    )
  {
    format = Format;
    format_flags = FormatFlags;
    dimension = Dimension;
    dxgi_format = DxgiFormat;
    support1 = Support1;
    support2 = Support2;

    for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      resource_format[i] = ResourceFormat[i];
      uav_format[i] = DXGI_FORMAT_UNKNOWN;
    }
  }

  static inline FormatBuilder NotSupported (
      GstVideoFormat Format
    )
  {
    auto f = FormatBuilder();
    f.format = Format;
    return f;
  }

  static inline FormatBuilder RgbPacked (
      GstVideoFormat Format,
      DXGI_FORMAT DxgiFormat,
      D3D12_FORMAT_SUPPORT1 Support1 = kDefaultFormatSupport1,
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_NONE,
      GstD3D12FormatFlags FormatFlags = GST_D3D12_FORMAT_FLAG_NONE
    )
  {
    DXGI_FORMAT resource_format[] = { DxgiFormat, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT_UNKNOWN };

    return FormatBuilder (Format, FormatFlags,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        DxgiFormat, resource_format, Support1, Support2);
  }

  static inline FormatBuilder Planar (
      GstVideoFormat Format,
      DXGI_FORMAT ResourceFormat = DXGI_FORMAT_R8_UNORM,
      D3D12_FORMAT_SUPPORT1 Support1 = kDefaultFormatSupport1,
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_NONE
    )
  {
    DXGI_FORMAT resource_format[] = { ResourceFormat, ResourceFormat,
        ResourceFormat, DXGI_FORMAT_UNKNOWN };
    return FormatBuilder (Format, GST_D3D12_FORMAT_FLAG_NONE,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_UNKNOWN,
        resource_format, Support1, Support2);
  }

  static inline FormatBuilder PlanarFull (
      GstVideoFormat Format,
      DXGI_FORMAT ResourceFormat = DXGI_FORMAT_R8_UNORM,
      D3D12_FORMAT_SUPPORT1 Support1 = kDefaultFormatSupport1,
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_NONE
    )
  {
    DXGI_FORMAT resource_format[] = { ResourceFormat, ResourceFormat,
        ResourceFormat, ResourceFormat };
    return FormatBuilder (Format, GST_D3D12_FORMAT_FLAG_NONE,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_UNKNOWN,
        resource_format, Support1, Support2);
  }

  static inline FormatBuilder YuvSemiPlanar (
      GstVideoFormat Format,
      DXGI_FORMAT DxgiFormat,
      DXGI_FORMAT ResourceFormatY,
      DXGI_FORMAT ResourceFormatUV,
      D3D12_FORMAT_SUPPORT1 Support1 = kDefaultFormatSupport1,
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_NONE
    )
  {
    DXGI_FORMAT resource_format[] = { ResourceFormatY, ResourceFormatUV,
        DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
    return FormatBuilder (Format, GST_D3D12_FORMAT_FLAG_NONE,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D, DxgiFormat, resource_format,
        Support1, Support2);
  }

  static inline FormatBuilder YuvSemiPlanarWithAlpha (
      GstVideoFormat Format,
      DXGI_FORMAT ResourceFormatY,
      DXGI_FORMAT ResourceFormatUV,
      DXGI_FORMAT ResourceFormatA,
      D3D12_FORMAT_SUPPORT1 Support1 = kDefaultFormatSupport1,
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_NONE
    )
  {
    DXGI_FORMAT resource_format[] = { ResourceFormatY, ResourceFormatUV,
        ResourceFormatA, DXGI_FORMAT_UNKNOWN };
    return FormatBuilder (Format, GST_D3D12_FORMAT_FLAG_NONE,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_UNKNOWN,
        resource_format, Support1, Support2);
  }

  static inline FormatBuilder YuvPacked (
      GstVideoFormat Format,
      DXGI_FORMAT DxgiFormat,
      DXGI_FORMAT ResourceFormat,
      GstD3D12FormatFlags FormatFlags = GST_D3D12_FORMAT_FLAG_OUTPUT_UAV,
      D3D12_FORMAT_SUPPORT1 Support1 = (D3D12_FORMAT_SUPPORT1_TEXTURE2D |
          D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW),
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE
    )
  {
    DXGI_FORMAT resource_format[] = { ResourceFormat, DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
    return FormatBuilder (Format, FormatFlags,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        DxgiFormat, resource_format, Support1, Support2);
  }

  static inline FormatBuilder Gray (
      GstVideoFormat Format,
      DXGI_FORMAT DxgiFormat,
      D3D12_FORMAT_SUPPORT1 Support1 = kDefaultFormatSupport1,
      D3D12_FORMAT_SUPPORT2 Support2 = D3D12_FORMAT_SUPPORT2_NONE
    )
  {
    DXGI_FORMAT resource_format[] = { DxgiFormat, DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
    return FormatBuilder (Format, GST_D3D12_FORMAT_FLAG_NONE,
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        DxgiFormat, resource_format, Support1, Support2);
  }

  static inline FormatBuilder Buffer (
      GstVideoFormat Format
    )
  {
    DXGI_FORMAT resource_format[] = { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
        DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN };
    return FormatBuilder (Format, GST_D3D12_FORMAT_FLAG_NONE,
        D3D12_RESOURCE_DIMENSION_BUFFER, DXGI_FORMAT_UNKNOWN, resource_format,
        D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE);
  }
};

static const GstD3D12Format g_format_map[] = {
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_UNKNOWN),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_ENCODED),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_I420),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_YV12),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_YUY2,
      DXGI_FORMAT_YUY2, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_UYVY,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_AYUV,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_RGBx,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_BGRx,
      DXGI_FORMAT_B8G8R8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_xRGB,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_xBGR,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_RGBA,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_BGRA,
      DXGI_FORMAT_B8G8R8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_ARGB,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_ABGR,
      DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_RGB,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_BGR,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_Y41B),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_Y42B),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_YVYU,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_Y444),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_v210,
      DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_v216,
      DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_NV12,
      DXGI_FORMAT_NV12, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_NV21,
      DXGI_FORMAT_NV12, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM),
  FormatBuilder::Gray (GST_VIDEO_FORMAT_GRAY8,
      DXGI_FORMAT_R8_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GRAY16_BE),
  FormatBuilder::Gray (GST_VIDEO_FORMAT_GRAY16_LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_v308,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_RGB16,
      DXGI_FORMAT_B5G6R5_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_BGR16,
      DXGI_FORMAT_B5G6R5_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_RGB15,
      DXGI_FORMAT_B5G5R5A1_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_BGR15,
      DXGI_FORMAT_B5G5R5A1_UNORM),
  FormatBuilder::Buffer (GST_VIDEO_FORMAT_UYVP),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A420),
  FormatBuilder::Buffer (GST_VIDEO_FORMAT_RGB8P),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_YUV9),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_YVU9),
  FormatBuilder::Buffer (GST_VIDEO_FORMAT_IYU1),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_ARGB64,
      DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_AYUV64,
      DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_r210,
      DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_UINT),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_I420_10BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_I420_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_I422_10BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_I422_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y444_10BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_Y444_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_GBR),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GBR_10BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_GBR_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_NV16,
      DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_NV24,
      DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_64Z32),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A420_10BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A420_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A422_10BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A422_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A444_10BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A444_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_NV61,
      DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_P010_10BE),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_P010_10LE,
      DXGI_FORMAT_P010, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_IYU2,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_VYUY,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_GBRA),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GBRA_10BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_GBRA_10LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GBR_12BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_GBR_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GBRA_12BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_GBRA_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_I420_12BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_I420_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_I422_12BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_I422_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y444_12BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_Y444_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GRAY10_LE32),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_10LE32),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV16_10LE32),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_10LE40),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_Y210,
      DXGI_FORMAT_Y210, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_Y410,
      DXGI_FORMAT_Y410, DXGI_FORMAT_R10G10B10A2_UNORM),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_VUYA,
      DXGI_FORMAT_AYUV, DXGI_FORMAT_R8G8B8A8_UNORM, GST_D3D12_FORMAT_FLAG_NONE,
      kDefaultFormatSupport1, D3D12_FORMAT_SUPPORT2_NONE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_BGR10A2_LE,
      DXGI_FORMAT_Y410, DXGI_FORMAT_R10G10B10A2_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_RGB10A2_LE,
      DXGI_FORMAT_R10G10B10A2_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y444_16BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_Y444_16LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_P016_BE),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_P016_LE,
      DXGI_FORMAT_P016, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_P012_BE),
  FormatBuilder::YuvSemiPlanar (GST_VIDEO_FORMAT_P012_LE,
      DXGI_FORMAT_P016, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y212_BE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_Y212_LE,
      DXGI_FORMAT_Y216, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y412_BE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_Y412_LE,
      DXGI_FORMAT_Y416, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_4L4),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_32L32),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_RGBP),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_BGRP),
  FormatBuilder::YuvSemiPlanarWithAlpha (GST_VIDEO_FORMAT_AV12,
      DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8_UNORM),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_ARGB64_LE,
      DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_ARGB64_BE),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_RGBA64_LE,
      DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_RGBA64_BE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_BGRA64_LE,
      DXGI_FORMAT_Y416, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_BGRA64_BE),
  FormatBuilder::RgbPacked (GST_VIDEO_FORMAT_ABGR64_LE,
      DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_ABGR64_BE),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_16L32S),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_8L128),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_10BE_8L128),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_NV12_10LE40_4L4),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_DMA_DRM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_MT2110T),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_MT2110R),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A422),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A444),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A444_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A444_12BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A422_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A422_12BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A420_12LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A420_12BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A444_16LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A444_16BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A422_16LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A422_16BE),
  FormatBuilder::PlanarFull (GST_VIDEO_FORMAT_A420_16LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_A420_16BE),
  FormatBuilder::Planar (GST_VIDEO_FORMAT_GBR_16LE,
      DXGI_FORMAT_R16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_GBR_16BE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_RBGA,
      DXGI_FORMAT_AYUV, DXGI_FORMAT_R8G8B8A8_UNORM, GST_D3D12_FORMAT_FLAG_NONE,
      kDefaultFormatSupport1, D3D12_FORMAT_SUPPORT2_NONE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_Y216_LE,
      DXGI_FORMAT_Y216, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y216_BE),
  FormatBuilder::YuvPacked (GST_VIDEO_FORMAT_Y416_LE,
      DXGI_FORMAT_Y416, DXGI_FORMAT_R16G16B16A16_UNORM),
  FormatBuilder::NotSupported(GST_VIDEO_FORMAT_Y416_BE),
};
/* *INDENT-ON* */

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
gst_d3d12_get_format (GstVideoFormat format, GstD3D12Format * d3d12_format)
{
  if ((guint) format >= G_N_ELEMENTS (g_format_map))
    return FALSE;

  const auto & f = g_format_map[(guint) format];
  g_assert (f.format == format);

  if (f.dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
    return FALSE;

  *d3d12_format = g_format_map[(guint) format];

  return TRUE;
}

guint
gst_d3d12_dxgi_format_get_resource_format (DXGI_FORMAT format,
    DXGI_FORMAT resource_format[GST_VIDEO_MAX_PLANES])
{
  g_return_val_if_fail (resource_format != nullptr, FALSE);

  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    resource_format[i] = DXGI_FORMAT_UNKNOWN;

  if (format == DXGI_FORMAT_UNKNOWN)
    return 0;

  for (guint i = 0; i < G_N_ELEMENTS (g_format_map); i++) {
    const auto & f = g_format_map[i];
    if (f.dxgi_format != format)
      continue;

    guint n_planes = 0;
    for (n_planes = 0; n_planes < GST_VIDEO_MAX_PLANES; n_planes++) {
      auto rf = f.resource_format[n_planes];
      if (rf == DXGI_FORMAT_UNKNOWN)
        break;

      resource_format[n_planes] = rf;
    }

    return n_planes;
  }

  resource_format[0] = format;

  return 1;
}

char *
gst_d3d12_dump_color_matrix (GstD3D12ColorMatrix * matrix)
{
  /* *INDENT-OFF* */
  static const gchar format[] =
      "[MATRIX]\n"
      "|% .6f, % .6f, % .6f|\n"
      "|% .6f, % .6f, % .6f|\n"
      "|% .6f, % .6f, % .6f|\n"
      "[OFFSET]\n"
      "|% .6f, % .6f, % .6f|\n"
      "[MIN]\n"
      "|% .6f, % .6f, % .6f|\n"
      "[MAX]\n"
      "|% .6f, % .6f, % .6f|";
  /* *INDENT-ON* */

  g_return_val_if_fail (matrix != nullptr, nullptr);

  return g_strdup_printf (format,
      matrix->matrix[0][0], matrix->matrix[0][1], matrix->matrix[0][2],
      matrix->matrix[1][0], matrix->matrix[1][1], matrix->matrix[1][2],
      matrix->matrix[2][0], matrix->matrix[2][1], matrix->matrix[2][2],
      matrix->offset[0], matrix->offset[1], matrix->offset[2],
      matrix->min[0], matrix->min[1], matrix->min[2],
      matrix->max[0], matrix->max[1], matrix->max[2]);
}

static void
color_matrix_copy (GstD3D12ColorMatrix * dst, const GstD3D12ColorMatrix * src)
{
  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      dst->matrix[i][j] = src->matrix[i][j];
    }
  }
}

static void
color_matrix_multiply (GstD3D12ColorMatrix * dst, GstD3D12ColorMatrix * a,
    GstD3D12ColorMatrix * b)
{
  GstD3D12ColorMatrix tmp;

  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      gdouble val = 0;
      for (guint k = 0; k < 3; k++) {
        val += a->matrix[i][k] * b->matrix[k][j];
      }

      tmp.matrix[i][j] = val;
    }
  }

  color_matrix_copy (dst, &tmp);
}

static void
color_matrix_identity (GstD3D12ColorMatrix * m)
{
  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      if (i == j)
        m->matrix[i][j] = 1.0;
      else
        m->matrix[i][j] = 0;
    }
  }
}

void
gst_d3d12_color_matrix_init (GstD3D12ColorMatrix * matrix)
{
  g_return_if_fail (matrix);

  color_matrix_identity (matrix);
  for (guint i = 0; i < 3; i++) {
    matrix->min[i] = 0;
    matrix->max[i] = 1;
    matrix->offset[i] = 0;
  }
}

static gboolean
color_matrix_invert (GstD3D12ColorMatrix * dst, GstD3D12ColorMatrix * src)
{
  GstD3D12ColorMatrix tmp;
  gdouble det;

  color_matrix_identity (&tmp);
  for (guint j = 0; j < 3; j++) {
    for (guint i = 0; i < 3; i++) {
      tmp.matrix[j][i] =
          src->matrix[(i + 1) % 3][(j + 1) % 3] *
          src->matrix[(i + 2) % 3][(j + 2) % 3] -
          src->matrix[(i + 1) % 3][(j + 2) % 3] *
          src->matrix[(i + 2) % 3][(j + 1) % 3];
    }
  }

  det = tmp.matrix[0][0] * src->matrix[0][0] +
      tmp.matrix[0][1] * src->matrix[1][0] +
      tmp.matrix[0][2] * src->matrix[2][0];
  if (det == 0)
    return FALSE;

  for (guint j = 0; j < 3; j++) {
    for (guint i = 0; i < 3; i++) {
      tmp.matrix[i][j] /= det;
    }
  }

  color_matrix_copy (dst, &tmp);

  return TRUE;
}

/**
 * gst_d3d12_color_range_adjust_matrix_unorm:
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @matrix: a #GstD3D12ColorMatrix
 *
 * Calculates matrix for color range adjustment. Both input and output
 * signals are in normalized [0.0..1.0] space.
 *
 * Resulting values can be calculated by
 * | Yout |                           | Yin |   | matrix.offset[0] |
 * | Uout | = clamp ( matrix.matrix * | Uin | + | matrix.offset[1] |, matrix.min, matrix.max )
 * | Vout |                           | Vin |   | matrix.offset[2] |
 *
 * Returns: %TRUE if successful
 */
gboolean
gst_d3d12_color_range_adjust_matrix_unorm (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstD3D12ColorMatrix * matrix)
{
  gboolean in_rgb, out_rgb;
  gint in_offset[GST_VIDEO_MAX_COMPONENTS];
  gint in_scale[GST_VIDEO_MAX_COMPONENTS];
  gint out_offset[GST_VIDEO_MAX_COMPONENTS];
  gint out_scale[GST_VIDEO_MAX_COMPONENTS];
  GstVideoColorRange in_range;
  GstVideoColorRange out_range;
  gdouble src_fullscale, dst_fullscale;

  g_return_val_if_fail (in_info != nullptr, FALSE);
  g_return_val_if_fail (out_info != nullptr, FALSE);
  g_return_val_if_fail (matrix != nullptr, FALSE);

  memset (matrix, 0, sizeof (GstD3D12ColorMatrix));
  for (guint i = 0; i < 3; i++) {
    matrix->matrix[i][i] = 1.0;
    matrix->matrix[i][i] = 1.0;
    matrix->matrix[i][i] = 1.0;
    matrix->max[i] = 1.0;
  }

  in_rgb = GST_VIDEO_INFO_IS_RGB (in_info);
  out_rgb = GST_VIDEO_INFO_IS_RGB (out_info);

  if (in_rgb != out_rgb) {
    GST_WARNING ("Invalid format conversion");
    return FALSE;
  }

  in_range = in_info->colorimetry.range;
  out_range = out_info->colorimetry.range;

  if (in_range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    GST_WARNING ("Unknown input color range");
    if (in_rgb || GST_VIDEO_INFO_IS_GRAY (in_info))
      in_range = GST_VIDEO_COLOR_RANGE_0_255;
    else
      in_range = GST_VIDEO_COLOR_RANGE_16_235;
  }

  if (out_range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    GST_WARNING ("Unknown output color range");
    if (out_rgb || GST_VIDEO_INFO_IS_GRAY (out_info))
      out_range = GST_VIDEO_COLOR_RANGE_0_255;
    else
      out_range = GST_VIDEO_COLOR_RANGE_16_235;
  }

  src_fullscale = (gdouble) ((1 << in_info->finfo->depth[0]) - 1);
  dst_fullscale = (gdouble) ((1 << out_info->finfo->depth[0]) - 1);

  gst_video_color_range_offsets (in_range, in_info->finfo, in_offset, in_scale);
  gst_video_color_range_offsets (out_range,
      out_info->finfo, out_offset, out_scale);

  matrix->min[0] = matrix->min[1] = matrix->min[2] =
      (gdouble) out_offset[0] / dst_fullscale;

  matrix->max[0] = (out_scale[0] + out_offset[0]) / dst_fullscale;
  matrix->max[1] = matrix->max[2] =
      (out_scale[1] + out_offset[0]) / dst_fullscale;

  if (in_info->colorimetry.range == out_info->colorimetry.range) {
    GST_DEBUG ("Same color range");
    return TRUE;
  }

  /* Formula
   *
   * 1) Scales and offset compensates input to [0..1] range
   * SRC_NORM[i] = (src[i] * src_fullscale - in_offset[i]) / in_scale[i]
   *             = (src[i] * src_fullscale / in_scale[i]) - in_offset[i] / in_scale[i]
   *
   * 2) Reverse to output UNIT scale
   * DST_UINT[i] = SRC_NORM[i] * out_scale[i] + out_offset[i]
   *             = src[i] * src_fullscale * out_scale[i] / in_scale[i]
   *               - in_offset[i] * out_scale[i] / in_scale[i]
   *               + out_offset[i]
   *
   * 3) Back to [0..1] scale
   * dst[i] = DST_UINT[i] / dst_fullscale
   *        = COEFF[i] * src[i] + OFF[i]
   * where
   *             src_fullscale * out_scale[i]
   * COEFF[i] = ------------------------------
   *             dst_fullscale * in_scale[i]
   *
   *            out_offset[i]     in_offset[i] * out_scale[i]
   * OFF[i] =  -------------- -  ------------------------------
   *            dst_fullscale     dst_fullscale * in_scale[i]
   */
  for (guint i = 0; i < 3; i++) {
    matrix->matrix[i][i] = (src_fullscale * out_scale[i]) /
        (dst_fullscale * in_scale[i]);
    matrix->offset[i] = (out_offset[i] / dst_fullscale) -
        ((gdouble) in_offset[i] * out_scale[i] / (dst_fullscale * in_scale[i]));
  }

  return TRUE;
}

/**
 * gst_d3d12_yuv_to_rgb_matrix_unorm:
 * @in_yuv_info: a #GstVideoInfo of input YUV signal
 * @out_rgb_info: a #GstVideoInfo of output RGB signal
 * @matrix: a #GstD3D12ColorMatrix
 *
 * Calculates transform matrix from YUV to RGB conversion. Both input and output
 * signals are in normalized [0.0..1.0] space and additional gamma decoding
 * or primary/transfer function transform is not performed by this matrix.
 *
 * Resulting non-linear RGB values can be calculated by
 * | R' |                           | Y' |   | matrix.offset[0] |
 * | G' | = clamp ( matrix.matrix * | Cb | + | matrix.offset[1] | matrix.min, matrix.max )
 * | B' |                           | Cr |   | matrix.offset[2] |
 *
 * Returns: %TRUE if successful
 */
gboolean
gst_d3d12_yuv_to_rgb_matrix_unorm (const GstVideoInfo * in_yuv_info,
    const GstVideoInfo * out_rgb_info, GstD3D12ColorMatrix * matrix)
{
  gint offset[4], scale[4];
  gdouble Kr, Kb, Kg;

  g_return_val_if_fail (in_yuv_info != nullptr, FALSE);
  g_return_val_if_fail (out_rgb_info != nullptr, FALSE);
  g_return_val_if_fail (matrix != nullptr, FALSE);

  /*
   * <Formula>
   *
   * Input: Unsigned normalized Y'CbCr(unorm), [0.0..1.0] range
   * Output: Unsigned normalized non-linear R'G'B'(unorm), [0.0..1.0] range
   *
   * 1) Y'CbCr(unorm) to scaled Y'CbCr
   * | Y' |     | Y'(unorm) |
   * | Cb | = S | Cb(unorm) |
   * | Cb |     | Cr(unorm) |
   * where S = (2 ^ bitdepth) - 1
   *
   * 2) Y'CbCr to YPbPr
   * Y  = (Y' - offsetY )    / scaleY
   * Pb = [(Cb - offsetCbCr) / scaleCbCr]
   * Pr = [(Cr - offsetCrCr) / scaleCrCr]
   * =>
   * Y  = Y'(unorm) * Sy  + Oy
   * Pb = Cb(unorm) * Suv + Ouv
   * Pb = Cr(unorm) * Suv + Ouv
   * where
   * Sy  = S / scaleY
   * Suv = S / scaleCbCr
   * Oy  = -(offsetY / scaleY)
   * Ouv = -(offsetCbCr / scaleCbCr)
   *
   * 3) YPbPr to R'G'B'
   * | R' |      | Y  |
   * | G' | = M *| Pb |
   * | B' |      | Pr |
   * where
   *     | vecR |
   * M = | vecG |
   *     | vecB |
   * vecR = | 1,         0           ,       2(1 - Kr)      |
   * vecG = | 1, -(Kb/Kg) * 2(1 - Kb), -(Kr/Kg) * 2(1 - Kr) |
   * vecB = | 1,       2(1 - Kb)     ,          0           |
   * =>
   * R' = dot(vecR, (Syuv * Y'CbCr(unorm))) + dot(vecR, Offset)
   * G' = dot(vecG, (Svuy * Y'CbCr(unorm))) + dot(vecG, Offset)
   * B' = dot(vecB, (Syuv * Y'CbCr(unorm)) + dot(vecB, Offset)
   * where
   *        | Sy,   0,   0 |
   * Syuv = |  0, Suv,   0 |
   *        |  0    0, Suv |
   *
   *          | Oy  |
   * Offset = | Ouv |
   *          | Ouv |
   *
   * 4) YUV -> RGB matrix
   * | R' |            | Y'(unorm) |   | offsetA |
   * | G' | = Matrix * | Cb(unorm) | + | offsetB |
   * | B' |            | Cr(unorm) |   | offsetC |
   *
   * where
   *          | vecR |
   * Matrix = | vecG | * Syuv
   *          | vecB |
   *
   * offsetA = dot(vecR, Offset)
   * offsetB = dot(vecG, Offset)
   * offsetC = dot(vecB, Offset)
   *
   * 4) Consider 16-235 scale RGB
   * RGBfull(0..255) -> RGBfull(16..235) matrix is represented by
   * | Rs |      | Rf |   | Or |
   * | Gs | = Ms | Gf | + | Og |
   * | Bs |      | Bf |   | Ob |
   *
   * Combining all matrix into
   * | Rs |                   | Y'(unorm) |   | offsetA |     | Or |
   * | Gs | = Ms * ( Matrix * | Cb(unorm) | + | offsetB | ) + | Og |
   * | Bs |                   | Cr(unorm) |   | offsetC |     | Ob |
   *
   *                        | Y'(unorm) |      | offsetA |   | Or |
   *        = Ms * Matrix * | Cb(unorm) | + Ms | offsetB | + | Og |
   *                        | Cr(unorm) |      | offsetC |   | Ob |
   */

  memset (matrix, 0, sizeof (GstD3D12ColorMatrix));
  for (guint i = 0; i < 3; i++)
    matrix->max[i] = 1.0;

  gst_video_color_range_offsets (in_yuv_info->colorimetry.range,
      in_yuv_info->finfo, offset, scale);

  if (gst_video_color_matrix_get_Kr_Kb (in_yuv_info->colorimetry.matrix,
          &Kr, &Kb)) {
    guint S;
    gdouble Sy, Suv;
    gdouble Oy, Ouv;
    gdouble vecR[3], vecG[3], vecB[3];

    Kg = 1.0 - Kr - Kb;

    vecR[0] = 1.0;
    vecR[1] = 0;
    vecR[2] = 2 * (1 - Kr);

    vecG[0] = 1.0;
    vecG[1] = -(Kb / Kg) * 2 * (1 - Kb);
    vecG[2] = -(Kr / Kg) * 2 * (1 - Kr);

    vecB[0] = 1.0;
    vecB[1] = 2 * (1 - Kb);
    vecB[2] = 0;

    /* Assume all components has the same bitdepth */
    S = (1 << in_yuv_info->finfo->depth[0]) - 1;
    Sy = (gdouble) S / scale[0];
    Suv = (gdouble) S / scale[1];
    Oy = -((gdouble) offset[0] / scale[0]);
    Ouv = -((gdouble) offset[1] / scale[1]);

    matrix->matrix[0][0] = Sy * vecR[0];
    matrix->matrix[1][0] = Sy * vecG[0];
    matrix->matrix[2][0] = Sy * vecB[0];

    matrix->matrix[0][1] = Suv * vecR[1];
    matrix->matrix[1][1] = Suv * vecG[1];
    matrix->matrix[2][1] = Suv * vecB[1];

    matrix->matrix[0][2] = Suv * vecR[2];
    matrix->matrix[1][2] = Suv * vecG[2];
    matrix->matrix[2][2] = Suv * vecB[2];

    matrix->offset[0] = vecR[0] * Oy + vecR[1] * Ouv + vecR[2] * Ouv;
    matrix->offset[1] = vecG[0] * Oy + vecG[1] * Ouv + vecG[2] * Ouv;
    matrix->offset[2] = vecB[0] * Oy + vecB[1] * Ouv + vecB[2] * Ouv;

    /* Apply RGB range scale matrix */
    if (out_rgb_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_16_235) {
      GstD3D12ColorMatrix scale_matrix, rst;
      GstVideoInfo full_rgb = *out_rgb_info;

      full_rgb.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

      if (gst_d3d12_color_range_adjust_matrix_unorm (&full_rgb,
              out_rgb_info, &scale_matrix)) {
        /* Ms * Matrix */
        color_matrix_multiply (&rst, &scale_matrix, matrix);

        /* Ms * transform offsets */
        for (guint i = 0; i < 3; i++) {
          gdouble val = 0;
          for (guint j = 0; j < 3; j++) {
            val += scale_matrix.matrix[i][j] * matrix->offset[j];
          }
          rst.offset[i] = val + scale_matrix.offset[i];
        }

        /* copy back to output matrix */
        for (guint i = 0; i < 3; i++) {
          for (guint j = 0; j < 3; j++) {
            matrix->matrix[i][j] = rst.matrix[i][j];
          }
          matrix->offset[i] = rst.offset[i];
          matrix->min[i] = scale_matrix.min[i];
          matrix->max[i] = scale_matrix.max[i];
        }
      }
    }
  } else {
    /* Unknown matrix */
    matrix->matrix[0][0] = 1.0;
    matrix->matrix[1][1] = 1.0;
    matrix->matrix[2][2] = 1.0;
  }

  return TRUE;
}

/**
 * gst_d3d12_rgb_to_yuv_matrix_unorm:
 * @in_rgb_info: a #GstVideoInfo of input RGB signal
 * @out_yuv_info: a #GstVideoInfo of output YUV signal
 * @matrix: a #GstD3D12ColorMatrix
 *
 * Calculates transform matrix from RGB to YUV conversion. Both input and output
 * signals are in normalized [0.0..1.0] space and additional gamma decoding
 * or primary/transfer function transform is not performed by this matrix.
 *
 * Resulting RGB values can be calculated by
 * | Y' |                           | R' |   | matrix.offset[0] |
 * | Cb | = clamp ( matrix.matrix * | G' | + | matrix.offset[1] |, matrix.min, matrix.max )
 * | Cr |                           | B' |   | matrix.offset[2] |
 *
 * Returns: %TRUE if successful
 */
gboolean
gst_d3d12_rgb_to_yuv_matrix_unorm (const GstVideoInfo * in_rgb_info,
    const GstVideoInfo * out_yuv_info, GstD3D12ColorMatrix * matrix)
{
  gint offset[4], scale[4];
  gdouble Kr, Kb, Kg;

  g_return_val_if_fail (in_rgb_info != nullptr, FALSE);
  g_return_val_if_fail (out_yuv_info != nullptr, FALSE);
  g_return_val_if_fail (matrix != nullptr, FALSE);

  /*
   * <Formula>
   *
   * Input: Unsigned normalized non-linear R'G'B'(unorm), [0.0..1.0] range
   * Output: Unsigned normalized Y'CbCr(unorm), [0.0..1.0] range
   *
   * 1) R'G'B' to YPbPr
   * | Y  |      | R' |
   * | Pb | = M *| G' |
   * | Pr |      | B' |
   * where
   *     | vecY |
   * M = | vecU |
   *     | vecV |
   * vecY = |       Kr      ,       Kg      ,      Kb       |
   * vecU = | -0.5*Kr/(1-Kb), -0.5*Kg/(1-Kb),     0.5       |
   * vecV = |      0.5      , -0.5*Kg/(1-Kr), -0.5*Kb(1-Kr) |
   *
   * 2) YPbPr to Y'CbCr(unorm)
   * Y'(unorm) = (Y  * scaleY + offsetY)       / S
   * Cb(unorm) = (Pb * scaleCbCr + offsetCbCr) / S
   * Cr(unorm) = (Pr * scaleCbCr + offsetCbCr) / S
   * =>
   * Y'(unorm) = (Y  * scaleY    / S) + (offsetY    / S)
   * Cb(unorm) = (Pb * scaleCbCr / S) + (offsetCbCr / S)
   * Cr(unorm) = (Pb * scaleCbCr / S) + (offsetCbCr / S)
   * where S = (2 ^ bitdepth) - 1
   *
   * 3) RGB -> YUV matrix
   * | Y'(unorm) |            | R' |   | offsetA |
   * | Cb(unorm) | = Matrix * | G' | + | offsetB |
   * | Cr(unorm) |            | B' |   | offsetC |
   *
   * where
   *          | (scaleY/S)    * vecY |
   * Matrix = | (scaleCbCr/S) * vecU |
   *          | (scaleCbCr/S) * vecV |
   *
   * offsetA = offsetY    / S
   * offsetB = offsetCbCr / S
   * offsetC = offsetCbCr / S
   *
   * 4) Consider 16-235 scale RGB
   * RGBstudio(16..235) -> RGBfull(0..255) matrix is represented by
   * | Rf |      | Rs |   | Or |
   * | Gf | = Ms | Gs | + | Og |
   * | Bf |      | Bs |   | Ob |
   *
   * Combining all matrix into
   * | Y'(unorm) |                 | Rs |   | Or |     | offsetA |
   * | Cb(unorm) | = Matrix * ( Ms | Gs | + | Og | ) + | offsetB |
   * | Cr(unorm) |                 | Bs |   | Ob |     | offsetC |
   *
   *                             | Rs |          | Or |   | offsetA |
   *               = Matrix * Ms | Gs | + Matrix | Og | + | offsetB |
   *                             | Bs |          | Ob |   | offsetB |
   */

  memset (matrix, 0, sizeof (GstD3D12ColorMatrix));
  for (guint i = 0; i < 3; i++)
    matrix->max[i] = 1.0;

  gst_video_color_range_offsets (out_yuv_info->colorimetry.range,
      out_yuv_info->finfo, offset, scale);

  if (gst_video_color_matrix_get_Kr_Kb (out_yuv_info->colorimetry.matrix,
          &Kr, &Kb)) {
    guint S;
    gdouble Sy, Suv;
    gdouble Oy, Ouv;
    gdouble vecY[3], vecU[3], vecV[3];

    Kg = 1.0 - Kr - Kb;

    vecY[0] = Kr;
    vecY[1] = Kg;
    vecY[2] = Kb;

    vecU[0] = -0.5 * Kr / (1 - Kb);
    vecU[1] = -0.5 * Kg / (1 - Kb);
    vecU[2] = 0.5;

    vecV[0] = 0.5;
    vecV[1] = -0.5 * Kg / (1 - Kr);
    vecV[2] = -0.5 * Kb / (1 - Kr);

    /* Assume all components has the same bitdepth */
    S = (1 << out_yuv_info->finfo->depth[0]) - 1;
    Sy = (gdouble) scale[0] / S;
    Suv = (gdouble) scale[1] / S;
    Oy = (gdouble) offset[0] / S;
    Ouv = (gdouble) offset[1] / S;

    for (guint i = 0; i < 3; i++) {
      matrix->matrix[0][i] = Sy * vecY[i];
      matrix->matrix[1][i] = Suv * vecU[i];
      matrix->matrix[2][i] = Suv * vecV[i];
    }

    matrix->offset[0] = Oy;
    matrix->offset[1] = Ouv;
    matrix->offset[2] = Ouv;

    matrix->min[0] = Oy;
    matrix->min[1] = Oy;
    matrix->min[2] = Oy;

    matrix->max[0] = ((gdouble) scale[0] + offset[0]) / S;
    matrix->max[1] = ((gdouble) scale[1] + offset[0]) / S;
    matrix->max[2] = ((gdouble) scale[1] + offset[0]) / S;

    /* Apply RGB range scale matrix */
    if (in_rgb_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_16_235) {
      GstD3D12ColorMatrix scale_matrix, rst;
      GstVideoInfo full_rgb = *in_rgb_info;

      full_rgb.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

      if (gst_d3d12_color_range_adjust_matrix_unorm (in_rgb_info,
              &full_rgb, &scale_matrix)) {
        /* Matrix * Ms */
        color_matrix_multiply (&rst, matrix, &scale_matrix);

        /* Matrix * scale offsets */
        for (guint i = 0; i < 3; i++) {
          gdouble val = 0;
          for (guint j = 0; j < 3; j++) {
            val += matrix->matrix[i][j] * scale_matrix.offset[j];
          }
          rst.offset[i] = val + matrix->offset[i];
        }

        /* copy back to output matrix */
        for (guint i = 0; i < 3; i++) {
          for (guint j = 0; j < 3; j++) {
            matrix->matrix[i][j] = rst.matrix[i][j];
          }
          matrix->offset[i] = rst.offset[i];
        }
      }
    }
  } else {
    /* Unknown matrix */
    matrix->matrix[0][0] = 1.0;
    matrix->matrix[1][1] = 1.0;
    matrix->matrix[2][2] = 1.0;
  }

  return TRUE;
}

static gboolean
rgb_to_xyz_matrix (const GstVideoColorPrimariesInfo * info,
    GstD3D12ColorMatrix * matrix)
{
  GstD3D12ColorMatrix m, im;
  gdouble Sr, Sg, Sb;
  gdouble Xw, Yw, Zw;

  if (info->Rx == 0 || info->Gx == 0 || info->By == 0 || info->Wy == 0)
    return FALSE;

  color_matrix_identity (&m);

  m.matrix[0][0] = info->Rx / info->Ry;
  m.matrix[1][0] = 1.0;
  m.matrix[2][0] = (1.0 - info->Rx - info->Ry) / info->Ry;

  m.matrix[0][1] = info->Gx / info->Gy;
  m.matrix[1][1] = 1.0;
  m.matrix[2][1] = (1.0 - info->Gx - info->Gy) / info->Gy;

  m.matrix[0][2] = info->Bx / info->By;
  m.matrix[1][2] = 1.0;
  m.matrix[2][2] = (1.0 - info->Bx - info->By) / info->By;

  if (!color_matrix_invert (&im, &m))
    return FALSE;

  Xw = info->Wx / info->Wy;
  Yw = 1.0;
  Zw = (1.0 - info->Wx - info->Wy) / info->Wy;

  Sr = im.matrix[0][0] * Xw + im.matrix[0][1] * Yw + im.matrix[0][2] * Zw;
  Sg = im.matrix[1][0] * Xw + im.matrix[1][1] * Yw + im.matrix[1][2] * Zw;
  Sb = im.matrix[2][0] * Xw + im.matrix[2][1] * Yw + im.matrix[2][2] * Zw;

  for (guint i = 0; i < 3; i++) {
    m.matrix[i][0] *= Sr;
    m.matrix[i][1] *= Sg;
    m.matrix[i][2] *= Sb;
  }

  color_matrix_copy (matrix, &m);

  return TRUE;
}

/**
 * gst_d3d12_color_primaries_matrix_unorm:
 * @in_info: a #GstVideoColorPrimariesInfo of input signal
 * @out_info: a #GstVideoColorPrimariesInfo of output signal
 * @matrix: a #GstD3D12ColorMatrix
 *
 * Calculates color primaries conversion matrix
 *
 * Resulting RGB values can be calculated by
 * | Rout |                              | Rin |
 * | Gout | = saturate ( matrix.matrix * | Gin | )
 * | Bout |                              | Bin |
 *
 * Returns: %TRUE if successful
 */
gboolean
gst_d3d12_color_primaries_matrix_unorm (const GstVideoColorPrimariesInfo *
    in_info, const GstVideoColorPrimariesInfo * out_info,
    GstD3D12ColorMatrix * matrix)
{
  GstD3D12ColorMatrix Ms, invMd, ret;

  g_return_val_if_fail (in_info != nullptr, FALSE);
  g_return_val_if_fail (out_info != nullptr, FALSE);
  g_return_val_if_fail (matrix != nullptr, FALSE);

  /*
   * <Formula>
   *
   * 1) RGB -> XYZ conversion
   * | X |     | R |
   * | Y | = M | G |
   * | Z |     | B |
   * where
   *     | SrXr, SgXg, SbXb |
   * M = | SrYr, SgYg, SbYb |
   *     | SrZr, SgZg, SbZb |
   *
   * Xr = xr / yr
   * Yr = 1
   * Zr = (1 - xr - yr) / yr
   * xr and yr are xy coordinates of red primary in the CIE 1931 color space.
   * And its applied to G and B components
   *
   * | Sr |        | Xr, Xg, Xb |     | Xw |
   * | Sg | = inv( | Yr, Yg, Yb | ) * | Yw |
   * | Sb |        | Zr, Zg, Zb |     | Zw |
   *
   * 2) XYZsrc -> XYZdst conversion
   * Apply chromatic adaptation
   * | Xdst |      | Xsrc |
   * | Ydst | = Mc | Ysrc |
   * | Zdst |      | Zsrc |
   * where
   *      | Xwdst / Xwsrc,       0      ,       0       |
   * Mc = |       0      , Ywdst / Ywsrc,       0       |
   *      |       0      ,       0      , Zwdst / Zwsrc |
   *
   * where
   *
   * 3) Final matrix
   * | Rd |                      | Rs |
   * | Gd | = inv (Md) * Mc * Ms | Gs |
   * | Bd |                      | Bs |
   */

  memset (matrix, 0, sizeof (GstD3D12ColorMatrix));
  for (guint i = 0; i < 3; i++)
    matrix->max[i] = 1.0;

  if (!rgb_to_xyz_matrix (in_info, &Ms)) {
    GST_WARNING ("Failed to get src XYZ matrix");
    return FALSE;
  }

  if (!rgb_to_xyz_matrix (out_info, &invMd) ||
      !color_matrix_invert (&invMd, &invMd)) {
    GST_WARNING ("Failed to get dst XYZ matrix");
    return FALSE;
  }

  if (in_info->Wx != out_info->Wx || in_info->Wy != out_info->Wy) {
    GstD3D12ColorMatrix Mc;

    color_matrix_identity (&Mc);
    Mc.matrix[0][0] = (out_info->Wx / out_info->Wy) /
        (in_info->Wx / in_info->Wy);
    /* Yw == 1.0 */
    Mc.matrix[2][2] = ((1.0 - out_info->Wx - out_info->Wy) / out_info->Wy) /
        ((1.0 - in_info->Wx - in_info->Wy) / in_info->Wy);

    color_matrix_multiply (&ret, &Mc, &Ms);
  } else {
    color_matrix_copy (&ret, &Ms);
  }

  color_matrix_multiply (&ret, &invMd, &ret);
  color_matrix_copy (matrix, &ret);

  return TRUE;
}

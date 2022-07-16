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

GType
gst_d3d11_format_support_get_type (void)
{
  static gsize support_type = 0;
  static const GFlagsValue support_values[] = {
    {D3D11_FORMAT_SUPPORT_BUFFER, "BUFFER", "buffer"},
    {D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER, "IA_VERTEX_BUFFER",
        "ia-vertex-buffer"},
    {D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER, "IA_INDEX_BUFFER",
        "ia-index-buffer"},
    {D3D11_FORMAT_SUPPORT_SO_BUFFER, "SO_BUFFER", "so-buffer"},
    {D3D11_FORMAT_SUPPORT_TEXTURE1D, "TEXTURE1D", "texture1d"},
    {D3D11_FORMAT_SUPPORT_TEXTURE2D, "TEXTURE2D", "texture2d"},
    {D3D11_FORMAT_SUPPORT_TEXTURE3D, "TEXTURE3D", "texture3d"},
    {D3D11_FORMAT_SUPPORT_TEXTURECUBE, "TEXTURECUBE", "texturecube"},
    {D3D11_FORMAT_SUPPORT_SHADER_LOAD, "SHADER_LOAD", "shader-load"},
    {D3D11_FORMAT_SUPPORT_SHADER_SAMPLE, "SHADER_SAMPLE", "shader-sample"},
    {D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON, "SHADER_COMPARISION",
        "shader-comparision"},
    {D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_MONO_TEXT, "SHADER_SAMPLE_MONO_TEXT",
        "shader-sample-mono-text"},
    {D3D11_FORMAT_SUPPORT_MIP, "MIP", "mip"},
    {D3D11_FORMAT_SUPPORT_MIP_AUTOGEN, "MIP_AUTOGEN", "mip-autogen"},
    {D3D11_FORMAT_SUPPORT_RENDER_TARGET, "RENDER_TARGET", "render-target"},
    {D3D11_FORMAT_SUPPORT_BLENDABLE, "BLANDABLE", "blandable"},
    {D3D11_FORMAT_SUPPORT_DEPTH_STENCIL, "DEPTH_STENCIL", "depth-stencil"},
    {D3D11_FORMAT_SUPPORT_CPU_LOCKABLE, "CPU_LOCKABLE", "cpu-lockable"},
    {D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE, "MULTISAMPLE_RESOLVE",
        "multisample-resolve"},
    {D3D11_FORMAT_SUPPORT_DISPLAY, "DISPLAY", "display"},
    {D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT, "CAST_WITHIN_BIT_LAYOUT",
        "cast-within-bit-layout"},
    {D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET, "MULTISAMPLE_RENDERTARGET",
        "multisample-rendertarget"},
    {D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD, "MULTISAMPLE_LOAD",
        "multisample-load"},
    {D3D11_FORMAT_SUPPORT_SHADER_GATHER, "SHADER_GATHER", "shader-gether"},
    {D3D11_FORMAT_SUPPORT_BACK_BUFFER_CAST, "BACK_BUFFER_CAST",
        "back-buffer-cast"},
    {D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW, "UNORDERED_ACCESS_VIEW",
        "unordered-access-view"},
    {D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON, "SHADER_GATHER_COMPARISON",
        "shader-gether-comparision"},
    {D3D11_FORMAT_SUPPORT_DECODER_OUTPUT, "DECODER_OUTPUT", "decoder-output"},
    {D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT, "VIDEO_PROCESSOR_OUTPUT",
        "video-processor-output"},
    {D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT, "VIDEO_PROCESSOR_INPUT",
        "video-processor-input"},
    {D3D11_FORMAT_SUPPORT_VIDEO_ENCODER, "VIDEO_ENCODER", "video-encoder"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&support_type)) {
    GType tmp = g_flags_register_static ("GstD3D11FormatSupport",
        support_values);
    g_once_init_leave (&support_type, tmp);
  }

  return (GType) support_type;
}

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
 * Since: 1.22
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
 * Since: 1.22
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

/**
 * gst_d3d11_dxgi_format_to_string:
 * @format: a DXGI_FORMAT
 *
 * Converts @format enum value to its string representation
 *
 * Returns: a string representation of @format
 *
 * Since: 1.22
 */
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

/* Some values are not defined in old MinGW toolchain */
typedef enum
{
  GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 = 0,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709 = 2,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020 = 3,
  GST_DXGI_COLOR_SPACE_RESERVED = 4,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601 = 5,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 = 6,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 = 7,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 = 8,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 = 9,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020 = 10,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 = 11,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020 = 13,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 = 14,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020 = 15,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 = 16,
  GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 = 17,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 = 18,
  GST_DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 = 19,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709 = 20,
  GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020 = 21,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709 = 22,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020 = 23,
  GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020 = 24,
} GST_DXGI_COLOR_SPACE_TYPE;

static gboolean
rgb_to_colorspace (const GstVideoColorimetry * cinfo,
    DXGI_COLOR_SPACE_TYPE * color_space)
{
  /* sRGB */
  GST_DXGI_COLOR_SPACE_TYPE type = GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

  /* Defined DXGI RGB colorspace
   * 1) DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 = 0 (sRGB)
   * 2) DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 = 1 (scRGB)
   * 3) DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709 = 2 (BT601/BT709 studio range)
   * 4) DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020 = 3 (BT2020 SDR studio range)
   * 5) DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12 (HDR10 full range)
   * 6) DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 = 13 (HDR10 studio range)
   * 7) DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020 = 17 (BT2020 SDR fullrange)
   * 8) DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709 = 20 (unused)
   * 9) DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020 = 21 (unused)
   *
   * Note that GStreamer does not define gamma2.4. So, 8) and 9) are excluded
   */
  if (cinfo->transfer == GST_VIDEO_TRANSFER_GAMMA10) {
    type = GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    goto done;
  }

  /* HLG RGB colorspace is not defined, approximated to HDR10 */
  if (cinfo->transfer == GST_VIDEO_TRANSFER_SMPTE2084 ||
      cinfo->transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67) {
    if (cinfo->range == GST_VIDEO_COLOR_RANGE_16_235) {
      type = GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020;
    } else {
      type = GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }
    goto done;
  }

  if (cinfo->primaries == GST_VIDEO_COLOR_PRIMARIES_BT2020) {
    if (cinfo->range == GST_VIDEO_COLOR_RANGE_16_235) {
      type = GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020;
    } else {
      type = GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
    }
    goto done;
  }

  if (cinfo->range == GST_VIDEO_COLOR_RANGE_16_235)
    type = GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709;

done:
  *color_space = (DXGI_COLOR_SPACE_TYPE) type;

  return TRUE;
}

static gboolean
yuv_to_colorspace (const GstVideoColorimetry * cinfo,
    GstVideoChromaSite chroma_site, DXGI_COLOR_SPACE_TYPE * color_space)
{
  /* BT709 */
  GST_DXGI_COLOR_SPACE_TYPE type =
      GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;

  /* Defined DXGI RGB colorspace
     * 1) DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601 = 5 (common JPG)
     * 2) DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601 = 6 (BT601 studio range)
     * 3) DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 = 7 (BT601 full range)
     * 4) DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 = 8 (BT709 studio range)
     * 5) DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 = 9 (BT709 full range)
     * 6) DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020 = 10 (BT2020 4:2:0 studio range)
     * 7) DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 = 11 (BT2020 full range)
     * 8) DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020 = 13 (HDR10 4:2:0 studio range)
     * 9) DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020 = 15 (BT2020 4:2:2 or 4:4:4: studio range)
     * 10) DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 = 16 (HDR10 4:2:2 or 4:4:4 studio range)
     * 11) DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 = 18 (HLG studio range)
     * 12) DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 = 19 (HLG full range)
     * 13) DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709 = 22 (unused)
     * 14) DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020 = 23 (unused)
     * 15) DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020 = 24 (unused)
     *
     * Note that GStreamer does not define gamma2.4. So, 13) ~ 15) are excluded
   */

  /* HLG */
  if (cinfo->transfer == GST_VIDEO_TRANSFER_ARIB_STD_B67) {
    if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255) {
      type = GST_DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020;
    } else {
      type = GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020;
    }
    goto done;
  }

  /* HDR10 */
  if (cinfo->transfer == GST_VIDEO_TRANSFER_SMPTE2084) {
    if (chroma_site == GST_VIDEO_CHROMA_SITE_H_COSITED) {
      type = GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020;
    } else {
      type = GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020;
    }
    goto done;
  }

  /* BT2020 */
  if (cinfo->primaries == GST_VIDEO_COLOR_PRIMARIES_BT2020) {
    if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255) {
      type = GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020;
    } else if (chroma_site == GST_VIDEO_CHROMA_SITE_H_COSITED) {
      type = GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
    } else {
      type = GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020;
    }
    goto done;
  }

  /* BT601/BT709 primaries are similar. Depends on RGB matrix */
  if (cinfo->matrix == GST_VIDEO_COLOR_MATRIX_BT601) {
    if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255) {
      if (cinfo->primaries == GST_VIDEO_COLOR_PRIMARIES_BT709) {
        type = GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601;
      } else {
        type = GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601;
      }
    } else {
      type = GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
    }
    goto done;
  }

  if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255)
    type = GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709;

done:
  *color_space = (DXGI_COLOR_SPACE_TYPE) type;

  return TRUE;
}

/**
 * gst_video_info_to_dxgi_color_space:
 * @info: a #GstVideoInfo
 * @color_space: (out): DXGI color space
 *
 * Derives DXGI_COLOR_SPACE_TYPE from @info
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.22
 */
gboolean
gst_video_info_to_dxgi_color_space (const GstVideoInfo * info,
    DXGI_COLOR_SPACE_TYPE * color_space)
{
  const GstVideoColorimetry *cinfo;
  GstVideoColorimetry c;

  g_return_val_if_fail (info != nullptr, FALSE);
  g_return_val_if_fail (color_space != nullptr, FALSE);

  cinfo = &info->colorimetry;

  if (GST_VIDEO_INFO_IS_RGB (info)) {
    /* ensure RGB matrix if format is already RGB */
    c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
  } else if (GST_VIDEO_INFO_IS_YUV (info) &&
      cinfo->matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
    /* Invalid matrix */
    c.matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
  } else {
    c.matrix = cinfo->matrix;
  }

  switch (cinfo->range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      break;
    case GST_VIDEO_COLOR_RANGE_16_235:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      break;
    default:
      if (c.matrix == GST_VIDEO_COLOR_MATRIX_RGB)
        c.range = GST_VIDEO_COLOR_RANGE_0_255;
      else
        c.range = GST_VIDEO_COLOR_RANGE_16_235;
      break;
  }

  /* DXGI primaries: BT601, BT709, BT2020 */
  switch (cinfo->primaries) {
    case GST_VIDEO_COLOR_PRIMARIES_BT2020:
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M:
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE240M:
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      break;
    default:
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
  }

  /* DXGI gamma functions: linear (RGB only), gamma2.2, PQ, and HLG */
  switch (cinfo->transfer) {
    case GST_VIDEO_TRANSFER_SMPTE2084:
      c.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      break;
    case GST_VIDEO_TRANSFER_ARIB_STD_B67:
      c.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
      break;
    case GST_VIDEO_TRANSFER_GAMMA10:
      /* Only DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 supports linear gamma */
      if (c.matrix == GST_VIDEO_COLOR_MATRIX_RGB) {
        c.transfer = GST_VIDEO_TRANSFER_GAMMA10;
        c.range = GST_VIDEO_COLOR_RANGE_0_255;
      } else {
        c.transfer = GST_VIDEO_TRANSFER_GAMMA22;
      }
      break;
    default:
      /* Simply map the rest of values to gamma 2.2. We don't have any other
       * choice */
      c.transfer = GST_VIDEO_TRANSFER_GAMMA22;
      break;
  }

  /* DXGI transform matrix: BT601, BT709, and BT2020 */
  switch (c.matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      break;
    case GST_VIDEO_COLOR_MATRIX_FCC:
    case GST_VIDEO_COLOR_MATRIX_BT601:
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      break;
    default:
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      break;
  }

  if (c.matrix == GST_VIDEO_COLOR_MATRIX_RGB)
    return rgb_to_colorspace (&c, color_space);

  return yuv_to_colorspace (&c, info->chroma_site, color_space);
}

static gboolean
dxgi_color_space_is_rgb (GST_DXGI_COLOR_SPACE_TYPE color_space)
{
  switch (color_space) {
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
      return TRUE;
    default:
      break;
  }

  return FALSE;
}

/**
 * gst_video_info_apply_dxgi_color_space:
 * @color_space: DXGI color space
 * @info: (inout): a #GstVideoInfo
 *
 * Updates color information of @info using @color_space
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.22
 */
gboolean
gst_video_info_apply_dxgi_color_space (DXGI_COLOR_SPACE_TYPE color_space,
    GstVideoInfo * info)
{
  GST_DXGI_COLOR_SPACE_TYPE type;
  GstVideoColorimetry c;

  g_return_val_if_fail (info != nullptr, FALSE);

  type = (GST_DXGI_COLOR_SPACE_TYPE) color_space;

  if (GST_VIDEO_INFO_IS_RGB (info) && !dxgi_color_space_is_rgb (type)) {
    GST_WARNING ("Invalid DXGI color space mapping");
    return FALSE;
  }

  switch (type) {
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      c.transfer = GST_VIDEO_TRANSFER_SRGB;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      c.transfer = GST_VIDEO_TRANSFER_GAMMA10;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      c.transfer = GST_VIDEO_TRANSFER_BT709;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) >= 12)
        c.transfer = GST_VIDEO_TRANSFER_BT2020_12;
      else
        c.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RESERVED:
      GST_WARNING ("Reserved color space");
      return FALSE;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      c.transfer = GST_VIDEO_TRANSFER_BT601;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      c.transfer = GST_VIDEO_TRANSFER_BT601;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      c.transfer = GST_VIDEO_TRANSFER_BT601;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      c.transfer = GST_VIDEO_TRANSFER_BT709;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      c.transfer = GST_VIDEO_TRANSFER_BT709;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) >= 12)
        c.transfer = GST_VIDEO_TRANSFER_BT2020_12;
      else
        c.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) >= 12)
        c.transfer = GST_VIDEO_TRANSFER_BT2020_12;
      else
        c.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      c.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      c.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      c.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) >= 12)
        c.transfer = GST_VIDEO_TRANSFER_BT2020_12;
      else
        c.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      c.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) >= 12)
        c.transfer = GST_VIDEO_TRANSFER_BT2020_12;
      else
        c.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_16_235;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      c.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
      c.range = GST_VIDEO_COLOR_RANGE_0_255;
      c.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      c.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
      c.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    default:
      GST_WARNING ("Unknown DXGI color space %d", type);
      return FALSE;
  }

  info->colorimetry = c;

  return TRUE;
}

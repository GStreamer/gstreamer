/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_format_debug);
#define GST_CAT_DEFAULT gst_d3d11_format_debug

guint
gst_d3d11_dxgi_format_n_planes (DXGI_FORMAT format)
{
  switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
      return 1;
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
      return 2;
    default:
      break;
  }

  return 0;
}

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
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16G16_UNORM:
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
 * gst_d3d11_device_get_supported_caps:
 * @device: a #GstD3DDevice
 * @flags: D3D11_FORMAT_SUPPORT flags
 *
 * Check supported format with given flags
 *
 * Returns: a #GstCaps representing supported format
 */
GstCaps *
gst_d3d11_device_get_supported_caps (GstD3D11Device * device,
    D3D11_FORMAT_SUPPORT flags)
{
  ID3D11Device *d3d11_device;
  HRESULT hr;
  gint i;
  GValue v_list = G_VALUE_INIT;
  GstCaps *supported_caps;
  static const GstVideoFormat format_list[] = {
    GST_VIDEO_FORMAT_BGRA,
    GST_VIDEO_FORMAT_RGBA,
    GST_VIDEO_FORMAT_RGB10A2_LE,
    GST_VIDEO_FORMAT_VUYA,
    GST_VIDEO_FORMAT_NV12,
    GST_VIDEO_FORMAT_P010_10LE,
    GST_VIDEO_FORMAT_P016_LE,
    GST_VIDEO_FORMAT_I420,
    GST_VIDEO_FORMAT_I420_10LE,
  };

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  d3d11_device = gst_d3d11_device_get_device_handle (device);
  g_value_init (&v_list, GST_TYPE_LIST);

  for (i = 0; i < G_N_ELEMENTS (format_list); i++) {
    UINT format_support = 0;
    GstVideoFormat format;
    const GstD3D11Format *d3d11_format;

    d3d11_format = gst_d3d11_device_format_from_gst (device, format_list[i]);
    if (!d3d11_format || d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN)
      continue;

    format = d3d11_format->format;
    hr = ID3D11Device_CheckFormatSupport (d3d11_device,
        d3d11_format->dxgi_format, &format_support);

    if (SUCCEEDED (hr) && ((format_support & flags) == flags)) {
      GValue v_str = G_VALUE_INIT;
      g_value_init (&v_str, G_TYPE_STRING);

      GST_LOG_OBJECT (device, "d3d11 device can support %s with flags 0x%x",
          gst_video_format_to_string (format), flags);
      g_value_set_string (&v_str, gst_video_format_to_string (format));
      gst_value_list_append_and_take_value (&v_list, &v_str);
    }
  }

  supported_caps = gst_caps_new_simple ("video/x-raw",
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  gst_caps_set_value (supported_caps, "format", &v_list);
  g_value_unset (&v_list);

  gst_caps_set_features_simple (supported_caps,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY));

  return supported_caps;
}

#if (DXGI_HEADER_VERSION >= 5)
gboolean
gst_d3d11_hdr_meta_data_to_dxgi (GstVideoMasteringDisplayInfo * minfo,
    GstVideoContentLightLevel * cll, DXGI_HDR_METADATA_HDR10 * dxgi_hdr10)
{
  g_return_val_if_fail (dxgi_hdr10 != NULL, FALSE);

  memset (dxgi_hdr10, 0, sizeof (DXGI_HDR_METADATA_HDR10));

  if (minfo) {
    dxgi_hdr10->RedPrimary[0] = minfo->display_primaries[0].x;
    dxgi_hdr10->RedPrimary[1] = minfo->display_primaries[0].y;
    dxgi_hdr10->GreenPrimary[0] = minfo->display_primaries[1].x;
    dxgi_hdr10->GreenPrimary[1] = minfo->display_primaries[1].y;
    dxgi_hdr10->BluePrimary[0] = minfo->display_primaries[2].x;
    dxgi_hdr10->BluePrimary[1] = minfo->display_primaries[2].y;

    dxgi_hdr10->WhitePoint[0] = minfo->white_point.x;
    dxgi_hdr10->WhitePoint[1] = minfo->white_point.y;
    dxgi_hdr10->MaxMasteringLuminance = minfo->max_display_mastering_luminance;
    dxgi_hdr10->MinMasteringLuminance = minfo->min_display_mastering_luminance;
  }

  if (cll) {
    dxgi_hdr10->MaxContentLightLevel = cll->max_content_light_level;
    dxgi_hdr10->MaxFrameAverageLightLevel = cll->max_frame_average_light_level;
  }

  return TRUE;
}
#endif

#if (DXGI_HEADER_VERSION >= 4)
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
  GST_DXGI_COLOR_SPACE_CUSTOM = 0xFFFFFFFF
} GST_DXGI_COLOR_SPACE_TYPE;

/* https://docs.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type */

#define MAKE_COLOR_MAP(d,r,m,t,p) \
  { GST_DXGI_COLOR_SPACE_ ##d, GST_VIDEO_COLOR_RANGE ##r, \
    GST_VIDEO_COLOR_MATRIX_ ##m, GST_VIDEO_TRANSFER_ ##t, \
    GST_VIDEO_COLOR_PRIMARIES_ ##p }

static const GstDxgiColorSpace rgb_colorspace_map[] = {
  /* RGB_FULL_G22_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT709, BT709),

  /* RGB_FULL_G10_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, GAMMA10, BT709),

  /* RGB_STUDIO_G22_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _16_235, UNKNOWN, BT709, BT709),

  /* RGB_STUDIO_G22_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _16_235, UNKNOWN, BT2020_10, BT2020),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _16_235, UNKNOWN, BT2020_12, BT2020),

  /* RGB_FULL_G2084_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_FULL_G2084_NONE_P2020, _0_255, UNKNOWN, SMPTE2084,
      BT2020),

  /* RGB_STUDIO_G2084_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_STUDIO_G2084_NONE_P2020,
      _16_235, UNKNOWN, SMPTE2084, BT2020),

  /* RGB_FULL_G22_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, BT2020_10, BT2020),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, BT2020_12, BT2020),

  /* RGB_STUDIO_G24_NONE_P709 */
  MAKE_COLOR_MAP (RGB_STUDIO_G24_NONE_P709, _16_235, UNKNOWN, SRGB, BT709),

  /* RGB_STUDIO_G24_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_STUDIO_G24_NONE_P709, _16_235, UNKNOWN, SRGB, BT2020),
};

static const GstDxgiColorSpace yuv_colorspace_map[] = {
  /* YCBCR_FULL_G22_NONE_P709_X601 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT709, BT709),

  /* YCBCR_STUDIO_G22_LEFT_P601 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT601, SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT709, SMPTE170M),

  /* YCBCR_FULL_G22_LEFT_P601 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT601, SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT709, SMPTE170M),

  /* YCBCR_STUDIO_G22_LEFT_P709 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT709, BT709),

  /* YCBCR_FULL_G22_LEFT_P709 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT709, BT709),

  /* YCBCR_STUDIO_G22_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P2020, _16_235, BT2020, BT2020_10,
      BT2020),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P2020, _16_235, BT2020, BT2020_12,
      BT2020),

  /* YCBCR_FULL_G22_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P2020, _0_255, BT2020, BT2020_10, BT2020),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P2020, _0_255, BT2020, BT2020_12, BT2020),

  /* YCBCR_STUDIO_G2084_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G2084_LEFT_P2020, _16_235, BT2020, SMPTE2084,
      BT2020),

  /* YCBCR_STUDIO_G22_TOPLEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_TOPLEFT_P2020, _16_235, BT2020, BT2020_10,
      BT2020),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_TOPLEFT_P2020, _16_235, BT2020, BT2020_12,
      BT2020),

  /* YCBCR_STUDIO_G2084_TOPLEFT_P2020 */
  /* FIXME: check chroma-site to differentiate this from
   * YCBCR_STUDIO_G2084_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G2084_TOPLEFT_P2020, _16_235, BT2020, SMPTE2084,
      BT2020),

  /* YCBCR_STUDIO_GHLG_TOPLEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_GHLG_TOPLEFT_P2020, _16_235, BT2020,
      ARIB_STD_B67, BT2020),

  /* YCBCR_STUDIO_GHLG_TOPLEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_FULL_GHLG_TOPLEFT_P2020, _0_255, BT2020, ARIB_STD_B67,
      BT2020),

  /* YCBCR_STUDIO_G24_LEFT_P709 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, SRGB, BT709),

  /* YCBCR_STUDIO_G24_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G24_LEFT_P2020, _16_235, BT2020, SRGB, BT2020),

  /* YCBCR_STUDIO_G24_TOPLEFT_P2020 */
  /* FIXME: check chroma-site to differentiate this from
   * YCBCR_STUDIO_G24_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G24_TOPLEFT_P2020, _16_235, BT2020, SRGB,
      BT2020),
};

#define SCORE_RANGE_MISMATCH 1
#define SCORE_MATRIX_MISMATCH 5
#define SCORE_TRANSFER_MISMATCH 5
#define SCORE_PRIMARY_MISMATCH 10

static gint
get_score (GstVideoInfo * info, const GstDxgiColorSpace * color_map,
    gboolean is_yuv)
{
  gint loss = 0;
  GstVideoColorimetry *color = &info->colorimetry;

  if (color->range != color_map->range)
    loss += SCORE_RANGE_MISMATCH;

  if (is_yuv && color->matrix != color_map->matrix)
    loss += SCORE_MATRIX_MISMATCH;

  if (color->transfer != color_map->transfer)
    loss += SCORE_TRANSFER_MISMATCH;

  if (color->primaries != color_map->primaries)
    loss += SCORE_PRIMARY_MISMATCH;

  return loss;
}

static const GstDxgiColorSpace *
gst_d3d11_video_info_to_dxgi_color_space_rgb (GstVideoInfo * info)
{
  gint best_score = G_MAXINT;
  gint score, i;
  const GstDxgiColorSpace *colorspace = NULL;

  for (i = 0; i < G_N_ELEMENTS (rgb_colorspace_map); i++) {
    score = get_score (info, &rgb_colorspace_map[i], TRUE);

    if (score < best_score) {
      best_score = score;
      colorspace = &rgb_colorspace_map[i];

      if (score == 0)
        break;
    }
  }

  return colorspace;
}

static const GstDxgiColorSpace *
gst_d3d11_video_info_to_dxgi_color_space_yuv (GstVideoInfo * info)
{
  gint best_score = G_MAXINT;
  gint score, i;
  const GstDxgiColorSpace *colorspace = NULL;

  for (i = 0; i < G_N_ELEMENTS (yuv_colorspace_map); i++) {
    score = get_score (info, &yuv_colorspace_map[i], TRUE);

    if (score < best_score) {
      best_score = score;
      colorspace = &yuv_colorspace_map[i];

      if (score == 0)
        break;
    }
  }

  return colorspace;
}

const GstDxgiColorSpace *
gst_d3d11_video_info_to_dxgi_color_space (GstVideoInfo * info)
{
  g_return_val_if_fail (info != NULL, NULL);

  if (GST_VIDEO_INFO_IS_RGB (info)) {
    return gst_d3d11_video_info_to_dxgi_color_space_rgb (info);
  } else if (GST_VIDEO_INFO_IS_YUV (info)) {
    return gst_d3d11_video_info_to_dxgi_color_space_yuv (info);
  }

  return NULL;
}

const GstDxgiColorSpace *
gst_d3d11_find_swap_chain_color_space (GstVideoInfo * info,
    IDXGISwapChain3 * swapchain, gboolean use_hdr10)
{
  const GstDxgiColorSpace *colorspace = NULL;
  gint best_score = G_MAXINT;
  gint i;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (swapchain != NULL, FALSE);

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    GST_WARNING ("Swapchain colorspace should be RGB format");
    return FALSE;
  }

  for (i = 0; i < G_N_ELEMENTS (rgb_colorspace_map); i++) {
    UINT can_support = 0;
    HRESULT hr;
    gint score;
    GST_DXGI_COLOR_SPACE_TYPE cur_type =
        rgb_colorspace_map[i].dxgi_color_space_type;

    /* FIXME: Non-HDR colorspace with BT2020 primaries will break rendering.
     * https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/1175
     * To workaround it, BT709 colorspace will be chosen for non-HDR case.
     */
    if (!use_hdr10 &&
        rgb_colorspace_map[i].primaries == GST_VIDEO_COLOR_PRIMARIES_BT2020)
      continue;

    hr = IDXGISwapChain3_CheckColorSpaceSupport (swapchain,
        cur_type, &can_support);

    if (FAILED (hr))
      continue;

    if ((can_support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ==
        DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) {
      score = get_score (info, &rgb_colorspace_map[i], FALSE);

      GST_DEBUG ("colorspace %d supported, score %d", cur_type, score);

      if (score < best_score) {
        best_score = score;
        colorspace = &rgb_colorspace_map[i];
      }
    }
  }

  return colorspace;
}

#endif

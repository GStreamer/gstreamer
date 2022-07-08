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

#include "gstd3d11pluginutils.h"

#include <windows.h>
#include <versionhelpers.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_plugin_utils_debug);
#define GST_CAT_DEFAULT gst_d3d11_plugin_utils_debug

/* Max Texture Dimension for feature level 11_0 ~ 12_1 */
static guint _gst_d3d11_texture_max_dimension = 16384;

void
gst_d3d11_plugin_utils_init (D3D_FEATURE_LEVEL feature_level)
{
  static gsize _init_once = 0;

  if (g_once_init_enter (&_init_once)) {
    /* https://docs.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro */
    if (feature_level >= D3D_FEATURE_LEVEL_11_0)
      _gst_d3d11_texture_max_dimension = 16384;
    else if (feature_level >= D3D_FEATURE_LEVEL_10_0)
      _gst_d3d11_texture_max_dimension = 8192;
    else
      _gst_d3d11_texture_max_dimension = 4096;

    g_once_init_leave (&_init_once, 1);
  }
}

GstCaps *
gst_d3d11_get_updated_template_caps (GstStaticCaps * template_caps)
{
  GstCaps *caps;

  g_return_val_if_fail (template_caps != NULL, NULL);

  caps = gst_static_caps_get (template_caps);
  if (!caps) {
    GST_ERROR ("Couldn't get caps from static caps");
    return NULL;
  }

  caps = gst_caps_make_writable (caps);
  gst_caps_set_simple (caps,
      "width", GST_TYPE_INT_RANGE, 1, _gst_d3d11_texture_max_dimension,
      "height", GST_TYPE_INT_RANGE, 1, _gst_d3d11_texture_max_dimension, NULL);

  return caps;
}

gboolean
gst_d3d11_is_windows_8_or_greater (void)
{
  static gsize version_once = 0;
  static gboolean ret = FALSE;

  if (g_once_init_enter (&version_once)) {
#if (!GST_D3D11_WINAPI_ONLY_APP)
    if (IsWindows8OrGreater ())
      ret = TRUE;
#else
    ret = TRUE;
#endif

    g_once_init_leave (&version_once, 1);
  }

  return ret;
}

GstD3D11DeviceVendor
gst_d3d11_get_device_vendor (GstD3D11Device * device)
{
  guint device_id = 0;
  guint vendor_id = 0;
  gchar *desc = NULL;
  GstD3D11DeviceVendor vendor = GST_D3D11_DEVICE_VENDOR_UNKNOWN;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device),
      GST_D3D11_DEVICE_VENDOR_UNKNOWN);

  g_object_get (device, "device-id", &device_id, "vendor-id", &vendor_id,
      "description", &desc, NULL);

  switch (vendor_id) {
    case 0:
      if (device_id == 0 && desc && g_strrstr (desc, "SraKmd"))
        vendor = GST_D3D11_DEVICE_VENDOR_XBOX;
      break;
    case 0x1002:
    case 0x1022:
      vendor = GST_D3D11_DEVICE_VENDOR_AMD;
      break;
    case 0x8086:
      vendor = GST_D3D11_DEVICE_VENDOR_INTEL;
      break;
    case 0x10de:
      vendor = GST_D3D11_DEVICE_VENDOR_NVIDIA;
      break;
    case 0x4d4f4351:
      vendor = GST_D3D11_DEVICE_VENDOR_QUALCOMM;
      break;
    default:
      break;
  }

  g_free (desc);

  return vendor;
}

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
  /* 1) DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709
   * 2) DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
   * 3) DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709
   * 4) DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020
   * 5) DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
   * 6) DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020
   * 7) DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020
   * 8) DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709
   * 9) DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020
   *
   * NOTE: if G24 (Gamma 2.4, SRGB) transfer is not defined,
   * it will be approximated as G22.
   * NOTE: BT470BG ~= BT709
   */

  /* 1) RGB_FULL_G22_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, SRGB, BT709),
  /* 1-1) Approximation for RGB_FULL_G22_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT709, BT709),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT601, BT709),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT2020_10, BT709),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT2020_12, BT709),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, SRGB, BT470BG),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT709, BT470BG),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT601, BT470BG),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT2020_10, BT470BG),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P709, _0_255, UNKNOWN, BT2020_12, BT470BG),

  /* 2) RGB_FULL_G10_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G10_NONE_P709, _0_255, UNKNOWN, GAMMA10, BT709),
  /* 2-1 ) Approximation for RGB_FULL_G10_NONE_P709 */
  MAKE_COLOR_MAP (RGB_FULL_G10_NONE_P709, _0_255, UNKNOWN, GAMMA10, BT470BG),

  /* 3) RGB_STUDIO_G22_NONE_P709 */
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT709, BT709),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT601, BT709),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT2020_10, BT709),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT2020_12, BT709),
  /* 3-1) Approximation for RGB_STUDIO_G22_NONE_P709 */
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT709, BT470BG),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT601, BT470BG),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT2020_10,
      BT470BG),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, BT2020_12,
      BT470BG),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, SRGB, BT709),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P709, _16_235, UNKNOWN, SRGB, BT470BG),

  /* 4) RGB_STUDIO_G22_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P2020, _16_235, UNKNOWN, BT709, BT2020),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P2020, _16_235, UNKNOWN, BT601, BT2020),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P2020, _16_235, UNKNOWN, BT2020_10,
      BT2020),
  MAKE_COLOR_MAP (RGB_STUDIO_G22_NONE_P2020, _16_235, UNKNOWN, BT2020_12,
      BT2020),

  /* 5) RGB_FULL_G2084_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_FULL_G2084_NONE_P2020, _0_255, UNKNOWN, SMPTE2084,
      BT2020),

  /* 6) RGB_STUDIO_G2084_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_STUDIO_G2084_NONE_P2020, _16_235, UNKNOWN, SMPTE2084,
      BT2020),

  /* 7) RGB_FULL_G22_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, BT709, BT2020),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, BT601, BT2020),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, BT2020_10, BT2020),
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, BT2020_12, BT2020),
  /* 7-1) Approximation for RGB_FULL_G22_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_FULL_G22_NONE_P2020, _0_255, UNKNOWN, SRGB, BT2020),

  /* 8) RGB_STUDIO_G24_NONE_P709 */
  MAKE_COLOR_MAP (RGB_STUDIO_G24_NONE_P709, _16_235, UNKNOWN, SRGB, BT709),
  /* 8-1) Approximation for RGB_STUDIO_G24_NONE_P709 */
  MAKE_COLOR_MAP (RGB_STUDIO_G24_NONE_P709, _16_235, UNKNOWN, SRGB, BT470BG),

  /* 9) RGB_STUDIO_G24_NONE_P2020 */
  MAKE_COLOR_MAP (RGB_STUDIO_G24_NONE_P2020, _16_235, UNKNOWN, SRGB, BT2020),
};

static const GstDxgiColorSpace yuv_colorspace_map[] = {
  /* 1) YCBCR_FULL_G22_NONE_P709_X601
   * 2) YCBCR_STUDIO_G22_LEFT_P601
   * 3) YCBCR_FULL_G22_LEFT_P601
   * 4) YCBCR_STUDIO_G22_LEFT_P709
   * 5) YCBCR_FULL_G22_LEFT_P709
   * 6) YCBCR_STUDIO_G22_LEFT_P2020
   * 7) YCBCR_FULL_G22_LEFT_P2020
   * 8) YCBCR_STUDIO_G2084_LEFT_P2020
   * 9) YCBCR_STUDIO_G22_TOPLEFT_P2020
   * 10) YCBCR_STUDIO_G2084_TOPLEFT_P2020
   * 11) YCBCR_STUDIO_GHLG_TOPLEFT_P2020
   * 12) YCBCR_FULL_GHLG_TOPLEFT_P2020
   * 13) YCBCR_STUDIO_G24_LEFT_P709
   * 14) YCBCR_STUDIO_G24_LEFT_P2020
   * 15) YCBCR_STUDIO_G24_TOPLEFT_P2020
   *
   * NOTE: BT470BG ~= BT709
   */

  /* 1) YCBCR_FULL_G22_NONE_P709_X601 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT709, BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT601, BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT2020_10,
      BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT2020_12,
      BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT709, BT470BG),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT601, BT470BG),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT2020_10,
      BT470BG),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_NONE_P709_X601, _0_255, BT601, BT2020_12,
      BT470BG),

  /* 2) YCBCR_STUDIO_G22_LEFT_P601 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT601, SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT709, SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT2020_10,
      SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT2020_12,
      SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT601, SMPTE240M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT709, SMPTE240M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT2020_10,
      SMPTE240M),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P601, _16_235, BT601, BT2020_12,
      SMPTE240M),

  /* 3) YCBCR_FULL_G22_LEFT_P601 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT601, SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT709, SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT2020_10,
      SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT2020_12,
      SMPTE170M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT601, SMPTE240M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT709, SMPTE240M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT2020_10,
      SMPTE240M),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P601, _0_255, BT601, BT2020_12,
      SMPTE240M),

  /* 4) YCBCR_STUDIO_G22_LEFT_P709 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT709, BT709),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT601, BT709),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT2020_10,
      BT709),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT2020_12,
      BT709),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT709, BT470BG),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT601, BT470BG),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT2020_10,
      BT470BG),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P709, _16_235, BT709, BT2020_12,
      BT470BG),

  /* 5) YCBCR_FULL_G22_LEFT_P709 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT709, BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT601, BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT2020_10, BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT2020_12, BT709),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT709, BT470BG),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT601, BT470BG),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT2020_10, BT470BG),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P709, _0_255, BT709, BT2020_12, BT470BG),

  /* 6) YCBCR_STUDIO_G22_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P2020, _16_235, BT2020, BT709, BT2020),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P2020, _16_235, BT2020, BT601, BT2020),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P2020, _16_235, BT2020, BT2020_10,
      BT2020),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_LEFT_P2020, _16_235, BT2020, BT2020_12,
      BT2020),

  /* 7) YCBCR_FULL_G22_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P2020, _0_255, BT2020, BT709, BT2020),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P2020, _0_255, BT2020, BT601, BT2020),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P2020, _0_255, BT2020, BT2020_10,
      BT2020),
  MAKE_COLOR_MAP (YCBCR_FULL_G22_LEFT_P2020, _0_255, BT2020, BT2020_12,
      BT2020),

  /* 8) YCBCR_STUDIO_G2084_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G2084_LEFT_P2020, _16_235, BT2020, SMPTE2084,
      BT2020),

  /* 9) YCBCR_STUDIO_G22_TOPLEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_TOPLEFT_P2020, _16_235, BT2020, BT2020_10,
      BT2020),
  MAKE_COLOR_MAP (YCBCR_STUDIO_G22_TOPLEFT_P2020, _16_235, BT2020, BT2020_12,
      BT2020),

  /* 10) YCBCR_STUDIO_G2084_TOPLEFT_P2020 */
  /* FIXME: check chroma-site to differentiate this from
   * YCBCR_STUDIO_G2084_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G2084_TOPLEFT_P2020, _16_235, BT2020, SMPTE2084,
      BT2020),

  /* 11) YCBCR_STUDIO_GHLG_TOPLEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_GHLG_TOPLEFT_P2020, _16_235, BT2020,
      ARIB_STD_B67, BT2020),

  /* 12) YCBCR_FULL_GHLG_TOPLEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_FULL_GHLG_TOPLEFT_P2020, _0_255, BT2020, ARIB_STD_B67,
      BT2020),

  /* 13) YCBCR_STUDIO_G24_LEFT_P709 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G24_LEFT_P709, _16_235, BT709, SRGB, BT709),

  /* 14) YCBCR_STUDIO_G24_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G24_LEFT_P2020, _16_235, BT2020, SRGB, BT2020),

  /* 15) YCBCR_STUDIO_G24_TOPLEFT_P2020 */
  /* FIXME: check chroma-site to differentiate this from
   * YCBCR_STUDIO_G24_LEFT_P2020 */
  MAKE_COLOR_MAP (YCBCR_STUDIO_G24_TOPLEFT_P2020, _16_235, BT2020, SRGB,
      BT2020),
};

#define SCORE_RANGE_MISMATCH 5
#define SCORE_MATRIX_MISMATCH 5
#define SCORE_TRANSFER_MISMATCH 5
#define SCORE_PRIMARY_MISMATCH 10

static gint
get_score (const GstVideoInfo * info, const GstDxgiColorSpace * color_map,
    gboolean is_yuv)
{
  gint loss = 0;
  const GstVideoColorimetry *color = &info->colorimetry;

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

static gboolean
gst_d3d11_video_info_to_dxgi_color_space_rgb (const GstVideoInfo * info,
    GstDxgiColorSpace * color_space)
{
  gint best_score = G_MAXINT;
  gint score;
  guint i;
  const GstDxgiColorSpace *best = nullptr;

  for (i = 0; i < G_N_ELEMENTS (rgb_colorspace_map); i++) {
    score = get_score (info, &rgb_colorspace_map[i], FALSE);

    if (score < best_score) {
      best_score = score;
      best = &rgb_colorspace_map[i];

      if (score == 0) {
        *color_space = rgb_colorspace_map[i];
        return TRUE;
      }
    }
  }

  if (best) {
    *color_space = *best;
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_d3d11_video_info_to_dxgi_color_space_yuv (const GstVideoInfo * info,
    GstDxgiColorSpace * color_space)
{
  gint best_score = G_MAXINT;
  gint score;
  guint i;
  const GstDxgiColorSpace *best = nullptr;

  for (i = 0; i < G_N_ELEMENTS (yuv_colorspace_map); i++) {
    score = get_score (info, &yuv_colorspace_map[i], TRUE);

    if (score < best_score) {
      best_score = score;
      best = &yuv_colorspace_map[i];

      if (score == 0) {
        *color_space = rgb_colorspace_map[i];
        return TRUE;
      }
    }
  }

  if (best) {
    *color_space = *best;
    return TRUE;
  }

  return FALSE;
}

gboolean
gst_d3d11_video_info_to_dxgi_color_space (const GstVideoInfo * info,
    GstDxgiColorSpace * color_space)
{
  g_return_val_if_fail (info != nullptr, FALSE);
  g_return_val_if_fail (color_space != nullptr, FALSE);

  if (GST_VIDEO_INFO_IS_RGB (info))
    return gst_d3d11_video_info_to_dxgi_color_space_rgb (info, color_space);

  return gst_d3d11_video_info_to_dxgi_color_space_yuv (info, color_space);
}

gboolean
gst_d3d11_colorimetry_from_dxgi_color_space (DXGI_COLOR_SPACE_TYPE colorspace,
    GstVideoColorimetry * colorimetry)
{
  /* XXX: because of ancient MinGW header */
  GST_DXGI_COLOR_SPACE_TYPE type = (GST_DXGI_COLOR_SPACE_TYPE) colorspace;
  GstVideoColorimetry color;

  switch (type) {
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SRGB;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_GAMMA10;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_BT709;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_NONE_P709_X601:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      color.transfer = GST_VIDEO_TRANSFER_BT601;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      color.transfer = GST_VIDEO_TRANSFER_BT601;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
      color.transfer = GST_VIDEO_TRANSFER_BT601;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      color.transfer = GST_VIDEO_TRANSFER_BT709;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
      color.transfer = GST_VIDEO_TRANSFER_BT709;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      color.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      color.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_TOPLEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      color.transfer = GST_VIDEO_TRANSFER_BT2020_10;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      color.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      color.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_0_255;
      color.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
      color.transfer = GST_VIDEO_TRANSFER_ARIB_STD_B67;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SRGB;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SRGB;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P709:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SRGB;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
      break;
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_LEFT_P2020:
    case GST_DXGI_COLOR_SPACE_YCBCR_STUDIO_G24_TOPLEFT_P2020:
      color.range = GST_VIDEO_COLOR_RANGE_16_235;
      color.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color.transfer = GST_VIDEO_TRANSFER_SRGB;
      color.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      break;
    default:
      return FALSE;
  }

  *colorimetry = color;

  return TRUE;
}

gboolean
gst_d3d11_find_swap_chain_color_space (const GstVideoInfo * info,
    IDXGISwapChain3 * swapchain, GstDxgiColorSpace * color_space)
{
  UINT can_support = 0;
  HRESULT hr;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (swapchain != NULL, FALSE);
  g_return_val_if_fail (color_space != NULL, FALSE);

  if (!GST_VIDEO_INFO_IS_RGB (info)) {
    GST_WARNING ("Swapchain colorspace should be RGB format");
    return FALSE;
  }

  /* Select PQ color space only if input is also PQ */
  if (info->colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_BT2020 &&
      info->colorimetry.transfer == GST_VIDEO_TRANSFER_SMPTE2084) {
    guint pq = GST_DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    hr = swapchain->CheckColorSpaceSupport ((DXGI_COLOR_SPACE_TYPE) pq,
        &can_support);
    if (SUCCEEDED (hr) && can_support) {
      color_space->dxgi_color_space_type = pq;
      color_space->range = GST_VIDEO_COLOR_RANGE_0_255;
      color_space->matrix = GST_VIDEO_COLOR_MATRIX_RGB;
      color_space->transfer = GST_VIDEO_TRANSFER_SMPTE2084;
      color_space->primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
      return TRUE;
    }
  }

  /* otherwise use standard sRGB color space */
  hr = swapchain->CheckColorSpaceSupport (
      (DXGI_COLOR_SPACE_TYPE) GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,
      &can_support);
  if (SUCCEEDED (hr) && can_support) {
    color_space->dxgi_color_space_type =
        GST_DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    color_space->range = GST_VIDEO_COLOR_RANGE_0_255;
    color_space->matrix = GST_VIDEO_COLOR_MATRIX_RGB;
    color_space->transfer = GST_VIDEO_TRANSFER_SRGB;
    color_space->primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
    return TRUE;
  }

  return FALSE;
}

static void
fill_staging_desc (const D3D11_TEXTURE2D_DESC * ref,
    D3D11_TEXTURE2D_DESC * staging)
{
  memset (staging, 0, sizeof (D3D11_TEXTURE2D_DESC));

  staging->Width = ref->Width;
  staging->Height = ref->Height;
  staging->MipLevels = 1;
  staging->Format = ref->Format;
  staging->SampleDesc.Count = 1;
  staging->ArraySize = 1;
  staging->Usage = D3D11_USAGE_STAGING;
  staging->CPUAccessFlags = (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);
}

GstBuffer *
gst_d3d11_allocate_staging_buffer_for (GstBuffer * buffer,
    const GstVideoInfo * info, gboolean add_videometa)
{
  GstD3D11Memory *dmem;
  GstD3D11Device *device;
  GstD3D11Allocator *alloc = NULL;
  GstBuffer *staging_buffer = NULL;
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  guint i;
  gsize size = 0;
  GstD3D11Format format;
  D3D11_TEXTURE2D_DESC desc;

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_d3d11_memory (mem)) {
      GST_DEBUG ("Not a d3d11 memory");

      return NULL;
    }
  }

  dmem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, 0);
  device = dmem->device;
  if (!gst_d3d11_device_get_format (device, GST_VIDEO_INFO_FORMAT (info),
          &format)) {
    GST_ERROR ("Unknown d3d11 format");
    return NULL;
  }

  alloc = (GstD3D11Allocator *) gst_allocator_find (GST_D3D11_MEMORY_NAME);
  if (!alloc) {
    GST_ERROR ("D3D11 allocator is not available");
    return NULL;
  }

  staging_buffer = gst_buffer_new ();
  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    D3D11_TEXTURE2D_DESC staging_desc;
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    GstD3D11Memory *new_mem;

    guint cur_stride = 0;

    gst_d3d11_memory_get_texture_desc (mem, &desc);
    fill_staging_desc (&desc, &staging_desc);

    new_mem = (GstD3D11Memory *)
        gst_d3d11_allocator_alloc (alloc, mem->device, &staging_desc);
    if (!new_mem) {
      GST_ERROR ("Failed to allocate memory");
      goto error;
    }

    if (!gst_d3d11_memory_get_resource_stride (new_mem, &cur_stride) ||
        cur_stride < staging_desc.Width) {
      GST_ERROR ("Failed to calculate memory size");
      gst_memory_unref (GST_MEMORY_CAST (mem));
      goto error;
    }

    offset[i] = size;
    stride[i] = cur_stride;
    size += GST_MEMORY_CAST (new_mem)->size;

    gst_buffer_append_memory (staging_buffer, GST_MEMORY_CAST (new_mem));
  }

  /* single texture semi-planar formats */
  if (format.dxgi_format != DXGI_FORMAT_UNKNOWN &&
      GST_VIDEO_INFO_N_PLANES (info) == 2) {
    stride[1] = stride[0];
    offset[1] = stride[0] * desc.Height;
  }

  gst_buffer_add_video_meta_full (staging_buffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
      offset, stride);

  if (alloc)
    gst_object_unref (alloc);

  return staging_buffer;

error:
  gst_clear_buffer (&staging_buffer);
  gst_clear_object (&alloc);

  return NULL;
}

static gboolean
gst_d3d11_buffer_copy_into_fallback (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, (GstVideoInfo *) info, src,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, (GstVideoInfo *) info, dst,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    GST_ERROR ("Invalid video buffer");
    return FALSE;
  }
}

gboolean
gst_d3d11_buffer_copy_into (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (dst), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (gst_buffer_n_memory (dst) != gst_buffer_n_memory (src)) {
    GST_LOG ("different memory layout, perform fallback copy");
    return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
  }

  if (!gst_is_d3d11_buffer (dst) || !gst_is_d3d11_buffer (src)) {
    GST_LOG ("non-d3d11 memory, perform fallback copy");
    return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
  }

  for (i = 0; i < gst_buffer_n_memory (dst); i++) {
    GstMemory *dst_mem, *src_mem;
    GstD3D11Memory *dst_dmem, *src_dmem;
    GstMapInfo dst_info;
    GstMapInfo src_info;
    ID3D11Resource *dst_texture, *src_texture;
    ID3D11DeviceContext *device_context;
    GstD3D11Device *device;
    D3D11_BOX src_box = { 0, };
    D3D11_TEXTURE2D_DESC dst_desc, src_desc;
    guint dst_subidx, src_subidx;

    dst_mem = gst_buffer_peek_memory (dst, i);
    src_mem = gst_buffer_peek_memory (src, i);

    dst_dmem = (GstD3D11Memory *) dst_mem;
    src_dmem = (GstD3D11Memory *) src_mem;

    device = dst_dmem->device;
    if (device != src_dmem->device) {
      GST_LOG ("different device, perform fallback copy");
      return gst_d3d11_buffer_copy_into_fallback (dst, src, info);
    }

    gst_d3d11_memory_get_texture_desc (dst_dmem, &dst_desc);
    gst_d3d11_memory_get_texture_desc (src_dmem, &src_desc);

    if (dst_desc.Format != src_desc.Format) {
      GST_WARNING ("different dxgi format");
      return FALSE;
    }

    device_context = gst_d3d11_device_get_device_context_handle (device);

    if (!gst_memory_map (dst_mem, &dst_info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR ("Cannot map dst d3d11 memory");
      return FALSE;
    }

    if (!gst_memory_map (src_mem, &src_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR ("Cannot map src d3d11 memory");
      gst_memory_unmap (dst_mem, &dst_info);
      return FALSE;
    }

    dst_texture = (ID3D11Resource *) dst_info.data;
    src_texture = (ID3D11Resource *) src_info.data;

    /* src/dst texture size might be different if padding was used.
     * select smaller size */
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = MIN (src_desc.Width, dst_desc.Width);
    src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

    dst_subidx = gst_d3d11_memory_get_subresource_index (dst_dmem);
    src_subidx = gst_d3d11_memory_get_subresource_index (src_dmem);

    gst_d3d11_device_lock (device);
    device_context->CopySubresourceRegion (dst_texture, dst_subidx, 0, 0, 0,
        src_texture, src_subidx, &src_box);
    gst_d3d11_device_unlock (device);

    gst_memory_unmap (src_mem, &src_info);
    gst_memory_unmap (dst_mem, &dst_info);
  }

  return TRUE;
}

gboolean
gst_is_d3d11_buffer (GstBuffer * buffer)
{
  guint i;
  guint size;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  size = gst_buffer_n_memory (buffer);
  if (size == 0)
    return FALSE;

  for (i = 0; i < size; i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    if (!gst_is_d3d11_memory (mem))
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d11_buffer_can_access_device (GstBuffer * buffer, ID3D11Device * device)
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_LOG ("Not a d3d11 buffer");
    return FALSE;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    ID3D11Device *handle;

    handle = gst_d3d11_device_get_device_handle (mem->device);
    if (handle != device) {
      GST_LOG ("D3D11 device is incompatible");
      return FALSE;
    }
  }

  return TRUE;
}

gboolean
gst_d3d11_buffer_map (GstBuffer * buffer, ID3D11Device * device,
    GstMapInfo info[GST_VIDEO_MAX_PLANES], GstMapFlags flags)
{
  GstMapFlags map_flags;
  guint num_mapped = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (!gst_d3d11_buffer_can_access_device (buffer, device))
    return FALSE;

  map_flags = (GstMapFlags) (flags | GST_MAP_D3D11);

  for (num_mapped = 0; num_mapped < gst_buffer_n_memory (buffer); num_mapped++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, num_mapped);

    if (!gst_memory_map (mem, &info[num_mapped], map_flags)) {
      GST_ERROR ("Couldn't map memory");
      goto error;
    }
  }

  return TRUE;

error:
  {
    guint i;
    for (i = 0; i < num_mapped; i++) {
      GstMemory *mem = gst_buffer_peek_memory (buffer, i);
      gst_memory_unmap (mem, &info[i]);
    }

    return FALSE;
  }
}

gboolean
gst_d3d11_buffer_unmap (GstBuffer * buffer,
    GstMapInfo info[GST_VIDEO_MAX_PLANES])
{
  guint i;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstMemory *mem = gst_buffer_peek_memory (buffer, i);

    gst_memory_unmap (mem, &info[i]);
  }

  return TRUE;
}

guint
gst_d3d11_buffer_get_shader_resource_view (GstBuffer * buffer,
    ID3D11ShaderResourceView * view[GST_VIDEO_MAX_PLANES])
{
  guint i;
  guint num_views = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (view != NULL, 0);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_ERROR ("Buffer contains non-d3d11 memory");
    return 0;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint view_size;
    guint j;

    view_size = gst_d3d11_memory_get_shader_resource_view_size (mem);
    if (!view_size) {
      GST_LOG ("SRV is unavailable for memory index %d", i);
      return 0;
    }

    for (j = 0; j < view_size; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("Too many SRVs");
        return 0;
      }

      view[num_views++] = gst_d3d11_memory_get_shader_resource_view (mem, j);
    }
  }

  return num_views;
}

guint
gst_d3d11_buffer_get_render_target_view (GstBuffer * buffer,
    ID3D11RenderTargetView * view[GST_VIDEO_MAX_PLANES])
{
  guint i;
  guint num_views = 0;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (view != NULL, 0);

  if (!gst_is_d3d11_buffer (buffer)) {
    GST_ERROR ("Buffer contains non-d3d11 memory");
    return 0;
  }

  for (i = 0; i < gst_buffer_n_memory (buffer); i++) {
    GstD3D11Memory *mem = (GstD3D11Memory *) gst_buffer_peek_memory (buffer, i);
    guint view_size;
    guint j;

    view_size = gst_d3d11_memory_get_render_target_view_size (mem);
    if (!view_size) {
      GST_LOG ("RTV is unavailable for memory index %d", i);
      return 0;
    }

    for (j = 0; j < view_size; j++) {
      if (num_views >= GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("Too many RTVs");
        return 0;
      }

      view[num_views++] = gst_d3d11_memory_get_render_target_view (mem, j);
    }
  }

  return num_views;
}

GstBufferPool *
gst_d3d11_buffer_pool_new_with_options (GstD3D11Device * device,
    GstCaps * caps, GstD3D11AllocationParams * alloc_params,
    guint min_buffers, guint max_buffers)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstVideoInfo info;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);
  g_return_val_if_fail (GST_IS_CAPS (caps), NULL);
  g_return_val_if_fail (alloc_params != NULL, NULL);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (device, "invalid caps");
    return NULL;
  }

  pool = gst_d3d11_buffer_pool_new (device);
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config,
      caps, GST_VIDEO_INFO_SIZE (&info), min_buffers, max_buffers);

  gst_buffer_pool_config_set_d3d11_allocation_params (config, alloc_params);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (pool, "Couldn't set config");
    gst_object_unref (pool);
    return NULL;
  }

  return pool;
}

gchar *
gst_d3d11_dump_color_matrix (GstD3D11ColorMatrix * matrix)
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
color_matrix_copy (GstD3D11ColorMatrix * dst, const GstD3D11ColorMatrix * src)
{
  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      dst->matrix[i][j] = src->matrix[i][j];
    }
  }
}

static void
color_matrix_multiply (GstD3D11ColorMatrix * dst, GstD3D11ColorMatrix * a,
    GstD3D11ColorMatrix * b)
{
  GstD3D11ColorMatrix tmp;

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
color_matrix_identity (GstD3D11ColorMatrix * m)
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

static gboolean
color_matrix_invert (GstD3D11ColorMatrix * dst, GstD3D11ColorMatrix * src)
{
  GstD3D11ColorMatrix tmp;
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
 * gst_d3d11_color_range_adjust_matrix_unorm:
 * @in_info: a #GstVideoInfo
 * @out_info: a #GstVideoInfo
 * @matrix: a #GstD3D11ColorMatrix
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
gst_d3d11_color_range_adjust_matrix_unorm (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstD3D11ColorMatrix * matrix)
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

  memset (matrix, 0, sizeof (GstD3D11ColorMatrix));
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
 * gst_d3d11_yuv_to_rgb_matrix_unorm:
 * @in_yuv_info: a #GstVideoInfo of input YUV signal
 * @out_rgb_info: a #GstVideoInfo of output RGB signal
 * @matrix: a #GstD3D11ColorMatrix
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
gst_d3d11_yuv_to_rgb_matrix_unorm (const GstVideoInfo * in_yuv_info,
    const GstVideoInfo * out_rgb_info, GstD3D11ColorMatrix * matrix)
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

  memset (matrix, 0, sizeof (GstD3D11ColorMatrix));
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
      GstD3D11ColorMatrix scale_matrix, rst;
      GstVideoInfo full_rgb = *out_rgb_info;

      full_rgb.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

      if (gst_d3d11_color_range_adjust_matrix_unorm (&full_rgb,
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
 * gst_d3d11_rgb_to_yuv_matrix_unorm:
 * @in_rgb_info: a #GstVideoInfo of input RGB signal
 * @out_yuv_info: a #GstVideoInfo of output YUV signal
 * @matrix: a #GstD3D11ColorMatrix
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
gst_d3d11_rgb_to_yuv_matrix_unorm (const GstVideoInfo * in_rgb_info,
    const GstVideoInfo * out_yuv_info, GstD3D11ColorMatrix * matrix)
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

  memset (matrix, 0, sizeof (GstD3D11ColorMatrix));
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
      GstD3D11ColorMatrix scale_matrix, rst;
      GstVideoInfo full_rgb = *in_rgb_info;

      full_rgb.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

      if (gst_d3d11_color_range_adjust_matrix_unorm (in_rgb_info,
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
    GstD3D11ColorMatrix * matrix)
{
  GstD3D11ColorMatrix m, im;
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
 * gst_d3d11_color_primaries_matrix_unorm:
 * @in_info: a #GstVideoColorPrimariesInfo of input signal
 * @out_info: a #GstVideoColorPrimariesInfo of output signal
 * @matrix: a #GstD3D11ColorMatrix
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
gst_d3d11_color_primaries_matrix_unorm (const GstVideoColorPrimariesInfo *
    in_info, const GstVideoColorPrimariesInfo * out_info,
    GstD3D11ColorMatrix * matrix)
{
  GstD3D11ColorMatrix Ms, invMd, ret;

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

  memset (matrix, 0, sizeof (GstD3D11ColorMatrix));
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
    GstD3D11ColorMatrix Mc;

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

/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gsthip.h"
#include "gsthip-private.h"

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_HIP_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("hipformat", 0, "hipformat");
  } GST_HIP_CALL_ONCE_END;

  return cat;
}
#endif

#define HIP_AD_FORMAT_NONE ((hipArray_Format) 0)
#define MAKE_FORMAT_YUV_PLANAR(f,cf) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D, \
    { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE },  {1, 1, 1, 0} }
#define MAKE_FORMAT_YUV_SEMI_PLANAR(f,cf) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D, \
    { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {1, 2, 0, 0} }
#define MAKE_FORMAT_RGB(f,cf) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D, \
    { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {4, 0, 0, 0} }
#define MAKE_FORMAT_RGBP(f,cf) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D, \
      { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE }, {1, 1, 1, 0} }
#define MAKE_FORMAT_RGBAP(f,cf) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D, \
    { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf, \
      HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_ ##cf }, {1, 1, 1, 1} }
#define MAKE_FORMAT_BUFFER(f) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_NONE, \
    { HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE }, {0, 0, 0, 0} }
#define MAKE_FORMAT_GRAY(f,cf) \
  { GST_VIDEO_FORMAT_ ##f, GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D, \
    { HIP_AD_FORMAT_ ##cf, HIP_AD_FORMAT_NONE, \
      HIP_AD_FORMAT_NONE, HIP_AD_FORMAT_NONE },  {1, 0, 0, 0} }

static const GstHipFormat format_map[] = {
  MAKE_FORMAT_YUV_PLANAR (I420, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (YV12, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV12, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV21, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P010_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P012_LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P016_LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I420_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I420_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (Y444_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444_16LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGB (RGBA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (BGRA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (RGBx, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (BGRx, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (ARGB, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (ARGB64, UNSIGNED_INT16),
  MAKE_FORMAT_RGB (ABGR, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (Y42B, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (I422_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I422_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (RGBP, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (BGRP, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (GBR, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (GBR_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (GBR_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (GBR_16LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBAP (GBRA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (VUYA, UNSIGNED_INT8),
  MAKE_FORMAT_BUFFER (RGB),
  MAKE_FORMAT_BUFFER (BGR),
  MAKE_FORMAT_BUFFER (BGR10A2_LE),
  MAKE_FORMAT_BUFFER (RGB10A2_LE),
  MAKE_FORMAT_BUFFER (YUY2),
  MAKE_FORMAT_BUFFER (UYVY),
  MAKE_FORMAT_RGB (RGBA_F16LE, HALF),
  MAKE_FORMAT_RGB (RGBA_F32LE, FLOAT),
  MAKE_FORMAT_RGBP (RGBP_F16LE, HALF),
  MAKE_FORMAT_RGBP (RGBP_F32LE, FLOAT),
  MAKE_FORMAT_BUFFER (RGB_F16LE),
  MAKE_FORMAT_BUFFER (RGB_F32LE),
  MAKE_FORMAT_GRAY (GRAY8, UNSIGNED_INT8),
  MAKE_FORMAT_GRAY (GRAY16_LE, UNSIGNED_INT16),
  MAKE_FORMAT_GRAY (GRAY_F16LE, HALF),
  MAKE_FORMAT_GRAY (GRAY_F32LE, FLOAT),
};

gboolean
gst_hip_device_get_format (GstHipDevice * device, GstVideoFormat format,
    GstHipFormat * hip_format)
{
  /* We may need per-device format meta support later,
   * but device is not used now */
  g_return_val_if_fail (hip_format, FALSE);
  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN, FALSE);

  for (guint i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == format) {
      *hip_format = format_map[i];
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
_get_texture_alignment (GstHipDevice * device, GstVideoFormat format,
    gint * stride_align, gint * offset_align)
{
  gint texture_align = 0;
  gint pitch_align = 0;

  for (guint i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == format) {
      if ((format_map[i].format_flags & GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D)
          != GST_HIP_FORMAT_FLAG_SUPPORT_TEXTURE_2D) {
        return FALSE;
      }

      auto hip_ret = gst_hip_device_get_attribute (device,
          hipDeviceAttributeTextureAlignment, &texture_align);
      if (hip_ret != hipSuccess)
        return FALSE;

      hip_ret = gst_hip_device_get_attribute (device,
          hipDeviceAttributeTexturePitchAlignment, &pitch_align);
      if (hip_ret != hipSuccess)
        return FALSE;

      break;
    }
  }

  if (texture_align <= 0 || pitch_align <= 0)
    return FALSE;

  *stride_align = pitch_align;
  *offset_align = texture_align;

  return TRUE;
}

static size_t
do_align (size_t value, size_t align)
{
  if (align == 0)
    return value;

  return ((value + align - 1) / align) * align;
}

gboolean
gst_hip_device_align_video_info_for_texture (GstHipDevice * device,
    const GstVideoInfo * reference, GstVideoInfo * aligned_info,
    gboolean * texture_supported)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), FALSE);
  g_return_val_if_fail (reference, FALSE);
  g_return_val_if_fail (aligned_info, FALSE);
  g_return_val_if_fail (texture_supported, FALSE);

  *texture_supported = FALSE;

  gint offset_align = 0;
  gint stride_align = 0;
  auto supported = _get_texture_alignment (device,
      GST_VIDEO_INFO_FORMAT (reference), &stride_align, &offset_align);
  if (!supported) {
    GST_LOG_OBJECT (device, "Device or format does not support texture");
    *aligned_info = *reference;
    return TRUE;
  }

  GstVideoInfo ret = *reference;
  gsize offset = 0;
  guint n_planes = GST_VIDEO_INFO_N_PLANES (reference);

  GST_LOG_OBJECT (device, "Aligning %s %dx%d for texture support, "
      "offset alignment: %d, stride alignment: %d",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (reference)),
      reference->width, reference->height, offset_align, stride_align);

  for (guint i = 0; i < n_planes; i++) {
    gint components[GST_VIDEO_MAX_COMPONENTS];

    gst_video_format_info_component (reference->finfo, i, components);
    if (components[0] < 0)
      return FALSE;

    auto height = GST_VIDEO_INFO_COMP_HEIGHT (reference, components[0]);
    auto stride = (gint) do_align (reference->stride[i], stride_align);

    offset = do_align (offset, offset_align);

    ret.stride[i] = stride;
    ret.offset[i] = offset;

    GST_LOG_OBJECT (device, "Plane %d: stride: %d -> %d, height: %d, offset: %"
        G_GSIZE_FORMAT, i, reference->stride[i], stride, height, offset);

    offset += stride * height;
  }

  GST_LOG_OBJECT (device, "Total size: %" G_GSIZE_FORMAT, offset);

  ret.size = offset;

  *aligned_info = ret;
  *texture_supported = TRUE;

  return TRUE;
}

gboolean
gst_hip_device_check_texture_support (GstHipDevice * device,
    const GstVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), FALSE);
  g_return_val_if_fail (info, FALSE);

  gint offset_align = 0;
  gint stride_align = 0;
  auto supported = _get_texture_alignment (device,
      GST_VIDEO_INFO_FORMAT (info), &stride_align, &offset_align);
  if (!supported) {
    GST_LOG_OBJECT (device, "Device or format does not support texture");
    return FALSE;
  }

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    if ((GST_VIDEO_INFO_PLANE_STRIDE (info, i) % stride_align) != 0) {
      GST_LOG_OBJECT (device,
          "Stride of plane %u is not aligned to %d, texture not supported",
          i, stride_align);
      return FALSE;
    }

    if ((GST_VIDEO_INFO_PLANE_OFFSET (info, i) % offset_align) != 0) {
      GST_LOG_OBJECT (device,
          "Offset of plane %u is not aligned to %d, texture not supported",
          i, offset_align);
      return FALSE;
    }
  }

  return TRUE;
}

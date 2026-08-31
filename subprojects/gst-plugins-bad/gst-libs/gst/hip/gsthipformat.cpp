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

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

#pragma once

#include <d3d11_4.h>
#include <dxgi1_6.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11format.h>
#include <gst/d3d11/gstd3d11device.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D11_FORMAT_SUPPORT (gst_d3d11_format_support_get_type())
GType gst_d3d11_format_support_get_type (void);

void  gst_d3d11_device_d3d11_debug (GstD3D11Device * device,
                                    const gchar * file,
                                    const gchar * function,
                                    gint line);

void  gst_d3d11_device_dxgi_debug  (GstD3D11Device * device,
                                    const gchar * file,
                                    const gchar * function,
                                    gint line);

#define GST_D3D11_CLEAR_COM(obj) G_STMT_START { \
    if (obj) { \
      (obj)->Release (); \
      (obj) = NULL; \
    } \
  } G_STMT_END


#define MAKE_FORMAT_MAP_YUV(g,d,r0,r1,r2,r3) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_ ##d, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    (D3D11_FORMAT_SUPPORT) (D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) }

#define MAKE_FORMAT_MAP_YUV_FULL(g,d,r0,r1,r2,r3,f) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_ ##d, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    { DXGI_FORMAT_ ##r0, DXGI_FORMAT_ ##r1, DXGI_FORMAT_ ##r2, DXGI_FORMAT_ ##r3 }, \
    (D3D11_FORMAT_SUPPORT) (f) }

#define MAKE_FORMAT_MAP_RGB(g,d) \
  { GST_VIDEO_FORMAT_ ##g, DXGI_FORMAT_ ##d, \
    { DXGI_FORMAT_ ##d, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN }, \
    { DXGI_FORMAT_ ##d, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN }, \
    (D3D11_FORMAT_SUPPORT) (D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) }

static const GstD3D11Format _gst_d3d11_default_format_map[] = {
  MAKE_FORMAT_MAP_RGB (BGRA, B8G8R8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (RGBA, R8G8B8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (BGRx, B8G8R8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (RGBx, R8G8B8A8_UNORM),
  MAKE_FORMAT_MAP_RGB (RGB10A2_LE, R10G10B10A2_UNORM),
  MAKE_FORMAT_MAP_RGB (RGBA64_LE, R16G16B16A16_UNORM),
  MAKE_FORMAT_MAP_YUV (AYUV, UNKNOWN, R8G8B8A8_UNORM, UNKNOWN, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (AYUV64, UNKNOWN, R16G16B16A16_UNORM, UNKNOWN, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (VUYA, AYUV, R8G8B8A8_UNORM, UNKNOWN, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (NV12, NV12, R8_UNORM, R8G8_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (NV21, UNKNOWN, R8_UNORM, R8G8_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (P010_10LE, P010, R16_UNORM, R16G16_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (P012_LE, P016, R16_UNORM, R16G16_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (P016_LE, P016, R16_UNORM, R16G16_UNORM, UNKNOWN, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I420, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (YV12, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I420_10LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I420_12LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y42B, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I422_10LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (I422_12LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444, UNKNOWN, R8_UNORM, R8_UNORM, R8_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444_10LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444_12LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  MAKE_FORMAT_MAP_YUV (Y444_16LE, UNKNOWN, R16_UNORM, R16_UNORM, R16_UNORM, UNKNOWN),
  /* GRAY */
  /* NOTE: To support conversion by using video processor,
   * mark DXGI_FORMAT_{R8,R16}_UNORM formats as known dxgi_format.
   * Otherwise, d3d11 elements will not try to use video processor for
   * those formats */
  MAKE_FORMAT_MAP_RGB (GRAY8, R8_UNORM),
  MAKE_FORMAT_MAP_RGB (GRAY16_LE, R16_UNORM),
  MAKE_FORMAT_MAP_YUV_FULL (Y410, Y410, R10G10B10A2_UNORM, UNKNOWN, UNKNOWN, UNKNOWN,
      D3D11_FORMAT_SUPPORT_SHADER_SAMPLE),
};

#undef MAKE_FORMAT_MAP_YUV
#undef MAKE_FORMAT_MAP_YUV_FULL
#undef MAKE_FORMAT_MAP_RGB

#define GST_D3D11_N_FORMATS G_N_ELEMENTS(_gst_d3d11_default_format_map)

typedef struct _GstD3D11ColorMatrix
{
  gdouble matrix[3][3];
  gdouble offset[3];
  gdouble min[3];
  gdouble max[3];
} GstD3D11ColorMatrix;

GST_D3D11_API
gchar *         gst_d3d11_dump_color_matrix (GstD3D11ColorMatrix * matrix);

GST_D3D11_API
gboolean        gst_d3d11_color_range_adjust_matrix_unorm (const GstVideoInfo * in_info,
                                                           const GstVideoInfo * out_info,
                                                           GstD3D11ColorMatrix * matrix);

GST_D3D11_API
gboolean        gst_d3d11_yuv_to_rgb_matrix_unorm (const GstVideoInfo * in_yuv_info,
                                                   const GstVideoInfo * out_rgb_info,
                                                   GstD3D11ColorMatrix * matrix);

GST_D3D11_API
gboolean        gst_d3d11_rgb_to_yuv_matrix_unorm (const GstVideoInfo * in_rgb_info,
                                                   const GstVideoInfo * out_yuv_info,
                                                   GstD3D11ColorMatrix * matrix);

GST_D3D11_API
gboolean        gst_d3d11_color_primaries_matrix_unorm (const GstVideoColorPrimariesInfo * in_info,
                                                        const GstVideoColorPrimariesInfo * out_info,
                                                        GstD3D11ColorMatrix * matrix);

G_END_DECLS

#ifdef __cplusplus
class GstD3D11DeviceLockGuard
{
public:
  explicit GstD3D11DeviceLockGuard(GstD3D11Device * device) : device_ (device)
  {
    gst_d3d11_device_lock (device_);
  }

  ~GstD3D11DeviceLockGuard()
  {
    gst_d3d11_device_unlock (device_);
  }

  GstD3D11DeviceLockGuard(const GstD3D11DeviceLockGuard&) = delete;
  GstD3D11DeviceLockGuard& operator=(const GstD3D11DeviceLockGuard&) = delete;

private:
    GstD3D11Device *device_;
};
#endif

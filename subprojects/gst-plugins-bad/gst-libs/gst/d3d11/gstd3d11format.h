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

#ifndef __GST_D3D11_FORMAT_H__
#define __GST_D3D11_FORMAT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11_fwd.h>

G_BEGIN_DECLS

#define GST_D3D11_COMMON_FORMATS \
    "BGRA, RGBA, RGB10A2_LE, BGRx, RGBx, VUYA, NV12, NV21, " \
    "P010_10LE, P012_LE, P016_LE, I420, YV12, I420_10LE, I420_12LE, " \
    "Y42B, I422_10LE, I422_12LE, Y444, Y444_10LE, Y444_12LE, Y444_16LE, " \
    "GRAY8, GRAY16_LE"

#define GST_D3D11_EXTRA_IN_FORMATS \
    "Y410"

#define GST_D3D11_SINK_FORMATS \
    "{ " GST_D3D11_COMMON_FORMATS " ," GST_D3D11_EXTRA_IN_FORMATS " }"

#define GST_D3D11_SRC_FORMATS \
    "{ " GST_D3D11_COMMON_FORMATS " }"

#define GST_D3D11_ALL_FORMATS \
    "{ " GST_D3D11_COMMON_FORMATS " ," GST_D3D11_EXTRA_IN_FORMATS " }"

struct _GstD3D11Format
{
  GstVideoFormat format;

  /* direct mapping to dxgi format if applicable */
  DXGI_FORMAT dxgi_format;

  /* formats for texture processing */
  DXGI_FORMAT resource_format[GST_VIDEO_MAX_PLANES];

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D11_API
guint           gst_d3d11_dxgi_format_n_planes      (DXGI_FORMAT format);

GST_D3D11_API
gboolean        gst_d3d11_dxgi_format_get_size      (DXGI_FORMAT format,
                                                     guint width,
                                                     guint height,
                                                     guint pitch,
                                                     gsize offset[GST_VIDEO_MAX_PLANES],
                                                     gint stride[GST_VIDEO_MAX_PLANES],
                                                     gsize *size);

GST_D3D11_API
GstVideoFormat  gst_d3d11_dxgi_format_to_gst        (DXGI_FORMAT format);

G_END_DECLS

#endif /* __GST_D3D11_FORMAT_H__ */

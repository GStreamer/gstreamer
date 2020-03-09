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

#include "gstd3d11_fwd.h"

#define GST_D3D11_FORMATS \
    "{ BGRA, RGBA, RGB10A2_LE, VUYA, NV12, P010_10LE, P016_LE, I420, I420_10LE }"
#define GST_D3D11_N_FORMATS 9

G_BEGIN_DECLS

typedef struct _GstDxgiColorSpace GstDxgiColorSpace;

struct _GstD3D11Format
{
  GstVideoFormat format;

  /* direct mapping to dxgi format if applicable */
  DXGI_FORMAT dxgi_format;

  /* formats for texture processing */
  DXGI_FORMAT resource_format[GST_VIDEO_MAX_COMPONENTS];
};

struct _GstDxgiColorSpace
{
  guint dxgi_color_space_type;
  GstVideoColorRange range;
  GstVideoColorMatrix matrix;
  GstVideoTransferFunction transfer;
  GstVideoColorPrimaries primaries;
};

guint           gst_d3d11_dxgi_format_n_planes      (DXGI_FORMAT format);

gboolean        gst_d3d11_dxgi_format_get_size      (DXGI_FORMAT format,
                                                     guint width,
                                                     guint height,
                                                     guint pitch,
                                                     gsize offset[GST_VIDEO_MAX_PLANES],
                                                     gint stride[GST_VIDEO_MAX_PLANES],
                                                     gsize *size);

GstCaps *       gst_d3d11_device_get_supported_caps (GstD3D11Device * device,
                                                     D3D11_FORMAT_SUPPORT flags);

#if (DXGI_HEADER_VERSION >= 5)
gboolean        gst_d3d11_hdr_meta_data_to_dxgi     (GstVideoMasteringDisplayInfo * minfo,
                                                     GstVideoContentLightLevel * cll,
                                                     DXGI_HDR_METADATA_HDR10 * dxgi_hdr10);
#endif

#if (DXGI_HEADER_VERSION >= 4)
const GstDxgiColorSpace * gst_d3d11_video_info_to_dxgi_color_space (GstVideoInfo * info);

const GstDxgiColorSpace * gst_d3d11_find_swap_chain_color_space (GstVideoInfo * info,
                                                                 IDXGISwapChain3 * swapchain,
                                                                 gboolean use_hdr10);
#endif

G_END_DECLS

#endif /* __GST_D3D11_FORMAT_H__ */

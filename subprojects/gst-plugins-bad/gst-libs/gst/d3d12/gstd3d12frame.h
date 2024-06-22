/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

typedef struct _GstD3D12FrameFence GstD3D12FrameFence;

/**
 * GstD3D12FrameMapFlags:
 * @GST_D3D12_FRAME_MAP_FLAG_NONE: No flags
 * @GST_D3D12_FRAME_MAP_FLAG_SRV: Frame mapping requires shared resource view
 * @GST_D3D12_FRAME_MAP_FLAG_UAV: Frame mapping requires unordered access view
 * @GST_D3D12_FRAME_MAP_FLAG_RTV: Frame mapping requires render target view
 *
 * Since: 1.26
 */
typedef enum
{
  GST_D3D12_FRAME_MAP_FLAG_NONE = 0,
  GST_D3D12_FRAME_MAP_FLAG_SRV = (1 << 0),
  GST_D3D12_FRAME_MAP_FLAG_UAV = (1 << 1),
  GST_D3D12_FRAME_MAP_FLAG_RTV = (1 << 2),
} GstD3D12FrameMapFlags;

DEFINE_ENUM_FLAG_OPERATORS (GstD3D12FrameMapFlags);

struct _GstD3D12FrameFence
{
  ID3D12Fence *fence;
  guint64 fence_value;
};

/**
 * GstD3D12Frame:
 * @info: the #GstVideoInfo
 * @frame_flags: #GstVideoFrameFlags for the frame
 * @d3d12_flags: #GstD3D12FrameMapFlags for the frame
 * @device: a #GstD3D12Device
 * @buffer: the mapped buffer
 * @map: mappings of the memory objects
 * @data: pointers to the plane data
 * @subresource_index: subresource index of the plane
 * @plane_rect: plane rectangle
 * @fence: external fences
 * @srv_desc_handle: shader resource view descriptor handle
 * @rtb_desc_handle: render target view descriptor handle
 *
 * A frame obtained from gst_d3d12_frame_map()
 *
 * Since: 1.26
 */
struct _GstD3D12Frame
{
  GstVideoInfo info;
  GstVideoFrameFlags frame_flags;
  GstD3D12FrameMapFlags d3d12_flags;
  GstD3D12Device *device;
  GstBuffer *buffer;

  GstMapInfo map[GST_VIDEO_MAX_PLANES];
  ID3D12Resource *data[GST_VIDEO_MAX_PLANES];
  guint subresource_index[GST_VIDEO_MAX_PLANES];
  D3D12_RECT plane_rect[GST_VIDEO_MAX_PLANES];
  GstD3D12FrameFence fence[GST_VIDEO_MAX_PLANES];
  D3D12_CPU_DESCRIPTOR_HANDLE srv_desc_handle[GST_VIDEO_MAX_PLANES];
  D3D12_CPU_DESCRIPTOR_HANDLE uav_desc_handle[GST_VIDEO_MAX_PLANES];
  D3D12_CPU_DESCRIPTOR_HANDLE rtv_desc_handle[GST_VIDEO_MAX_PLANES];

  /*< private >*/
  guint64 _gst_reserved[GST_PADDING_LARGE];
};

GST_D3D12_API
gboolean  gst_d3d12_frame_map         (GstD3D12Frame * frame,
                                       const GstVideoInfo * info,
                                       GstBuffer * buffer,
                                       GstMapFlags map_flags,
                                       GstD3D12FrameMapFlags d3d12_flags);

GST_D3D12_API
void      gst_d3d12_frame_unmap       (GstD3D12Frame * frame);

GST_D3D12_API
gboolean  gst_d3d12_frame_copy        (GstD3D12Frame * dest,
                                       const GstD3D12Frame * src,
                                       guint64 * fence_value);

GST_D3D12_API
gboolean  gst_d3d12_frame_copy_plane  (GstD3D12Frame * dest,
                                       const GstD3D12Frame * src,
                                       guint plane,
                                       guint64 * fence_value);

GST_D3D12_API
gboolean  gst_d3d12_frame_fence_gpu_wait (const GstD3D12Frame * frame,
                                          GstD3D12CommandQueue * queue);

GST_D3D12_API
gboolean  gst_d3d12_frame_fence_cpu_wait (const GstD3D12Frame * frame);

G_END_DECLS


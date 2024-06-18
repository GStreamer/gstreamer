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

enum GstD3D12WAFlags
{
  GST_D3D12_WA_NONE = 0,
  GST_D3D12_WA_DECODER_RACE = (1 << 0),
};

DEFINE_ENUM_FLAG_OPERATORS (GstD3D12WAFlags);

struct GstD3D12CopyTextureRegionArgs
{
  D3D12_TEXTURE_COPY_LOCATION dst;
  guint dst_x;
  guint dst_y;
  guint dst_z;
  D3D12_TEXTURE_COPY_LOCATION src;
  const D3D12_BOX * src_box;
};

GST_D3D12_API
gboolean  gst_d3d12_device_copy_texture_region (GstD3D12Device * device,
                                                guint num_args,
                                                const GstD3D12CopyTextureRegionArgs * args,
                                                GstD3D12FenceData * fence_data,
                                                guint num_fences_to_wait,
                                                ID3D12Fence ** fences_to_wait,
                                                const guint64 * fence_values_to_wait,
                                                D3D12_COMMAND_LIST_TYPE command_type,
                                                guint64 * fence_value);

GST_D3D12_API
gboolean  gst_d3d12_device_acquire_fence_data (GstD3D12Device * device,
                                               GstD3D12FenceData ** fence_data);

GST_D3D12_API
void      gst_d3d12_device_clear_yuv_texture (GstD3D12Device * device,
                                              GstMemory * mem);

GST_D3D12_API
void      gst_d3d12_device_d3d12_debug       (GstD3D12Device * device,
                                              const gchar * file,
                                              const gchar * function,
                                              gint line);

GST_D3D12_API
IUnknown *  gst_d3d12_device_get_11on12_handle  (GstD3D12Device * device);

GST_D3D12_API
void        gst_d3d12_device_11on12_lock        (GstD3D12Device * device);

GST_D3D12_API
void        gst_d3d12_device_11on12_unlock      (GstD3D12Device * device);

GST_D3D12_API
void        gst_d3d12_device_check_device_removed (GstD3D12Device * device);

GST_D3D12_API
GstD3D12CommandQueue * gst_d3d12_device_get_decode_queue (GstD3D12Device * device);

GST_D3D12_API
void        gst_d3d12_device_decoder_lock (GstD3D12Device * device);

GST_D3D12_API
void        gst_d3d12_device_decoder_unlock (GstD3D12Device * device);

GST_D3D12_API
GstD3D12WAFlags gst_d3d12_device_get_workaround_flags (GstD3D12Device * device);

G_END_DECLS


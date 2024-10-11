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

#define GST_TYPE_D3D12_PACK (gst_d3d12_pack_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Pack, gst_d3d12_pack,
    GST, D3D12_PACK, GstObject);

GstD3D12Pack * gst_d3d12_pack_new (GstD3D12Device * device,
                                   const GstVideoInfo * converter_output_info);

gboolean       gst_d3d12_pack_get_video_info (GstD3D12Pack * pack,
                                              GstVideoInfo * pack_input_info);

GstBuffer *    gst_d3d12_pack_acquire_render_target (GstD3D12Pack * pack,
                                                     GstBuffer * buffer);

gboolean       gst_d3d12_pack_execute (GstD3D12Pack * pack,
                                       GstBuffer * in_buffer,
                                       GstBuffer * out_buffer,
                                       GstD3D12FenceData * fence_data,
                                       ID3D12GraphicsCommandList * cl);

G_END_DECLS

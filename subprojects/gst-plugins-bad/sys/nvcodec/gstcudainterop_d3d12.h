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
#include <gst/d3d12/gstd3d12.h>
#include <gst/cuda/gstcuda.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_CUDA_D3D12_INTEROP (gst_cuda_d3d12_interop_get_type())
G_DECLARE_FINAL_TYPE (GstCudaD3D12Interop, gst_cuda_d3d12_interop,
    GST, CUDA_D3D12_INTEROP, GstObject)

GType gst_cuda_d3d12_interop_resource_get_type (void);

GstCudaD3D12Interop * gst_cuda_d3d12_interop_new (GstCudaContext * context,
                                                  GstD3D12Device * device,
                                                  const GstVideoInfo * info);

gboolean gst_cuda_d3d12_interop_upload_async (GstCudaD3D12Interop * interop,
                                              GstBuffer * dst_cuda,
                                              GstBuffer * src_d3d12,
                                              GstCudaStream * stream);

G_END_DECLS


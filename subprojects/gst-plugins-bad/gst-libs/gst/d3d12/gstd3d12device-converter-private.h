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
#include <memory>
#include <wrl.h>

HRESULT     gst_d3d12_device_get_sampler_state (GstD3D12Device * device,
                                                D3D12_FILTER filter,
                                                ID3D12DescriptorHeap ** heap);

HRESULT     gst_d3d12_device_get_converter_resources (GstD3D12Device * device,
                                                      ID3D12Resource * index_buf,
                                                      ID3D12Resource * index_upload,
                                                      const D3D12_VERTEX_BUFFER_VIEW * vbv,
                                                      const D3D12_INDEX_BUFFER_VIEW * ibv,
                                                      GstVideoTransferFunction gamma_dec_func,
                                                      ID3D12Resource ** gamma_dec,
                                                      GstVideoTransferFunction gamma_enc_func,
                                                      ID3D12Resource ** gamma_enc,
                                                      ID3D12Fence ** fence,
                                                      guint64 * fence_val);

struct GstD3D12TextureEntry
{
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
};

typedef std::shared_ptr<GstD3D12TextureEntry> GstD3D12TextureEntryPtr;

GstD3D12TextureEntryPtr gst_d3d12_device_acquire_mipmap_texture (GstD3D12Device * device,
                                                                 guint width,
                                                                 guint height,
                                                                 DXGI_FORMAT format);
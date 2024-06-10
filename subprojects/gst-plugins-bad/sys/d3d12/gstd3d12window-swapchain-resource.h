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

#include <gst/d3d12/gstd3d12.h>
#include "gstd3d12pluginutils.h"
#include "gstd3d12overlaycompositor.h"
#include <mutex>
#include <vector>
#include <queue>
#include <memory>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <wrl.h>

struct SwapChainBuffer
{
  SwapChainBuffer (GstBuffer * buffer, ID3D12Resource * backbuf_resource);
  SwapChainBuffer () = delete;
  ~SwapChainBuffer();

  Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2d_target;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> wrapped_resource;
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  GstBuffer *backbuf = nullptr;
  bool is_first = true;
};

struct SwapChainResource
{
  SwapChainResource (GstD3D12Device * device);
  SwapChainResource () = delete;
  ~SwapChainResource();

  void clear_resource ();
  bool ensure_d3d11_target (SwapChainBuffer * swapbuf);
  bool ensure_d2d_target (SwapChainBuffer * swapbuf);

  Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cl;
  Microsoft::WRL::ComPtr<ID3D11On12Device> device11on12;
  Microsoft::WRL::ComPtr<ID3D11Device> device11;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
  Microsoft::WRL::ComPtr<ID2D1Factory3> factory2d;
  Microsoft::WRL::ComPtr<ID2D1Device2> device2d;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext2> context2d;

  std::vector<std::shared_ptr<SwapChainBuffer>> buffers;
  GstBuffer *msaa_buf = nullptr;
  GstBuffer *cached_buf = nullptr;
  GstD3D12Converter *conv = nullptr;
  GstD3D12OverlayCompositor *comp = nullptr;
  GstD3D12Device *device = nullptr;
  GstD3D12CommandAllocatorPool *ca_pool = nullptr;
  HANDLE event_handle = nullptr;
  UINT64 fence_val = 0;
  std::queue<UINT64> prev_fence_val;
  DXGI_FORMAT render_format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOURCE_DESC buffer_desc;
};


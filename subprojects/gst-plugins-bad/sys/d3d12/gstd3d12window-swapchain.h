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
#include "gstd3d12window-swapchain-resource.h"
#include "gstd3d12window.h"
#include <mutex>
#include <memory>

class SwapChain
{
public:
  SwapChain (GstD3D12Device * device);
  ~SwapChain ();

  GstFlowReturn setup_swapchain (GstD3D12Window * window,
      GstD3D12Device * device, HWND hwnd, DXGI_FORMAT format,
      const GstVideoInfo * in_info, const GstVideoInfo * out_info,
      GstStructure * conv_config, bool & is_new_swapchain);
  void disable_alt_enter (HWND hwnd);
  GstFlowReturn resize_buffer (GstD3D12Window * window);
  GstFlowReturn set_buffer (GstD3D12Window * window, GstBuffer * buffer);
  GstFlowReturn present ();
  void expose (GstD3D12Window * window);

private:
  void before_rendering ();
  void after_rendering ();

private:
  std::unique_ptr<SwapChainResource> resource_;
  DXGI_FORMAT render_format_ = DXGI_FORMAT_UNKNOWN;
  std::recursive_mutex lock_;
  GstVideoFormat in_format_ = GST_VIDEO_FORMAT_UNKNOWN;
  GstStructure *converter_config_ = nullptr;
  bool first_present_ = true;
  bool backbuf_rendered_ = false;
  RECT output_rect_ = { };
  D3D12_BOX crop_rect_ = { };
  D3D12_BOX prev_crop_rect_ = { };
};


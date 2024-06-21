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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12window-swapchain.h"
#include <directx/d3dx12.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_window_debug);
#define GST_CAT_DEFAULT gst_d3d12_window_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

#define BACK_BUFFER_COUNT 3

SwapChainBuffer::SwapChainBuffer (GstBuffer * buffer,
    ID3D12Resource * backbuf_resource)
{
  backbuf = buffer;
  resource = backbuf_resource;
}

SwapChainBuffer::~SwapChainBuffer ()
{
  d2d_target = nullptr;
  wrapped_resource = nullptr;
  resource = nullptr;
  gst_clear_buffer (&backbuf);
}

SwapChainResource::SwapChainResource (GstD3D12Device * dev)
{
  event_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  device = (GstD3D12Device *) gst_object_ref (dev);
  auto device_handle = gst_d3d12_device_get_device_handle (device);
  ca_pool = gst_d3d12_command_allocator_pool_new (device_handle,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
}

SwapChainResource::~SwapChainResource ()
{
  GST_DEBUG_OBJECT (device, "Releasing swapchain resource");

  context2d = nullptr;
  device2d = nullptr;
  factory2d = nullptr;

  context11 = nullptr;
  device11 = nullptr;
  device11on12 = nullptr;

  buffers.clear();
  swapchain = nullptr;
  cl = nullptr;

  gst_clear_buffer (&msaa_buf);
  gst_clear_buffer (&cached_buf);

  gst_clear_object (&conv);
  gst_clear_object (&comp);
  gst_clear_object (&conv);
  gst_clear_object (&ca_pool);
  gst_clear_object (&device);

  CloseHandle (event_handle);
}

void
SwapChainResource::clear_resource ()
{
  if (!buffers.empty ()) {
    auto cq = gst_d3d12_device_get_command_queue (device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    gst_d3d12_command_queue_idle_for_swapchain (cq, fence_val, event_handle);
    prev_fence_val = { };
  }

  if (context11)
    gst_d3d12_device_11on12_lock (device);

  buffers.clear ();
  gst_clear_buffer (&msaa_buf);

  if (context2d)
    context2d->SetTarget (nullptr);

  if (context11) {
    context11->ClearState ();
    context11->Flush ();
    gst_d3d12_device_11on12_unlock (device);
  }
}

static bool
ensure_d3d11 (SwapChainResource * resource)
{
  if (resource->device11on12)
    return true;

  auto unknown = gst_d3d12_device_get_11on12_handle (resource->device);
  if (!unknown)
    return false;

  unknown->QueryInterface (IID_PPV_ARGS (&resource->device11on12));
  resource->device11on12.As (&resource->device11);
  resource->device11->GetImmediateContext (&resource->context11);

  return true;
}

static bool
ensure_d2d (SwapChainResource * resource)
{
  if (resource->context2d)
    return true;

  if (!ensure_d3d11 (resource))
    return false;

  HRESULT hr;
  if (!resource->factory2d) {
    hr = D2D1CreateFactory (D2D1_FACTORY_TYPE_SINGLE_THREADED,
        IID_PPV_ARGS (&resource->factory2d));
    if (FAILED (hr))
      return false;
  }

  GstD3D12Device11on12LockGuard lk (resource->device);
  if (!resource->device2d) {
    ComPtr<IDXGIDevice> device_dxgi;
    hr = resource->device11.As (&device_dxgi);
    if (FAILED (hr))
      return false;

    hr = resource->factory2d->CreateDevice (device_dxgi.Get (),
        &resource->device2d);
    if (FAILED (hr))
      return false;
  }

  if (!resource->context2d) {
    hr = resource->device2d->CreateDeviceContext (
      D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &resource->context2d);
    if (FAILED (hr))
      return false;
  }

  return true;
}

bool
SwapChainResource::ensure_d3d11_target (SwapChainBuffer * swapbuf)
{
  if (swapbuf->wrapped_resource)
    return true;

  if (!ensure_d3d11 (this))
    return false;

  D3D11_RESOURCE_FLAGS d3d11_flags = { };
  d3d11_flags.BindFlags = D3D11_BIND_RENDER_TARGET;
  auto hr = device11on12->CreateWrappedResource (swapbuf->resource.Get (),
        &d3d11_flags, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        IID_PPV_ARGS (&swapbuf->wrapped_resource));
  if (FAILED (hr))
    return false;

  return true;
}

bool
SwapChainResource::ensure_d2d_target (SwapChainBuffer * swapbuf)
{
  if (swapbuf->d2d_target)
      return true;

  if (!ensure_d2d (this))
    return false;

  if (!ensure_d3d11_target (swapbuf))
    return false;

  GstD3D12Device11on12LockGuard lk (device);
  ComPtr<IDXGISurface> surface;
  auto hr = swapbuf->wrapped_resource.As (&surface);
  if (FAILED (hr))
    return false;

  D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

  hr = context2d->CreateBitmapFromDxgiSurface (surface.Get (), &props,
      &swapbuf->d2d_target);
  if (FAILED (hr))
    return false;

  return true;
}

SwapChain::SwapChain (GstD3D12Device * device)
{
  resource_ = std::make_unique <SwapChainResource> (device);
}

struct AsyncReleaseData
{
  std::unique_ptr<SwapChainResource> resource;
};

SwapChain::~SwapChain()
{
  lock_.lock ();
  if (!resource_->buffers.empty ()) {
    auto cq = gst_d3d12_device_get_command_queue (resource_->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    gst_d3d12_command_queue_idle_for_swapchain (cq, resource_->fence_val,
        resource_->event_handle);
  }

  resource_ = nullptr;

  if (converter_config_)
    gst_structure_free (converter_config_);

  lock_.unlock ();
}

static inline bool
is_expected_error (HRESULT hr)
{
  switch (hr) {
    case DXGI_ERROR_INVALID_CALL:
    case E_ACCESSDENIED:
      return true;
    break;
  }

  return false;
}

GstFlowReturn
SwapChain::setup_swapchain (GstD3D12Window * window, GstD3D12Device * device,
    HWND hwnd, DXGI_FORMAT format, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * conv_config,
    bool & is_new_swapchain)
{
  is_new_swapchain = false;
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!gst_d3d12_device_is_equal (device, resource_->device)) {
     gst_d3d12_device_fence_wait (resource_->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT, resource_->fence_val,
        resource_->event_handle);
    resource_ = std::make_unique <SwapChainResource> (device);
  }

  if (!resource_->swapchain) {
    DXGI_SWAP_CHAIN_DESC1 desc = { };
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = BACK_BUFFER_COUNT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    auto device = resource_->device;
    auto factory = gst_d3d12_device_get_factory_handle (device);
    auto cq = gst_d3d12_device_get_command_queue (device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    auto cq_handle = gst_d3d12_command_queue_get_handle (cq);

    ComPtr < IDXGISwapChain1 > swapchain;
    auto hr = factory->CreateSwapChainForHwnd (cq_handle, hwnd, &desc, nullptr,
        nullptr, &swapchain);
    if (FAILED (hr)) {
      if (is_expected_error (hr)) {
        GST_WARNING_OBJECT (window,
            "Expected error 0x%x, maybe window is being closed", (guint) hr);
        return GST_D3D12_WINDOW_FLOW_CLOSED;
      }

      gst_d3d12_result (hr, device);
      return GST_FLOW_ERROR;
    }

    hr = swapchain.As (&resource_->swapchain);
    if (!gst_d3d12_result (hr, device))
      return GST_FLOW_ERROR;

    is_new_swapchain = true;
  } else {
    resource_->clear_resource ();
    if (render_format_ != format) {
      gst_clear_object (&resource_->comp);
      gst_clear_object (&resource_->conv);
    } else {
      if (GST_VIDEO_INFO_FORMAT (in_info) != in_format_) {
        gst_clear_object (&resource_->conv);
      } else if (converter_config_ &&
          !gst_structure_is_equal (converter_config_, conv_config)) {
        gst_clear_object (&resource_->conv);
      }
    }
  }

  if (converter_config_)
    gst_structure_free (converter_config_);
  converter_config_ = gst_structure_copy (conv_config);

  if (!resource_->conv) {
    resource_->conv = gst_d3d12_converter_new (resource_->device, nullptr,
      in_info, out_info, nullptr, nullptr, gst_structure_copy (conv_config));
    if (!resource_->conv) {
      GST_ERROR ("Couldn't create converter");
      return GST_FLOW_ERROR;
    }
  } else {
    g_object_set (resource_->conv, "src-x", (gint) 0, "src-y", (gint) 0,
        "src-width", in_info->width, "src-height", in_info->height, nullptr);
  }

  if (!resource_->comp) {
    resource_->comp = gst_d3d12_overlay_compositor_new (resource_->device,
      out_info);
    if (!resource_->comp) {
      GST_ERROR ("Couldn't create overlay compositor");
      return GST_FLOW_ERROR;
    }
  }

  render_format_ = format;
  crop_rect_ = CD3DX12_BOX (0, 0, in_info->width, in_info->height);
  prev_crop_rect_ = crop_rect_;

  return resize_buffer (window);
}

void
SwapChain::disable_alt_enter (HWND hwnd)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!resource_ || !resource_->swapchain)
    return;

  HRESULT hr = E_FAIL;
  {
    /* DXGI API is not thread safe, takes global lock */
    static std::recursive_mutex factory_lock;
    std::lock_guard <std::recursive_mutex> flk (factory_lock);
    ComPtr < IDXGIFactory1 > parent_factory;
    auto hr = resource_->swapchain->GetParent (IID_PPV_ARGS (&parent_factory));
    if (SUCCEEDED (hr))
      hr = parent_factory->MakeWindowAssociation (hwnd, DXGI_MWA_NO_ALT_ENTER);
  }

  if (SUCCEEDED (hr))
    GST_DEBUG ("Alt-Enter is disabled for hwnd %p", hwnd);
}

GstFlowReturn
SwapChain::resize_buffer (GstD3D12Window * window)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!resource_->swapchain)
    return GST_FLOW_OK;

  auto device = resource_->device;

  resource_->clear_resource ();

  DXGI_SWAP_CHAIN_DESC desc = { };
  resource_->swapchain->GetDesc (&desc);
  auto hr = resource_->swapchain->ResizeBuffers (BACK_BUFFER_COUNT,
      0, 0, render_format_, desc.Flags);
  if (FAILED (hr)) {
    if (is_expected_error (hr)) {
      GST_WARNING_OBJECT (window,
          "Expected error 0x%x, maybe window is being closed", (guint) hr);
      return GST_D3D12_WINDOW_FLOW_CLOSED;
    }

    gst_d3d12_result (hr, device);
    return GST_FLOW_ERROR;
  }

  for (guint i = 0; i < BACK_BUFFER_COUNT; i++) {
    ComPtr < ID3D12Resource > backbuf;
    hr = resource_->swapchain->GetBuffer (i, IID_PPV_ARGS (&backbuf));
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (device, "Couldn't get backbuffer");
      return GST_FLOW_ERROR;
    }

    if (i == 0)
      resource_->buffer_desc = GetDesc (backbuf);

    auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, device,
        backbuf.Get (), 0, nullptr, nullptr);
    auto buf = gst_buffer_new ();
    gst_buffer_append_memory (buf, mem);
    auto swapbuf = std::make_shared < SwapChainBuffer > (buf, backbuf.Get ());
    resource_->buffers.push_back (swapbuf);
  }

  auto buffer_desc = resource_->buffer_desc;
  GstD3D12MSAAMode msaa_mode;
  gst_d3d12_window_get_msaa (window, msaa_mode);

  UINT sample_count = 1;
  switch (msaa_mode) {
    case GST_D3D12_MSAA_2X:
      sample_count = 2;
      break;
    case GST_D3D12_MSAA_4X:
      sample_count = 4;
      break;
    case GST_D3D12_MSAA_8X:
      sample_count = 8;
      break;
    default:
      break;
  }

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS feature_data = { };
  feature_data.Format = buffer_desc.Format;
  feature_data.SampleCount = sample_count;

  while (feature_data.SampleCount > 1) {
    hr = device_handle->CheckFeatureSupport (
      D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &feature_data, sizeof (feature_data));
    if (SUCCEEDED (hr) && feature_data.NumQualityLevels > 0)
      break;

    feature_data.SampleCount /= 2;
  }

  if (feature_data.SampleCount > 1 && feature_data.NumQualityLevels > 0) {
    GST_DEBUG_OBJECT (device, "Enable MSAA x%d with quality level %d",
        feature_data.SampleCount, feature_data.NumQualityLevels - 1);
    D3D12_HEAP_PROPERTIES heap_prop =
        CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC resource_desc =
        CD3DX12_RESOURCE_DESC::Tex2D (buffer_desc.Format,
        buffer_desc.Width, buffer_desc.Height,
        1, 1, feature_data.SampleCount, feature_data.NumQualityLevels - 1,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_CLEAR_VALUE clear_value = { };
    clear_value.Format = buffer_desc.Format;
    clear_value.Color[0] = 0.0f;
    clear_value.Color[1] = 0.0f;
    clear_value.Color[2] = 0.0f;
    clear_value.Color[3] = 1.0f;

    ComPtr < ID3D12Resource > msaa_texture;
    hr = device_handle->CreateCommittedResource (&heap_prop,
        D3D12_HEAP_FLAG_NONE,
        &resource_desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
        IID_PPV_ARGS (&msaa_texture));
    if (gst_d3d12_result (hr, device)) {
      auto mem = gst_d3d12_allocator_alloc_wrapped (nullptr, device,
        msaa_texture.Get (), 0, nullptr, nullptr);
      resource_->msaa_buf = gst_buffer_new ();
      gst_buffer_append_memory (resource_->msaa_buf, mem);
    }
  }

  first_present_ = true;
  backbuf_rendered_ = false;

  GstFlowReturn ret = GST_FLOW_OK;
  if (resource_->cached_buf) {
    ret = set_buffer (window, resource_->cached_buf);
    if (ret == GST_FLOW_OK)
      ret = present ();
  }

  return ret;
}

GstFlowReturn
SwapChain::set_buffer (GstD3D12Window * window, GstBuffer * buffer)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!resource_->swapchain) {
    if (!buffer) {
      GST_DEBUG_OBJECT (window, "Swapchain is not configured");
      return GST_FLOW_OK;
    }

    GST_ERROR_OBJECT (window, "Couldn't set buffer without swapchain");
    return GST_FLOW_ERROR;
  }

  if (buffer)
    gst_buffer_replace (&resource_->cached_buf, buffer);

  if (!resource_->cached_buf)
    return GST_FLOW_OK;

  auto crop_rect = crop_rect_;
  auto crop_meta = gst_buffer_get_video_crop_meta (resource_->cached_buf);
  if (crop_meta) {
    crop_rect = CD3DX12_BOX (crop_meta->x, crop_meta->y,
        crop_meta->x + crop_meta->width, crop_meta->y + crop_meta->height);
  }

  if (crop_rect != prev_crop_rect_) {
    g_object_set (resource_->conv, "src-x", (gint) crop_rect.left,
        "src-y", (gint) crop_rect.top,
        "src-width", (gint) (crop_rect.right - crop_rect.left),
        "src-height", (gint) (crop_rect.bottom - crop_rect.top), nullptr);
    prev_crop_rect_ = crop_rect;
  }

  before_rendering ();
  auto ret = gst_d3d12_window_render (window, resource_.get (),
      resource_->cached_buf, first_present_, output_rect_);
  after_rendering ();
  if (ret == GST_FLOW_OK)
    backbuf_rendered_ = true;

  return ret;
}

GstFlowReturn
SwapChain::present ()
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!resource_->swapchain)
    return GST_FLOW_ERROR;

  if (!backbuf_rendered_)
    return GST_FLOW_OK;

  DXGI_PRESENT_PARAMETERS params = { };
  if (!first_present_) {
    params.DirtyRectsCount = 1;
    params.pDirtyRects = &output_rect_;
  }

  auto hr = resource_->swapchain->Present1 (0, 0, &params);

  switch (hr) {
    case DXGI_ERROR_DEVICE_REMOVED:
    case E_OUTOFMEMORY:
      gst_d3d12_result (hr, resource_->device);
      return GST_FLOW_ERROR;
    case DXGI_ERROR_INVALID_CALL:
    case E_ACCESSDENIED:
      GST_WARNING ("Present failed, hr: 0x%x", (guint) hr);
      return GST_D3D12_WINDOW_FLOW_CLOSED;
    default:
      /* Ignore other return code */
      break;
  }

  first_present_ = false;
  backbuf_rendered_ = false;

  gst_d3d12_device_execute_command_lists (resource_->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT, 0, nullptr, &resource_->fence_val);
  resource_->prev_fence_val.push (resource_->fence_val);

  return GST_FLOW_OK;
}

void
SwapChain::expose (GstD3D12Window * window)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!resource_->swapchain || !resource_->cached_buf)
    return;

  auto ret = set_buffer (window, resource_->cached_buf);
  if (ret == GST_FLOW_OK)
    present ();
}

void
SwapChain::before_rendering ()
{
  UINT64 fence_val_to_wait = 0;
  auto resource = resource_.get ();

  while (resource->prev_fence_val.size () > BACK_BUFFER_COUNT + 1) {
    fence_val_to_wait = resource->prev_fence_val.front ();
    resource->prev_fence_val.pop ();
  }

  if (fence_val_to_wait) {
    auto completed = gst_d3d12_device_get_completed_value (resource->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (completed < fence_val_to_wait) {
      gst_d3d12_device_fence_wait (resource_->device,
          D3D12_COMMAND_LIST_TYPE_DIRECT, fence_val_to_wait,
          resource->event_handle);
    }
  }
}

void SwapChain::after_rendering ()
{
  resource_->prev_fence_val.push (resource_->fence_val);
}
/* *INDENT-ON* */

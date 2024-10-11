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

#ifdef WINAPI_PARTITION_APP
#undef WINAPI_PARTITION_APP
#endif

#define WINAPI_PARTITION_APP 1

#include "gstd3d12graphicscapture.h"
#include "gstd3d12pluginutils.h"
#include <d3d11_4.h>
#include <dxgi.h>
#include <directx/d3dx12.h>
#include <gmodule.h>
#include <dwmapi.h>
#include <winstring.h>
#include <roapi.h>
#include <dispatcherqueue.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <wrl.h>
#include <string.h>
#include <windows.h>
#include <windows.system.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_screen_capture_debug);
#define GST_CAT_DEFAULT gst_d3d12_screen_capture_debug

#define CAPTURE_POOL_SIZE 2

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace ABI::Windows::System;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Graphics;
using namespace ABI::Windows::Graphics::Capture;
using namespace ABI::Windows::Graphics::DirectX;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::DirectX::Direct3D11;

typedef ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable_t
    IFrameArrivedHandler;

typedef ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CGraphics__CCapture__CGraphicsCaptureItem_IInspectable_t
    IItemClosedHandler;

struct GraphicsCaptureVTable
{
  gboolean loaded;

  /* d3d11.dll */
  HRESULT (WINAPI * CreateDirect3D11DeviceFromDXGIDevice) (IDXGIDevice *
      dxgi_device, IInspectable ** graphics_device);

  /* combase.dll */
  HRESULT (WINAPI * RoInitialize) (RO_INIT_TYPE init_type);
  HRESULT (WINAPI * RoUninitialize) (void);
  HRESULT (WINAPI * WindowsCreateString) (PCNZWCH source_string,
      UINT32 length, HSTRING * string);
  HRESULT (WINAPI * WindowsDeleteString) (HSTRING string);
  HRESULT (WINAPI * RoGetActivationFactory) (HSTRING activatable_class_id,
      REFIID iid, void ** factory);

  /* user32.dll */
  DPI_AWARENESS_CONTEXT (WINAPI * SetThreadDpiAwarenessContext) (DPI_AWARENESS_CONTEXT context);

  /* coremessaging.dll */
  HRESULT (WINAPI * CreateDispatcherQueueController) (DispatcherQueueOptions options,
      PDISPATCHERQUEUECONTROLLER * controller);
};

static GraphicsCaptureVTable g_vtable = { };

#define LOAD_SYMBOL(module,name,func) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &g_vtable.func)) { \
    GST_WARNING ("Failed to load '%s', %s", G_STRINGIFY (name), g_module_error()); \
    if (d3d11_module) \
      g_module_close (d3d11_module); \
    if (combase_module) \
      g_module_close (combase_module); \
    if (user32_module) \
      g_module_close (user32_module); \
    return; \
  } \
} G_STMT_END

gboolean
gst_d3d12_graphics_capture_load_library (void)
{
  static GModule *d3d11_module = nullptr;
  static GModule *combase_module = nullptr;
  static GModule *user32_module = nullptr;
  static GModule *coremessaging_module = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    g_vtable.loaded = FALSE;

    d3d11_module = g_module_open ("d3d11.dll", G_MODULE_BIND_LAZY);
    if (!d3d11_module)
      return;

    combase_module = g_module_open ("combase.dll", G_MODULE_BIND_LAZY);
    if (!combase_module) {
      g_module_close (d3d11_module);
      return;
    }

    user32_module = g_module_open ("user32.dll", G_MODULE_BIND_LAZY);
    if (!user32_module) {
      g_module_close (combase_module);
      g_module_close (d3d11_module);
      return;
    }

    coremessaging_module =
        g_module_open ("coremessaging.dll", G_MODULE_BIND_LAZY);
    if (!coremessaging_module) {
      g_module_close (user32_module);
      g_module_close (combase_module);
      g_module_close (d3d11_module);
      return;
    }

    LOAD_SYMBOL (d3d11_module, CreateDirect3D11DeviceFromDXGIDevice,
        CreateDirect3D11DeviceFromDXGIDevice);
    LOAD_SYMBOL (combase_module, RoInitialize, RoInitialize);
    LOAD_SYMBOL (combase_module, RoUninitialize, RoUninitialize);
    LOAD_SYMBOL (combase_module, WindowsCreateString, WindowsCreateString);
    LOAD_SYMBOL (combase_module, WindowsDeleteString, WindowsDeleteString);
    LOAD_SYMBOL (combase_module, RoGetActivationFactory,
        RoGetActivationFactory);
    LOAD_SYMBOL (user32_module, SetThreadDpiAwarenessContext,
        SetThreadDpiAwarenessContext);
    LOAD_SYMBOL (coremessaging_module, CreateDispatcherQueueController,
        CreateDispatcherQueueController);

    g_vtable.loaded = TRUE;
  } GST_D3D12_CALL_ONCE_END;

  return g_vtable.loaded;
}

template < typename InterfaceType, PCNZWCH runtime_class_id >
static HRESULT
GstGetActivationFactory (InterfaceType ** factory)
{
  if (!gst_d3d12_graphics_capture_load_library ())
    return E_NOINTERFACE;

  HSTRING class_id_hstring;
  HRESULT hr = g_vtable.WindowsCreateString (runtime_class_id,
      wcslen (runtime_class_id), &class_id_hstring);

  if (FAILED (hr))
    return hr;

  hr = g_vtable.RoGetActivationFactory (class_id_hstring,
      IID_PPV_ARGS (factory));

  if (FAILED (hr)) {
    g_vtable.WindowsDeleteString (class_id_hstring);
    return hr;
  }

  return g_vtable.WindowsDeleteString (class_id_hstring);
}

class GraphicsCapture : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    FtmBase, IFrameArrivedHandler, IItemClosedHandler>
{
public:
  GraphicsCapture ()
  {
    gst_video_info_init (&pool_info_);
  }

  virtual ~GraphicsCapture ()
  {
    GST_INFO_OBJECT (obj_, "Fin");

    if (d3d12_pool_) {
      gst_buffer_pool_set_active (d3d12_pool_, FALSE);
      gst_object_unref (d3d12_pool_);
    }

    if (video_pool_) {
      gst_buffer_pool_set_active (video_pool_, FALSE);
      gst_object_unref (video_pool_);
    }

    gst_clear_object (&device12_);
  }

  STDMETHODIMP
  RuntimeClassInitialize (GstD3D12GraphicsCapture * obj,
      GstD3D12Device * device12, ID3D11Device * device11,
      IDirect3DDevice * d3d_device,
      IDirect3D11CaptureFramePoolStatics * pool_statics,
      IGraphicsCaptureItem * item, HWND window_handle)
  {
    obj_ = obj;
    device12_ = (GstD3D12Device *) gst_object_ref (device12);

    device11->QueryInterface (IID_PPV_ARGS (&device11_));
    if (!device11_) {
      GST_ERROR_OBJECT (obj_, "ID3D11Device5 interface unavailable");
      return E_FAIL;
    }

    ComPtr<ID3D11DeviceContext> context;
    device11_->GetImmediateContext (&context);
    context.As (&context_);
    if (!context_) {
      GST_ERROR_OBJECT (obj_, "ID3D11DeviceContext4 interface unavailable");
      return E_FAIL;
    }

    auto hr = device11_->CreateFence (0, D3D11_FENCE_FLAG_SHARED,
        IID_PPV_ARGS (&shared_fence11_));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj_, "Couldn't create d3d11 fence");
      return E_FAIL;
    }

    HANDLE fence_handle;
    hr = shared_fence11_->CreateSharedHandle (nullptr,
        GENERIC_ALL, nullptr, &fence_handle);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj_, "Couldn't create shared handle");
      return E_FAIL;
    }

    auto device12_handle = gst_d3d12_device_get_device_handle (device12);
    hr = device12_handle->OpenSharedHandle (fence_handle,
        IID_PPV_ARGS (&shared_fence12_));
    CloseHandle (fence_handle);
    if (!gst_d3d12_result (hr, device12)) {
      GST_ERROR_OBJECT (obj_, "Couldn't open d3d12 fence");
      return E_FAIL;
    }

    device_ = d3d_device;
    item_ = item;
    hwnd_ = window_handle;

    hr = item->get_Size (&frame_size_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj, "Couldn't query item size");
      return hr;
    }

    hr = item->add_Closed (this, &closed_token_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj, "Couldn't install closed callback");
      return hr;
    }

    hr = pool_statics->Create (d3d_device,
        DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
        CAPTURE_POOL_SIZE, frame_size_, &frame_pool_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj, "Couldn't create frame pool");
      return hr;
    }

    hr = frame_pool_->add_FrameArrived (this, &arrived_token_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj, "Couldn't install FrameArrived callback");
      return hr;
    }

    hr = frame_pool_->CreateCaptureSession (item, &session_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj, "Couldn't create session");
      return hr;
    }

    session_.As (&session2_);
    if (session2_)
      session2_->put_IsCursorCaptureEnabled (FALSE);

    session_.As (&session3_);
    if (session3_)
      session3_->put_IsBorderRequired (FALSE);

    hr = session_->StartCapture ();
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj, "Couldn't start capture");
      return hr;
    }

    return S_OK;
  }

  IFACEMETHOD(Invoke) (IDirect3D11CaptureFramePool * pool, IInspectable * arg)
  {
    ComPtr < IDirect3D11CaptureFrame > frame;

    GST_LOG_OBJECT (obj_, "Frame arrived");

    pool->TryGetNextFrame (&frame);
    if (!frame) {
      GST_WARNING_OBJECT (obj_, "No frame");
      return S_OK;
    }

    SizeInt32 frame_size;
    auto hr = frame->get_ContentSize (&frame_size);
    if (FAILED (hr)) {
      GST_WARNING_OBJECT (obj_, "Couldn't get content size");
      return S_OK;
    }

    if (frame_size.Width != frame_size_.Width ||
        frame_size.Height != frame_size_.Height) {
      GST_DEBUG_OBJECT (obj_, "Frame size changed %dx%d -> %dx%d",
          frame_size_.Width, frame_size_.Height,
          frame_size.Width, frame_size.Height);
      std::lock_guard <std::mutex> lk (lock_);
      if (d3d12_pool_) {
        gst_buffer_pool_set_active (d3d12_pool_, FALSE);
        gst_clear_object (&d3d12_pool_);
      }

      if (video_pool_) {
        gst_buffer_pool_set_active (video_pool_, FALSE);
        gst_clear_object (&video_pool_);
      }

      frame_ = nullptr;
      texture_ = nullptr;
      staging_ = nullptr;
      pool->Recreate (device_.Get (),
          DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
          CAPTURE_POOL_SIZE, frame_size);
      frame_size_ = frame_size;
      return S_OK;
    }

    ComPtr < IDirect3DSurface > surface;
    frame->get_Surface (&surface);
    if (!surface) {
      GST_WARNING_OBJECT (obj_, "IDirect3DSurface interface unavailable");
      return S_OK;
    }

    ComPtr < IDirect3DDxgiInterfaceAccess > access;
    surface.As (&access);
    if (!access) {
      GST_WARNING_OBJECT (obj_,
          "IDirect3DDxgiInterfaceAccess interface unavailable");
      return S_OK;
    }

    ComPtr < ID3D11Texture2D > texture;
    access->GetInterface (IID_PPV_ARGS (&texture));
    if (!texture) {
      GST_WARNING_OBJECT (obj_,
          "ID3D11Texture2D interface unavailable");
      return S_OK;
    }

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc (&desc);

    std::lock_guard <std::mutex> lk (lock_);
    crop_box_.left = 0;
    crop_box_.top = 0;
    crop_box_.right = desc.Width;
    crop_box_.bottom = desc.Height;
    crop_box_.front = 0;
    crop_box_.back = 1;
    texture_ = texture;
    frame_ = frame;

    if (hwnd_ && client_only_)
      updateClientBox (desc, crop_box_);

    cond_.notify_all ();

    return S_OK;
  }

  IFACEMETHOD(Invoke) (IGraphicsCaptureItem * pool, IInspectable * arg)
  {
    GST_INFO_OBJECT (obj_, "Item closed");

    std::lock_guard <std::mutex> lk (lock_);
    closed_ = true;
    cond_.notify_all ();

    return S_OK;
  }

  void SetCursorEnabled (boolean value)
  {
    if (session2_)
      session2_->put_IsCursorCaptureEnabled (value);
  }

  void SetBorderRequired (boolean value)
  {
    if (session3_)
      session3_->put_IsBorderRequired (value);
  }

  void SetClientOnly (bool value)
  {
    client_only_ = value;
  }

  void GetSize (guint * width, guint * height)
  {
    std::lock_guard <std::mutex> lk (lock_);
    *width = MAX (1, (guint) frame_size_.Width);
    *height = MAX (1, (guint) frame_size_.Height);
  }

  GstFlowReturn GetD3D12Frame (const CaptureCropRect * crop_rect,
      GstBuffer ** buffer, guint * width, guint * height)
  {
    std::unique_lock <std::mutex> lk (lock_);
    while (!frame_ && !flushing_ && !closed_)
      cond_.wait (lk);

    if (flushing_)
      return GST_FLOW_FLUSHING;
    if (closed_)
      return GST_FLOW_EOS;

    guint crop_w = crop_box_.right - crop_box_.left;
    guint crop_h = crop_box_.bottom - crop_box_.top;
    D3D12_BOX crop_box = crop_box_;
    if (crop_rect->crop_x + crop_rect->crop_w > crop_w ||
        crop_rect->crop_y + crop_rect->crop_h > crop_h) {
      /* Ignore this crop rect */
    } else {
      if (crop_rect->crop_w)
        crop_w = crop_rect->crop_w;
      if (crop_rect->crop_h)
        crop_h = crop_rect->crop_h;

      crop_box.left += crop_rect->crop_x;
      crop_box.top += crop_rect->crop_y;
      crop_box.right = crop_box.left + crop_w;
      crop_box.bottom = crop_box.top + crop_h;
    }

    if (!d3d12_pool_ || pool_info_.width != crop_w ||
        pool_info_.height != crop_h) {
      GST_DEBUG_OBJECT (obj_, "Size changed, recrate buffer pool");

      if (d3d12_pool_) {
        gst_buffer_pool_set_active (d3d12_pool_, FALSE);
        gst_clear_object (&d3d12_pool_);
      }

      gst_video_info_set_format (&pool_info_, GST_VIDEO_FORMAT_BGRA,
          crop_w, crop_h);
      d3d12_pool_ = gst_d3d12_buffer_pool_new (device12_);
      if (!d3d12_pool_) {
        GST_ERROR_OBJECT (obj_, "Couldn't create buffer pool");
        return GST_FLOW_ERROR;
      }

      auto caps = gst_video_info_to_caps (&pool_info_);
      auto config = gst_buffer_pool_get_config (d3d12_pool_);
      gst_buffer_pool_config_set_params (config, caps, pool_info_.size, 0, 0);
      gst_caps_unref (caps);

      auto params = gst_d3d12_allocation_params_new (device12_, &pool_info_,
          GST_D3D12_ALLOCATION_FLAG_DEFAULT,
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_SHARED);
      gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
      gst_d3d12_allocation_params_free (params);
      if (!gst_buffer_pool_set_config (d3d12_pool_, config)) {
        GST_ERROR_OBJECT (obj_, "Couldn't set buffer pool config");
        gst_clear_object (&d3d12_pool_);
        return GST_FLOW_ERROR;
      }

      if (!gst_buffer_pool_set_active (d3d12_pool_, TRUE)) {
        GST_ERROR_OBJECT (obj_, "Couldn't activate pool");
        gst_clear_object (&d3d12_pool_);
        return GST_FLOW_ERROR;
      }
    }

    GstBuffer *outbuf = nullptr;
    gst_buffer_pool_acquire_buffer (d3d12_pool_, &outbuf, nullptr);
    if (!outbuf) {
      GST_ERROR_OBJECT (obj_, "Couldn't acquire buffer");
      return GST_FLOW_ERROR;
    }

    GstMapInfo map_info;
    auto mem = gst_buffer_peek_memory (outbuf, 0);
    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    GST_MEMORY_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);
    gst_d3d12_memory_sync (dmem);

    auto texture11 = gst_d3d12_memory_get_d3d11_texture (dmem,
        device11_.Get ());
    if (!texture11) {
      GST_ERROR_OBJECT (obj_, "Couldn't get sharable d3d11 texture");
      gst_buffer_unref (outbuf);
      return GST_FLOW_ERROR;
    }

    if (!gst_memory_map (mem, &map_info, GST_MAP_WRITE_D3D12)) {
      GST_ERROR_OBJECT (obj_, "Couldn't map memory");
      gst_buffer_unref (outbuf);
      return GST_FLOW_ERROR;
    }

    context_->CopySubresourceRegion (texture11,
        0, 0, 0, 0, texture_.Get (), 0, (const D3D11_BOX *) &crop_box);
    fence_val_++;
    context_->Signal (shared_fence11_.Get (), fence_val_);
    gst_memory_unmap (mem, &map_info);

    gst_d3d12_memory_set_fence (dmem,
        shared_fence12_.Get (), fence_val_, FALSE);

    *width = crop_w;
    *height = crop_h;
    *buffer = outbuf;

    return GST_FLOW_OK;
  }

  GstFlowReturn GetVideoFrame (const CaptureCropRect * crop_rect,
      GstBuffer ** buffer, guint * width, guint * height)
  {
    std::unique_lock <std::mutex> lk (lock_);
    while (!frame_ && !flushing_ && !closed_)
      cond_.wait (lk);

    if (flushing_)
      return GST_FLOW_FLUSHING;
    if (closed_)
      return GST_FLOW_EOS;

    guint crop_w = crop_box_.right - crop_box_.left;
    guint crop_h = crop_box_.bottom - crop_box_.top;
    D3D12_BOX crop_box = crop_box_;
    if (crop_rect->crop_x + crop_rect->crop_w > crop_w ||
        crop_rect->crop_y + crop_rect->crop_h > crop_h) {
      /* Ignore this crop rect */
    } else {
      if (crop_rect->crop_w)
        crop_w = crop_rect->crop_w;
      if (crop_rect->crop_h)
        crop_h = crop_rect->crop_h;

      crop_box.left += crop_rect->crop_x;
      crop_box.top += crop_rect->crop_y;
      crop_box.right = crop_box.left + crop_w;
      crop_box.bottom = crop_box.top + crop_h;
    }

    if (!video_pool_ || pool_info_.width != crop_w ||
        pool_info_.height != crop_h) {
      GST_DEBUG_OBJECT (obj_, "Size changed, recrate buffer pool");

      if (video_pool_) {
        gst_buffer_pool_set_active (video_pool_, FALSE);
        gst_clear_object (&video_pool_);
      }

      staging_ = nullptr;

      D3D11_TEXTURE2D_DESC desc = { };
      desc.Width = crop_w;
      desc.Height = crop_h;
      desc.MipLevels = 1;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      desc.SampleDesc.Count = 1;
      desc.ArraySize = 1;
      desc.Usage = D3D11_USAGE_STAGING;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

      auto hr = device11_->CreateTexture2D (&desc, nullptr, &staging_);
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (obj_, "Couldn't create staging texture");
        return GST_FLOW_ERROR;
      }

      gst_video_info_set_format (&pool_info_, GST_VIDEO_FORMAT_BGRA,
          crop_w, crop_h);
      video_pool_ = gst_video_buffer_pool_new ();
      if (!video_pool_) {
        GST_ERROR_OBJECT (obj_, "Couldn't create buffer pool");
        return GST_FLOW_ERROR;
      }

      auto caps = gst_video_info_to_caps (&pool_info_);
      auto config = gst_buffer_pool_get_config (video_pool_);
      gst_buffer_pool_config_set_params (config, caps, pool_info_.size, 0, 0);
      gst_caps_unref (caps);

      if (!gst_buffer_pool_set_config (video_pool_, config)) {
        GST_ERROR_OBJECT (obj_, "Couldn't set buffer pool config");
        gst_clear_object (&video_pool_);
        return GST_FLOW_ERROR;
      }

      if (!gst_buffer_pool_set_active (video_pool_, TRUE)) {
        GST_ERROR_OBJECT (obj_, "Couldn't activate pool");
        gst_clear_object (&video_pool_);
        return GST_FLOW_ERROR;
      }
    }

    context_->CopySubresourceRegion (staging_.Get (), 0, 0, 0, 0,
        texture_.Get (), 0, (const D3D11_BOX *) &crop_box);

    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    auto hr = context_->Map (staging_.Get (), 0, D3D11_MAP_READ,
        0, &mapped_resource);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (obj_, "Couldn't map staging texture");
      return GST_FLOW_ERROR;
    }

    GstBuffer *outbuf = nullptr;
    gst_buffer_pool_acquire_buffer (video_pool_, &outbuf, nullptr);
    if (!outbuf) {
      GST_ERROR_OBJECT (obj_, "Couldn't acquire buffer");
      context_->Unmap (staging_.Get (), 0);
      return GST_FLOW_ERROR;
    }

    GstVideoFrame vframe;
    if (!gst_video_frame_map (&vframe, &pool_info_, outbuf, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (obj_, "Couldn't map video frame");
      context_->Unmap (staging_.Get (), 0);
      gst_buffer_unref (outbuf);
      return GST_FLOW_ERROR;
    }

    guint8 *dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
    guint8 *src = (guint8 *) mapped_resource.pData;
    guint width_in_bytes = GST_VIDEO_FRAME_COMP_PSTRIDE (&vframe, 0)
        * GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);
    for (guint i = 0; i < GST_VIDEO_FRAME_HEIGHT (&vframe); i++) {
      memcpy (dst, src, width_in_bytes);
      dst += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      src += mapped_resource.RowPitch;
    }
    gst_video_frame_unmap (&vframe);
    context_->Unmap (staging_.Get (), 0);

    *width = crop_w;
    *height = crop_h;
    *buffer = outbuf;

    return GST_FLOW_OK;
  }

  void SetFlushing (bool flushing)
  {
    std::lock_guard <std::mutex> lk (lock_);
    flushing_ = flushing;
    cond_.notify_all ();
  }

  void Close ()
  {
    if (item_)
      item_->remove_Closed (closed_token_);

    if (frame_pool_)
      frame_pool_->remove_FrameArrived (arrived_token_);

    texture_ = nullptr;
    frame_ = nullptr;
    staging_ = nullptr;

    if (session_) {
      ComPtr<IClosable> closable;
      session_.As (&closable);
      if (closable)
        closable->Close ();
    }

    if (frame_pool_) {
      ComPtr<IClosable> closable;
      frame_pool_.As (&closable);
      if (closable)
        closable->Close ();
    }

    if (item_) {
      ComPtr<IClosable> closable;
      item_.As (&closable);
      if (closable)
        closable->Close ();
    }
  }

private:
  bool updateClientBox (const D3D11_TEXTURE2D_DESC & desc, D3D12_BOX & box)
  {
    if (IsIconic (hwnd_))
      return false;

    RECT client_rect;
    if (!GetClientRect (hwnd_, &client_rect))
      return false;

    if (client_rect.right <= 0 || client_rect.bottom <= 0)
      return false;

    RECT bound_rect;
    auto hr = DwmGetWindowAttribute (hwnd_,
        DWMWA_EXTENDED_FRAME_BOUNDS, &bound_rect, sizeof (RECT));
    if (FAILED (hr))
      return false;

    POINT client_pos = { };
    if (!ClientToScreen (hwnd_, &client_pos))
      return false;

    UINT left = 0;
    if (client_pos.x > bound_rect.left)
      left = client_pos.x - bound_rect.left;

    UINT width = 1;
    if (desc.Width > left) {
      width = desc.Width - left;
      if (width > client_rect.right)
        width = client_rect.right;
    }

    UINT right = left + width;
    if (right > desc.Width)
      return false;

    UINT top = 0;
    if (client_pos.y > bound_rect.top)
      top = client_pos.y - bound_rect.top;

    UINT height = 1;
    if (desc.Height > top) {
      height = desc.Height - top;
      if (height > client_rect.bottom)
        height = client_rect.bottom;
    }

    UINT bottom = top + height;
    if (bottom > desc.Height)
      return false;

    box.left = left;
    box.top = top;
    box.right = right;
    box.bottom = bottom;
    box.front = 0;
    box.back = 1;

    return true;
  }

private:
  GstD3D12GraphicsCapture *obj_ = nullptr;
  HWND hwnd_ = nullptr;
  GstD3D12Device *device12_ = nullptr;
  GstVideoInfo pool_info_;
  GstBufferPool *d3d12_pool_ = nullptr;
  GstBufferPool *video_pool_ = nullptr;
  ComPtr<ID3D11Device5> device11_;
  ComPtr<ID3D11DeviceContext4> context_;
  ComPtr<IDirect3DDevice> device_;
  ComPtr<IGraphicsCaptureItem> item_;
  ComPtr<IDirect3D11CaptureFramePool> frame_pool_;
  ComPtr<IGraphicsCaptureSession> session_;
  ComPtr<IGraphicsCaptureSession2> session2_;
  ComPtr<IGraphicsCaptureSession3> session3_;
  ComPtr<IDirect3D11CaptureFrame> frame_;
  ComPtr<ID3D11Texture2D> texture_;
  ComPtr<ID3D11Texture2D> staging_;
  SizeInt32 frame_size_ = { };
  EventRegistrationToken arrived_token_ = { };
  EventRegistrationToken closed_token_ = { };
  std::mutex lock_;
  std::condition_variable cond_;
  bool closed_ = false;
  bool flushing_ = false;
  std::atomic<bool> client_only_ = { false };
  D3D12_BOX crop_box_ = { };
  UINT64 fence_val_ = 0;
  ComPtr<ID3D11Fence> shared_fence11_;
  ComPtr<ID3D12Fence> shared_fence12_;
};

class AsyncWaiter : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    FtmBase, IAsyncActionCompletedHandler>
{
public:
  STDMETHODIMP
  RuntimeClassInitialize (HANDLE event_handle)
  {
    event_handle_ = event_handle;
    return S_OK;
  }

  STDMETHOD (Invoke) (IAsyncAction * action, AsyncStatus status)
  {
    SetEvent (event_handle_);

    return S_OK;
  }

private:
  HANDLE event_handle_;
};

enum LoopState
{
  LOOP_STATE_INIT,
  LOOP_STATE_RUNNING,
  LOOP_STATE_STOPPED,
};

struct GstD3D12GraphicsCapturePrivate
{
  GstD3D12GraphicsCapturePrivate ()
  {
    shutdown_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  }

  ~GstD3D12GraphicsCapturePrivate ()
  {
    SetEvent (shutdown_handle);
    g_clear_pointer (&loop_thread, g_thread_join);

    CloseHandle (shutdown_handle);
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;
  ComPtr<ID3D11Device> d3d11_device;
  ComPtr<GraphicsCapture> capture;
  HMONITOR monitor_handle = nullptr;
  HWND window_handle = nullptr;
  HWND hidden_hwnd = nullptr;
  HANDLE shutdown_handle;
  GThread *loop_thread = nullptr;
  LoopState loop_state = LOOP_STATE_INIT;
  std::mutex loop_lock;
  std::condition_variable loop_cond;
};
/* *INDENT-ON* */

struct _GstD3D12GraphicsCapture
{
  GstObject parent;

  GstD3D12GraphicsCapturePrivate *priv;
};

static void gst_d3d12_graphics_capture_finalize (GObject * object);

static GstFlowReturn
gst_d3d12_graphics_capture_prepare (GstD3D12ScreenCapture * capture);
static gboolean
gst_d3d12_graphics_capture_get_size (GstD3D12ScreenCapture * capture,
    guint * width, guint * height);
static gboolean
gst_d3d12_graphics_capture_unlock (GstD3D12ScreenCapture * capture);
static gboolean
gst_d3d12_graphics_capture_unlock_stop (GstD3D12ScreenCapture * capture);

#define gst_d3d12_graphics_capture_parent_class parent_class
G_DEFINE_TYPE (GstD3D12GraphicsCapture,
    gst_d3d12_graphics_capture, GST_TYPE_D3D12_SCREEN_CAPTURE);

static void
gst_d3d12_graphics_capture_class_init (GstD3D12GraphicsCaptureClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto capture_class = GST_D3D12_SCREEN_CAPTURE_CLASS (klass);

  object_class->finalize = gst_d3d12_graphics_capture_finalize;

  capture_class->prepare =
      GST_DEBUG_FUNCPTR (gst_d3d12_graphics_capture_prepare);
  capture_class->get_size =
      GST_DEBUG_FUNCPTR (gst_d3d12_graphics_capture_get_size);
  capture_class->unlock = GST_DEBUG_FUNCPTR (gst_d3d12_graphics_capture_unlock);
  capture_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_d3d12_graphics_capture_unlock_stop);
}

static void
gst_d3d12_graphics_capture_init (GstD3D12GraphicsCapture * self)
{
  self->priv = new GstD3D12GraphicsCapturePrivate ();
}

static void
gst_d3d12_graphics_capture_finalize (GObject * object)
{
  auto self = GST_D3D12_GRAPHICS_CAPTURE (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
open_device (GstD3D12GraphicsCapture * self, IDirect3DDevice ** d3d_device)
{
  auto priv = self->priv;

  static const D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
  };

  auto adapter = gst_d3d12_device_get_adapter_handle (priv->device);
  auto hr = D3D11CreateDevice (adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
      G_N_ELEMENTS (feature_levels), D3D11_SDK_VERSION,
      &priv->d3d11_device, nullptr, nullptr);

  if (FAILED (hr)) {
    hr = D3D11CreateDevice (adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, &feature_levels[1],
        G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION,
        &priv->d3d11_device, nullptr, nullptr);
  }

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create d3d11 device");
    return FALSE;
  }

  ComPtr < ID3D10Multithread > multi_thread;
  priv->d3d11_device.As (&multi_thread);
  if (multi_thread)
    multi_thread->SetMultithreadProtected (TRUE);

  ComPtr < IDXGIDevice > dxgi_device;
  hr = priv->d3d11_device.As (&dxgi_device);
  if (FAILED (hr)) {
    GST_WARNING_OBJECT (self, "IDXGIDevice interface unavailable");
    return FALSE;
  }

  ComPtr < IInspectable > inspectable;
  hr = g_vtable.CreateDirect3D11DeviceFromDXGIDevice (dxgi_device.Get (),
      &inspectable);
  if (FAILED (hr)) {
    GST_WARNING_OBJECT (self, "CreateDirect3D11DeviceFromDXGIDevice failed");
    return FALSE;
  }

  ComPtr < IDirect3DDevice > device;
  hr = inspectable.As (&device);
  if (FAILED (hr)) {
    GST_WARNING_OBJECT (self, "IDirect3DDevice interface unavailable");
    return FALSE;
  }

  *d3d_device = device.Detach ();

  return TRUE;
}

static gboolean
create_session (GstD3D12GraphicsCapture * self, IDirect3DDevice * d3d_device)
{
  auto priv = self->priv;
  auto capture = priv->capture;

  ComPtr < IGraphicsCaptureItemInterop > interop;
  auto hr = GstGetActivationFactory < IGraphicsCaptureItemInterop,
      RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem > (&interop);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "IGraphicsCaptureItemInterop is not available");
    return FALSE;
  }

  ComPtr < IGraphicsCaptureItem > item;
  if (priv->monitor_handle) {
    hr = interop->CreateForMonitor (priv->monitor_handle, IID_PPV_ARGS (&item));
  } else {
    hr = interop->CreateForWindow (priv->window_handle, IID_PPV_ARGS (&item));
  }

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create item");
    return FALSE;
  }

  ComPtr < IDirect3D11CaptureFramePoolStatics > pool_statics;
  hr = GstGetActivationFactory < IDirect3D11CaptureFramePoolStatics,
      RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool >
      (&pool_statics);
  if (FAILED (hr)) {
    GST_WARNING_OBJECT (self,
        "IDirect3D11CaptureFramePoolStatics is unavailable");
    return FALSE;
  }

  hr = MakeAndInitialize < GraphicsCapture > (&priv->capture,
      self, priv->device, priv->d3d11_device.Get (), d3d_device,
      pool_statics.Get (), item.Get (), priv->window_handle);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't initialize capture object");
    return FALSE;
  }

  return TRUE;
}

static gpointer
gst_d3d11_graphics_thread_func (GstD3D12GraphicsCapture * self)
{
  HRESULT hr;
  ComPtr < IDispatcherQueueController > queue_ctrl;
  auto priv = self->priv;
  HANDLE event_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  ComPtr < IDirect3DDevice > d3d_device;
  HANDLE waitables[] = { priv->shutdown_handle, event_handle };
  ComPtr < AsyncWaiter > async_waiter;
  ComPtr < IAsyncAction > shutdown_action;

  GST_INFO_OBJECT (self, "Entering loop thread");

  g_vtable.SetThreadDpiAwarenessContext
      (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

  g_vtable.RoInitialize (RO_INIT_MULTITHREADED);

  hr = MakeAndInitialize < AsyncWaiter > (&async_waiter, event_handle);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create async waiter");
    goto out;
  }

  DispatcherQueueOptions queue_opt;
  queue_opt.dwSize = sizeof (DispatcherQueueOptions);
  queue_opt.threadType = DQTYPE_THREAD_CURRENT;
  queue_opt.apartmentType = DQTAT_COM_NONE;

  hr = g_vtable.CreateDispatcherQueueController (queue_opt, &queue_ctrl);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create dispatcher queue controller");
    goto out;
  }

  if (!open_device (self, &d3d_device)) {
    GST_ERROR_OBJECT (self, "Couldn't open device");
    goto out;
  }

  if (!create_session (self, d3d_device.Get ())) {
    GST_ERROR_OBJECT (self, "Couldn't open session");
    goto out;
  }

  {
    std::lock_guard < std::mutex > lk (priv->loop_lock);
    priv->loop_state = LOOP_STATE_RUNNING;
    priv->loop_cond.notify_all ();
  }

  while (true) {
    MSG msg;
    while (PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }

    auto wait_ret = MsgWaitForMultipleObjects (G_N_ELEMENTS (waitables),
        waitables, FALSE, INFINITE, QS_ALLINPUT);

    if (wait_ret == WAIT_OBJECT_0) {
      GST_DEBUG_OBJECT (self, "Begin shutdown");
      priv->capture->Close ();
      hr = queue_ctrl->ShutdownQueueAsync (&shutdown_action);
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Shutdown failed");
        break;
      }

      hr = shutdown_action->put_Completed (async_waiter.Get ());
      if (FAILED (hr)) {
        GST_ERROR_OBJECT (self, "Couldn't install shutdown callback");
        break;
      }
    } else if (wait_ret == WAIT_OBJECT_0 + 1) {
      GST_DEBUG_OBJECT (self, "Shutdown completed");
      break;
    } else if (wait_ret != WAIT_OBJECT_0 + G_N_ELEMENTS (waitables)) {
      GST_ERROR_OBJECT (self, "Unexpected wait return %u", (guint) wait_ret);
      break;
    }
  }

out:
  {
    std::lock_guard < std::mutex > lk (priv->loop_lock);
    priv->loop_state = LOOP_STATE_STOPPED;
    priv->loop_cond.notify_all ();
  }

  priv->capture = nullptr;
  queue_ctrl = nullptr;
  shutdown_action = nullptr;
  async_waiter = nullptr;

  if (d3d_device) {
    ComPtr < IClosable > closable;
    d3d_device.As (&closable);
    if (closable)
      closable->Close ();

    closable = nullptr;
    d3d_device = nullptr;
  }

  CloseHandle (event_handle);

  g_vtable.RoUninitialize ();

  GST_INFO_OBJECT (self, "Leaving loop thread");

  return nullptr;
}

static GstFlowReturn
gst_d3d12_graphics_capture_prepare (GstD3D12ScreenCapture * capture)
{
  return GST_FLOW_OK;
}

static gboolean
gst_d3d12_graphics_capture_get_size (GstD3D12ScreenCapture * capture,
    guint * width, guint * height)
{
  auto self = GST_D3D12_GRAPHICS_CAPTURE (capture);
  auto priv = self->priv;
  priv->capture->GetSize (width, height);

  return TRUE;
}

static gboolean
gst_d3d12_graphics_capture_unlock (GstD3D12ScreenCapture * capture)
{
  auto self = GST_D3D12_GRAPHICS_CAPTURE (capture);
  auto priv = self->priv;
  priv->capture->SetFlushing (true);

  return TRUE;
}

static gboolean
gst_d3d12_graphics_capture_unlock_stop (GstD3D12ScreenCapture * capture)
{
  auto self = GST_D3D12_GRAPHICS_CAPTURE (capture);
  auto priv = self->priv;
  priv->capture->SetFlushing (false);

  return TRUE;
}

GstD3D12ScreenCapture *
gst_d3d12_graphics_capture_new (GstD3D12Device * device, HWND window_handle,
    HMONITOR monitor_handle)
{
  g_return_val_if_fail (device, nullptr);

  if (!gst_d3d12_graphics_capture_load_library ()) {
    GST_WARNING ("Couldn't load library");
    return nullptr;
  }

  if (window_handle && !IsWindow (window_handle)) {
    GST_ERROR ("%p is not a valid HWND", window_handle);
    return nullptr;
  }

  auto self = (GstD3D12GraphicsCapture *)
      g_object_new (GST_TYPE_D3D12_GRAPHICS_CAPTURE, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->window_handle = window_handle;
  priv->monitor_handle = monitor_handle;

  priv->loop_thread = g_thread_new ("GstD3D12GraphicsCapture",
      (GThreadFunc) gst_d3d11_graphics_thread_func, self);

  bool configured = false;
  {
    std::unique_lock < std::mutex > lk (priv->loop_lock);
    while (priv->loop_state == LOOP_STATE_INIT)
      priv->loop_cond.wait (lk);

    if (priv->loop_state == LOOP_STATE_RUNNING)
      configured = true;
  }

  if (!configured)
    gst_clear_object (&self);

  return (GstD3D12ScreenCapture *) self;
}

void
gst_d3d12_graphics_capture_show_border (GstD3D12GraphicsCapture * capture,
    gboolean show)
{
  auto priv = capture->priv;
  priv->capture->SetBorderRequired (show);
}

void
gst_d3d12_graphics_capture_show_cursor (GstD3D12GraphicsCapture * capture,
    gboolean show)
{
  auto priv = capture->priv;
  priv->capture->SetCursorEnabled (show);
}

void
gst_d3d12_graphics_capture_set_client_only (GstD3D12GraphicsCapture * capture,
    gboolean client_only)
{
  auto priv = capture->priv;
  priv->capture->SetClientOnly (client_only);
}

GstFlowReturn
gst_d3d12_graphics_capture_do_capture (GstD3D12GraphicsCapture * capture,
    gboolean is_d3d12, const CaptureCropRect * crop_rect, GstBuffer ** buffer,
    guint * width, guint * height)
{
  auto priv = capture->priv;

  if (is_d3d12)
    return priv->capture->GetD3D12Frame (crop_rect, buffer, width, height);

  return priv->capture->GetVideoFrame (crop_rect, buffer, width, height);
}

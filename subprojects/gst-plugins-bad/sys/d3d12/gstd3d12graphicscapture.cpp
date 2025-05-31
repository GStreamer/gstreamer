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
#include <queue>
#include <functional>

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

struct QueueManger;

static gpointer dispatcher_main_thread (QueueManger * m);

struct EnqueueData
{
  ComPtr<IDispatcherQueueHandler> handler;
  std::mutex lock;
  std::condition_variable cond;
  bool signalled = false;
  HRESULT hr;
};

enum LoopState
{
  LOOP_STATE_INIT,
  LOOP_STATE_RUNNING,
  LOOP_STATE_STOPPED,
};

struct QueueManger
{
  QueueManger (const QueueManger &) = delete;
  QueueManger& operator= (const QueueManger &) = delete;

  static QueueManger * GetInstance ()
  {
    static QueueManger *ins = nullptr;
    GST_D3D12_CALL_ONCE_BEGIN {
      ins = new QueueManger ();
    } GST_D3D12_CALL_ONCE_END;

    return ins;
  }

  HRESULT RunOnDispatcherThread (IDispatcherQueueHandler * handler)
  {
    if (!Init ())
      return E_FAIL;

    auto item = std::make_shared<EnqueueData> ();
    item->handler = handler;

    {
      std::lock_guard <std::mutex> lk (loop_lock);
      user_events.push (item);
    }

    SetEvent (enqueue_handle);

    std::unique_lock <std::mutex> lk (item->lock);
    while (!item->signalled)
      item->cond.wait (lk);

    return item->hr;
  }

  static void Deinit ()
  {
    auto ins = GetInstance ();
    SetEvent (ins->shutdown_handle);
    if (ins->thread)
      g_thread_join (ins->thread);

    delete ins;
  }

private:
  QueueManger ()
  {
    shutdown_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    enqueue_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  }

  ~QueueManger ()
  {
    CloseHandle (shutdown_handle);
    CloseHandle (enqueue_handle);
  }

  bool Init ()
  {
    static std::once_flag once;
    std::call_once (once, [] {
      gst_d3d12_graphics_capture_load_library ();
    });

    if (!g_vtable.loaded)
      return false;

    std::unique_lock <std::mutex> lk (loop_lock);
    if (!thread) {
      thread = g_thread_new ("DispatcherThread",
          (GThreadFunc) dispatcher_main_thread, this);
    }

    while (loop_state == LOOP_STATE_INIT)
      loop_cond.wait (lk);

    if (loop_state == LOOP_STATE_RUNNING)
      return true;

    return false;
  }

public:
  LoopState loop_state = LOOP_STATE_INIT;
  IDispatcherQueueController *queue_ctrl = nullptr;
  HANDLE shutdown_handle;
  HANDLE enqueue_handle;
  GThread *thread = nullptr;
  std::mutex loop_lock;
  std::condition_variable loop_cond;
  std::queue<std::shared_ptr<EnqueueData>> user_events;
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

static void
dispatcher_main_thread_inner (QueueManger * m)
{
  DispatcherQueueOptions queue_opt;
  queue_opt.dwSize = sizeof (DispatcherQueueOptions);
  queue_opt.threadType = DQTYPE_THREAD_CURRENT;
  queue_opt.apartmentType = DQTAT_COM_NONE;

  auto hr = g_vtable.CreateDispatcherQueueController (queue_opt,
      &m->queue_ctrl);
  if (FAILED (hr)) {
    GST_ERROR ("Couldn't create queue ctrl");
    return;
  }

  ComPtr < AsyncWaiter > async_waiter;
  ComPtr < IAsyncAction > shutdown_action;

  HANDLE event_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  hr = MakeAndInitialize < AsyncWaiter > (&async_waiter, event_handle);
  if (FAILED (hr)) {
    GST_ERROR ("Couldn't create async waiter");
    return;
  }

  {
    std::lock_guard <std::mutex> lk (m->loop_lock);
    GST_DEBUG ("Loop running");
    m->loop_state = LOOP_STATE_RUNNING;
    m->loop_cond.notify_all ();
  }

  HANDLE waitables[] = { m->shutdown_handle, event_handle, m->enqueue_handle };

  while (true) {
    MSG msg;
    while (PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }

    auto wait_ret = MsgWaitForMultipleObjects (G_N_ELEMENTS (waitables),
            waitables, FALSE, INFINITE, QS_ALLINPUT);

    if (wait_ret == WAIT_OBJECT_0) {
      hr = m->queue_ctrl->ShutdownQueueAsync (&shutdown_action);
      if (FAILED (hr)) {
        GST_ERROR ("Shutdown failed");
        return;
      }

      hr = shutdown_action->put_Completed (async_waiter.Get ());
      if (FAILED (hr)) {
        GST_ERROR ("Couldn't put completed");
        return;
      }
    } else if (wait_ret == WAIT_OBJECT_0 + 1) {
      GST_DEBUG ("Shutdown completed");
      if (shutdown_action)
        shutdown_action->GetResults ();

      return;
    } else if (wait_ret == WAIT_OBJECT_0 + 2) {
      std::unique_lock <std::mutex> lk (m->loop_lock);
      while (!m->user_events.empty ()) {
        auto item = m->user_events.front ();
        m->user_events.pop ();
        lk.unlock ();

        item->hr = item->handler->Invoke ();

        {
          std::lock_guard <std::mutex> item_lk (item->lock);
          item->signalled = true;
          item->cond.notify_all ();
        }

        lk.lock ();
      }

    } else if (wait_ret != WAIT_OBJECT_0 + G_N_ELEMENTS (waitables)) {
      GST_ERROR ("Unexpected wait return %u", (guint) wait_ret);
      return;
    }
  }
}

static gpointer
dispatcher_main_thread (QueueManger * m)
{
  g_vtable.SetThreadDpiAwarenessContext
      (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
  g_vtable.RoInitialize (RO_INIT_MULTITHREADED);

  dispatcher_main_thread_inner (m);

  g_vtable.RoUninitialize ();

  std::lock_guard <std::mutex> lk (m->loop_lock);
  m->loop_state = LOOP_STATE_STOPPED;
  m->loop_cond.notify_all ();

  return nullptr;
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
    GST_INFO_OBJECT (device12_, "Fin");

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
  RuntimeClassInitialize (GstD3D12Device * device, HMONITOR monitor, HWND hwnd)
  {
    device12_ = (GstD3D12Device *) gst_object_ref (device);

    static const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
    };

    hwnd_ = hwnd;

    auto adapter = gst_d3d12_device_get_adapter_handle (device);
    ComPtr<ID3D11Device> device11;

    auto hr = D3D11CreateDevice (adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels,
        G_N_ELEMENTS (feature_levels), D3D11_SDK_VERSION,
        &device11, nullptr, nullptr);

    if (FAILED (hr)) {
      hr = D3D11CreateDevice (adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
          D3D11_CREATE_DEVICE_BGRA_SUPPORT, &feature_levels[1],
          G_N_ELEMENTS (feature_levels) - 1, D3D11_SDK_VERSION,
          &device11, nullptr, nullptr);
    }

    if (!device11) {
      GST_ERROR_OBJECT (device, "Couldn't create d3d11 device");
      return E_FAIL;
    }

    hr = device11.As (&device11_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't get ID3D11Device5 interface");
      return E_FAIL;
    }

    ComPtr<ID3D11DeviceContext> ctx;
    device11->GetImmediateContext (&ctx);
    hr = ctx.As (&context11_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't get ID3D11DeviceContext4 interface");
      return E_FAIL;
    }

    ComPtr < ID3D10Multithread > multi_thread;
    device11_.As (&multi_thread);
    if (multi_thread)
      multi_thread->SetMultithreadProtected (TRUE);

    ComPtr < IDXGIDevice > dxgi_device;
    hr = device11_.As (&dxgi_device);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't get IDXGIDevice interface");
      return E_FAIL;
    }

    ComPtr < IInspectable > inspectable;
    hr = g_vtable.CreateDirect3D11DeviceFromDXGIDevice (dxgi_device.Get (),
        &inspectable);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "CreateDirect3D11DeviceFromDXGIDevice failed");
      return E_FAIL;
    }

    hr = inspectable.As (&d3d_device_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't get IDirect3DDevice interface");
      return E_FAIL;
    }

    ComPtr < IGraphicsCaptureItemInterop > item_interop;
    hr = GstGetActivationFactory < IGraphicsCaptureItemInterop,
        RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem > (&item_interop);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "IGraphicsCaptureItemInterop is not available");
      return E_FAIL;
    }

    if (monitor)
      hr = item_interop->CreateForMonitor (monitor, IID_PPV_ARGS (&item_));
    else
      hr = item_interop->CreateForWindow (hwnd, IID_PPV_ARGS (&item_));

    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't create item");
      return E_FAIL;
    }

    ComPtr < IDirect3D11CaptureFramePoolStatics > pool_statics;
    hr = GstGetActivationFactory < IDirect3D11CaptureFramePoolStatics,
        RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool >
        (&pool_statics);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device,
          "IDirect3D11CaptureFramePoolStatics is unavailable");
      return E_FAIL;
    }

    hr = device11_->CreateFence (0, D3D11_FENCE_FLAG_SHARED,
        IID_PPV_ARGS (&shared_fence11_));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't create d3d11 fence");
      return E_FAIL;
    }

    HANDLE fence_handle;
    hr = shared_fence11_->CreateSharedHandle (nullptr,
        GENERIC_ALL, nullptr, &fence_handle);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't create shared handle");
      return E_FAIL;
    }

    auto device12_handle = gst_d3d12_device_get_device_handle (device12_);
    hr = device12_handle->OpenSharedHandle (fence_handle,
        IID_PPV_ARGS (&shared_fence12_));
    CloseHandle (fence_handle);
    if (!gst_d3d12_result (hr, device12_)) {
      GST_ERROR_OBJECT (device, "Couldn't open d3d12 fence");
      return E_FAIL;
    }

    hr = item_->get_Size (&frame_size_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't query item size");
      return hr;
    }

    hr = item_->add_Closed (this, &closed_token_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't install closed callback");
      return hr;
    }

    hr = pool_statics->Create (d3d_device_.Get (),
        DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
        2, frame_size_, &frame_pool_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't create frame pool");
      return hr;
    }

    hr = frame_pool_->add_FrameArrived (this, &arrived_token_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't install FrameArrived callback");
      return hr;
    }

    hr = frame_pool_->CreateCaptureSession (item_.Get (), &session_);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device, "Couldn't create session");
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
      GST_ERROR_OBJECT (device, "Couldn't start capture");
      return hr;
    }

    return S_OK;
  }

  IFACEMETHOD(Invoke) (IDirect3D11CaptureFramePool * pool, IInspectable * arg)
  {
    ComPtr < IDirect3D11CaptureFrame > frame;

    GST_LOG_OBJECT (device12_, "Frame arrived");

    if (!frame_pool_)
      return S_OK;

    pool->TryGetNextFrame (&frame);
    if (!frame) {
      GST_WARNING_OBJECT (device12_, "No frame");
      return S_OK;
    }

    SizeInt32 frame_size;
    auto hr = frame->get_ContentSize (&frame_size);
    if (FAILED (hr)) {
      GST_WARNING_OBJECT (device12_, "Couldn't get content size");
      return S_OK;
    }

    if (frame_size.Width != frame_size_.Width ||
        frame_size.Height != frame_size_.Height) {
      GST_DEBUG_OBJECT (device12_, "Frame size changed %dx%d -> %dx%d",
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
      pool->Recreate (d3d_device_.Get (),
          DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
          CAPTURE_POOL_SIZE, frame_size);
      frame_size_ = frame_size;
      return S_OK;
    }

    ComPtr < IDirect3DSurface > surface;
    frame->get_Surface (&surface);
    if (!surface) {
      GST_WARNING_OBJECT (device12_, "IDirect3DSurface interface unavailable");
      return S_OK;
    }

    ComPtr < IDirect3DDxgiInterfaceAccess > access;
    surface.As (&access);
    if (!access) {
      GST_WARNING_OBJECT (device12_,
          "IDirect3DDxgiInterfaceAccess interface unavailable");
      return S_OK;
    }

    ComPtr < ID3D11Texture2D > texture;
    access->GetInterface (IID_PPV_ARGS (&texture));
    if (!texture) {
      GST_WARNING_OBJECT (device12_,
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
    GST_INFO_OBJECT (device12_, "Item closed");

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
      GST_DEBUG_OBJECT (device12_, "Size changed, recrate buffer pool");

      if (d3d12_pool_) {
        gst_buffer_pool_set_active (d3d12_pool_, FALSE);
        gst_clear_object (&d3d12_pool_);
      }

      gst_video_info_set_format (&pool_info_, GST_VIDEO_FORMAT_BGRA,
          crop_w, crop_h);
      d3d12_pool_ = gst_d3d12_buffer_pool_new (device12_);
      if (!d3d12_pool_) {
        GST_ERROR_OBJECT (device12_, "Couldn't create buffer pool");
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
        GST_ERROR_OBJECT (device12_, "Couldn't set buffer pool config");
        gst_clear_object (&d3d12_pool_);
        return GST_FLOW_ERROR;
      }

      if (!gst_buffer_pool_set_active (d3d12_pool_, TRUE)) {
        GST_ERROR_OBJECT (device12_, "Couldn't activate pool");
        gst_clear_object (&d3d12_pool_);
        return GST_FLOW_ERROR;
      }
    }

    GstBuffer *outbuf = nullptr;
    gst_buffer_pool_acquire_buffer (d3d12_pool_, &outbuf, nullptr);
    if (!outbuf) {
      GST_ERROR_OBJECT (device12_, "Couldn't acquire buffer");
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
      GST_ERROR_OBJECT (device12_, "Couldn't get sharable d3d11 texture");
      gst_buffer_unref (outbuf);
      return GST_FLOW_ERROR;
    }

    if (!gst_memory_map (mem, &map_info, GST_MAP_WRITE_D3D12)) {
      GST_ERROR_OBJECT (device12_, "Couldn't map memory");
      gst_buffer_unref (outbuf);
      return GST_FLOW_ERROR;
    }

    context11_->CopySubresourceRegion (texture11,
        0, 0, 0, 0, texture_.Get (), 0, (const D3D11_BOX *) &crop_box);
    fence_val_++;
    context11_->Signal (shared_fence11_.Get (), fence_val_);
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
      GST_DEBUG_OBJECT (device12_, "Size changed, recrate buffer pool");

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
        GST_ERROR_OBJECT (device12_, "Couldn't create staging texture");
        return GST_FLOW_ERROR;
      }

      gst_video_info_set_format (&pool_info_, GST_VIDEO_FORMAT_BGRA,
          crop_w, crop_h);
      video_pool_ = gst_video_buffer_pool_new ();
      if (!video_pool_) {
        GST_ERROR_OBJECT (device12_, "Couldn't create buffer pool");
        return GST_FLOW_ERROR;
      }

      auto caps = gst_video_info_to_caps (&pool_info_);
      auto config = gst_buffer_pool_get_config (video_pool_);
      gst_buffer_pool_config_set_params (config, caps, pool_info_.size, 0, 0);
      gst_caps_unref (caps);

      if (!gst_buffer_pool_set_config (video_pool_, config)) {
        GST_ERROR_OBJECT (device12_, "Couldn't set buffer pool config");
        gst_clear_object (&video_pool_);
        return GST_FLOW_ERROR;
      }

      if (!gst_buffer_pool_set_active (video_pool_, TRUE)) {
        GST_ERROR_OBJECT (device12_, "Couldn't activate pool");
        gst_clear_object (&video_pool_);
        return GST_FLOW_ERROR;
      }
    }

    context11_->CopySubresourceRegion (staging_.Get (), 0, 0, 0, 0,
        texture_.Get (), 0, (const D3D11_BOX *) &crop_box);

    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    auto hr = context11_->Map (staging_.Get (), 0, D3D11_MAP_READ,
        0, &mapped_resource);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (device12_, "Couldn't map staging texture");
      return GST_FLOW_ERROR;
    }

    GstBuffer *outbuf = nullptr;
    gst_buffer_pool_acquire_buffer (video_pool_, &outbuf, nullptr);
    if (!outbuf) {
      GST_ERROR_OBJECT (device12_, "Couldn't acquire buffer");
      context11_->Unmap (staging_.Get (), 0);
      return GST_FLOW_ERROR;
    }

    GstVideoFrame vframe;
    if (!gst_video_frame_map (&vframe, &pool_info_, outbuf, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (device12_, "Couldn't map video frame");
      context11_->Unmap (staging_.Get (), 0);
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
    context11_->Unmap (staging_.Get (), 0);

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
    if (frame_pool_) {
      frame_pool_->remove_FrameArrived (arrived_token_);

      ComPtr<IClosable> closable;
      frame_pool_.As (&closable);
      if (closable)
        closable->Close ();

      frame_pool_ = nullptr;
    }

    if (item_) {
      item_->remove_Closed (closed_token_);

      ComPtr<IClosable> closable;
      item_.As (&closable);
      if (closable)
        closable->Close ();

      item_ = nullptr;
    }

    session3_ = nullptr;
    session2_ = nullptr;

    if (session_) {
      ComPtr<IClosable> closable;
      session_.As (&closable);
      if (closable)
        closable->Close ();

      session_ = nullptr;
    }

    if (d3d_device_) {
      ComPtr<IClosable> closable;
      d3d_device_.As (&closable);
      if (closable)
        closable->Close ();

      d3d_device_ = nullptr;
    }

    texture_ = nullptr;
    staging_ = nullptr;

    shared_fence11_ = nullptr;
    shared_fence12_ = nullptr;

    if (context11_)
      context11_->Flush ();

    context11_ = nullptr;
    device11_ = nullptr;
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
  HWND hwnd_ = nullptr;
  GstD3D12Device *device12_ = nullptr;
  GstVideoInfo pool_info_;
  GstBufferPool *d3d12_pool_ = nullptr;
  GstBufferPool *video_pool_ = nullptr;
  ComPtr<ID3D11Device5> device11_;
  ComPtr<ID3D11DeviceContext4> context11_;
  ComPtr<IDirect3DDevice> d3d_device_;
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

template<typename Fn>
class LambdaHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
  IDispatcherQueueHandler>
{
public:
  LambdaHandler() = default;

  HRESULT RuntimeClassInitialize(Fn&& fn)
  {
    func_ = std::move(fn);
    return S_OK;
  }

  IFACEMETHODIMP Invoke() override
  {
    return func_ ? func_() : S_OK;
  }

private:
  Fn func_;
};

struct GstD3D12GraphicsCapturePrivate
{
  ~GstD3D12GraphicsCapturePrivate ()
  {
    if (capture) {
      ComPtr<IDispatcherQueueHandler> handler;
      auto hr = MakeAndInitialize<LambdaHandler<std::function<HRESULT()>>>(&handler,
          [object = capture.Detach ()]() -> HRESULT {
            object->Close ();
            object->Release ();
            return S_OK;
          });

      if (SUCCEEDED (hr))
        QueueManger::GetInstance ()->RunOnDispatcherThread (handler.Get ());
    }
  }

  ComPtr<GraphicsCapture> capture;
};
/* *INDENT-ON* */

struct _GstD3D12GraphicsCapture
{
  GstObject parent;

  GstD3D12GraphicsCapturePrivate *priv;
};

static void gst_d3d12_graphics_capture_finalize (GObject * object);

static GstFlowReturn
gst_d3d12_graphics_capture_prepare (GstD3D12ScreenCapture * capture,
    guint flags);
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

static GstFlowReturn
gst_d3d12_graphics_capture_prepare (GstD3D12ScreenCapture * capture,
    guint flags)
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

/* *INDENT-OFF* */
GstD3D12ScreenCapture *
gst_d3d12_graphics_capture_new (GstD3D12Device * device, HWND window_handle,
    HMONITOR monitor_handle)
{
  g_return_val_if_fail (device, nullptr);

  if (!gst_d3d12_graphics_capture_load_library ()) {
    GST_WARNING_OBJECT (device, "Couldn't load library");
    return nullptr;
  }

  if (window_handle && !IsWindow (window_handle)) {
    GST_ERROR_OBJECT (device, "%p is not a valid HWND", window_handle);
    return nullptr;
  }

  ComPtr<GraphicsCapture> capture;
  ComPtr<IDispatcherQueueHandler> handler;
  auto hr = MakeAndInitialize<LambdaHandler<std::function<HRESULT()>>>(&handler,
      [device, monitor_handle, window_handle, output = capture.GetAddressOf()]() -> HRESULT
      {
        ComPtr<GraphicsCapture> capture;
        HRESULT hr = MakeAndInitialize<GraphicsCapture>(&capture,
              device, monitor_handle, window_handle);
        if (FAILED(hr))
          return hr;

        *output = capture.Detach();
        return S_OK;
      });

  if (FAILED (hr)) {
    GST_ERROR_OBJECT (device, "Couldn't create callback object");
    return nullptr;
  }

  hr = QueueManger::GetInstance ()->RunOnDispatcherThread (handler.Get ());
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (device, "Couldn't create capture object");
    return nullptr;
  }

  auto self = (GstD3D12GraphicsCapture *)
      g_object_new (GST_TYPE_D3D12_GRAPHICS_CAPTURE, nullptr);
  gst_object_ref_sink (self);

  self->priv->capture = capture;

  return (GstD3D12ScreenCapture *) self;
}
/* *INDENT-ON* */

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

void
gst_d3d12_graphics_capture_deinit (void)
{
  QueueManger::Deinit ();
}

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

#include "gstwebview2object.h"
#include <gst/d3d11/gstd3d11-private.h>
#include <webview2.h>
#include <dcomp.h>
#include <wrl.h>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <string.h>
#include <shobjidl.h>
#include <dwmapi.h>
#include <locale>
#include <codecvt>
#include <gmodule.h>
#include <winstring.h>
#include <roapi.h>
#include <mmsystem.h>
#include <chrono>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>

GST_DEBUG_CATEGORY_EXTERN (gst_webview2_src_debug);
#define GST_CAT_DEFAULT gst_webview2_src_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Graphics;
using namespace ABI::Windows::Graphics::Capture;
using namespace ABI::Windows::Graphics::DirectX;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::DirectX::Direct3D11;
/* *INDENT-ON* */

#define WEBVIEW2_OBJECT_PROP_NAME "gst-d3d11-webview2-object"
#define WEBVIEW2_WINDOW_OFFSET (-16384)

template < typename InterfaceType, PCNZWCH runtime_class_id > static HRESULT
GstGetActivationFactory (InterfaceType ** factory)
{
  HSTRING class_id_hstring;
  HRESULT hr = WindowsCreateString (runtime_class_id,
      wcslen (runtime_class_id), &class_id_hstring);

  if (FAILED (hr))
    return hr;

  hr = RoGetActivationFactory (class_id_hstring, IID_PPV_ARGS (factory));

  if (FAILED (hr)) {
    WindowsDeleteString (class_id_hstring);
    return hr;
  }

  return WindowsDeleteString (class_id_hstring);
}

enum
{
  PROP_0,
  PROP_DEVICE,
};

enum WebView2State
{
  WEBVIEW2_STATE_INIT,
  WEBVIEW2_STATE_RUNNING,
  WEBVIEW2_STATE_ERROR,
};

struct WebView2StatusData
{
  GstWebView2Object *object;
  WebView2State state;
};

struct GstWebView2;

struct GstWebView2ObjectPrivate
{
  GstWebView2ObjectPrivate ()
  {
    context = g_main_context_new ();
    loop = g_main_loop_new (context, FALSE);
  }

   ~GstWebView2ObjectPrivate ()
  {
    g_main_loop_quit (loop);
    g_clear_pointer (&main_thread, g_thread_join);
    g_main_loop_unref (loop);
    g_main_context_unref (context);
    if (pool)
      gst_buffer_pool_set_active (pool, FALSE);
    gst_clear_object (&pool);
    gst_clear_object (&device);
    gst_clear_caps (&caps);
  }

  GstD3D11Device *device = nullptr;
  std::mutex lock;
  std::condition_variable cond;
  std::shared_ptr < GstWebView2 > webview;
  ComPtr < ID3D11Texture2D > staging;
  GThread *main_thread = nullptr;
  GMainContext *context = nullptr;
  GMainLoop *loop = nullptr;
  GstBufferPool *pool = nullptr;
  GstCaps *caps = nullptr;
  GstVideoInfo info;
  std::string location;
  HWND hwnd;
  WebView2State state = WEBVIEW2_STATE_INIT;
};

struct _GstWebView2Object
{
  GstObject parent;

  GstWebView2ObjectPrivate *priv;
};

static gboolean gst_webview2_callback (WebView2StatusData * data);

#define CLOSE_COM(obj) G_STMT_START { \
  if (obj) { \
    ComPtr<IClosable> closable; \
    obj.As (&closable); \
    if (closable) \
      closable->Close (); \
    obj = nullptr; \
  } \
} G_STMT_END

struct GstWebView2
{
  GstWebView2 (GstWebView2Object * object, HWND hwnd)
  :object_ (object), hwnd_ (hwnd)
  {
    ID3D11Device *device_handle;
      ComPtr < ID3D10Multithread > multi_thread;
    HRESULT hr;

      device_ = (GstD3D11Device *) gst_object_ref (object->priv->device);

      device_handle = gst_d3d11_device_get_device_handle (device_);
      hr = device_handle->QueryInterface (IID_PPV_ARGS (&multi_thread));
    if (SUCCEEDED (hr))
        multi_thread->SetMultithreadProtected (TRUE);

      device_handle->QueryInterface (IID_PPV_ARGS (&dxgi_device_));
  }

   ~GstWebView2 ()
  {
    if (comp_ctrl_)
      comp_ctrl_->put_RootVisualTarget (nullptr);
    webview_ = nullptr;
    ctrl_ = nullptr;
    comp_ctrl_ = nullptr;
    env_ = nullptr;

    root_visual_ = nullptr;
    comp_target_ = nullptr;
    comp_device_ = nullptr;

    CLOSE_COM (session_);
    CLOSE_COM (pool_);
    CLOSE_COM (item_);
    CLOSE_COM (d3d_device_);

    dxgi_device_ = nullptr;

    gst_object_unref (device_);
  }

  HRESULT Open ()
  {
    HRESULT hr;

    if (!dxgi_device_)
      return E_FAIL;

    hr = SetupCapture ();
    if (FAILED (hr))
      return hr;

    hr = SetupComposition ();
    if (FAILED (hr))
      return hr;

    return CreateCoreWebView2Environment (Callback <
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler > (this,
            &GstWebView2::OnCreateEnvironmentCompleted).Get ());
  }

  HRESULT SetupCapture ()
  {
    ComPtr < ID3D10Multithread > multi_thread;
    ComPtr < IGraphicsCaptureItemInterop > interop;
    ComPtr < IDXGIDevice > dxgi_device;
    ComPtr < IInspectable > inspectable;
    ComPtr < IDirect3D11CaptureFramePoolStatics > pool_statics;
    ComPtr < IDirect3D11CaptureFramePoolStatics2 > pool_statics2;
    ComPtr < IGraphicsCaptureSession2 > session2;
    HRESULT hr;

    hr = GstGetActivationFactory < IGraphicsCaptureItemInterop,
        RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem > (&interop);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = interop->CreateForWindow (hwnd_, IID_PPV_ARGS (&item_));
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = item_->get_Size (&pool_size_);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = CreateDirect3D11DeviceFromDXGIDevice (dxgi_device_.Get (),
        &inspectable);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = inspectable.As (&d3d_device_);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = GstGetActivationFactory < IDirect3D11CaptureFramePoolStatics,
        RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool >
        (&pool_statics);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = pool_statics.As (&pool_statics2);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = pool_statics2->CreateFreeThreaded (d3d_device_.Get (),
        DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
        1, pool_size_, &pool_);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = pool_->CreateCaptureSession (item_.Get (), &session_);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    session_.As (&session2);
    if (session2)
      session2->put_IsCursorCaptureEnabled (FALSE);

    hr = session_->StartCapture ();
    if (!gst_d3d11_result (hr, device_))
      return hr;

    return S_OK;
  }

  HRESULT SetupComposition ()
  {
    HRESULT hr;

    hr = DCompositionCreateDevice (dxgi_device_.Get (),
        IID_PPV_ARGS (&comp_device_));
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = comp_device_->CreateTargetForHwnd (hwnd_, TRUE, &comp_target_);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = comp_device_->CreateVisual (&root_visual_);
    if (!gst_d3d11_result (hr, device_))
      return hr;

    hr = comp_target_->SetRoot (root_visual_.Get ());
    if (!gst_d3d11_result (hr, device_))
      return hr;

    return S_OK;
  }

  HRESULT
      OnCreateEnvironmentCompleted (HRESULT hr, ICoreWebView2Environment * env)
  {
    ComPtr < ICoreWebView2Environment3 > env3;

    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_, "Couldn't create environment");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    env_ = env;
    hr = env_.As (&env3);
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_,
          "ICoreWebView2Environment3 interface is unavailable");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    hr = env3->CreateCoreWebView2CompositionController (hwnd_,
        Callback <
        ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler >
        (this, &GstWebView2::OnCreateCoreWebView2ControllerCompleted).Get ());
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_,
          "CreateCoreWebView2CompositionController failed");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    return S_OK;
  }

  HRESULT
      OnCreateCoreWebView2ControllerCompleted (HRESULT hr,
      ICoreWebView2CompositionController * comp_ctr) {
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_, "Couldn't create composition controller");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    comp_ctrl_ = comp_ctr;
    hr = comp_ctrl_.As (&ctrl_);
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_, "Couldn't get controller interface");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    hr = comp_ctrl_->put_RootVisualTarget (root_visual_.Get ());
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_, "Couldn't set root visual object");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    hr = ctrl_->get_CoreWebView2 (&webview_);
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_, "Couldn't get webview2 interface");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return hr;
    }

    /* TODO: add audio mute property */
#if 0
    ComPtr < ICoreWebView2_8 > webview8;
    hr = webview_.As (&webview8);
    if (!gst_d3d11_result (hr, nullptr)) {
      GST_WARNING_OBJECT (object_, "ICoreWebView2_8 interface is unavailable");
      NotifyState (WEBVIEW2_STATE_ERROR);
      return E_FAIL;
    }

    webview8->put_IsMuted (TRUE);
#endif

    RECT bounds;
    GetClientRect (hwnd_, &bounds);
    ctrl_->put_Bounds (bounds);
    ctrl_->put_IsVisible (TRUE);

    GST_INFO_OBJECT (object_, "All configured");

    NotifyState (WEBVIEW2_STATE_RUNNING);

    return S_OK;
  }

  void NotifyState (WebView2State state)
  {
    WebView2StatusData *data = g_new0 (WebView2StatusData, 1);

    data->object = object_;
    data->state = state;

    g_main_context_invoke_full (object_->priv->context,
        G_PRIORITY_DEFAULT,
        (GSourceFunc) gst_webview2_callback, data, (GDestroyNotify) g_free);
  }

  HRESULT DoCompose ()
  {
    HRESULT hr;
    GstD3D11DeviceLockGuard lk (device_);
    hr = comp_device_->Commit ();
    if (!gst_d3d11_result (hr, device_))
      return hr;

    return comp_device_->WaitForCommitCompletion ();
  }

  GstFlowReturn DoCapture (ID3D11Texture2D * dst_texture)
  {
    HRESULT hr;
    ComPtr < IDirect3D11CaptureFrame > frame;
    GstClockTime timeout = gst_util_get_timestamp () + 5 * GST_SECOND;
    SizeInt32 size;
    ComPtr < IDirect3DSurface > surface;
    ComPtr < IDirect3DDxgiInterfaceAccess > access;
    ComPtr < ID3D11Texture2D > texture;
    TimeSpan time;
    GstClockTime pts;
    RECT object_rect, bound_rect;
    POINT object_pos = { 0, };
    UINT width, height;
    D3D11_TEXTURE2D_DESC src_desc;
    D3D11_TEXTURE2D_DESC dst_desc;
    UINT x_offset = 0;
    UINT y_offset = 0;
    D3D11_BOX box = { 0, };

  again:
    std::unique_lock < std::mutex > flush_lk (lock_);
    do {
      if (flushing_)
        return GST_FLOW_FLUSHING;

      hr = pool_->TryGetNextFrame (&frame);
      if (frame)
        break;

      if (!gst_d3d11_result (hr, device_))
        return GST_FLOW_ERROR;

      cond_.wait_for (flush_lk, std::chrono::milliseconds (1));
    } while (gst_util_get_timestamp () < timeout);
    flush_lk.unlock ();

    if (!frame) {
      GST_ERROR_OBJECT (object_, "Timeout");
      return GST_FLOW_ERROR;
    }

    hr = frame->get_ContentSize (&size);
    if (!gst_d3d11_result (hr, device_))
      return GST_FLOW_ERROR;

    if (size.Width != pool_size_.Width || size.Height != pool_size_.Height) {
      GST_DEBUG_OBJECT (object_, "Size changed %dx%d -> %dx%d",
          pool_size_.Width, pool_size_.Height, size.Width, size.Height);
      pool_size_ = size;
      frame = nullptr;
      hr = pool_->Recreate (d3d_device_.Get (),
          DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized, 1,
          size);
      if (!gst_d3d11_result (hr, device_))
        return GST_FLOW_ERROR;

      goto again;
    }

    hr = frame->get_SystemRelativeTime (&time);
    if (SUCCEEDED (hr))
      pts = time.Duration * 100;
    else
      pts = gst_util_get_timestamp ();

    hr = frame->get_Surface (&surface);
    if (!gst_d3d11_result (hr, device_))
      return GST_FLOW_ERROR;

    hr = surface.As (&access);
    if (!gst_d3d11_result (hr, device_))
      return GST_FLOW_ERROR;

    hr = access->GetInterface (IID_PPV_ARGS (&texture));
    if (!gst_d3d11_result (hr, device_))
      return GST_FLOW_ERROR;

    if (!GetClientRect (hwnd_, &object_rect)) {
      GST_ERROR_OBJECT (object_, "Couldn't get object rect");
      return GST_FLOW_ERROR;
    }

    hr = DwmGetWindowAttribute (hwnd_, DWMWA_EXTENDED_FRAME_BOUNDS, &bound_rect,
        sizeof (RECT));
    if (!gst_d3d11_result (hr, device_))
      return GST_FLOW_ERROR;

    if (!ClientToScreen (hwnd_, &object_pos)) {
      GST_ERROR_OBJECT (object_, "Couldn't get position");
      return GST_FLOW_ERROR;
    }

    width = object_rect.right - object_rect.left;
    height = object_rect.bottom - object_rect.top;

    width = MAX (width, 1);
    height = MAX (height, 1);

    if (object_pos.x > bound_rect.left)
      x_offset = object_pos.x - bound_rect.left;

    if (object_pos.y > bound_rect.top)
      y_offset = object_pos.y - bound_rect.top;

    box.front = 0;
    box.back = 1;

    texture->GetDesc (&src_desc);
    dst_texture->GetDesc (&dst_desc);

    box.left = x_offset;
    box.left = MIN (src_desc.Width - 1, box.left);

    box.top = y_offset;
    box.top = MIN (src_desc.Height - 1, box.top);

    box.right = dst_desc.Width + x_offset;
    box.right = MIN (src_desc.Width, box.right);

    box.bottom = dst_desc.Height + y_offset;
    box.bottom = MIN (src_desc.Height, box.right);

    {
      auto context = gst_d3d11_device_get_device_context_handle (device_);
      GstD3D11DeviceLockGuard lk (device_);

      context->CopySubresourceRegion (dst_texture, 0, 0, 0, 0,
          texture.Get (), 0, &box);
    }

    return GST_FLOW_OK;
  }

  void SetFlushing (bool flushing)
  {
    std::lock_guard < std::mutex > lk (lock_);
    flushing_ = flushing;
    cond_.notify_all ();
  }

  HWND hwnd_;
  ComPtr < IDXGIDevice > dxgi_device_;

  ComPtr < IDCompositionDevice > comp_device_;
  ComPtr < IDCompositionTarget > comp_target_;
  ComPtr < IDCompositionVisual > root_visual_;

  ComPtr < ICoreWebView2Environment > env_;
  ComPtr < ICoreWebView2 > webview_;
  ComPtr < ICoreWebView2Controller > ctrl_;
  ComPtr < ICoreWebView2CompositionController > comp_ctrl_;

  ComPtr < IDirect3DDevice > d3d_device_;
  ComPtr < IGraphicsCaptureItem > item_;
  ComPtr < IDirect3D11CaptureFramePool > pool_;
  ComPtr < IGraphicsCaptureSession > session_;
  SizeInt32 pool_size_;

  HRESULT last_hr_ = S_OK;
  GstWebView2Object *object_;
  GstD3D11Device *device_;

  std::mutex lock_;
  std::condition_variable cond_;

  bool flushing_ = false;
};

static void gst_webview2_object_constructed (GObject * object);
static void gst_webview2_object_finalize (GObject * object);
static void gst_webview2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gpointer gst_webview2_thread_func (GstWebView2Object * self);

#define gst_webview2_object_parent_class parent_class
G_DEFINE_TYPE (GstWebView2Object, gst_webview2_object, GST_TYPE_OBJECT);

static void
gst_webview2_object_class_init (GstWebView2ObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = gst_webview2_object_constructed;
  object_class->finalize = gst_webview2_object_finalize;
  object_class->set_property = gst_webview2_set_property;

  g_object_class_install_property (object_class, PROP_DEVICE,
      g_param_spec_object ("device", "D3D11 Device",
          "GstD3D11Device object for operating",
          GST_TYPE_D3D11_DEVICE, (GParamFlags)
          (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
              G_PARAM_STATIC_STRINGS)));
}

static void
gst_webview2_object_init (GstWebView2Object * self)
{
  self->priv = new GstWebView2ObjectPrivate ();
}

static void
gst_webview2_object_constructed (GObject * object)
{
  GstWebView2Object *self = GST_WEBVIEW2_OBJECT (object);
  auto priv = self->priv;

  priv->main_thread = g_thread_new ("d3d11-webview2",
      (GThreadFunc) gst_webview2_thread_func, self);

  std::unique_lock < std::mutex > lk (priv->lock);
  while (!priv->state != WEBVIEW2_STATE_INIT)
    priv->cond.wait (lk);
  lk.unlock ();

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_webview2_object_finalize (GObject * object)
{
  GstWebView2Object *self = GST_WEBVIEW2_OBJECT (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webview2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebView2Object *self = GST_WEBVIEW2_OBJECT (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE:
      priv->device = (GstD3D11Device *) g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_webview2_callback (WebView2StatusData * data)
{
  GstWebView2Object *self = data->object;
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  GST_DEBUG_OBJECT (self, "Got callback, state: %d", data->state);

  priv->state = data->state;
  priv->cond.notify_all ();

  return G_SOURCE_REMOVE;
}

static LRESULT CALLBACK
WndProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  GstWebView2Object *self;

  switch (msg) {
    case WM_CREATE:
      self = (GstWebView2Object *)
          ((LPCREATESTRUCTA) lparam)->lpCreateParams;
      SetPropA (hwnd, WEBVIEW2_OBJECT_PROP_NAME, self);
      break;
    case WM_SIZE:
      self = (GstWebView2Object *)
          GetPropA (hwnd, WEBVIEW2_OBJECT_PROP_NAME);
      if (self && self->priv->webview && self->priv->webview->ctrl_) {
        RECT bounds;
        GetClientRect (hwnd, &bounds);
        self->priv->webview->ctrl_->put_Bounds (bounds);
      }
      break;
    default:
      break;
  }

  return DefWindowProcA (hwnd, msg, wparam, lparam);
}

static HWND
gst_webview2_create_hwnd (GstWebView2Object * self)
{
  HINSTANCE inst = GetModuleHandle (nullptr);

  GST_D3D11_CALL_ONCE_BEGIN {
    WNDCLASSEXA wc;
    memset (&wc, 0, sizeof (WNDCLASSEXA));

    wc.cbSize = sizeof (WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpszClassName = "GstD3D11Webview2Window";
    RegisterClassExA (&wc);
  }
  GST_D3D11_CALL_ONCE_END;

  return CreateWindowExA (0, "GstD3D11Webview2Window", "GstD3D11Webview2Window",
      WS_POPUP, WEBVIEW2_WINDOW_OFFSET,
      WEBVIEW2_WINDOW_OFFSET, 1920, 1080, nullptr, nullptr, inst, self);
}

static gboolean
msg_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

static gpointer
gst_webview2_thread_func (GstWebView2Object * self)
{
  auto priv = self->priv;
  GSource *msg_source;
  GIOChannel *msg_io_channel;
  ComPtr < ITaskbarList > taskbar_list;
  HRESULT hr;
  TIMECAPS time_caps;
  guint timer_res = 0;

  if (timeGetDevCaps (&time_caps, sizeof (TIMECAPS)) == TIMERR_NOERROR) {
    guint resolution;

    resolution = MIN (MAX (time_caps.wPeriodMin, 1), time_caps.wPeriodMax);

    if (timeBeginPeriod (resolution) != TIMERR_NOERROR)
      timer_res = resolution;
  }

  GST_DEBUG_OBJECT (self, "Entering thread");

  RoInitialize (RO_INIT_SINGLETHREADED);
  g_main_context_push_thread_default (priv->context);

  SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

  priv->hwnd = gst_webview2_create_hwnd (self);

  msg_io_channel = g_io_channel_win32_new_messages (0);
  msg_source = g_io_create_watch (msg_io_channel, G_IO_IN);
  g_source_set_callback (msg_source, (GSourceFunc) msg_cb, self, NULL);
  g_source_attach (msg_source, priv->context);

  ShowWindow (priv->hwnd, SW_SHOW);

  priv->webview = std::make_shared < GstWebView2 > (self, priv->hwnd);
  hr = priv->webview->Open ();
  if (FAILED (hr) || priv->state == WEBVIEW2_STATE_ERROR) {
    GST_ERROR_OBJECT (self, "Couldn't open webview2");
    goto out;
  }

  hr = CoCreateInstance (CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS (&taskbar_list));
  if (SUCCEEDED (hr)) {
    taskbar_list->DeleteTab (priv->hwnd);
    taskbar_list = nullptr;
  }

  GST_DEBUG_OBJECT (self, "Run loop");
  g_main_loop_run (priv->loop);
  GST_DEBUG_OBJECT (self, "Exit loop");

out:
  g_source_destroy (msg_source);
  g_source_unref (msg_source);
  g_io_channel_unref (msg_io_channel);

  priv->webview = nullptr;
  DestroyWindow (priv->hwnd);

  GST_DEBUG_OBJECT (self, "Leaving thread");

  g_main_context_pop_thread_default (priv->context);
  RoUninitialize ();

  if (timer_res != 0)
    timeEndPeriod (timer_res);

  return nullptr;
}

GstWebView2Object *
gst_webview2_object_new (GstD3D11Device * device)
{
  GstWebView2Object *self;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), nullptr);

  self = (GstWebView2Object *)
      g_object_new (GST_TYPE_WEBVIEW2_OBJECT, "device", device, nullptr);
  gst_object_ref_sink (self);

  if (self->priv->state != WEBVIEW2_STATE_RUNNING) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

static gboolean
gst_webview2_update_location (GstWebView2Object * self)
{
  auto priv = self->priv;
  std::wstring_convert < std::codecvt_utf8 < wchar_t >>conv;
  std::wstring location_wide = conv.from_bytes (priv->location);
  HRESULT hr;

  GST_DEBUG_OBJECT (self, "Navigate to %s", priv->location.c_str ());
  hr = priv->webview->webview_->Navigate (location_wide.c_str ());

  if (FAILED (hr))
    GST_WARNING_OBJECT (self, "Couldn't navigate to %s",
        priv->location.c_str ());

  return G_SOURCE_REMOVE;
}

gboolean
gst_webview2_object_set_location (GstWebView2Object * object,
    const std::string & location)
{
  auto priv = object->priv;
  std::unique_lock < std::mutex > lk (priv->lock);

  if (priv->state != WEBVIEW2_STATE_RUNNING) {
    GST_WARNING_OBJECT (object, "Not running state");
    return FALSE;
  }
  priv->location = location;
  lk.unlock ();

  g_main_context_invoke (priv->context,
      (GSourceFunc) gst_webview2_update_location, object);

  return TRUE;
}

static gboolean
gst_d3d11_webview_object_update_size (GstWebView2Object * self)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Updating size to %dx%d", priv->info.width,
      priv->info.height);

  MoveWindow (priv->hwnd, WEBVIEW2_WINDOW_OFFSET,
      WEBVIEW2_WINDOW_OFFSET, priv->info.width, priv->info.height, TRUE);

  return G_SOURCE_REMOVE;
}

gboolean
gst_webview2_object_set_caps (GstWebView2Object * object, GstCaps * caps)
{
  auto priv = object->priv;
  std::unique_lock < std::mutex > lk (priv->lock);
  bool is_d3d11 = false;

  if (priv->pool) {
    gst_buffer_pool_set_active (priv->pool, FALSE);
    gst_object_unref (priv->pool);
  }

  priv->staging = nullptr;

  gst_video_info_from_caps (&priv->info, caps);

  auto features = gst_caps_get_features (caps, 0);
  if (features
      && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    priv->pool = gst_d3d11_buffer_pool_new (priv->device);
    is_d3d11 = true;
  } else {
    priv->pool = gst_video_buffer_pool_new ();
  }

  auto config = gst_buffer_pool_get_config (priv->pool);

  if (is_d3d11) {
    auto params = gst_d3d11_allocation_params_new (priv->device, &priv->info,
        GST_D3D11_ALLOCATION_FLAG_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0);
    gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
    gst_d3d11_allocation_params_free (params);
  } else {
    D3D11_TEXTURE2D_DESC desc = { 0, };
    ID3D11Device *device_handle =
        gst_d3d11_device_get_device_handle (priv->device);
    HRESULT hr;

    desc.Width = priv->info.width;
    desc.Height = priv->info.height;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device_handle->CreateTexture2D (&desc, nullptr, &priv->staging);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (object, "Couldn't create staging texture");
      gst_clear_object (&priv->pool);
      return FALSE;
    }
  }

  gst_buffer_pool_config_set_params (config, caps, priv->info.size, 0, 0);
  gst_caps_replace (&priv->caps, caps);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (priv->pool, config)) {
    GST_ERROR_OBJECT (object, "Couldn't set pool config");
    gst_clear_object (&priv->pool);
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (priv->pool, TRUE)) {
    GST_ERROR_OBJECT (object, "Couldn't set active");
    gst_clear_object (&priv->pool);
    return FALSE;
  }

  lk.unlock ();

  g_main_context_invoke (priv->context,
      (GSourceFunc) gst_d3d11_webview_object_update_size, object);

  return TRUE;
}

struct NavigationEventData
{
  NavigationEventData ()
  {
    if (event)
      gst_event_unref (event);
  }

  GstWebView2Object *object;
  GstEvent *event = nullptr;
};

static void
navigation_event_free (NavigationEventData * data)
{
  delete data;
}

static gboolean
gst_webview2_on_navigation_event (NavigationEventData * data)
{
  GstWebView2Object *self = data->object;
  auto priv = self->priv;
  GstEvent *event = data->event;
  GstNavigationEventType type;
  gdouble x, y;
  gint button;

  if (!priv->webview || !priv->webview->comp_ctrl_)
    goto out;

  type = gst_navigation_event_get_type (event);

  switch (type) {
      /* FIXME: Implement key event */
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      if (gst_navigation_event_parse_mouse_button_event (event,
              &button, &x, &y)) {
        GST_TRACE_OBJECT (self, "Mouse press, button %d, %lfx%lf",
            button, x, y);
        COREWEBVIEW2_MOUSE_EVENT_KIND kind;
        POINT point;

        point.x = (LONG) x;
        point.y = (LONG) y;

        switch (button) {
          case 1:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
            break;
          case 2:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN;
            break;
          case 3:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN;
            break;
          default:
            goto out;
        }

        /* FIXME: need to know the virtual key state */
        priv->webview->comp_ctrl_->SendMouseInput (kind,
            COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, point);
      }
      break;
    case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
      if (gst_navigation_event_parse_mouse_button_event (event,
              &button, &x, &y)) {
        GST_TRACE_OBJECT (self, "Mouse release, button %d, %lfx%lf",
            button, x, y);
        COREWEBVIEW2_MOUSE_EVENT_KIND kind;
        POINT point;

        point.x = (LONG) x;
        point.y = (LONG) y;

        switch (button) {
          case 1:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
            break;
          case 2:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
            break;
          case 3:
            kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
            break;
          default:
            goto out;
        }

        /* FIXME: need to know the virtual key state */
        priv->webview->comp_ctrl_->SendMouseInput (kind,
            COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, point);
      }
      break;
    case GST_NAVIGATION_EVENT_MOUSE_MOVE:
      if (gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
        GST_TRACE_OBJECT (self, "Mouse move, %lfx%lf", x, y);
        POINT point;

        point.x = (LONG) x;
        point.y = (LONG) y;

        /* FIXME: need to know the virtual key state */
        priv->webview->
            comp_ctrl_->SendMouseInput (COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
            COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE, 0, point);
      }
      break;
    default:
      break;
  }

out:
  return G_SOURCE_REMOVE;
}

void
gst_webview2_object_send_event (GstWebView2Object * object, GstEvent * event)
{
  auto priv = object->priv;
  auto data = new NavigationEventData ();
  data->object = object;
  data->event = gst_event_ref (event);

  g_main_context_invoke_full (priv->context, G_PRIORITY_DEFAULT,
      (GSourceFunc) gst_webview2_on_navigation_event, data,
      (GDestroyNotify) navigation_event_free);
}

struct CaptureData
{
  GstWebView2Object *object;
  bool notified = false;
    std::mutex lock;
    std::condition_variable cond;
  GstBuffer *buffer = nullptr;
  GstFlowReturn ret = GST_FLOW_ERROR;
};

static gboolean
gst_webview2_do_capture (CaptureData * data)
{
  GstWebView2Object *self = data->object;
  auto priv = self->priv;
  HRESULT hr;
  GstFlowReturn ret;
  GstBuffer *buffer;
  GstMemory *mem;
  GstMapInfo info;
  GstClockTime pts;
  ID3D11Texture2D *texture;

  if (!priv->pool) {
    GST_ERROR_OBJECT (self, "Pool was not configured");
    goto out;
  }

  hr = priv->webview->DoCompose ();
  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't compose");
    goto out;
  }

  pts = gst_util_get_timestamp ();

  ret = gst_buffer_pool_acquire_buffer (priv->pool, &buffer, nullptr);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    goto out;
  }

  if (priv->staging) {
    texture = priv->staging.Get ();
  } else {
    mem = gst_buffer_peek_memory (buffer, 0);
    if (!gst_memory_map (mem, &info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Couldn't map memory");
      gst_buffer_unref (buffer);
      goto out;
    }

    texture = (ID3D11Texture2D *) info.data;
  }

  ret = priv->webview->DoCapture (texture);

  if (!priv->staging)
    gst_memory_unmap (mem, &info);

  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (buffer);
    data->ret = ret;
    goto out;
  }

  if (priv->staging) {
    GstVideoFrame frame;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11DeviceContext *context =
        gst_d3d11_device_get_device_context_handle (priv->device);
    GstD3D11DeviceLockGuard lk (priv->device);
    guint8 *dst;
    guint8 *src;
    guint width_in_bytes;

    hr = context->Map (priv->staging.Get (), 0, D3D11_MAP_READ, 0, &map);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't map staging texture");
      gst_buffer_unref (buffer);
      goto out;
    }

    if (!gst_video_frame_map (&frame, &priv->info, buffer, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (self, "Couldn't map frame");
      gst_buffer_unref (buffer);
      context->Unmap (priv->staging.Get (), 0);
      goto out;
    }

    src = (guint8 *) map.pData;
    dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);
    width_in_bytes = GST_VIDEO_FRAME_COMP_PSTRIDE (&frame, 0)
        * GST_VIDEO_FRAME_WIDTH (&frame);

    for (guint i = 0; i < GST_VIDEO_FRAME_HEIGHT (&frame); i++) {
      memcpy (dst, src, width_in_bytes);
      dst += GST_VIDEO_FRAME_PLANE_STRIDE (&frame, 0);
      src += map.RowPitch;
    }

    gst_video_frame_unmap (&frame);
    context->Unmap (priv->staging.Get (), 0);
  }

  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_DTS (buffer) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;

  data->buffer = buffer;
  data->ret = GST_FLOW_OK;

out:
  std::lock_guard < std::mutex > lk (data->lock);
  data->notified = true;
  data->cond.notify_one ();

  return G_SOURCE_REMOVE;
}

GstFlowReturn
gst_webview2_object_get_buffer (GstWebView2Object * object, GstBuffer ** buffer)
{
  auto priv = object->priv;
  CaptureData data;

  data.object = object;

  g_main_context_invoke (priv->context,
      (GSourceFunc) gst_webview2_do_capture, &data);

  std::unique_lock < std::mutex > lk (data.lock);
  while (!data.notified)
    data.cond.wait (lk);

  if (!data.buffer)
    return data.ret;

  *buffer = data.buffer;
  return GST_FLOW_OK;
}

void
gst_webview2_object_set_flushing (GstWebView2Object * object, bool flushing)
{
  auto priv = object->priv;

  priv->webview->SetFlushing (flushing);
}

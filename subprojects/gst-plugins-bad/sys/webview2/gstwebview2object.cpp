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
#include <WebView2EnvironmentOptions.h>
#include <dcomp.h>
#include <wrl.h>
#include <mutex>
#include <condition_variable>
#include <string>
#include <string.h>
#include <dwmapi.h>
#include <locale>
#include <codecvt>
#include <winstring.h>
#include <roapi.h>
#include <dispatcherqueue.h>
#include <windows.system.h>
#include <windows.ui.composition.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>

GST_DEBUG_CATEGORY_EXTERN (gst_webview2_src_debug);
#define GST_CAT_DEFAULT gst_webview2_src_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace ABI::Windows::System;
using namespace ABI::Windows::UI::Composition;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Graphics;
using namespace ABI::Windows::Graphics::Capture;
using namespace ABI::Windows::Graphics::DirectX;
using namespace ABI::Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::DirectX::Direct3D11;

typedef ABI::Windows::Foundation::__FITypedEventHandler_2_Windows__CGraphics__CCapture__CDirect3D11CaptureFramePool_IInspectable_t
    IFrameArrivedHandler;

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080

static void gst_webview2_object_initialized (GstWebView2Object * obj);
static void gst_webview2_object_frame_arrived (GstWebView2Object * obj,
    ID3D11Texture2D * texture);

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_USER_DATA_FOLDER,
};

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

#define CLOSE_COM(obj) G_STMT_START { \
  if (obj) { \
    ComPtr<IClosable> closable; \
    obj.As (&closable); \
    if (closable) \
      closable->Close (); \
    obj = nullptr; \
  } \
} G_STMT_END

#define CHECK_HR_AND_RETURN(hr,func) G_STMT_START { \
  if (FAILED (hr)) { \
    GST_ERROR_OBJECT (obj_, G_STRINGIFY (func) " failed, hr 0x%x", (guint) hr); \
    SetEvent (event_handle_); \
    return hr; \
  } \
} G_STMT_END

class GstWebView2Item : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    FtmBase, IFrameArrivedHandler,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
    ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler,
    ICoreWebView2NavigationCompletedEventHandler,
    ICoreWebView2ExecuteScriptCompletedHandler>
{
public:
  static LRESULT CALLBACK
  WndProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
  {
    return DefWindowProcA (hwnd, msg, wparam, lparam);
  }

  STDMETHODIMP
  RuntimeClassInitialize (GstWebView2Object * obj, GstD3D11Device * device,
      HANDLE event_handle, HWND hwnd, PCWSTR user_data_folder)
  {
    obj_ = obj;
    device_ = device;
    event_handle_ = event_handle;
    hwnd_ = hwnd;

    HRESULT hr;
    ComPtr<IInspectable> insp;
    HSTRING class_id_hstring;
    WindowsCreateString (RuntimeClass_Windows_UI_Composition_Compositor,
      wcslen (RuntimeClass_Windows_UI_Composition_Compositor), &class_id_hstring);
    hr = RoActivateInstance (class_id_hstring, &insp);
    WindowsDeleteString (class_id_hstring);

    CHECK_HR_AND_RETURN (hr, RoActivateInstance);

    hr = insp.As (&comp_);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = comp_->CreateContainerVisual (&root_container_visual_);
    CHECK_HR_AND_RETURN (hr, CreateContainerVisual);

    hr = root_container_visual_.As (&root_visual_);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = root_visual_->put_Size ({ DEFAULT_WIDTH, DEFAULT_HEIGHT });
    CHECK_HR_AND_RETURN (hr, put_Size);

    hr = root_visual_->put_IsVisible (TRUE);
    CHECK_HR_AND_RETURN (hr, put_IsVisible);

    ComPtr<IVisualCollection> collection;
    hr = root_container_visual_->get_Children (&collection);
    CHECK_HR_AND_RETURN (hr, get_Children);

    hr = comp_->CreateContainerVisual (&webview_container_visual_);
    CHECK_HR_AND_RETURN (hr, CreateContainerVisual);

    hr = webview_container_visual_.As (&webview_visual_);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = webview_visual_.As (&webview_visual2_);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = collection->InsertAtTop (webview_visual_.Get ());
    CHECK_HR_AND_RETURN (hr, InsertAtTop);

    hr = webview_visual2_->put_RelativeSizeAdjustment ({ 1, 1 });
    CHECK_HR_AND_RETURN (hr, put_RelativeSizeAdjustment);

    hr = webview_visual_->put_IsVisible (TRUE);
    CHECK_HR_AND_RETURN (hr, put_IsVisible);

    if (user_data_folder) {
      auto option = Make<CoreWebView2EnvironmentOptions>();
      return CreateCoreWebView2EnvironmentWithOptions (nullptr,
          user_data_folder, option.Get (), this);
    }

    return CreateCoreWebView2Environment (this);
  }

  IFACEMETHOD (Invoke) (HRESULT hr, ICoreWebView2Environment * env)
  {
    CHECK_HR_AND_RETURN (hr, OnEnvironmentCompleted);

    hr = env->QueryInterface (IID_PPV_ARGS (&env_));
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = env_->CreateCoreWebView2CompositionController (hwnd_, this);
    CHECK_HR_AND_RETURN (hr, CreateCoreWebView2CompositionController);

    return S_OK;
  }

  IFACEMETHOD (Invoke) (HRESULT hr, ICoreWebView2CompositionController * comp_ctrl)
  {
    CHECK_HR_AND_RETURN (hr, OnControllerCompleted);

    comp_ctrl_ = comp_ctrl;
    ComPtr<ICoreWebView2Controller> ctrl;
    hr = comp_ctrl_.As (&ctrl);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = ctrl.As (&ctrl_);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    hr = ctrl_->put_BoundsMode (COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
    CHECK_HR_AND_RETURN (hr, put_BoundsMode);

    RECT rect = {};
    rect.left = 0;
    rect.top = 0;
    rect.right = DEFAULT_WIDTH;
    rect.bottom = DEFAULT_HEIGHT;
    hr = ctrl_->put_Bounds (rect);
    CHECK_HR_AND_RETURN (hr, put_Bounds);

    hr = ctrl_->put_ShouldDetectMonitorScaleChanges (FALSE);
    CHECK_HR_AND_RETURN (hr, put_ShouldDetectMonitorScaleChanges);

    hr = ctrl_->put_RasterizationScale (1);
    CHECK_HR_AND_RETURN (hr, put_RasterizationScale);

    hr = ctrl_->put_IsVisible (TRUE);
    CHECK_HR_AND_RETURN (hr, put_IsVisible);

    hr = comp_ctrl_->put_RootVisualTarget (webview_container_visual_.Get ());
    CHECK_HR_AND_RETURN (hr, put_RootVisualTarget);

    hr = ctrl_->get_CoreWebView2 (&webview_);
    CHECK_HR_AND_RETURN (hr, get_CoreWebView2);

    hr = webview_->add_NavigationCompleted (this, &navigation_compl_token_);
    CHECK_HR_AND_RETURN (hr, get_CoreWebView2);

    ComPtr<ICoreWebView2_8> webview8;
    hr = webview_.As (&webview8);
    if (SUCCEEDED (hr))
      webview8->put_IsMuted (TRUE);

    hr = startCapture ();
    CHECK_HR_AND_RETURN (hr, startCapture);

    gst_webview2_object_initialized (obj_);

    return S_OK;
  }

  IFACEMETHOD(Invoke) (HRESULT hr, LPCWSTR result_json)
  {
    GST_DEBUG_OBJECT (obj_, "Executing script result 0x%x", (guint) hr);

    return S_OK;
  }

  IFACEMETHOD(Invoke) (ICoreWebView2 * sender,
      ICoreWebView2NavigationCompletedEventArgs * arg)
  {
    GST_DEBUG_OBJECT (obj_, "Navigation completed");

    if (!script_.empty ()) {
      GST_DEBUG_OBJECT (obj_, "Executing script");
      sender->ExecuteScript (script_.c_str (), this);
    }

    return S_OK;
  }

  HRESULT
  startCapture ()
  {
    ComPtr<IGraphicsCaptureItemStatics> item_statics;

    auto hr = GstGetActivationFactory <IGraphicsCaptureItemStatics,
        RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem> (&item_statics);
    CHECK_HR_AND_RETURN (hr, GstGetActivationFactory);

    hr = item_statics->CreateFromVisual (root_visual_.Get (), &item_);
    CHECK_HR_AND_RETURN (hr, CreateFromVisual);

    auto device = gst_d3d11_device_get_device_handle (device_);
    ComPtr<ID3D10Multithread> multi_thread;
    hr = device->QueryInterface (IID_PPV_ARGS (&multi_thread));
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    multi_thread->SetMultithreadProtected (TRUE);
    ComPtr<IDXGIDevice> dxgi_device;
    hr = device->QueryInterface (IID_PPV_ARGS (&dxgi_device));
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    ComPtr < IInspectable > insp;
    hr = CreateDirect3D11DeviceFromDXGIDevice (dxgi_device.Get (), &insp);
    CHECK_HR_AND_RETURN (hr, CreateDirect3D11DeviceFromDXGIDevice);

    hr = insp.As (&d3d_device_);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    ComPtr < IDirect3D11CaptureFramePoolStatics > pool_statics;
    hr = GstGetActivationFactory < IDirect3D11CaptureFramePoolStatics,
        RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool >
        (&pool_statics);
    CHECK_HR_AND_RETURN (hr, GstGetActivationFactory);

    frame_size_.Width = DEFAULT_WIDTH;
    frame_size_.Height = DEFAULT_HEIGHT;

    hr = pool_statics->Create (d3d_device_.Get (),
        DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
        2, frame_size_, &frame_pool_);
    CHECK_HR_AND_RETURN (hr, Create);

    hr = frame_pool_->add_FrameArrived (this, &arrived_token_);
    CHECK_HR_AND_RETURN (hr, add_FrameArrived);

    hr = frame_pool_->CreateCaptureSession (item_.Get (), &session_);
    CHECK_HR_AND_RETURN (hr, CreateCaptureSession);

    hr = session_->StartCapture ();
    CHECK_HR_AND_RETURN (hr, StartCapture);

    return S_OK;
  }

  IFACEMETHOD(Invoke) (IDirect3D11CaptureFramePool * pool, IInspectable * arg)
  {
    HRESULT hr;

    GST_LOG_OBJECT (obj_, "Frame arrived");

    ComPtr < IDirect3D11CaptureFrame > new_frame;
    pool->TryGetNextFrame (&new_frame);
    if (!new_frame) {
      GST_WARNING_OBJECT (obj_, "No frame");
      return S_OK;
    }

    ComPtr < IDirect3DSurface > surface;
    hr = new_frame->get_Surface (&surface);
    CHECK_HR_AND_RETURN (hr, get_Surface);

    ComPtr < IDirect3DDxgiInterfaceAccess > access;
    hr = surface.As (&access);
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    ComPtr < ID3D11Texture2D > texture;
    hr = access->GetInterface (IID_PPV_ARGS (&texture));
    CHECK_HR_AND_RETURN (hr, QueryInterface);

    gst_webview2_object_frame_arrived (obj_, texture.Get ());

    return S_OK;
  }

  void Close ()
  {
    if (frame_pool_)
      frame_pool_->remove_FrameArrived (arrived_token_);

    CLOSE_COM (session_);
    CLOSE_COM (frame_pool_);
    CLOSE_COM (item_);

    if (webview_) {
      webview_->Stop ();
      webview_ = nullptr;
    }

    if (ctrl_) {
      ctrl_->Close ();
      ctrl_ = nullptr;
    }

    comp_ctrl_ = nullptr;
    ctrl_ = nullptr;
    env_ = nullptr;

    webview_visual2_ = nullptr;
    webview_visual_ = nullptr;
    webview_container_visual_ = nullptr;
    root_visual_ = nullptr;
    root_container_visual_ = nullptr;
    comp_ = nullptr;
  }

  HRESULT Navigate (LPCWSTR location, LPCWSTR script)
  {
    if (!webview_ || !location)
      return E_FAIL;

    if (script)
      script_ = script;
    else
      script_.clear ();

    return webview_->Navigate (location);
  }

  HRESULT UpdateSize (FLOAT width, FLOAT height)
  {
    GST_DEBUG_OBJECT (obj_, "Updating size to %dx%d",
        (UINT) width, (UINT) height);

    auto hr = root_visual_->put_Size ({width, height});
    CHECK_HR_AND_RETURN (hr, put_Size);

    RECT rect = {};
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    hr = ctrl_->put_Bounds (rect);
    CHECK_HR_AND_RETURN (hr, put_Bounds);

    frame_size_.Width = width;
    frame_size_.Height = height;
    hr = frame_pool_->Recreate (d3d_device_.Get (),
      DirectXPixelFormat::DirectXPixelFormat_B8G8R8A8UIntNormalized,
      2, frame_size_);
    CHECK_HR_AND_RETURN (hr, Recreate);

    return S_OK;
  }

  void HandleEvent (GstEvent * event)
  {
    auto type = gst_navigation_event_get_type (event);
    gdouble x, y;
    gint button;

    switch (type) {
      /* FIXME: Implement key event */
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
      case GST_NAVIGATION_EVENT_MOUSE_DOUBLE_CLICK:
        if (gst_navigation_event_parse_mouse_button_event (event,
                &button, &x, &y)) {
          GST_TRACE_OBJECT (obj_, "Mouse press, button %d, %lfx%lf",
              button, x, y);
          COREWEBVIEW2_MOUSE_EVENT_KIND kind;
          COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS vkeys =
              COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
          GstNavigationModifierType state = GST_NAVIGATION_MODIFIER_NONE;
          POINT point;

          point.x = (LONG) x;
          point.y = (LONG) y;

          switch (button) {
            case 1:
              if (type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS)
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
              else if (type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE)
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
              else
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK;
              break;
            case 2:
              if (type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS)
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN;
              else if (type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE)
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
              else
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK;
              break;
            case 3:
              if (type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS)
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN;
              else if (type == GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE)
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
              else
                kind = COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOUBLE_CLICK;
              break;
            default:
              return;
          }

          if (gst_navigation_event_parse_modifier_state (event, &state)) {
            if ((state & GST_NAVIGATION_MODIFIER_SHIFT_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_SHIFT;
            if ((state & GST_NAVIGATION_MODIFIER_CONTROL_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_CONTROL;
            if ((state & GST_NAVIGATION_MODIFIER_BUTTON1_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
            if ((state & GST_NAVIGATION_MODIFIER_BUTTON2_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON;
          }

          comp_ctrl_->SendMouseInput (kind, vkeys, 0, point);
        }
        break;
      case GST_NAVIGATION_EVENT_MOUSE_MOVE:
        if (gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
          GST_TRACE_OBJECT (obj_, "Mouse move, %lfx%lf", x, y);
          POINT point;

          point.x = (LONG) x;
          point.y = (LONG) y;

          COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS vkeys =
              COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
          GstNavigationModifierType state = GST_NAVIGATION_MODIFIER_NONE;

          if (gst_navigation_event_parse_modifier_state (event, &state)) {
            if ((state & GST_NAVIGATION_MODIFIER_SHIFT_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_SHIFT;
            if ((state & GST_NAVIGATION_MODIFIER_CONTROL_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_CONTROL;
            if ((state & GST_NAVIGATION_MODIFIER_BUTTON1_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
            if ((state & GST_NAVIGATION_MODIFIER_BUTTON2_MASK) != 0)
              vkeys |= COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON;
          }

          comp_ctrl_->SendMouseInput (COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
              vkeys, 0, point);
        }
        break;
      default:
        break;
    }
  }

private:
  HANDLE event_handle_;
  HWND hwnd_ = nullptr;
  GstWebView2Object *obj_ = nullptr;
  GstD3D11Device *device_ = nullptr;
  ComPtr<ICompositor> comp_;
  ComPtr<IContainerVisual> root_container_visual_;
  ComPtr<IVisual> root_visual_;
  ComPtr<IContainerVisual> webview_container_visual_;
  ComPtr<IVisual> webview_visual_;
  ComPtr<IVisual2> webview_visual2_;

  ComPtr<ICoreWebView2Environment3> env_;
  ComPtr<ICoreWebView2Controller3> ctrl_;
  ComPtr<ICoreWebView2CompositionController > comp_ctrl_;
  ComPtr<ICoreWebView2 > webview_;
  EventRegistrationToken navigation_compl_token_ = { };
  std::wstring script_;

  ComPtr<IGraphicsCaptureItem> item_;
  SizeInt32 frame_size_ = { };
  ComPtr<IDirect3DDevice> d3d_device_;
  ComPtr<IDirect3D11CaptureFramePool> frame_pool_;
  ComPtr<IGraphicsCaptureSession> session_;
  EventRegistrationToken arrived_token_ = { };
};

class NaviEventHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    FtmBase, IDispatcherQueueHandler>
{
public:
  STDMETHODIMP
  RuntimeClassInitialize (GstWebView2Item * item, GstEvent * event)
  {
    item_ = item;
    event_ = gst_event_ref (event);

    return S_OK;
  }

  IFACEMETHOD(Invoke) (void)
  {
    item_->HandleEvent (event_);
    item_ = nullptr;

    return S_OK;
  }

  ~NaviEventHandler ()
  {
    gst_clear_event (&event_);
  }

private:
  ComPtr<GstWebView2Item> item_;
  GstEvent *event_;
};

class UpdateSizeHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    FtmBase, IDispatcherQueueHandler>
{
public:
  STDMETHODIMP
  RuntimeClassInitialize (GstWebView2Item * item, guint width, guint height)
  {
    item_ = item;
    width_ = width;
    height_ = height;

    return S_OK;
  }

  IFACEMETHOD(Invoke) (void)
  {
    item_->UpdateSize (width_, height_);
    item_ = nullptr;

    return S_OK;
  }

private:
  ComPtr<GstWebView2Item> item_;
  FLOAT width_;
  FLOAT height_;
};

class UpdateLocationHandler : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    FtmBase, IDispatcherQueueHandler>
{
public:
  STDMETHODIMP
  RuntimeClassInitialize (GstWebView2Item * item, const std::string & location,
      const std::string & script)
  {
    item_ = item;
    location_wide_ = g_utf8_to_utf16 (location.c_str (),
        -1, nullptr, nullptr, nullptr);
    if (!script.empty ()) {
      script_wide_ = g_utf8_to_utf16 (script.c_str (),
          -1, nullptr, nullptr, nullptr);
    }

    return S_OK;
  }

  IFACEMETHOD(Invoke) (void)
  {
    item_->Navigate ((LPCWSTR) location_wide_, (LPCWSTR) script_wide_);
    item_ = nullptr;

    return S_OK;
  }

  ~UpdateLocationHandler ()
  {
    g_free (location_wide_);
    g_free (script_wide_);
  }

private:
  ComPtr<GstWebView2Item> item_;
  gunichar2 *location_wide_ = nullptr;
  gunichar2 *script_wide_ = nullptr;
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

enum WebView2State
{
  WEBVIEW2_STATE_INIT,
  WEBVIEW2_STATE_RUNNING,
  WEBVIEW2_STATE_EXIT,
};

struct GstWebView2ObjectPrivate
{
  GstWebView2ObjectPrivate ()
  {
    shutdown_begin_handle = CreateEventEx (nullptr,
        nullptr, 0, EVENT_ALL_ACCESS);
    shutdown_end_handle = CreateEventEx (nullptr,
        nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
  }

   ~GstWebView2ObjectPrivate ()
  {
    SetEvent (shutdown_begin_handle);
    g_clear_pointer (&main_thread, g_thread_join);

    CloseHandle (shutdown_begin_handle);
    CloseHandle (shutdown_end_handle);
    gst_clear_object (&device);
  }

  GstD3D11Device *device = nullptr;
  std::mutex lock;
  std::condition_variable cond;
  ComPtr<GstWebView2Item> item;
  ComPtr<ID3D11Texture2D> texture;
  ComPtr<IDispatcherQueue> queue;
  GThread *main_thread = nullptr;
  std::string location;
  std::string user_data_folder;
  HANDLE shutdown_begin_handle;
  HANDLE shutdown_end_handle;
  WebView2State state = WEBVIEW2_STATE_INIT;
  gboolean flushing = FALSE;
};
/* *INDENT-ON* */

struct _GstWebView2Object
{
  GstObject parent;

  GstWebView2ObjectPrivate *priv;
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

  g_object_class_install_property (object_class, PROP_USER_DATA_FOLDER,
      g_param_spec_string ("user-data-folder", "User Data Folder",
          "User data folder location. Default location is ${APP_EXE}.WebView2 "
          "but can be varying depending on platform",
          nullptr, (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
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
  auto self = GST_WEBVIEW2_OBJECT (object);
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
  auto self = GST_WEBVIEW2_OBJECT (object);

  GST_DEBUG_OBJECT (self, "Clearing engine");

  delete self->priv;

  GST_DEBUG_OBJECT (self, "Cleared");

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_webview2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_WEBVIEW2_OBJECT (object);
  auto priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_DEVICE:
      priv->device = (GstD3D11Device *) g_value_dup_object (value);
      break;
    case PROP_USER_DATA_FOLDER:
    {
      auto udf = g_value_get_string (value);
      if (udf)
        priv->user_data_folder = udf;
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webview2_event_loop (GstWebView2Object * self)
{
  auto priv = self->priv;
  ComPtr < AsyncWaiter > async_waiter;
  ComPtr < IAsyncAction > shutdown_action;
  HRESULT hr;
  ComPtr < IDispatcherQueueController > queue_ctrl;
  DispatcherQueueOptions queue_opt;
  HWND hwnd = nullptr;
  HANDLE waitables[] = { priv->shutdown_begin_handle,
    priv->shutdown_end_handle
  };
  gunichar2 *udf_location = nullptr;

  if (!priv->user_data_folder.empty ()) {
    udf_location = g_utf8_to_utf16 (priv->user_data_folder.c_str (),
        -1, nullptr, nullptr, nullptr);
  }

  GST_D3D11_CALL_ONCE_BEGIN {
    WNDCLASSEXA wc = { };
    wc.cbSize = sizeof (WNDCLASSEXA);
    wc.lpfnWndProc = &GstWebView2Item::WndProc;
    wc.hInstance = GetModuleHandle (nullptr);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpszClassName = "GstWebView2Item";
    RegisterClassExA (&wc);
  }
  GST_D3D11_CALL_ONCE_END;

  hwnd = CreateWindowExA (0, "GstWebView2Item", "GstWebView2Item", 0,
      CW_DEFAULT, CW_DEFAULT, 0, 0, HWND_MESSAGE, nullptr,
      GetModuleHandle (nullptr), nullptr);
  if (!hwnd) {
    GST_ERROR_OBJECT (self, "Couldn't create message hwnd");
    goto out;
  }

  hr = MakeAndInitialize < AsyncWaiter > (&async_waiter,
      priv->shutdown_end_handle);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create shutdown waiter");
    goto out;
  }

  queue_opt.dwSize = sizeof (DispatcherQueueOptions);
  queue_opt.threadType = DQTYPE_THREAD_CURRENT;
  queue_opt.apartmentType = DQTAT_COM_NONE;

  hr = CreateDispatcherQueueController (queue_opt, &queue_ctrl);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create queue controller");
    goto out;
  }

  hr = queue_ctrl->get_DispatcherQueue (&priv->queue);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't get dispatcher queue");
    goto out;
  }

  hr = MakeAndInitialize < GstWebView2Item > (&priv->item, self, priv->device,
      priv->shutdown_begin_handle, hwnd, (PCWSTR) udf_location);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't initialize item");
    goto out;
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
      {
        std::lock_guard < std::mutex > lk (priv->lock);
        priv->texture = nullptr;
        priv->queue = nullptr;
        priv->item->Close ();
      }
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
    } else if (wait_ret == WAIT_IO_COMPLETION) {
      /* Do nothing */
    } else if (wait_ret != WAIT_OBJECT_0 + G_N_ELEMENTS (waitables)) {
      GST_ERROR_OBJECT (self, "Unexpected wait return %u", (guint) wait_ret);
      break;
    }
  }

out:
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->state = WEBVIEW2_STATE_EXIT;
  priv->cond.notify_all ();

  priv->item = nullptr;
  priv->queue = nullptr;
  if (hwnd)
    CloseWindow (hwnd);

  g_free (udf_location);
}

static gpointer
gst_webview2_thread_func (GstWebView2Object * self)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Entering thread");

  SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);

  RoInitialize (RO_INIT_SINGLETHREADED);
  gst_webview2_event_loop (self);
  RoUninitialize ();

  SetEvent (priv->shutdown_end_handle);

  return nullptr;
}

static void
gst_webview2_object_initialized (GstWebView2Object * obj)
{
  auto priv = obj->priv;

  GST_DEBUG_OBJECT (obj, "Initialized");

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->state = WEBVIEW2_STATE_RUNNING;
  priv->cond.notify_all ();
}

static void
gst_webview2_object_frame_arrived (GstWebView2Object * obj,
    ID3D11Texture2D * texture)
{
  auto priv = obj->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->texture = nullptr;
  priv->texture = texture;
  priv->cond.notify_all ();
}

GstWebView2Object *
gst_webview2_object_new (GstD3D11Device * device,
    const std::string & user_data_folder)
{
  GstWebView2Object *self;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), nullptr);

  self = (GstWebView2Object *)
      g_object_new (GST_TYPE_WEBVIEW2_OBJECT, "device", device,
      "user-data-folder", user_data_folder.c_str (), nullptr);
  gst_object_ref_sink (self);

  if (self->priv->state != WEBVIEW2_STATE_RUNNING) {
    gst_object_unref (self);
    return nullptr;
  }

  return self;
}

gboolean
gst_webview2_object_set_location (GstWebView2Object * object,
    const std::string & location, const std::string & script)
{
  auto priv = object->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->queue || !priv->item)
    return FALSE;

  ComPtr < UpdateLocationHandler > handler;
  auto hr = MakeAndInitialize < UpdateLocationHandler > (&handler,
      priv->item.Get (), location, script);
  if (FAILED (hr))
    return FALSE;

  boolean ret;
  priv->queue->TryEnqueue (handler.Get (), &ret);

  return ret;
}

gboolean
gst_webview_object_update_size (GstWebView2Object * object,
    guint width, guint height)
{
  auto priv = object->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->queue || !priv->item)
    return FALSE;

  ComPtr < UpdateSizeHandler > handler;
  auto hr = MakeAndInitialize < UpdateSizeHandler > (&handler,
      priv->item.Get (), width, height);
  if (FAILED (hr))
    return FALSE;

  boolean ret;
  priv->queue->TryEnqueue (handler.Get (), &ret);

  return ret;
}

void
gst_webview2_object_send_event (GstWebView2Object * object, GstEvent * event)
{
  auto priv = object->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->queue || !priv->item)
    return;

  ComPtr < NaviEventHandler > handler;
  auto hr = MakeAndInitialize < NaviEventHandler > (&handler,
      priv->item.Get (), event);
  if (FAILED (hr))
    return;

  boolean ret;
  priv->queue->TryEnqueue (handler.Get (), &ret);
}

GstFlowReturn
gst_webview2_object_do_capture (GstWebView2Object * object,
    ID3D11Texture2D * texture, ID3D11DeviceContext4 * context4,
    ID3D11Fence * fence, guint64 * fence_val, gboolean need_signal)
{
  auto priv = object->priv;

  std::unique_lock < std::mutex > lk (priv->lock);
  while (!priv->flushing && priv->state == WEBVIEW2_STATE_RUNNING &&
      !priv->texture) {
    priv->cond.wait (lk);
  }

  if (priv->flushing) {
    GST_DEBUG_OBJECT (object, "We are flushing");
    return GST_FLOW_FLUSHING;
  }

  if (priv->state != WEBVIEW2_STATE_RUNNING) {
    GST_DEBUG_OBJECT (object, "Not a running state");
    return GST_FLOW_EOS;
  }

  D3D11_TEXTURE2D_DESC src_desc;
  D3D11_TEXTURE2D_DESC dst_desc;

  priv->texture->GetDesc (&src_desc);
  texture->GetDesc (&dst_desc);
  auto context = gst_d3d11_device_get_device_context_handle (priv->device);
  GstD3D11DeviceLockGuard dlk (priv->device);

  D3D11_BOX box = { };
  box.right = MIN (src_desc.Width, dst_desc.Width);
  box.bottom = MIN (src_desc.Height, dst_desc.Height);
  box.front = 0;
  box.back = 1;

  context->CopySubresourceRegion (texture, 0, 0, 0, 0, priv->texture.Get (),
      0, &box);
  if (need_signal) {
    auto fence_value = *fence_val + 1;
    auto hr = context4->Signal (fence, fence_value);
    if (!gst_d3d11_result (hr, priv->device)) {
      GST_ERROR_OBJECT (object, "Signal failed");
      return GST_FLOW_ERROR;
    }

    *fence_val = fence_value;
  }

  return GST_FLOW_OK;
}

void
gst_webview2_object_set_flushing (GstWebView2Object * object, gboolean flushing)
{
  auto priv = object->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->flushing = flushing;
  priv->cond.notify_all ();
}

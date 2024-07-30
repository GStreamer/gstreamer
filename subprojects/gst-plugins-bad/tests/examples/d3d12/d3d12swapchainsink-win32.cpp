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

#include <gst/gst.h>
#include <gst/video/video.h>

#include <windows.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl.h>
#include <memory>

using namespace Microsoft::WRL;

static GMainLoop *loop_ = nullptr;
static HWND hwnd_ = nullptr;

struct GpuResource
{
  ComPtr<IDCompositionDesktopDevice> dcomp_device;
  ComPtr<IDCompositionTarget> target;
  ComPtr<IDCompositionVisual2> visual;
  ComPtr<IDCompositionVirtualSurface> bg_surface;
  ComPtr<IDCompositionVisual2> swapchain_visual;
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> context11;
};

struct AppData
{
  GstElement *pipeline = nullptr;
  std::shared_ptr<GpuResource> resource;
};

#define APP_DATA_PROP_NAME L"EXAMPLE-APP-DATA"

static LRESULT CALLBACK
window_proc (HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message) {
    case WM_NCCREATE:
    {
      LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW) lparam;
      auto data = (AppData *) lpcs->lpCreateParams;
      SetPropW (hwnd, APP_DATA_PROP_NAME, data);
      break;
    }
    case WM_DESTROY:
      gst_println ("Destroy window");
      if (loop_)
        g_main_loop_quit (loop_);
      break;
    case WM_SIZE:
    {
      auto data = (AppData *) GetPropW (hwnd, APP_DATA_PROP_NAME);
      if (!data)
        break;

      auto resource = data->resource;
      if (!resource)
        break;

      RECT rect = { };
      GetClientRect (hwnd, &rect);
      gint width = (rect.right - rect.left);
      gint height = (rect.bottom - rect.top);

      if (width > 0 && height > 0) {
        POINT offset;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        auto hr = resource->bg_surface->Resize (width, height);
        if (SUCCEEDED (hr)) {
          hr = resource->bg_surface->BeginDraw (nullptr,
              IID_PPV_ARGS (&texture), &offset);
        }

        if (SUCCEEDED (hr)) {
          hr = resource->device11->CreateRenderTargetView (texture.Get (), nullptr,
              &rtv);
        }

        if (SUCCEEDED (hr)) {
          FLOAT bg_color[] = { 0.5, 0.5, 0.5, 0.5 };
          resource->context11->ClearRenderTargetView (rtv.Get (), bg_color);
          hr = resource->bg_surface->EndDraw ();
        }

        if (SUCCEEDED (hr)) {
          if (width > 320) {
            FLOAT offset_x = ((FLOAT) (width - 320)) / 2.0;
            resource->swapchain_visual->SetOffsetX (offset_x);
          } else {
            resource->swapchain_visual->SetOffsetX (0.0);
          }

          if (height > 240) {
            FLOAT offset_y = ((FLOAT) (height - 240)) / 2.0;
            resource->swapchain_visual->SetOffsetY (offset_y);
          } else {
            resource->swapchain_visual->SetOffsetY (0.0);
          }

          resource->dcomp_device->Commit ();
        }
      }
      break;
    }
    default:
      break;
  }

  return DefWindowProcW (hwnd, message, wparam, lparam);
}

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, AppData * data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      gst_printerrln ("ERROR %s", err->message);
      if (dbg != nullptr)
        gst_printerrln ("ERROR debug information: %s", dbg);
      g_clear_error (&err);
      g_free (dbg);

      g_main_loop_quit (loop_);
      break;
    }
    case GST_MESSAGE_EOS:
    {
      gst_println ("Got EOS");
      g_main_loop_quit (loop_);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
msg_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

int
main (int argc, char ** argv)
{
  GIOChannel *msg_io_channel = nullptr;
  AppData app_data = { };
  HRESULT hr;
  gchar *uri = nullptr;
  GOptionEntry options[] = {
    {"uri", 0, 0, G_OPTION_ARG_STRING, &uri, "URI to play"},
    {nullptr}
  };

  auto opt_ctx = g_option_context_new ("D3D12 swapchainsink");
  g_option_context_add_main_entries (opt_ctx, options, nullptr);
  g_option_context_set_help_enabled (opt_ctx, TRUE);
  g_option_context_add_group (opt_ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (opt_ctx, &argc, &argv, nullptr)) {
    gst_printerrln ("option parsing failed");
    return 1;
  }

  loop_ = g_main_loop_new (nullptr, FALSE);

  /* Creates pipeline */
  GstElement *sink;
  if (uri) {
    app_data.pipeline = gst_element_factory_make ("playbin3", nullptr);
    if (!app_data.pipeline) {
      gst_printerrln ("Couldn't create pipeline");
      return 1;
    }

    sink = gst_element_factory_make ("d3d12swapchainsink", nullptr);
    if (!sink) {
      gst_printerrln ("Couldn't create sink");
      return 1;
    }

    g_object_set (app_data.pipeline, "video-sink", sink, "uri", uri, nullptr);
  } else {
    app_data.pipeline = gst_parse_launch ("d3d12testsrc ! "
        "video/x-raw(memory:D3D12Memory),format=RGBA,width=240,height=240 ! "
        "dwritetimeoverlay font-size=50 ! queue ! d3d12swapchainsink name=sink",
        nullptr);

    if (!app_data.pipeline) {
      gst_printerrln ("Couldn't create pipeline");
      return 1;
    }

    sink = gst_bin_get_by_name (GST_BIN (app_data.pipeline), "sink");
    g_assert (sink);
  }

  gst_bus_add_watch (GST_ELEMENT_BUS (app_data.pipeline), (GstBusFunc) bus_msg,
      &app_data);

  /* Set swapchain resolution and border color */
  g_signal_emit_by_name (sink, "resize", 320, 240);

  guint64 border_color = 0;
  /* alpha */
  border_color |= ((guint64) (G_MAXUINT16 / 2)) << 48;
  /* red */
  border_color |= ((guint64) (G_MAXUINT16 / 2)) << 32;
  g_object_set (sink, "border-color", border_color, nullptr);

  /* Gets swapchain handle. This swapchain will be bound to a dcomp visual node */
  IUnknown *swapchain = nullptr;
  g_object_get (sink, "swapchain", &swapchain, nullptr);
  if (!swapchain) {
    gst_printerrln ("Couldn't get swapchain");
    return 1;
  }

  /* playbin will take floating refcount */
  if (!uri)
    gst_object_unref (sink);

  /* Creates d3d11 device to initialize dcomp device.
   * Note that d3d11 (or d2d) device will not be required if swapchain is
   * the only visual node (i.e., root node) which needs to be composed.
   * In that case, an application can pass nullptr device to
   * DCompositionCreateDevice2() */
  auto resource = std::make_shared<GpuResource> ();
  ComPtr<IDXGIFactory1> factory;
  ComPtr<IDXGIAdapter> adapter;

  hr = CreateDXGIFactory1 (IID_PPV_ARGS (&factory));
  if (FAILED (hr)) {
    gst_printerrln ("CreateDXGIFactory1 failed");
    return 1;
  }

  hr = factory->EnumAdapters (0, &adapter);
  if (FAILED (hr)) {
    gst_printerrln ("EnumAdapters failed");
    return 1;
  }

  static const D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_11_1,
  };
  hr = D3D11CreateDevice (adapter.Get (), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, feature_levels, 1, D3D11_SDK_VERSION,
      &resource->device11, nullptr, &resource->context11);
  if (FAILED (hr)) {
    gst_printerrln ("D3D11CreateDevice failed");
    return 1;
  }

  /* Prepare main window */
  WNDCLASSEXW wc = { };
  RECT wr = { 0, 0, 640, 480 };
  HINSTANCE hinstance = GetModuleHandle (nullptr);
  wc.cbSize = sizeof (WNDCLASSEXW);
  wc.lpfnWndProc = (WNDPROC) window_proc;
  wc.hInstance = hinstance;
  wc.hIcon = LoadIcon (nullptr, IDI_WINLOGO);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
  wc.lpszClassName = L"GstD3D12SwapChainSinkExample";

  RegisterClassExW (&wc);
  AdjustWindowRect (&wr, WS_OVERLAPPEDWINDOW, FALSE);

  hwnd_ = CreateWindowExW (WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
      L"D3D12SwapChainSink Example - Win32",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) nullptr, (HMENU) nullptr,
      hinstance, &app_data);

  msg_io_channel = g_io_channel_win32_new_messages (0);
  g_io_add_watch (msg_io_channel, G_IO_IN, msg_cb, nullptr);

  /* Create DComp resources */
  hr = DCompositionCreateDevice2 (resource->device11.Get (),
      IID_PPV_ARGS (&resource->dcomp_device));
  if (FAILED (hr)) {
    gst_printerrln ("Couldn't create composition device");
    return 1;
  }

  hr = resource->dcomp_device->CreateTargetForHwnd (hwnd_, TRUE,
      &resource->target);
  if (FAILED (hr)) {
    gst_printerrln ("CreateTargetForHwnd failed");
    return 1;
  }

  hr = resource->dcomp_device->CreateVisual (&resource->visual);
  if (FAILED (hr)) {
    gst_printerrln ("CreateVisual failed");
    return 1;
  }

  hr = resource->target->SetRoot (resource->visual.Get ());
  if (FAILED (hr)) {
    gst_printerrln ("SetRoot failed");
    return 1;
  }

  /* Create background visual, and clear color using d3d11 API */
  hr = resource->dcomp_device->CreateVirtualSurface (640, 480,
      DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
      &resource->bg_surface);
  if (FAILED (hr)) {
    gst_printerrln ("CreateVirtualSurface failed");
    return 1;
  }

  hr = resource->visual->SetContent (resource->bg_surface.Get ());
  if (FAILED (hr)) {
    gst_printerrln ("SetContent failed");
    return 1;
  }

  {
    POINT offset;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    hr = resource->bg_surface->BeginDraw (nullptr, IID_PPV_ARGS (&texture),
        &offset);
    if (FAILED (hr)) {
      gst_printerrln ("BeginDraw failed");
      return 1;
    }

    hr = resource->device11->CreateRenderTargetView (texture.Get (),
        nullptr, &rtv);
    if (FAILED (hr)) {
      gst_printerrln ("CreateRenderTargetView failed");
      return 1;
    }

    /* Draw semi-transparent background */
    FLOAT bg_color[] = { 0.5, 0.5, 0.5, 0.5 };
    resource->context11->ClearRenderTargetView (rtv.Get (), bg_color);
    hr = resource->bg_surface->EndDraw ();
    if (FAILED (hr)) {
      gst_printerrln ("EndDraw failed");
      return 1;
    }
  }

  hr = resource->dcomp_device->CreateVisual (&resource->swapchain_visual);
  if (FAILED (hr)) {
    gst_printerrln ("CreateVisual failed");
    return 1;
  }

  hr = resource->visual->AddVisual (resource->swapchain_visual.Get (), TRUE, nullptr);
  if (FAILED (hr)) {
    gst_printerrln ("AddVisual failed");
    return 1;
  }

  hr = resource->swapchain_visual->SetOffsetX (160.0);
  if (FAILED (hr)) {
    gst_printerrln ("SetOffsetX failed");
    return 1;
  }

  hr = resource->swapchain_visual->SetOffsetY (120.0);
  if (FAILED (hr)) {
    gst_printerrln ("SetOffsetY failed");
    return 1;
  }

  hr = resource->swapchain_visual->SetContent (swapchain);
  if (FAILED (hr)) {
    gst_printerrln ("SetContent failed");
    return 1;
  }

  hr = resource->dcomp_device->Commit ();
  if (FAILED (hr)) {
    gst_printerrln ("Commit failed");
    return 1;
  }

  app_data.resource = std::move (resource);

  gst_element_set_state (app_data.pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop_);

  gst_element_set_state (app_data.pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (app_data.pipeline));

  app_data.resource = nullptr;
  gst_object_unref (app_data.pipeline);

  if (hwnd_)
    DestroyWindow (hwnd_);

  g_io_channel_unref (msg_io_channel);
  g_main_loop_unref (loop_);
  g_free (uri);

  gst_deinit ();

  return 0;
}

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
#include <wrl.h>
#include <string.h>
#include <winstring.h>
#include <roapi.h>
#include <dispatcherqueue.h>
#include <windows.system.h>
#include <windows.ui.composition.h>
#include <windows.ui.composition.interop.h>
#include <windows.ui.composition.desktop.h>

using namespace Microsoft::WRL;
using namespace ABI::Windows::System;
using namespace ABI::Windows::UI::Composition;
using namespace ABI::Windows::UI::Composition::Desktop;
using namespace ABI::Windows::Foundation;

static LRESULT CALLBACK
window_proc (HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  switch (message) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }

  return DefWindowProcW (hwnd, message, wparam, lparam);
}

static void
app_main (void)
{
  auto pipeline = gst_parse_launch ("d3d12testsrc ! "
      "video/x-raw(memory:D3D12Memory),format=RGBA,width=240,height=240 ! "
      "dwritetimeoverlay font-size=50 ! queue ! d3d12swapchainsink name=sink",
      nullptr);
  if (!pipeline) {
    gst_printerrln ("Couldn't create pipeline");
    return;
  }

  auto sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  g_assert (sink);

  /* Set swapchain resolution and border color */
  g_signal_emit_by_name (sink, "resize", 320, 240);

  guint64 border_color = 0;
  /* alpha */
  border_color |= ((guint64) (G_MAXUINT16 / 2)) << 48;
  /* red */
  border_color |= ((guint64) (G_MAXUINT16 / 2)) << 32;
  g_object_set (sink, "border-color", border_color, nullptr);

  IUnknown *swapchain = nullptr;
  g_object_get (sink, "swapchain", &swapchain, nullptr);
  if (!swapchain) {
    gst_printerrln ("Couldn't get swapchain");
    return;
  }
  gst_object_unref (sink);

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

  HWND hwnd = CreateWindowExW (WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName,
      L"D3D12SwapChainSink Example - WinRT",
      WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT,
      wr.right - wr.left, wr.bottom - wr.top, (HWND) nullptr, (HMENU) nullptr,
      hinstance, nullptr);

  /* compositor requires dispatcher queue. Creates one for the current main thread */
  DispatcherQueueOptions queue_opt = { };
  queue_opt.dwSize = sizeof (DispatcherQueueOptions);
  queue_opt.threadType = DQTYPE_THREAD_CURRENT;
  queue_opt.apartmentType = DQTAT_COM_NONE;

  ComPtr < IDispatcherQueueController > queue_ctrl;
  HRESULT hr = CreateDispatcherQueueController (queue_opt, &queue_ctrl);
  g_assert (SUCCEEDED (hr));

  ComPtr<IDispatcherQueue> dqueue;
  hr = queue_ctrl->get_DispatcherQueue (&dqueue);
  g_assert (SUCCEEDED (hr));

  /* Creates compositor */
  ComPtr<IInspectable> insp;
  HSTRING class_id_hstring;
  WindowsCreateString (RuntimeClass_Windows_UI_Composition_Compositor,
    wcslen (RuntimeClass_Windows_UI_Composition_Compositor), &class_id_hstring);
  hr = RoActivateInstance (class_id_hstring, &insp);
  WindowsDeleteString (class_id_hstring);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositor> compositor;
  hr = insp.As (&compositor);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositorDesktopInterop> compositor_desktop_interop;
  hr = compositor.As (&compositor_desktop_interop);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositorInterop> compositor_interop;
  hr = compositor.As (&compositor_interop);
  g_assert (SUCCEEDED (hr));

  /* Creates compositor target for the main HWND */
  ComPtr<IDesktopWindowTarget> desktop_target;
  hr = compositor_desktop_interop->CreateDesktopWindowTarget (hwnd,
      TRUE, &desktop_target);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositionTarget> target;
  hr = desktop_target.As (&target);
  g_assert (SUCCEEDED (hr));

  /* Creates container visual and put background static color visual */
  ComPtr<IContainerVisual> root;
  hr = compositor->CreateContainerVisual (&root);
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisual> root_visual;
  hr = root.As (&root_visual);
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisual2> root_visual2;
  hr = root.As (&root_visual2);
  g_assert (SUCCEEDED (hr));

  Numerics::Vector2 vec2 = { 1.0, 1.0 };
  hr = root_visual2->put_RelativeSizeAdjustment (vec2);
  g_assert (SUCCEEDED (hr));

  hr = target->put_Root (root_visual.Get ());
  g_assert (SUCCEEDED (hr));

  ABI::Windows::UI::Color bg_color = { };
  bg_color.R = 128;
  bg_color.G = 128;
  bg_color.B = 128;
  bg_color.A = 128;

  ComPtr<ICompositionColorBrush> bg_color_brush;
  hr = compositor->CreateColorBrushWithColor (bg_color, &bg_color_brush);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositionBrush> bg_brush;
  hr = bg_color_brush.As (&bg_brush);
  g_assert (SUCCEEDED (hr));

  ComPtr<ISpriteVisual> bg_sprite_visual;
  hr = compositor->CreateSpriteVisual (&bg_sprite_visual);
  g_assert (SUCCEEDED (hr));

  hr = bg_sprite_visual->put_Brush (bg_brush.Get ());
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisual> bg_visual;
  hr = bg_sprite_visual.As (&bg_visual);
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisual2> bg_visual2;
  hr = bg_sprite_visual.As (&bg_visual2);
  g_assert (SUCCEEDED (hr));

  hr = bg_visual2->put_RelativeSizeAdjustment (vec2);
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisualCollection> children;
  hr = root->get_Children (&children);
  g_assert (SUCCEEDED (hr));

  hr = children->InsertAtBottom (bg_visual.Get ());
  g_assert (SUCCEEDED (hr));

  /* Creates swapchain visual */
  ComPtr<ICompositionSurface> swapchain_surface;
  hr = compositor_interop->CreateCompositionSurfaceForSwapChain (swapchain,
      &swapchain_surface);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositionSurfaceBrush> swapchain_surface_brush;
  hr = compositor->CreateSurfaceBrushWithSurface (swapchain_surface.Get (),
      &swapchain_surface_brush);
  g_assert (SUCCEEDED (hr));

  ComPtr<ICompositionBrush> swapchain_brush;
  hr = swapchain_surface_brush.As (&swapchain_brush);
  g_assert (SUCCEEDED (hr));

  /* Place swapchain visual at center */
  hr = swapchain_surface_brush->put_HorizontalAlignmentRatio (0.5);
  g_assert (SUCCEEDED (hr));

  hr = swapchain_surface_brush->put_VerticalAlignmentRatio (0.5);
  g_assert (SUCCEEDED (hr));

  /* Scale swapchain visual with aspect-ratio preserved */
  hr = swapchain_surface_brush->put_Stretch (CompositionStretch_Uniform);
  g_assert (SUCCEEDED (hr));

  ComPtr<ISpriteVisual> swapchain_sprite_visual;
  hr = compositor->CreateSpriteVisual (&swapchain_sprite_visual);
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisual> swapchain_visual;
  hr = swapchain_sprite_visual.As (&swapchain_visual);
  g_assert (SUCCEEDED (hr));

  vec2.X = 0.5;
  vec2.Y = 0.5;
  hr = swapchain_visual->put_AnchorPoint (vec2);
  g_assert (SUCCEEDED (hr));

  ComPtr<IVisual2> swapchain_visual2;
  hr = swapchain_sprite_visual.As (&swapchain_visual2);
  g_assert (SUCCEEDED (hr));

  hr = swapchain_visual2->put_RelativeSizeAdjustment (vec2);
  g_assert (SUCCEEDED (hr));

  Numerics::Vector3 vec3 = { 0.5, 0.5, 0.0 };
  hr = swapchain_visual2->put_RelativeOffsetAdjustment (vec3);
  g_assert (SUCCEEDED (hr));

  hr = swapchain_sprite_visual->put_Brush (swapchain_brush.Get ());
  g_assert (SUCCEEDED (hr));

  hr = children->InsertAtTop (swapchain_visual.Get ());
  g_assert (SUCCEEDED (hr));

  /* Compositor and visual tree are configured, run pipeline */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  auto bus = gst_element_get_bus (pipeline);

  MSG msg = { };
  while (msg.message != WM_QUIT) {
    if (PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage (&msg);
      DispatchMessage (&msg);
    }

    auto gst_msg = gst_bus_pop (bus);
    if (gst_msg) {
      switch (GST_MESSAGE_TYPE (gst_msg)) {
        case GST_MESSAGE_ERROR:
        {
          GError *err;
          gchar *dbg;

          gst_message_parse_error (gst_msg, &err, &dbg);
          gst_printerrln ("ERROR %s", err->message);
          if (dbg != nullptr)
            gst_printerrln ("ERROR debug information: %s", dbg);
          g_clear_error (&err);
          g_free (dbg);
          PostQuitMessage (0);
          break;
        }
        case GST_MESSAGE_EOS:
        {
          gst_println ("Got EOS");
          PostQuitMessage (0);
          break;
        }
        default:
          break;
      }

      gst_message_unref (gst_msg);
    }
  }

  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

int
main (int argc, char ** argv)
{
  gst_init (nullptr, nullptr);

  RoInitialize (RO_INIT_SINGLETHREADED);
  app_main ();
  RoUninitialize ();

  gst_deinit ();

  return 0;
}

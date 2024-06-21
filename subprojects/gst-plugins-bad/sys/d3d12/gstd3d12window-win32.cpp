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

#include "gstd3d12window-win32.h"

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_window_debug);
#define GST_CAT_DEFAULT gst_d3d12_window_debug

#define WS_GST_D3D12 (WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPEDWINDOW)

#define WM_GST_D3D12_FULLSCREEN (WM_USER + 1)
#define WM_GST_D3D12_ATTACH_INTERNAL_WINDOW (WM_USER + 2)
#define WM_GST_D3D12_CREATE_PROXY (WM_USER + 3)
#define WM_GST_D3D12_DESTROY_INTERNAL_WINDOW (WM_USER + 4)
#define WM_GST_D3D12_UPDATE_RENDER_RECT (WM_USER + 5)
#define WM_GST_D3D12_PARENT_SIZE (WM_USER + 6)
#define WM_GST_D3D12_SWAPCHAIN_CREATED (WM_USER + 7)

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif

#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

/* *INDENT-OFF* */
SwapChainProxy::SwapChainProxy (GstD3D12Window * window, SIZE_T id)
{
  g_assert (GST_IS_D3D12_WINDOW (window));
  window_ = (GstD3D12Window *) gst_object_ref (window);
  id_ = id;

  GST_DEBUG_OBJECT (window_, "Creating proxy %" G_GSIZE_FORMAT, id_);
}

SwapChainProxy::~SwapChainProxy ()
{
  GST_DEBUG_OBJECT (window_, "Destroying proxy %" G_GSIZE_FORMAT, id_);

  swapchain_ = nullptr;
  if (window_thread_ && hwnd_ && hwnd_ != parent_hwnd_) {
    if (window_thread_ == g_thread_self ())
      DestroyWindow (hwnd_);
    else
      PostMessageW (hwnd_, WM_GST_D3D12_DESTROY_INTERNAL_WINDOW, 0, 0);
  }

  gst_object_unref (window_);
}

void
SwapChainProxy::set_window_handles (HWND parent_hwnd, HWND child_hwnd)
{
  parent_hwnd_ = parent_hwnd;
  hwnd_ = child_hwnd;
  window_thread_ = g_thread_self ();
}

HWND
SwapChainProxy::get_window_handle ()
{
  return hwnd_;
}

SIZE_T
SwapChainProxy::get_id ()
{
  return id_;
}

GstD3D12Window *
SwapChainProxy::get_window ()
{
  return window_;
}

bool
SwapChainProxy::has_parent ()
{
  return parent_hwnd_ ? true : false;
}

void
SwapChainProxy::on_destroy ()
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  hwnd_ = nullptr;
  swapchain_ = nullptr;
}

void
SwapChainProxy::set_fullscreen_on_alt_enter (bool enable)
{
  fstate_.fullscreen_on_alt_enter = enable;
}

void
SwapChainProxy::toggle_fullscreen (bool enable)
{
  bool send_msg = false;

  {
    std::lock_guard <std::recursive_mutex> lk (lock_);

    /* fullscreen toggle is supported only for internal hwnd */
    if (parent_hwnd_ || !hwnd_)
      return;

    if (window_thread_ == g_thread_self ())
      send_msg = true;
  }

  if (send_msg)
    SendMessageW (hwnd_, WM_GST_D3D12_FULLSCREEN, 0, (LPARAM) enable);
  else
    PostMessageW (hwnd_, WM_GST_D3D12_FULLSCREEN, 0, (LPARAM) enable);
}

void
SwapChainProxy::update_render_rect ()
{
  bool send_msg = false;
  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    if (!hwnd_ || hwnd_ == parent_hwnd_)
      return;

    if (window_thread_ == g_thread_self ())
      send_msg = true;
  }

  if (send_msg)
    SendMessageW (hwnd_, WM_GST_D3D12_UPDATE_RENDER_RECT, 0, 0);
  else
    PostMessageW (hwnd_, WM_GST_D3D12_UPDATE_RENDER_RECT, 0, 0);
}

void
SwapChainProxy::handle_update_render_rect ()
{
  GstVideoRectangle rect;
  gst_d3d12_window_get_render_rect (window_, &rect);

  if (rect.w == -1 && rect.h == -1 && parent_hwnd_) {
    GST_DEBUG_OBJECT (window_, "Back to parent size");

    RECT parent_rect;
    GetClientRect (parent_hwnd_, &parent_rect);
    MoveWindow (hwnd_, parent_rect.left, parent_rect.top,
        parent_rect.right - parent_rect.left,
        parent_rect.bottom - parent_rect.top, FALSE);
  } else if (rect.w > 0 && rect.h > 0) {
    GST_DEBUG_OBJECT (window_, "Applying render rect");
    MoveWindow (hwnd_, rect.x, rect.y, rect.w, rect.h, FALSE);
  }
}

void
SwapChainProxy::handle_fullscreen_change (bool is_fullscreen)
{
  if (is_fullscreen == fstate_.applied_fullscreen)
    return;

  if (is_fullscreen) {
    GST_DEBUG_OBJECT (window_, "Enable fullscreen");
    GetWindowPlacement (hwnd_, &fstate_.restore_placement);

    ShowWindow (hwnd_, SW_SHOW);

    fstate_.restore_style = GetWindowLong (hwnd_, GWL_STYLE);

    SetWindowLongA (hwnd_, GWL_STYLE,
        fstate_.restore_style &
        ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU |
            WS_THICKFRAME | WS_MAXIMIZE));

    HMONITOR monitor = MonitorFromWindow (hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO minfo = { };
    minfo.cbSize = sizeof (minfo);
    if (!GetMonitorInfo (monitor, &minfo)) {
      GST_WARNING_OBJECT (window_, "Couldn't get monitor info");
      return;
    }

    SetWindowPos (hwnd_, HWND_TOP, minfo.rcMonitor.left, minfo.rcMonitor.top,
        minfo.rcMonitor.right - minfo.rcMonitor.left,
        minfo.rcMonitor.bottom - minfo.rcMonitor.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE);
    ShowWindow (hwnd_, SW_MAXIMIZE);
  } else {
    GST_DEBUG_OBJECT (window_, "Back to window mode");

    SetWindowLongW (hwnd_, GWL_STYLE, fstate_.restore_style);
    SetWindowPlacement (hwnd_, &fstate_.restore_placement);
  }

  fstate_.applied_fullscreen = is_fullscreen;
}

void
SwapChainProxy::handle_syskey_down ()
{
  if (!fstate_.fullscreen_on_alt_enter)
    return;

  WORD state = GetKeyState (VK_RETURN);
  BYTE high = HIBYTE (state);
  if (high & 0x1) {
    LPARAM param = 1;
    if (fstate_.applied_fullscreen)
      param = 0;

    SendMessageW (hwnd_, WM_GST_D3D12_FULLSCREEN, 0, param);
  }
}

void
SwapChainProxy::handle_key_event (UINT msg, WPARAM wparam, LPARAM lparam)
{
  if (!gst_d3d12_window_get_navigation_events_enabled (window_))
    return;

  gunichar2 keyname[128];
  const gchar *event;

  if (!GetKeyNameTextW (lparam, (LPWSTR) keyname, 128))
    return;

  gchar *name = g_utf16_to_utf8 (keyname, 128, nullptr, nullptr, nullptr);
  if (!name)
    return;

  if (msg == WM_KEYDOWN)
    event = "key-press";
  else
    event = "key-release";

  gst_d3d12_window_on_key_event (window_, event, name);
  g_free (name);
}

void
SwapChainProxy::handle_mouse_event (UINT msg, WPARAM wparam, LPARAM lparam)
{
  if (!gst_d3d12_window_get_navigation_events_enabled (window_))
    return;

  gint button = 0;
  const gchar *event = nullptr;
  guint modifier = 0;

  auto xpos = GET_X_LPARAM (lparam);
  auto ypos = GET_Y_LPARAM (lparam);

  if (parent_hwnd_ && parent_hwnd_ != hwnd_) {
    POINT updated_pos;
    updated_pos.x = xpos;
    updated_pos.y = ypos;

    if (!ClientToScreen (parent_hwnd_, &updated_pos)) {
      GST_WARNING_OBJECT (window_, "Couldn't convert parent position to screen");
      return;
    }

    if (!ScreenToClient (hwnd_, &updated_pos)) {
      GST_WARNING_OBJECT (window_, "Couldn't convert screen position to client");
      return;
    }

    xpos = updated_pos.x;
    ypos = updated_pos.y;
  }

  switch (msg) {
    case WM_MOUSEMOVE:
      button = 0;
      event = "mouse-move";
      break;
    case WM_LBUTTONDOWN:
      button = 1;
      event = "mouse-button-press";
      break;
    case WM_LBUTTONUP:
      button = 1;
      event = "mouse-button-release";
      break;
    case WM_LBUTTONDBLCLK:
      button = 1;
      event = "mouse-double-click";
      break;
    case WM_RBUTTONDOWN:
      button = 2;
      event = "mouse-button-press";
      break;
    case WM_RBUTTONUP:
      button = 2;
      event = "mouse-button-release";
      break;
    case WM_RBUTTONDBLCLK:
      button = 2;
      event = "mouse-double-click";
      break;
    case WM_MBUTTONDOWN:
      button = 3;
      event = "mouse-button-press";
      break;
    case WM_MBUTTONUP:
      button = 3;
      event = "mouse-button-release";
      break;
    case WM_MBUTTONDBLCLK:
      button = 3;
      event = "mouse-double-click";
      break;
    default:
      return;
  }

  if ((wparam & MK_CONTROL) != 0)
    modifier |= GST_NAVIGATION_MODIFIER_CONTROL_MASK;
  if ((wparam & MK_LBUTTON) != 0)
    modifier |= GST_NAVIGATION_MODIFIER_BUTTON1_MASK;
  if ((wparam & MK_RBUTTON) != 0)
    modifier |= GST_NAVIGATION_MODIFIER_BUTTON2_MASK;
  if ((wparam & MK_MBUTTON) != 0)
    modifier |= GST_NAVIGATION_MODIFIER_BUTTON3_MASK;
  if ((wparam & MK_SHIFT) != 0)
    modifier |= GST_NAVIGATION_MODIFIER_SHIFT_MASK;

  GstVideoRectangle output_rect = { };
  GstVideoOrientationMethod orientation;
  gint in_w, in_h;
  gst_d3d12_window_get_mouse_pos_info (window_, &output_rect,
      in_w, in_h, orientation);

  if (in_w <= 0 || in_h <= 0 || xpos < output_rect.x ||
      xpos >= output_rect.x + output_rect.w || ypos < output_rect.y ||
      ypos >= output_rect.y + output_rect.h) {
    return;
  }

  gint src_w, src_h;
  switch (orientation) {
    case GST_VIDEO_ORIENTATION_90R:
    case GST_VIDEO_ORIENTATION_90L:
    case GST_VIDEO_ORIENTATION_UL_LR:
    case GST_VIDEO_ORIENTATION_UR_LL:
      src_w = in_h;
      src_h = in_w;
      break;
    default:
      src_w = in_w;
      src_h = in_h;
      break;
  }

  xpos = ((xpos - output_rect.x) / (double) output_rect.w) * src_w;
  ypos = ((ypos - output_rect.y) / (double) output_rect.h) * src_h;

  xpos = CLAMP (xpos, 0, (LONG) (src_w - 1));
  ypos = CLAMP (ypos, 0, (LONG) (src_h - 1));

  double final_x = 0;
  double final_y = 0;

  switch (orientation) {
    case GST_VIDEO_ORIENTATION_90R:
      final_x = ypos;
      final_y = src_w - xpos;
      break;
    case GST_VIDEO_ORIENTATION_90L:
      final_x = src_h - ypos;
      final_y = xpos;
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      final_x = src_h - ypos;
      final_y = src_w - xpos;
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      final_x = ypos;
      final_y = xpos;
      break;
    case GST_VIDEO_ORIENTATION_180:
      final_x = src_w - xpos;
      final_y = src_h - ypos;
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      final_x = src_w - xpos;
      final_y = ypos;
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      final_x = xpos;
      final_y = src_h - ypos;
      break;
    default:
      final_x = xpos;
      final_y = ypos;
      break;
  }

  gst_d3d12_window_on_mouse_event (window_,
      event, button, final_x, final_y, modifier);
}

GstFlowReturn
SwapChainProxy::setup_swapchain (GstD3D12Device * device,
    DXGI_FORMAT format, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * conv_config)
{
  std::shared_ptr<SwapChain> sc;
  HWND hwnd = nullptr;
  bool is_new_swapchain = false;
  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    if (!hwnd_) {
      GST_WARNING_OBJECT (window_, "Window was closed");
      return GST_D3D12_WINDOW_FLOW_CLOSED;
    }

    if (!swapchain_)
      swapchain_ = std::make_shared<SwapChain> (device);

    sc = swapchain_;
    hwnd = hwnd_;
  }

  auto ret = sc->setup_swapchain (window_, device,
      hwnd, format, in_info, out_info, conv_config, is_new_swapchain);
  if (ret != GST_FLOW_OK)
    return ret;

  if (is_new_swapchain)
    PostMessageW (hwnd, WM_GST_D3D12_SWAPCHAIN_CREATED, 0, 0);

  return ret;
}

std::shared_ptr<SwapChain>
SwapChainProxy::get_swapchain ()
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!hwnd_) {
    GST_DEBUG_OBJECT (window_, "Window handle is not configured");
    return nullptr;
  }

  if (!swapchain_) {
    GST_DEBUG_OBJECT (window_, "Swapchain is not configured");
    return nullptr;
  }

  return swapchain_;
}

void
SwapChainProxy::handle_swapchain_created ()
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  if (!hwnd_ || !swapchain_)
    return;

  swapchain_->disable_alt_enter (hwnd_);
}

void
SwapChainProxy::handle_position_changed (INT width, INT height)
{
  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    if (!hwnd_ || !swapchain_)
      return;

    if (width != width_ || height != height_) {
      width_ = width;
      height_ = height;
    } else {
      return;
    }
  }

  auto sc = get_swapchain ();
  if (sc)
    sc->resize_buffer (window_);
}

void
SwapChainProxy::release_swapchin ()
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  swapchain_ = nullptr;
}

GstFlowReturn
SwapChainProxy::resize_buffer (INT width, INT height)
{
  auto sc = get_swapchain ();
  if (!sc)
    return GST_FLOW_OK;

  if (width > 0 && height > 0) {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    width_ = width;
    height_ = height;
  }

  return sc->resize_buffer (window_);
}

GstFlowReturn
SwapChainProxy::set_buffer (GstBuffer * buffer)
{
  auto sc = get_swapchain ();
  if (!sc)
    return GST_D3D12_WINDOW_FLOW_CLOSED;

  return sc->set_buffer (window_, buffer);
}

GstFlowReturn
SwapChainProxy::present ()
{
  auto sc = get_swapchain ();
  if (!sc)
    return GST_D3D12_WINDOW_FLOW_CLOSED;

  return sc->present ();
}

void
SwapChainProxy::expose ()
{
  auto sc = get_swapchain ();
  if (sc)
    sc->expose (window_);
}

void
HwndServer::register_window (GstD3D12Window * window)
{
  std::lock_guard<std::recursive_mutex> lk (lock_);
  GST_DEBUG_OBJECT (window, "Register");
  state_.insert ({window, std::make_shared<State> ()});
}

void
HwndServer::unregister_window (GstD3D12Window * window)
{
  std::lock_guard<std::recursive_mutex> lk (lock_);
  GST_DEBUG_OBJECT (window, "Unregister");
  state_.erase (window);
}

void
HwndServer::unlock_window (GstD3D12Window * window)
{
  std::lock_guard<std::recursive_mutex> lk (lock_);
  auto it = state_.find (window);

  if (it != state_.end ()) {
    std::lock_guard<std::mutex> lk (it->second->create_lock);
    it->second->flushing = true;
    it->second->create_cond.notify_all ();
  }
}

void
HwndServer::unlock_stop_window (GstD3D12Window * window)
{
  std::lock_guard<std::recursive_mutex> lk (lock_);
  auto it = state_.find (window);

  if (it != state_.end ()) {
    std::lock_guard<std::mutex> lk (it->second->create_lock);
    it->second->flushing = false;
    it->second->create_cond.notify_all ();
  }
}

#define EXTERNAL_PROC_PROP_NAME L"gst-d3d12-hwnd-external-proc"
#define D3D12_WINDOW_PROP_NAME L"gst-d3d12-hwnd-obj"
#define D3D12_WINDOW_ID_PROP_NAME L"gst-d3d12-hwnd-obj-id"

static LRESULT CALLBACK
parent_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  auto external_window_proc =
      (WNDPROC) GetPropW (hwnd, EXTERNAL_PROC_PROP_NAME);

  if (!external_window_proc) {
    GST_WARNING ("null external proc");
    return DefWindowProcW (hwnd, msg, wparam, lparam);
  }

  auto server = HwndServer::get_instance ();
  if (msg == WM_GST_D3D12_ATTACH_INTERNAL_WINDOW) {
    GST_DEBUG ("Attach internal window");
    server->create_child_hwnd_finish ((GstD3D12Window *) lparam, hwnd,
        (SIZE_T) wparam);
    return 0;
  } else if (msg == WM_GST_D3D12_CREATE_PROXY) {
    server->create_proxy_finish ((GstD3D12Window *) lparam, hwnd,
        (SIZE_T) wparam);
    return 0;
  }

  server->forward_parent_message (hwnd, msg, wparam, lparam);

  auto direct_proxy = server->get_direct_proxy (hwnd);
  switch (msg) {
    case WM_SIZE:
    {
      auto dproxy = server->get_direct_proxy (hwnd);
      if (dproxy)
        dproxy->resize_buffer (LOWORD (lparam), HIWORD (lparam));
      break;
    }
    case WM_WINDOWPOSCHANGED:
    {
      WINDOWPOS *pos = (WINDOWPOS *) lparam;
      if ((pos->flags & SWP_HIDEWINDOW) == 0) {
        INT width = pos->cx;
        INT height = pos->cy;
        if ((pos->flags & SWP_NOSIZE) != 0) {
          RECT rect = { };
          GetClientRect (hwnd, &rect);
          width = rect.right - rect.left;
          height = rect.bottom - rect.top;
        }
        auto dproxy = server->get_direct_proxy (hwnd);
        if (dproxy)
          dproxy->handle_position_changed (width, height);
      }
      break;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
      auto dproxy = server->get_direct_proxy (hwnd);
      if (dproxy)
        dproxy->handle_key_event (msg, wparam, lparam);
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    {
      auto proxy = server->get_direct_proxy (hwnd);
      if (proxy)
        proxy->handle_mouse_event (msg, wparam, lparam);
      break;
    }
    default:
      break;
  }

  if (msg == WM_DESTROY) {
    GST_INFO ("Parent HWND %p is being destroyed", hwnd);
    server->on_parent_destroy (hwnd);
  }

  return CallWindowProcW (external_window_proc, hwnd, msg, wparam, lparam);
}

struct WindowCreateParams
{
  GstD3D12Window *window;
  SIZE_T id;
};

static LRESULT CALLBACK
internal_wnd_proc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  auto server = HwndServer::get_instance ();

  if (msg == WM_NCCREATE) {
    LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW) lparam;
    auto params = (WindowCreateParams *) lpcs->lpCreateParams;
    SetPropW (hwnd, D3D12_WINDOW_PROP_NAME, (HANDLE) params->window);
    SetPropW (hwnd, D3D12_WINDOW_ID_PROP_NAME, (HANDLE) params->id);

    gst_object_ref (params->window);

    return DefWindowProcW (hwnd, msg, wparam, lparam);
  } else if (msg == WM_GST_D3D12_DESTROY_INTERNAL_WINDOW) {
    GST_INFO ("%p, Got custom destroy window event", hwnd);
    DestroyWindow (hwnd);
    return 0;
  }

  auto window = (GstD3D12Window *) GetPropW (hwnd, D3D12_WINDOW_PROP_NAME);
  auto id = (SIZE_T) GetPropW (hwnd, D3D12_WINDOW_ID_PROP_NAME);

  if (!window)
    return DefWindowProcW (hwnd, msg, wparam, lparam);

  /* Custom event handler */
  if (msg == WM_GST_D3D12_PARENT_SIZE) {
    auto proxy = server->get_proxy (window, id);
    if (!proxy)
      return 0;

    WORD width, height;

    width = LOWORD (lparam);
    height = HIWORD (lparam);

    GST_LOG_OBJECT (window, "Parent resize %dx%d", width, height);

    GstVideoRectangle rect;
    gst_d3d12_window_get_render_rect (window, &rect);
    if (rect.w > 0 && rect.h > 0)
      MoveWindow (hwnd, rect.x, rect.y, rect.w, rect.h, FALSE);
    else
      MoveWindow (hwnd, 0, 0, width, height, FALSE);

    return 0;
  } else if (msg == WM_GST_D3D12_UPDATE_RENDER_RECT) {
    auto proxy = server->get_proxy (window, id);
    if (!proxy)
      return 0;

    proxy->handle_update_render_rect ();
    return 0;
  } else if (msg == WM_GST_D3D12_FULLSCREEN) {
    auto proxy = server->get_proxy (window, id);
    if (!proxy)
      return 0;

    proxy->handle_fullscreen_change (lparam ? true : false);
    return 0;
  } else if (msg == WM_GST_D3D12_SWAPCHAIN_CREATED) {
    auto proxy = server->get_proxy (window, id);
    if (!proxy)
      return 0;

    proxy->handle_swapchain_created ();
    return 0;
  }

  switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    {
      auto proxy = server->get_proxy (window, id);
      if (proxy)
        proxy->handle_key_event (msg, wparam, lparam);
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    {
      auto proxy = server->get_proxy (window, id);
      if (proxy)
        proxy->handle_mouse_event (msg, wparam, lparam);
      break;
    }
    case WM_NCHITTEST:
    {
      auto proxy = server->get_proxy (window, id);
      if (proxy && proxy->has_parent ()) {
        /* To passthrough mouse event if external window is used.
         * Only hit-test succeeded window can receive/handle some mouse events
         * and we want such events to be handled by parent (application) window
         */
        return (LRESULT) HTTRANSPARENT;
      }
      break;
    }
    case WM_SIZE:
    {
      auto proxy = server->get_proxy (window, id);
      if (proxy)
        proxy->resize_buffer (0, 0);
      break;
    }
    case WM_SYSKEYDOWN:
    {
      auto proxy = server->get_proxy (window, id);
      if (proxy)
        proxy->handle_syskey_down ();
      break;
    }
    case WM_DESTROY:
    {
      GST_DEBUG ("%p, WM_DESTROY", hwnd);
      RemovePropW (hwnd, D3D12_WINDOW_PROP_NAME);
      RemovePropW (hwnd, D3D12_WINDOW_ID_PROP_NAME);

      auto proxy = server->get_proxy (window, id);
      if (proxy) {
        proxy->on_destroy ();
        server->on_proxy_destroy (window, id);
      }

      gst_object_unref (window);
      break;
    }
    default:
      break;
  }

  return DefWindowProcW (hwnd, msg, wparam, lparam);
}

static void
register_window_class ()
{
  GST_D3D12_CALL_ONCE_BEGIN {
    auto inst = GetModuleHandle (nullptr);
    WNDCLASSEXW wc = { };

    wc.cbSize = sizeof (WNDCLASSEXW);
    wc.lpfnWndProc = internal_wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIcon (nullptr, IDI_WINLOGO);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.hCursor = LoadCursor (nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
    wc.lpszClassName = L"GstD3D12Hwnd";

    RegisterClassExW (&wc);
  } GST_D3D12_CALL_ONCE_END;
}

GstFlowReturn
HwndServer::create_child_hwnd (GstD3D12Window * window, HWND parent_hwnd,
    gboolean direct_swapchain, SIZE_T & proxy_id)
{
  proxy_id = 0;
  if (!IsWindow (parent_hwnd)) {
    GST_WARNING_OBJECT (window, "%p is not window handle", parent_hwnd);
    return GST_D3D12_WINDOW_FLOW_CLOSED;
  }

  std::shared_ptr<State> state;
  {
    std::lock_guard<std::recursive_mutex> lk (lock_);
    auto external_proc = (WNDPROC)
          GetWindowLongPtrW (parent_hwnd, GWLP_WNDPROC);
    if (external_proc != (WNDPROC) parent_wnd_proc) {
      if (!SetPropW (parent_hwnd, EXTERNAL_PROC_PROP_NAME,
          (HANDLE) external_proc)) {
        GST_WARNING_OBJECT (window,
            "Couldn't store original procedure function");
        return GST_D3D12_WINDOW_FLOW_CLOSED;
      }

      SetWindowLongPtrW (parent_hwnd, GWLP_WNDPROC,
          (LONG_PTR) parent_wnd_proc);

      GST_DEBUG_OBJECT (window,
          "subclass proc installed for hwnd %p", parent_hwnd);
    }

    /* Cannot attach multiple swapchain to a single HWND.
     * release swapchain if needed */
    if (direct_swapchain) {
      for (auto it : state_) {
        auto state = it.second;
        if (state) {
          auto proxy = state->proxy;
          if (proxy && proxy->get_window_handle () == parent_hwnd) {
            proxy->release_swapchin ();
            std::unique_lock<std::mutex> lk (state->create_lock);
            state->proxy = nullptr;
          }
        }
      }

      auto it = direct_proxy_map_.find (parent_hwnd);
      if (it != direct_proxy_map_.end ()) {
        auto proxy = it->second.lock ();
        if (proxy)
          proxy->release_swapchin ();
      }

      direct_proxy_map_.erase (parent_hwnd);
    }

    auto it = state_.find (window);
    state = it->second;
  }

  std::unique_lock<std::mutex> lk (state->create_lock);
  state->id++;
  if (state->id == 0)
    state->id++;

  SIZE_T id = state->id;
  state->proxy = std::make_shared<SwapChainProxy> (window, id);

  if (state->flushing) {
    GST_INFO_OBJECT (window, "Window is flushing");
    state->proxy = nullptr;
    return GST_FLOW_FLUSHING;
  }

  state->create_state = CreateState::Waiting;
  if (!PostMessageW (parent_hwnd, direct_swapchain ?
      WM_GST_D3D12_CREATE_PROXY: WM_GST_D3D12_ATTACH_INTERNAL_WINDOW,
      (WPARAM) id, (LPARAM) window)) {
    GST_WARNING_OBJECT (window, "Couldn't post message");
    state->create_state = CreateState::None;
    state->proxy = nullptr;
    return GST_D3D12_WINDOW_FLOW_CLOSED;
  }

  while (!state->flushing && state->create_state == CreateState::Waiting)
    state->create_cond.wait(lk);

  GstFlowReturn ret = GST_D3D12_WINDOW_FLOW_CLOSED;
  if (state->create_state == CreateState::Opened) {
    ret = GST_FLOW_OK;
    proxy_id = id;
  } else {
    state->proxy = nullptr;
    if (state->flushing)
      ret = GST_FLOW_FLUSHING;
  }

  state->create_state = CreateState::None;

  return ret;
}

void
HwndServer::create_child_hwnd_finish (GstD3D12Window * window,
    HWND parent_hwnd, SIZE_T proxy_id)
{
  std::shared_ptr<State> state;
  std::shared_ptr<SwapChainProxy> proxy;

  {
    std::lock_guard<std::recursive_mutex> lk (lock_);
    auto it = state_.find (window);
    if (it == state_.end ()) {
      GST_WARNING ("Window is not registered");
      return;
    }

    state = it->second;
    proxy = state->proxy;
  }

  if (!proxy) {
    GST_INFO ("Proxy was released");
    return;
  }

  if (proxy->get_id () != proxy_id) {
    GST_INFO ("Different proxy id");
    return;
  }

  register_window_class ();

  WindowCreateParams params;
  params.window = window;
  params.id = proxy_id;

  auto child = CreateWindowExW (0,
      L"GstD3D12Hwnd", L"GstD3D12Hwnd",
      WS_GST_D3D12, 0, 0, 0, 0, (HWND) nullptr, (HMENU) nullptr,
      GetModuleHandle (nullptr), &params);
  SetWindowLongPtrW (child, GWL_STYLE, WS_CHILD | WS_MAXIMIZE);
  SetParent (child, parent_hwnd);

  RECT rect;
  GetClientRect (parent_hwnd, &rect);

  GstVideoRectangle user_rect = { };
  gst_d3d12_window_get_render_rect (window, &user_rect);

  if (user_rect.w > 0 && user_rect.h > 0) {
    rect.left = user_rect.x;
    rect.top = user_rect.y;
    rect.right = user_rect.x + user_rect.w;
    rect.bottom = user_rect.y + user_rect.h;
  }

  SetWindowPos (child, HWND_TOP, rect.left, rect.top,
      rect.right - rect.left, rect.bottom - rect.top,
      SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
      SWP_FRAMECHANGED | SWP_NOACTIVATE);
  MoveWindow (child, rect.left, rect.top,
      rect.right - rect.left, rect.bottom - rect.top, FALSE);
  ShowWindow (child, SW_SHOW);

  {
    std::lock_guard<std::recursive_mutex> lk (lock_);
    auto it = parent_hwnd_map_.find (parent_hwnd);
    if (it == parent_hwnd_map_.end ()) {
      GST_DEBUG ("Register parent hwnd %p with child %p", parent_hwnd, child);
      std::vector<HWND> hwnd_list;
      hwnd_list.push_back (child);
      parent_hwnd_map_.insert ({parent_hwnd, hwnd_list});
    } else {
      it->second.push_back (child);
      GST_DEBUG ("New child hwnd %p is added for parent %p, num child %" G_GSIZE_FORMAT,
          parent_hwnd, child, it->second.size ());
    }
  }

  {
    std::lock_guard <std::mutex> lk (state->create_lock);
    proxy->set_window_handles (parent_hwnd, child);
    state->create_state = CreateState::Opened;
    state->create_cond.notify_all ();
  }
}

void
HwndServer::create_proxy_finish (GstD3D12Window * window,
    HWND parent_hwnd, SIZE_T proxy_id)
{
  std::shared_ptr<State> state;
  std::shared_ptr<SwapChainProxy> proxy;

  std::lock_guard<std::recursive_mutex> lk (lock_);
  auto it = state_.find (window);
  if (it == state_.end ()) {
    GST_WARNING ("Window is not registered");
    return;
  }

  state = it->second;
  proxy = state->proxy;

  if (!proxy) {
    GST_INFO ("Proxy was released");
    return;
  }

  if (proxy->get_id () != proxy_id) {
    GST_INFO ("Different proxy id");
    return;
  }

  direct_proxy_map_.insert ({parent_hwnd, proxy});

  {
    std::lock_guard <std::mutex> lk (state->create_lock);
    proxy->set_window_handles (parent_hwnd, parent_hwnd);
    state->create_state = CreateState::Opened;
    state->create_cond.notify_all ();
  }
}

SIZE_T
HwndServer::create_internal_window (GstD3D12Window * window)
{
  std::wstring title;
  GstVideoRectangle rect;
  GstVideoOrientationMethod orientation;
  int x = CW_USEDEFAULT;
  int y = CW_USEDEFAULT;
  int w, h;

  gst_d3d12_window_get_create_params (window, title, &rect, w, h, orientation);

  std::shared_ptr<State> state;
  {
    std::lock_guard<std::recursive_mutex> lk (lock_);
    auto it = state_.find (window);
    state = it->second;
  }

  {
    std::unique_lock<std::mutex> lk (state->create_lock);
    state->id++;
    if (state->id == 0)
      state->id++;
  }

  SIZE_T id = state->id;
  auto proxy = std::make_shared<SwapChainProxy> (window, id);

  DWORD style = WS_GST_D3D12 | WS_VISIBLE;
  if (rect.w > 0 && rect.h > 0) {
    x = rect.x;
    y = rect.y;
    w = rect.w;
    h = rect.h;
  } else {
    RECT rect = { };
    switch (orientation) {
      case GST_VIDEO_ORIENTATION_90R:
      case GST_VIDEO_ORIENTATION_90L:
      case GST_VIDEO_ORIENTATION_UL_LR:
      case GST_VIDEO_ORIENTATION_UR_LL:
        rect.right = h;
        rect.bottom = w;
        break;
      default:
        rect.right = w;
        rect.bottom = h;
        break;
    }

    AdjustWindowRect (&rect, WS_GST_D3D12, FALSE);

    w = rect.right - rect.left;
    h = rect.bottom - rect.top;
  }

  register_window_class ();

  WindowCreateParams params;
  params.window = window;
  params.id = id;
  auto hwnd = CreateWindowExW (0, L"GstD3D12Hwnd", title.c_str (),
      style, x, y, w, h, (HWND) nullptr, (HMENU) nullptr,
      GetModuleHandle (nullptr), &params);
  proxy->set_window_handles (nullptr, hwnd);

  {
    std::lock_guard <std::mutex> slk (state->create_lock);
    state->proxy = std::move (proxy);
  }

  return id;
}

void
HwndServer::release_proxy (GstD3D12Window * window, SIZE_T proxy_id)
{
  std::shared_ptr<SwapChainProxy> proxy;

  {
    std::lock_guard<std::recursive_mutex> lk (lock_);
    auto it = state_.find (window);
    if (it == state_.end ())
      return;

    auto state = it->second;
    {
      std::lock_guard <std::mutex> slk (state->create_lock);
      if (state->proxy && state->proxy->get_id () == proxy_id) {
        proxy = state->proxy;
        state->proxy = nullptr;
      }
    }

    auto dit = direct_proxy_map_.begin ();
    while (dit != direct_proxy_map_.end ()) {
      auto proxy = dit->second.lock ();
      if (!proxy || proxy->get_window () == window)
        dit = direct_proxy_map_.erase (dit);
      else
        dit++;
    }
  }
}

static bool
translate_message (UINT & msg, WPARAM & wparam, LPARAM & lparam)
{
  switch (msg) {
    case WM_SIZE:
      msg = WM_GST_D3D12_PARENT_SIZE;
      return true;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
      return true;
    default:
      break;
  }

  return false;
}

void
HwndServer::forward_parent_message (HWND parent, UINT msg, WPARAM wparam, LPARAM lparam)
{
  if (translate_message (msg, wparam, lparam)) {
    std::vector<HWND> child_hwnds;

    {
      std::lock_guard <std::recursive_mutex> lk (lock_);
      auto it = parent_hwnd_map_.find (parent);
      if (it == parent_hwnd_map_.end ())
        return;

      child_hwnds = it->second;
    }

    for (auto child : child_hwnds)
      SendMessageW (child, msg, wparam, lparam);
  }
}

void
HwndServer::on_parent_destroy (HWND parent_hwnd)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  parent_hwnd_map_.erase (parent_hwnd);
  direct_proxy_map_.erase (parent_hwnd);
  for (auto it : state_) {
    auto state = it.second;
    if (state) {
      auto proxy = state->proxy;
      if (proxy && proxy->get_window_handle () == parent_hwnd) {
        proxy->release_swapchin ();
        std::unique_lock<std::mutex> lk (state->create_lock);
        state->proxy = nullptr;
      }
    }
  }
}

void
HwndServer::on_proxy_destroy (GstD3D12Window * window,
    SIZE_T proxy_id)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  auto it = state_.find (window);
  if (it != state_.end ()) {
    auto state = it->second;
    std::lock_guard <std::mutex> slk (state->create_lock);
    if (state->proxy && state->proxy->get_id () == proxy_id) {
      state->proxy = nullptr;
    }
  }
}

std::shared_ptr<SwapChainProxy>
HwndServer::get_proxy (GstD3D12Window * window, SIZE_T proxy_id)
{
  std::shared_ptr<SwapChainProxy> ret;

  {
    std::lock_guard <std::recursive_mutex> lk (lock_);
    auto it = state_.find (window);
    if (it == state_.end ())
      return nullptr;

    {
      std::lock_guard <std::mutex> slk (it->second->create_lock);
      ret = it->second->proxy;
      if (ret && ret->get_id () != proxy_id)
        ret = nullptr;
    }
  }

  return ret;
}

std::shared_ptr<SwapChainProxy>
HwndServer::get_direct_proxy (HWND parent_hwnd)
{
  std::lock_guard <std::recursive_mutex> lk (lock_);
  auto it = direct_proxy_map_.find (parent_hwnd);
  if (it == direct_proxy_map_.end ())
    return nullptr;

  return it->second.lock ();
}

/* *INDENT-ON* */

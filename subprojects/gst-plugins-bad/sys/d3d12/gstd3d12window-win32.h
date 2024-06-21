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

#include "gstd3d12window.h"
#include "gstd3d12window-swapchain.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include <memory>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <wrl.h>

struct FullscreenState
{
  std::atomic<bool> fullscreen_on_alt_enter = { false };
  std::atomic<bool> requested_fullscreen = { false };
  std::atomic<bool> applied_fullscreen = { false };
  LONG restore_style = 0;
  WINDOWPLACEMENT restore_placement = { };
};

class SwapChainProxy
{
public:
  SwapChainProxy (GstD3D12Window * window, SIZE_T id);
  ~SwapChainProxy ();

  void set_window_handles (HWND parent_hwnd, HWND child_hwnd);
  HWND get_window_handle ();
  SIZE_T get_id ();
  GstD3D12Window *get_window ();
  bool has_parent ();
  void on_destroy ();
  void set_fullscreen_on_alt_enter (bool enable);
  void toggle_fullscreen (bool enable);
  void update_render_rect ();
  void handle_fullscreen_change (bool is_fullscreen);
  void handle_syskey_down ();
  void handle_update_render_rect ();
  void handle_key_event (UINT msg, WPARAM wparam, LPARAM lparam);
  void handle_mouse_event (UINT msg, WPARAM wparam, LPARAM lparam);
  void handle_swapchain_created ();
  void handle_position_changed (INT width, INT height);
  void release_swapchin ();
  GstFlowReturn setup_swapchain (GstD3D12Device * device, DXGI_FORMAT format,
      const GstVideoInfo * in_info, const GstVideoInfo * out_info,
      GstStructure * conv_config);
  GstFlowReturn resize_buffer (INT width, INT height);
  GstFlowReturn set_buffer (GstBuffer * buffer);
  GstFlowReturn present ();
  void expose ();

private:
  std::shared_ptr<SwapChain> get_swapchain ();

private:
  GstD3D12Window *window_ = nullptr;
  SIZE_T id_ = 0;
  HWND hwnd_ = nullptr;
  HWND parent_hwnd_ = nullptr;
  GThread *window_thread_ = nullptr;
  FullscreenState fstate_;
  std::shared_ptr<SwapChain> swapchain_;
  INT width_ = 0;
  INT height_ = 0;

  std::recursive_mutex lock_;
};

class HwndServer
{
public:
  HwndServer(const HwndServer &) = delete;
  HwndServer& operator=(const HwndServer &) = delete;
  static HwndServer * get_instance()
  {
    static HwndServer *inst = nullptr;
    GST_D3D12_CALL_ONCE_BEGIN {
      inst = new HwndServer ();
    } GST_D3D12_CALL_ONCE_END;

    return inst;
  }

  void register_window (GstD3D12Window * window);
  void unregister_window (GstD3D12Window * window);

  void unlock_window (GstD3D12Window * window);
  void unlock_stop_window (GstD3D12Window * window);

  GstFlowReturn create_child_hwnd (GstD3D12Window * window,
      HWND parent_hwnd, gboolean direct_swapchain, SIZE_T & proxy_id);

  void create_child_hwnd_finish (GstD3D12Window * window,
      HWND parent_hwnd, SIZE_T proxy_id);
  void create_proxy_finish (GstD3D12Window * window,
      HWND parent_hwnd, SIZE_T proxy_id);

  SIZE_T create_internal_window (GstD3D12Window * window);

  void release_proxy (GstD3D12Window * window, SIZE_T proxy_id);

  void forward_parent_message (HWND parent,
      UINT msg, WPARAM wparam, LPARAM lparam);

  void on_parent_destroy (HWND parent_hwnd);
  void on_proxy_destroy (GstD3D12Window * window,
      SIZE_T proxy_id);

  std::shared_ptr<SwapChainProxy> get_proxy (GstD3D12Window * window,
      SIZE_T proxy_id);
  std::shared_ptr<SwapChainProxy> get_direct_proxy (HWND parent_hwnd);

private:
  enum CreateState
  {
    None,
    Waiting,
    Opened,
    Closed,
  };

  struct State
  {
    std::mutex create_lock;
    std::condition_variable create_cond;
    std::atomic<SIZE_T> id = { 0 };
    bool flushing = false;
    CreateState create_state = CreateState::None;
    std::shared_ptr<SwapChainProxy> proxy;
  };

  HwndServer () {}
  ~HwndServer () {}
  std::recursive_mutex lock_;
  std::unordered_map<GstD3D12Window *, std::shared_ptr<State>> state_;
  std::unordered_map<HWND, std::vector<HWND>> parent_hwnd_map_;
  std::unordered_map<HWND, std::weak_ptr<SwapChainProxy>> direct_proxy_map_;
};


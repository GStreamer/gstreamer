/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12window.h"
#include "gstd3d12window-win32.h"
#include <directx/d3dx12.h>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>
#include <atomic>
#include <wrl.h>
#include <string>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY (gst_d3d12_window_debug);
#define GST_CAT_DEFAULT gst_d3d12_window_debug

enum
{
  SIGNAL_KEY_EVENT,
  SIGNAL_MOUSE_EVENT,
  SIGNAL_FULLSCREEN,
  SIGNAL_OVERLAY,
  SIGNAL_LAST
};

static guint d3d12_window_signals[SIGNAL_LAST] = { 0, };

GType
gst_d3d12_window_overlay_mode_get_type (void)
{
  static GType mode_type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GFlagsValue mode_types[] = {
      {GST_D3D12_WINDOW_OVERLAY_NONE, "None", "none"},
      {GST_D3D12_WINDOW_OVERLAY_D3D12,
          "Emits present signal with Direct3D12 resources", "d3d12"},
      {GST_D3D12_WINDOW_OVERLAY_D3D11,
          "Emits present signal with Direct3D12/11 resources", "d3d11"},
      {GST_D3D12_WINDOW_OVERLAY_D2D,
            "Emit present signal with Direct3D12/11 and Direct2D resources",
          "d2d"},
      {0, nullptr, nullptr},
    };

    mode_type = g_flags_register_static ("GstD3D12WindowOverlayMode",
        mode_types);
  } GST_D3D12_CALL_ONCE_END;

  return mode_type;
}

/* *INDENT-OFF* */

struct GstD3D12WindowPrivate
{
  GstD3D12WindowPrivate ()
  {
    main_context = g_main_context_new ();
    loop = g_main_loop_new (main_context, FALSE);

    render_rect.w = -1;
    render_rect.h = -1;

    gst_video_info_init (&input_info);
    gst_video_info_init (&display_info);

    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12WindowPrivate ()
  {
    g_main_loop_unref (loop);
    g_main_context_unref (main_context);
    gst_clear_object (&fence_data_pool);
  }

  std::recursive_mutex lock;
  DXGI_FORMAT display_format = DXGI_FORMAT_R8G8B8A8_UNORM;

  GstVideoOrientationMethod orientation = GST_VIDEO_ORIENTATION_IDENTITY;
  gfloat fov = 90.0f;
  gboolean ortho = FALSE;
  gfloat rotation_x = 0;
  gfloat rotation_y = 0;
  gfloat rotation_z = 0;
  gfloat scale_x = 1.0f;
  gfloat scale_y = 1.0f;

  /* fullscreen related variables */
  std::atomic<gboolean> fullscreen_on_alt_enter = { FALSE };
  std::atomic<gboolean> requested_fullscreen = { FALSE };

  GstD3D12FenceDataPool *fence_data_pool;

  /* User specified rect */
  GstVideoRectangle render_rect = { };

  GstVideoRectangle output_rect = { };
  RECT dirty_rect = { };
  GstVideoInfo input_info;
  GstVideoInfo display_info;
  guint display_width = 8;
  guint display_height = 8;

  std::atomic<gboolean> enable_navigation = { TRUE };
  gboolean force_aspect_ratio = TRUE;
  gboolean output_updated = FALSE;

  std::weak_ptr<SwapChainProxy> proxy;
  SIZE_T proxy_id = 0;

  std::wstring title;

  std::atomic<GstD3D12MSAAMode> msaa = { GST_D3D12_MSAA_DISABLED };
  std::atomic<GstD3D12WindowOverlayMode> overlay_mode =
      { GST_D3D12_WINDOW_OVERLAY_NONE };

  /* Win32 window handles */
  GThread *main_loop_thread = nullptr;
  GMainLoop *loop = nullptr;
  GMainContext *main_context = nullptr;
  std::mutex loop_lock;
  std::condition_variable loop_cond;
};

struct _GstD3D12Window
{
  GstObject parent;

  GstD3D12Device *device;

  GstD3D12WindowPrivate *priv;
};
/* *INDENT-ON* */

#define gst_d3d12_window_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Window, gst_d3d12_window, GST_TYPE_OBJECT);

static void gst_d3d12_window_finalize (GObject * object);

static void
gst_d3d12_window_class_init (GstD3D12WindowClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_window_finalize;

  d3d12_window_signals[SIGNAL_KEY_EVENT] =
      g_signal_new_class_handler ("key-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, nullptr, nullptr, nullptr, nullptr,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

  d3d12_window_signals[SIGNAL_MOUSE_EVENT] =
      g_signal_new_class_handler ("mouse-event", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, nullptr, nullptr, nullptr, nullptr,
      G_TYPE_NONE, 5, G_TYPE_STRING, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_DOUBLE,
      G_TYPE_UINT);

  d3d12_window_signals[SIGNAL_FULLSCREEN] =
      g_signal_new_class_handler ("fullscreen", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, nullptr, nullptr, nullptr, nullptr,
      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

  d3d12_window_signals[SIGNAL_OVERLAY] =
      g_signal_new_class_handler ("overlay", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, nullptr, nullptr, nullptr, nullptr,
      G_TYPE_NONE, 6, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER,
      G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_POINTER);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_window_debug,
      "d3d12window", 0, "d3d12window");
}

static void
gst_d3d12_window_init (GstD3D12Window * self)
{
  self->priv = new GstD3D12WindowPrivate ();
}

static void
gst_d3d12_window_finalize (GObject * object)
{
  auto self = GST_D3D12_WINDOW (object);

  delete self->priv;

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
gst_d3d12_window_on_key_event (GstD3D12Window * window, const gchar * event,
    const gchar * name)
{
  g_signal_emit (window, d3d12_window_signals[SIGNAL_KEY_EVENT], 0, event,
      name);
}

void
gst_d3d12_window_on_mouse_event (GstD3D12Window * window, const gchar * event,
    gint button, double xpos, double ypos, guint modifier)
{
  g_signal_emit (window, d3d12_window_signals[SIGNAL_MOUSE_EVENT], 0,
      event, button, xpos, ypos, modifier);
}

static gboolean
msg_io_cb (GIOChannel * source, GIOCondition condition, gpointer data)
{
  MSG msg;

  if (!PeekMessage (&msg, nullptr, 0, 0, PM_REMOVE))
    return G_SOURCE_CONTINUE;

  TranslateMessage (&msg);
  DispatchMessage (&msg);

  return G_SOURCE_CONTINUE;
}

static gpointer
gst_d3d12_window_hwnd_thread_func (GstD3D12Window * self)
{
  auto priv = self->priv;
  auto server = HwndServer::get_instance ();

  g_main_context_push_thread_default (priv->main_context);

  priv->proxy_id = server->create_internal_window (self);
  auto proxy = server->get_proxy (self, priv->proxy_id);
  priv->proxy = proxy;

  proxy->set_fullscreen_on_alt_enter (priv->fullscreen_on_alt_enter);
  proxy->toggle_fullscreen (priv->requested_fullscreen);

  auto msg_io_ch = g_io_channel_win32_new_messages (0);
  auto msg_source = g_io_create_watch (msg_io_ch, G_IO_IN);
  g_source_set_callback (msg_source, (GSourceFunc) msg_io_cb, nullptr, nullptr);
  g_source_attach (msg_source, priv->main_context);

  auto idle_source = g_idle_source_new ();
  g_source_set_callback (idle_source,[](gpointer data)->gboolean {
        auto self = GST_D3D12_WINDOW (data);
        auto priv = self->priv;
        std::lock_guard < std::mutex > lk (priv->loop_lock);
        priv->loop_cond.notify_all ();
        return G_SOURCE_REMOVE;
      }
      , self, nullptr);

  g_source_attach (idle_source, priv->main_context);
  g_source_unref (idle_source);

  g_main_loop_run (priv->loop);

  proxy = nullptr;

  g_source_destroy (msg_source);
  g_source_unref (msg_source);
  g_io_channel_unref (msg_io_ch);

  g_main_context_pop_thread_default (priv->main_context);

  return nullptr;
}

void
gst_d3d12_window_unprepare (GstD3D12Window * window)
{
  GST_DEBUG_OBJECT (window, "Start unprepare");

  auto priv = window->priv;
  auto server = HwndServer::get_instance ();

  priv->proxy.reset ();
  server->release_proxy (window, priv->proxy_id);

  g_main_loop_quit (priv->loop);
  g_clear_pointer (&priv->main_loop_thread, g_thread_join);

  GST_DEBUG_OBJECT (window, "Unprepare done");
}

void
gst_d3d12_window_unlock (GstD3D12Window * window)
{
  GST_DEBUG_OBJECT (window, "Unlock");
  auto server = HwndServer::get_instance ();
  server->unlock_window (window);
}

void
gst_d3d12_window_unlock_stop (GstD3D12Window * window)
{
  GST_DEBUG_OBJECT (window, "Unlock stop");

  auto server = HwndServer::get_instance ();
  server->unlock_stop_window (window);
}

static GstFlowReturn
gst_d3d12_window_resize_buffer (GstD3D12Window * self)
{
  auto priv = self->priv;
  auto proxy = priv->proxy.lock ();
  if (!proxy)
    return GST_FLOW_OK;

  return proxy->resize_buffer (0, 0);
}

GstFlowReturn
gst_d3d12_window_open (GstD3D12Window * window, GstD3D12Device * device,
    guint display_width, guint display_height, HWND parent_hwnd,
    gboolean direct_swapchain)
{
  auto priv = window->priv;
  auto server = HwndServer::get_instance ();

  GST_DEBUG_OBJECT (window, "Opening new window");

  gst_d3d12_window_unprepare (window);

  priv->display_width = display_width;
  priv->display_height = display_height;

  if (!parent_hwnd) {
    priv->main_loop_thread = g_thread_new ("GstD3D12Window",
        (GThreadFunc) gst_d3d12_window_hwnd_thread_func, window);

    std::unique_lock < std::mutex > lk (priv->loop_lock);
    while (!g_main_loop_is_running (priv->loop))
      priv->loop_cond.wait (lk);

    return GST_FLOW_OK;
  }

  auto ret = server->create_child_hwnd (window, parent_hwnd,
      direct_swapchain, priv->proxy_id);
  if (ret == GST_FLOW_OK)
    priv->proxy = server->get_proxy (window, priv->proxy_id);

  return ret;
}

GstFlowReturn
gst_d3d12_window_prepare (GstD3D12Window * window, GstD3D12Device * device,
    guint display_width, guint display_height,
    GstCaps * caps, GstStructure * config, DXGI_FORMAT display_format)
{
  auto priv = window->priv;
  GstVideoInfo in_info;
  GstVideoFormat format = GST_VIDEO_FORMAT_RGBA;
  priv->display_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  priv->display_width = display_width;
  priv->display_height = display_height;

  gst_video_info_from_caps (&in_info, caps);

  if (display_format != DXGI_FORMAT_UNKNOWN) {
    priv->display_format = display_format;
    format = gst_d3d12_dxgi_format_to_gst (display_format);
  } else if (GST_VIDEO_INFO_COMP_DEPTH (&in_info, 0) > 8) {
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support = { };
    const D3D12_FORMAT_SUPPORT1 support_flags =
        D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_DISPLAY;
    format_support.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    format_support.Support1 = support_flags;
    auto hr = device_handle->CheckFeatureSupport (D3D12_FEATURE_FORMAT_SUPPORT,
        &format_support, sizeof (format_support));
    if (SUCCEEDED (hr) && (format_support.Support1 & support_flags)
        == support_flags) {
      priv->display_format = DXGI_FORMAT_R10G10B10A2_UNORM;
      format = GST_VIDEO_FORMAT_RGB10A2_LE;
    }
  }

  gst_video_info_set_format (&priv->display_info, format,
      display_width, display_height);

  if (!gst_d3d12_device_is_equal (window->device, device)) {
    gst_clear_object (&window->device);
    window->device = (GstD3D12Device *) gst_object_ref (device);
  }

  auto proxy = priv->proxy.lock ();
  if (!proxy) {
    GST_WARNING_OBJECT (window, "Window was closed");
    return GST_D3D12_WINDOW_FLOW_CLOSED;
  }

  return proxy->setup_swapchain (device, priv->display_format, &in_info,
      &priv->display_info, config);
}

GstFlowReturn
gst_d3d12_window_render (GstD3D12Window * self, SwapChainResource * resource,
    GstBuffer * buffer, bool is_first, RECT & output_rect)
{
  auto priv = self->priv;
  auto device = resource->device;
  auto cur_idx = resource->swapchain->GetCurrentBackBufferIndex ();
  auto swapbuf = resource->buffers[cur_idx];

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (is_first || priv->output_updated) {
      GstVideoRectangle dst_rect = { };
      GstVideoRectangle rst_rect = { };
      dst_rect.w = (gint) resource->buffer_desc.Width;
      dst_rect.h = (gint) resource->buffer_desc.Height;

      for (size_t i = 0; i < resource->buffers.size (); i++)
        resource->buffers[i]->is_first = true;

      if (priv->force_aspect_ratio) {
        GstVideoRectangle src_rect = { };

        switch (priv->orientation) {
          case GST_VIDEO_ORIENTATION_90R:
          case GST_VIDEO_ORIENTATION_90L:
          case GST_VIDEO_ORIENTATION_UL_LR:
          case GST_VIDEO_ORIENTATION_UR_LL:
            src_rect.w = GST_VIDEO_INFO_HEIGHT (&priv->display_info);
            src_rect.h = GST_VIDEO_INFO_WIDTH (&priv->display_info);
            break;
          default:
            src_rect.w = GST_VIDEO_INFO_WIDTH (&priv->display_info);
            src_rect.h = GST_VIDEO_INFO_HEIGHT (&priv->display_info);
            break;
        }

        gst_video_sink_center_rect (src_rect, dst_rect, &rst_rect, TRUE);
      } else {
        rst_rect = dst_rect;
      }

      priv->output_rect = rst_rect;
      priv->dirty_rect.left = rst_rect.x;
      priv->dirty_rect.top = rst_rect.y;
      priv->dirty_rect.right = rst_rect.x + rst_rect.w;
      priv->dirty_rect.bottom = rst_rect.y + rst_rect.h;

      output_rect = priv->dirty_rect;

      g_object_set (resource->conv, "dest-x", priv->output_rect.x,
          "dest-y", priv->output_rect.y, "dest-width", priv->output_rect.w,
          "dest-height", priv->output_rect.h, nullptr);

      if (gst_d3d12_need_transform (priv->rotation_x, priv->rotation_y,
              priv->rotation_z, priv->scale_x, priv->scale_y)) {
        gst_d3d12_converter_apply_transform (resource->conv, priv->orientation,
            priv->output_rect.w, priv->output_rect.h, priv->fov, priv->ortho,
            priv->rotation_x, priv->rotation_y, priv->rotation_z,
            priv->scale_x, priv->scale_y);
      } else {
        g_object_set (resource->conv,
            "video-direction", priv->orientation, nullptr);
      }

      gst_d3d12_overlay_compositor_update_viewport (resource->comp,
          &priv->output_rect);
    }

    priv->output_updated = FALSE;
  }

  gst_d3d12_overlay_compositor_upload (resource->comp, buffer);

  GstD3D12CommandAllocator *gst_ca;
  if (!gst_d3d12_command_allocator_pool_acquire (resource->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    return GST_FLOW_ERROR;
  }

  auto ca = gst_d3d12_command_allocator_get_handle (gst_ca);
  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_command_allocator_unref (gst_ca);
    return GST_FLOW_ERROR;
  }

  ComPtr < ID3D12GraphicsCommandList > cl;
  if (!resource->cl) {
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&cl));
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't create command list");
      gst_d3d12_command_allocator_unref (gst_ca);
      return GST_FLOW_ERROR;
    }

    resource->cl = cl;
  } else {
    cl = resource->cl;
    hr = cl->Reset (ca, nullptr);
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't reset command list");
      gst_d3d12_command_allocator_unref (gst_ca);
      return GST_FLOW_ERROR;
    }
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto mem = (GstD3D12Memory *) gst_buffer_peek_memory (swapbuf->backbuf, 0);
  auto backbuf_texture = gst_d3d12_memory_get_resource_handle (mem);
  ID3D12Resource *msaa_resource = nullptr;
  GstBuffer *conv_outbuf = swapbuf->backbuf;
  if (resource->msaa_buf) {
    conv_outbuf = resource->msaa_buf;
    mem = (GstD3D12Memory *) gst_buffer_peek_memory (conv_outbuf, 0);
    msaa_resource = gst_d3d12_memory_get_resource_handle (mem);
    /* MSAA resource must be render target state here already */
  } else {
    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier (1, &barrier);
  }

  if (swapbuf->is_first || priv->overlay_mode != GST_D3D12_WINDOW_OVERLAY_NONE) {
    FLOAT clear_color[4] = { 0, 0, 0, 1 };
    auto rtv_heap = gst_d3d12_memory_get_render_target_view_heap (mem);
    auto cpu_handle = GetCPUDescriptorHandleForHeapStart (rtv_heap);
    cl->ClearRenderTargetView (cpu_handle, clear_color, 0, nullptr);
  }

  swapbuf->is_first = false;

  auto cq = gst_d3d12_device_get_command_queue (device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  auto cq_handle = gst_d3d12_command_queue_get_handle (cq);
  if (!gst_d3d12_converter_convert_buffer (resource->conv,
          buffer, conv_outbuf, fence_data, cl.Get (), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't build convert command");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  if (!gst_d3d12_overlay_compositor_draw (resource->comp,
          conv_outbuf, fence_data, cl.Get ())) {
    GST_ERROR_OBJECT (self, "Couldn't build overlay command");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  D3D12_RESOURCE_STATES state_after = D3D12_RESOURCE_STATE_COMMON;
  GstD3D12WindowOverlayMode selected_overlay_mode =
      GST_D3D12_WINDOW_OVERLAY_NONE;
  bool signal_with_lock = false;
  bool set_d2d_target = false;
  GstD3D12WindowOverlayMode overlay_mode = priv->overlay_mode;
  if ((overlay_mode & GST_D3D12_WINDOW_OVERLAY_D3D12) != 0) {
    selected_overlay_mode |= GST_D3D12_WINDOW_OVERLAY_D3D12;
    state_after = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }

  if ((overlay_mode & GST_D3D12_WINDOW_OVERLAY_D3D11) ==
      GST_D3D12_WINDOW_OVERLAY_D3D11 &&
      resource->ensure_d3d11_target (swapbuf.get ())) {
    selected_overlay_mode |= GST_D3D12_WINDOW_OVERLAY_D3D11;
    signal_with_lock = true;
  }

  if ((overlay_mode & GST_D3D12_WINDOW_OVERLAY_D2D) ==
      GST_D3D12_WINDOW_OVERLAY_D2D &&
      (priv->display_format == DXGI_FORMAT_R8G8B8A8_UNORM ||
          priv->display_format == DXGI_FORMAT_B8G8R8A8_UNORM) &&
      resource->ensure_d2d_target (swapbuf.get ())) {
    selected_overlay_mode |= GST_D3D12_WINDOW_OVERLAY_D2D;
    set_d2d_target = true;
  }

  if (msaa_resource) {
    std::vector < D3D12_RESOURCE_BARRIER > barriers;
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (msaa_resource,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE));
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RESOLVE_DEST));
    cl->ResourceBarrier (barriers.size (), barriers.data ());

    cl->ResolveSubresource (backbuf_texture, 0, msaa_resource, 0,
        priv->display_format);

    barriers.clear ();
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (msaa_resource,
            D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET));
    barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
            D3D12_RESOURCE_STATE_RESOLVE_DEST, state_after));
    cl->ResourceBarrier (barriers.size (), barriers.data ());
  } else if (state_after == D3D12_RESOURCE_STATE_COMMON) {
    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);

    cl->ResourceBarrier (1, &barrier);
  }

  hr = cl->Close ();
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cmd_list[] = { cl.Get () };
  hr = gst_d3d12_command_queue_execute_command_lists (cq,
      1, cmd_list, &resource->fence_val);
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Signal failed");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  gst_d3d12_command_queue_set_notify (cq, resource->fence_val,
      fence_data, (GDestroyNotify) gst_d3d12_fence_data_unref);

  if (selected_overlay_mode != GST_D3D12_WINDOW_OVERLAY_NONE) {
    D3D12_RECT viewport = priv->dirty_rect;
    if (signal_with_lock)
      gst_d3d12_device_11on12_lock (device);

    if (set_d2d_target)
      resource->context2d->SetTarget (swapbuf->d2d_target.Get ());

    g_signal_emit (self, d3d12_window_signals[SIGNAL_OVERLAY], 0,
        cq_handle, backbuf_texture, resource->device11on12.Get (),
        swapbuf->wrapped_resource.Get (), resource->context2d.Get (),
        &viewport);

    if (signal_with_lock)
      gst_d3d12_device_11on12_unlock (device);
  }

  if (state_after != D3D12_RESOURCE_STATE_COMMON) {
    if (!gst_d3d12_command_allocator_pool_acquire (resource->ca_pool, &gst_ca)) {
      GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
      return GST_FLOW_ERROR;
    }

    ca = gst_d3d12_command_allocator_get_handle (gst_ca);
    hr = ca->Reset ();
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
      gst_d3d12_command_allocator_unref (gst_ca);
      return GST_FLOW_ERROR;
    }

    hr = cl->Reset (ca, nullptr);
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't reset command list");
      gst_d3d12_command_allocator_unref (gst_ca);
      return GST_FLOW_ERROR;
    }

    gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
    gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition (backbuf_texture,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    cl->ResourceBarrier (1, &barrier);
    hr = cl->Close ();
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't close command list");
      gst_d3d12_fence_data_unref (fence_data);
      return GST_FLOW_ERROR;
    }

    hr = gst_d3d12_command_queue_execute_command_lists (cq,
        1, cmd_list, &resource->fence_val);
    if (!gst_d3d12_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Signal failed");
      gst_d3d12_fence_data_unref (fence_data);
      return GST_FLOW_ERROR;
    }

    gst_d3d12_command_queue_set_notify (cq, resource->fence_val,
        fence_data, (GDestroyNotify) gst_d3d12_fence_data_unref);
  }

  return GST_FLOW_OK;
}

void
gst_d3d12_window_expose (GstD3D12Window * window)
{
  auto priv = window->priv;
  auto proxy = priv->proxy.lock ();

  if (proxy)
    proxy->expose ();
}

GstFlowReturn
gst_d3d12_window_set_buffer (GstD3D12Window * window, GstBuffer * buffer)
{
  auto priv = window->priv;
  auto proxy = priv->proxy.lock ();

  if (!proxy) {
    GST_WARNING_OBJECT (window, "Window was closed");
    return GST_D3D12_WINDOW_FLOW_CLOSED;
  }

  return proxy->set_buffer (buffer);
}

GstFlowReturn
gst_d3d12_window_present (GstD3D12Window * window)
{
  auto priv = window->priv;
  auto proxy = priv->proxy.lock ();
  if (!proxy) {
    GST_WARNING_OBJECT (window, "Window was closed");
    return GST_D3D12_WINDOW_FLOW_CLOSED;
  }

  return proxy->present ();
}

guintptr
gst_d3d12_window_get_window_handle (GstD3D12Window * window)
{
  auto priv = window->priv;
  auto proxy = priv->proxy.lock ();
  if (!proxy)
    return 0;

  return (guintptr) proxy->get_window_handle ();
}

void
gst_d3d12_window_set_render_rect (GstD3D12Window * window,
    const GstVideoRectangle * rect)
{
  auto priv = window->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    priv->render_rect = *rect;
  }

  auto proxy = priv->proxy.lock ();
  if (proxy)
    proxy->update_render_rect ();
}

void
gst_d3d12_window_get_render_rect (GstD3D12Window * window,
    GstVideoRectangle * rect)
{
  auto priv = window->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  *rect = priv->render_rect;
}

void
gst_d3d12_window_set_force_aspect_ratio (GstD3D12Window * window,
    gboolean force_aspect_ratio)
{
  auto priv = window->priv;
  bool updated = false;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (priv->force_aspect_ratio != force_aspect_ratio) {
      priv->force_aspect_ratio = force_aspect_ratio;
      priv->output_updated = TRUE;
      updated = true;
    }
  }

  if (updated)
    gst_d3d12_window_set_buffer (window, nullptr);
}

void
gst_d3d12_window_set_enable_navigation_events (GstD3D12Window * window,
    gboolean enable)
{
  auto priv = window->priv;
  priv->enable_navigation = enable;
}

gboolean
gst_d3d12_window_get_navigation_events_enabled (GstD3D12Window * window)
{
  auto priv = window->priv;
  return priv->enable_navigation;
}

void
gst_d3d12_window_set_orientation (GstD3D12Window * window, gboolean immediate,
    GstVideoOrientationMethod orientation, gfloat fov, gboolean ortho,
    gfloat rotation_x, gfloat rotation_y, gfloat rotation_z,
    gfloat scale_x, gfloat scale_y)
{
  auto priv = window->priv;
  bool updated = false;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (priv->orientation != orientation || priv->fov != fov
        || priv->ortho != ortho
        || priv->rotation_x != rotation_x || priv->rotation_y != rotation_y
        || priv->rotation_z != rotation_z || priv->scale_x != scale_x
        || priv->scale_y != scale_y) {
      priv->orientation = orientation;
      priv->fov = fov;
      priv->ortho = ortho;
      priv->rotation_x = rotation_x;
      priv->rotation_y = rotation_y;
      priv->rotation_z = rotation_z;
      priv->scale_x = scale_x;
      priv->scale_y = scale_y;
      priv->output_updated = TRUE;
      updated = true;
    }
  }

  if (updated && immediate)
    gst_d3d12_window_set_buffer (window, nullptr);
}

void
gst_d3d12_window_set_title (GstD3D12Window * window, const gchar * title)
{
  auto priv = window->priv;
  wchar_t *wtitle = nullptr;

  if (title)
    wtitle = (wchar_t *) g_utf8_to_utf16 (title, -1, nullptr, nullptr, nullptr);

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!wtitle) {
    priv->title.clear ();
  } else {
    priv->title = wtitle;
  }
  g_free (wtitle);
}

GstD3D12Window *
gst_d3d12_window_new (void)
{
  auto self = (GstD3D12Window *) g_object_new (GST_TYPE_D3D12_WINDOW, nullptr);
  gst_object_ref_sink (self);

  auto server = HwndServer::get_instance ();
  server->register_window (self);

  return self;
}

void
gst_d3d12_window_invalidate (GstD3D12Window * window)
{
  auto server = HwndServer::get_instance ();
  server->unregister_window (window);
}

gboolean
gst_d3d12_window_is_closed (GstD3D12Window * window)
{
  auto priv = window->priv;

  if (priv->proxy.expired ())
    return TRUE;

  return FALSE;
}

void
gst_d3d12_window_enable_fullscreen_on_alt_enter (GstD3D12Window * window,
    gboolean enable)
{
  auto priv = window->priv;
  auto proxy = priv->proxy.lock ();

  priv->fullscreen_on_alt_enter = enable;
  if (proxy)
    proxy->set_fullscreen_on_alt_enter (enable);
}

void
gst_d3d12_window_set_fullscreen (GstD3D12Window * window, gboolean enable)
{
  auto priv = window->priv;
  auto proxy = priv->proxy.lock ();

  priv->requested_fullscreen = enable;
  if (proxy)
    proxy->toggle_fullscreen (enable);
}

void
gst_d3d12_window_set_msaa (GstD3D12Window * window, GstD3D12MSAAMode msaa)
{
  auto priv = window->priv;
  auto prev_val = priv->msaa.exchange (msaa);
  if (prev_val != msaa)
    gst_d3d12_window_resize_buffer (window);
}

void
gst_d3d12_window_get_msaa (GstD3D12Window * window, GstD3D12MSAAMode & msaa)
{
  auto priv = window->priv;
  msaa = priv->msaa;
}

void
gst_d3d12_window_set_overlay_mode (GstD3D12Window * window,
    GstD3D12WindowOverlayMode mode)
{
  auto priv = window->priv;
  priv->overlay_mode = mode;
}

void
gst_d3d12_window_get_create_params (GstD3D12Window * window,
    std::wstring & title, GstVideoRectangle * rect, int &display_width,
    int &display_height, GstVideoOrientationMethod & orientation)
{
  auto priv = window->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (priv->title.empty ())
    title = L"Direct3D12 Renderer";
  else
    title = priv->title;

  *rect = priv->render_rect;

  display_width = priv->display_width;
  display_height = priv->display_height;
  orientation = priv->orientation;
}

void
gst_d3d12_window_get_mouse_pos_info (GstD3D12Window * window,
    GstVideoRectangle * out_rect, int &input_width, int &input_height,
    GstVideoOrientationMethod & orientation)
{
  auto priv = window->priv;
  std::lock_guard < std::recursive_mutex > lk (priv->lock);

  *out_rect = priv->output_rect;
  input_width = priv->input_info.width;
  input_height = priv->input_info.height;
  orientation = priv->orientation;
}

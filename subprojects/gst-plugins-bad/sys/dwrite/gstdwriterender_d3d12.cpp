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

#include "gstdwriterender_d3d12.h"
#include "gstdwrite-renderer.h"
#include <gst/d3d12/gstd3d12-private.h>
#include <d3d11on12.h>
#include <wrl.h>
#include <vector>
#include <queue>

GST_DEBUG_CATEGORY_EXTERN (dwrite_overlay_object_debug);
#define GST_CAT_DEFAULT dwrite_overlay_object_debug

#define ASYNC_DEPTH 4

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct GstDWriteD3D12RenderPrivate
{
  GstDWriteD3D12RenderPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
    event_handle = CreateEventEx (nullptr, nullptr, 0, EVENT_ALL_ACCESS);
  }

  ~GstDWriteD3D12RenderPrivate ()
  {
    renderer = nullptr;
    dwrite_factory = nullptr;
    d2d_factory = nullptr;
    ClearResource ();
    gst_clear_object (&fence_data_pool);
    CloseHandle (event_handle);
  }

  void ClearResource ()
  {
    if (device) {
      gst_d3d12_device_fence_wait (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
          fence_val, event_handle);
    }

    gst_clear_object (&ca_pool);
    cl = nullptr;

    {
      GstD3D12Device11on12LockGuard lk (device);
      d2d_target = nullptr;
      wrapped_texture = nullptr;
      layout_resource = nullptr;
      device11on12 = nullptr;
      d3d11_context = nullptr;
      device11 = nullptr;
    }

    if (layout_pool)
      gst_buffer_pool_set_active (layout_pool, FALSE);
    gst_clear_object (&layout_pool);
    if (blend_pool)
      gst_buffer_pool_set_active (blend_pool, FALSE);
    gst_clear_object (&blend_pool);
    gst_clear_object (&pre_conv);
    gst_clear_object (&blend_conv);
    gst_clear_object (&post_conv);
    gst_clear_object (&device);
    prepared = FALSE;
    fence_val = 0;
    scheduled = { };
  }

  GstD3D12Device *device = nullptr;
  ComPtr<ID2D1Factory> d2d_factory;
  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IGstDWriteTextRenderer> renderer;
  ComPtr<ID3D11Texture2D> wrapped_texture;
  ComPtr<ID3D12Resource> layout_resource;
  ComPtr<ID2D1RenderTarget> d2d_target;
  GstBufferPool *layout_pool = nullptr;
  GstBufferPool *blend_pool = nullptr;
  GstVideoInfo layout_info;
  GstVideoInfo blend_info;
  GstVideoInfo info;
  gboolean direct_blend = FALSE;
  gboolean prepared = FALSE;

  GstD3D12Converter *pre_conv = nullptr;
  GstD3D12Converter *blend_conv = nullptr;
  GstD3D12Converter *post_conv = nullptr;

  HANDLE event_handle;
  guint64 fence_val = 0;

  ComPtr<ID3D12GraphicsCommandList> cl;
  GstD3D12FenceDataPool *fence_data_pool;
  GstD3D12CommandAllocatorPool *ca_pool = nullptr;
  ComPtr<ID3D11On12Device> device11on12;
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11DeviceContext> d3d11_context;
  std::queue<guint64> scheduled;
};
/* *INDENT-ON* */

struct _GstDWriteD3D12Render
{
  GstDWriteRender parent;
  GstDWriteD3D12RenderPrivate *priv;
};

static void gst_dwrite_d3d12_render_finalize (GObject * object);
static GstBuffer *gst_dwrite_d3d12_render_draw_layout (GstDWriteRender * render,
    IDWriteTextLayout * layout, gint x, gint y);
static gboolean gst_dwrite_d3d12_render_blend (GstDWriteRender * render,
    GstBuffer * layout_buf, gint x, gint y, GstBuffer * output);
static gboolean
gst_dwrite_d3d12_render_update_device (GstDWriteRender * render,
    GstBuffer * buffer);
static gboolean
gst_dwrite_d3d12_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query);
static gboolean gst_dwrite_d3d12_render_can_inplace (GstDWriteRender * render,
    GstBuffer * buffer);
static gboolean gst_dwrite_d3d12_render_upload (GstDWriteRender * render,
    const GstVideoInfo * info, GstBuffer * in_buf, GstBuffer * out_buf);

static gboolean gst_dwrite_d3d12_render_prepare (GstDWriteD3D12Render * self);

#define gst_dwrite_d3d12_render_parent_class parent_class
G_DEFINE_FINAL_TYPE (GstDWriteD3D12Render, gst_dwrite_d3d12_render,
    GST_TYPE_DWRITE_RENDER);

static void
gst_dwrite_d3d12_render_class_init (GstDWriteD3D12RenderClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto render_class = GST_DWRITE_RENDER_CLASS (klass);

  object_class->finalize = gst_dwrite_d3d12_render_finalize;

  render_class->draw_layout = gst_dwrite_d3d12_render_draw_layout;
  render_class->blend = gst_dwrite_d3d12_render_blend;
  render_class->update_device = gst_dwrite_d3d12_render_update_device;
  render_class->handle_allocation_query =
      gst_dwrite_d3d12_render_handle_allocation_query;
  render_class->can_inplace = gst_dwrite_d3d12_render_can_inplace;
  render_class->upload = gst_dwrite_d3d12_render_upload;
}

static void
gst_dwrite_d3d12_render_init (GstDWriteD3D12Render * self)
{
  self->priv = new GstDWriteD3D12RenderPrivate ();
}

static void
gst_dwrite_d3d12_render_finalize (GObject * object)
{
  auto self = GST_DWRITE_D3D12_RENDER (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstBufferPool *
gst_dwrite_d3d12_render_create_pool (GstDWriteD3D12Render * self,
    const GstVideoInfo * info)
{
  auto priv = self->priv;

  auto caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Invalid info");
    return nullptr;
  }

  auto pool = gst_d3d12_buffer_pool_new (priv->device);
  auto config = gst_buffer_pool_get_config (pool);
  auto params = gst_d3d12_allocation_params_new (priv->device, info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_NONE);
  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps, 0, 0, 0);
  gst_caps_unref (caps);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set config");
    gst_object_unref (pool);
    return nullptr;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't set active");
    gst_object_unref (pool);
    return nullptr;
  }

  return pool;
}

static GstBuffer *
gst_dwrite_d3d12_render_draw_layout (GstDWriteRender * render,
    IDWriteTextLayout * layout, gint x, gint y)
{
  auto self = GST_DWRITE_D3D12_RENDER (render);
  auto priv = self->priv;

  if (!priv->prepared) {
    GST_ERROR_OBJECT (self, "Not prepapred");
    return nullptr;
  }

  auto width = (gint) layout->GetMaxWidth ();
  auto height = (gint) layout->GetMaxHeight ();

  if (priv->layout_pool && (priv->layout_info.width != width ||
          priv->layout_info.height != height)) {
    gst_buffer_pool_set_active (priv->layout_pool, FALSE);
    gst_clear_object (&priv->layout_pool);

    GstD3D12Device11on12LockGuard lk (priv->device);
    priv->d2d_target = nullptr;
    priv->wrapped_texture = nullptr;
    priv->layout_resource = nullptr;
  }

  if (!priv->layout_pool) {
    gst_video_info_set_format (&priv->layout_info, GST_VIDEO_FORMAT_BGRA,
        width, height);

    priv->layout_pool = gst_dwrite_d3d12_render_create_pool (self,
        &priv->layout_info);
    if (!priv->layout_pool) {
      GST_ERROR_OBJECT (self, "Couldn't create pool");
      return nullptr;
    }
  }

  if (!priv->layout_resource) {
    auto device = gst_d3d12_device_get_device_handle (priv->device);
    D3D12_HEAP_PROPERTIES heap_prop = { };
    heap_prop.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_prop.CreationNodeMask = 1;
    heap_prop.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = { };
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    auto hr = device->CreateCommittedResource (&heap_prop,
        D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS (&priv->layout_resource));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create layout texture");
      return nullptr;
    }

    GstD3D12Device11on12LockGuard lk (priv->device);
    D3D11_RESOURCE_FLAGS flags11 = { };
    flags11.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    flags11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = priv->device11on12->CreateWrappedResource (priv->
        layout_resource.Get (), &flags11, D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        IID_PPV_ARGS (&priv->wrapped_texture));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create wrappred resource");
      priv->layout_resource = nullptr;
      return nullptr;
    }

    ComPtr < IDXGISurface > surface;
    static const D2D1_RENDER_TARGET_PROPERTIES props = {
      D2D1_RENDER_TARGET_TYPE_DEFAULT, DXGI_FORMAT_B8G8R8A8_UNORM,
      D2D1_ALPHA_MODE_PREMULTIPLIED, 0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
      D2D1_FEATURE_LEVEL_DEFAULT
    };

    hr = priv->wrapped_texture->QueryInterface (IID_PPV_ARGS (&surface));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't get DXGI surface");
      priv->wrapped_texture = nullptr;
      priv->layout_resource = nullptr;
      return nullptr;
    }

    hr = priv->d2d_factory->CreateDxgiSurfaceRenderTarget (surface.Get (),
        props, &priv->d2d_target);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't create d2d render target");
      priv->wrapped_texture = nullptr;
      priv->layout_resource = nullptr;
      return nullptr;
    }
  }

  if (priv->scheduled.size () >= ASYNC_DEPTH) {
    auto fence_to_wait = priv->scheduled.front ();
    priv->scheduled.pop ();
    gst_d3d12_device_fence_wait (priv->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT, fence_to_wait, priv->event_handle);
  }

  GstBuffer *layout_buf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->layout_pool, &layout_buf, nullptr);
  if (!layout_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    return nullptr;
  }

  {
    GstD3D12Device11on12LockGuard lk (priv->device);
    ID3D11Resource *wrapped[] = { priv->wrapped_texture.Get () };
    priv->device11on12->AcquireWrappedResources (wrapped, 1);
    priv->d2d_target->BeginDraw ();
    priv->d2d_target->Clear (D2D1::ColorF (D2D1::ColorF::Black, 0.0));
    priv->renderer->Draw (D2D1::Point2F (),
        D2D1::Rect (0, 0, width, height), layout, priv->d2d_target.Get ());
    priv->d2d_target->EndDraw ();
    priv->device11on12->ReleaseWrappedResources (wrapped, 1);
    priv->d3d11_context->Flush ();
  }

  auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (layout_buf, 0);
  auto texture = gst_d3d12_memory_get_resource_handle (dmem);

  GstD3D12CopyTextureRegionArgs args = { };
  args.src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  args.src.pResource = priv->layout_resource.Get ();
  args.dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  args.dst.pResource = texture;

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  auto resource_clone = priv->layout_resource;
  auto wrapped_clone = priv->wrapped_texture;

  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_COM (resource_clone.Detach ()));
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_COM (wrapped_clone.Detach ()));

  gst_d3d12_device_copy_texture_region (priv->device,
      1, &args, fence_data, 0, nullptr, nullptr, D3D12_COMMAND_LIST_TYPE_DIRECT,
      &priv->fence_val);

  priv->scheduled.push (priv->fence_val);
  gst_d3d12_memory_set_fence (dmem,
      gst_d3d12_device_get_fence_handle (priv->device,
          D3D12_COMMAND_LIST_TYPE_DIRECT), priv->fence_val, FALSE);

  GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
  GST_MINI_OBJECT_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  return layout_buf;
}

static gboolean
gst_dwrite_d3d12_render_blend (GstDWriteRender * render, GstBuffer * layout_buf,
    gint x, gint y, GstBuffer * output)
{
  auto self = GST_DWRITE_D3D12_RENDER (render);
  auto priv = self->priv;

  if (!priv->prepared) {
    GST_ERROR_OBJECT (self, "Not prepapred");
    return FALSE;
  }

  if (priv->scheduled.size () >= ASYNC_DEPTH) {
    auto fence_to_wait = priv->scheduled.front ();
    priv->scheduled.pop ();
    gst_d3d12_device_fence_wait (priv->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT, fence_to_wait, priv->event_handle);
  }

  GstD3D12Frame out_frame;
  if (!gst_d3d12_frame_map (&out_frame, &priv->info, output,
          GST_MAP_WRITE_D3D12, GST_D3D12_FRAME_MAP_FLAG_RTV)) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    return FALSE;
  }
  gst_d3d12_frame_unmap (&out_frame);

  GstD3D12CommandAllocator *gst_ca;
  if (!gst_d3d12_command_allocator_pool_acquire (priv->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    return FALSE;
  }

  auto ca = gst_d3d12_command_allocator_get_handle (gst_ca);
  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_command_allocator_unref (gst_ca);
    return FALSE;
  }

  if (!priv->cl) {
    auto device = gst_d3d12_device_get_device_handle (priv->device);
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&priv->cl));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create command list");
      gst_d3d12_command_allocator_unref (gst_ca);
      return FALSE;
    }
  } else {
    hr = priv->cl->Reset (ca, nullptr);
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't reset command list");
      gst_d3d12_command_allocator_unref (gst_ca);
      return FALSE;
    }
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  g_object_set (priv->blend_conv, "src-width", priv->layout_info.width,
      "src-height", priv->layout_info.height,
      "dest-x", x, "dest-y", y, "dest-width", priv->layout_info.width,
      "dest-height", priv->layout_info.height, nullptr);

  gboolean ret = TRUE;
  GstBuffer *bgra_buf = nullptr;
  auto cq = gst_d3d12_device_get_command_queue (priv->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  auto fence = gst_d3d12_command_queue_get_fence_handle (cq);

  if (priv->direct_blend) {
    GST_LOG_OBJECT (self, "Direct blend");
    ret = gst_d3d12_converter_convert_buffer (priv->blend_conv,
        layout_buf, output, fence_data, priv->cl.Get (), TRUE);
  } else {
    GST_LOG_OBJECT (self, "Need conversion for blending");

    gst_buffer_pool_acquire_buffer (priv->blend_pool, &bgra_buf, nullptr);
    if (!bgra_buf) {
      GST_ERROR_OBJECT (self, "Couldn't acquire preconv buffer");
      ret = FALSE;
    }

    if (ret) {
      ret = gst_d3d12_converter_convert_buffer (priv->pre_conv,
          output, bgra_buf, fence_data, priv->cl.Get (), TRUE);
    }

    if (ret) {
      auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (bgra_buf, 0);
      auto resource = gst_d3d12_memory_get_resource_handle (dmem);
      D3D12_RESOURCE_BARRIER barrier = { };
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = resource;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.StateAfter =
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      priv->cl->ResourceBarrier (1, &barrier);

      ret = gst_d3d12_converter_convert_buffer (priv->blend_conv,
          layout_buf, bgra_buf, fence_data, priv->cl.Get (), FALSE);
    }

    if (ret) {
      std::vector < D3D12_RESOURCE_BARRIER > barriers;
      auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (bgra_buf, 0);
      auto resource = gst_d3d12_memory_get_resource_handle (dmem);

      D3D12_RESOURCE_BARRIER barrier = { };
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.pResource = resource;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.StateAfter =
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barriers.push_back (barrier);

      for (guint i = 0; i < gst_buffer_n_memory (output); i++) {
        dmem = (GstD3D12Memory *) gst_buffer_peek_memory (output, i);
        resource = gst_d3d12_memory_get_resource_handle (dmem);
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers.push_back (barrier);
      }

      priv->cl->ResourceBarrier (barriers.size (), barriers.data ());

      ret = gst_d3d12_converter_convert_buffer (priv->post_conv,
          bgra_buf, output, fence_data, priv->cl.Get (), FALSE);
    }

    gst_clear_buffer (&bgra_buf);
  }

  hr = priv->cl->Close ();
  if (ret)
    ret = gst_d3d12_result (hr, priv->device);

  if (ret) {
    ID3D12CommandList *cl[] = { priv->cl.Get () };
    hr = gst_d3d12_command_queue_execute_command_lists (cq,
        1, cl, &priv->fence_val);
    ret = gst_d3d12_result (hr, priv->device);
  }

  if (ret) {
    gst_d3d12_command_queue_set_notify (cq, priv->fence_val,
        FENCE_NOTIFY_MINI_OBJECT (fence_data));

    priv->scheduled.push (priv->fence_val);

    for (guint i = 0; i < gst_buffer_n_memory (output); i++) {
      auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (output, i);
      gst_d3d12_memory_set_fence (dmem, fence, priv->fence_val, FALSE);
      GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
      GST_MINI_OBJECT_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);
    }
  } else {
    gst_d3d12_fence_data_unref (fence_data);
  }

  return ret;
}

static gboolean
gst_dwrite_d3d12_render_update_device (GstDWriteRender * render,
    GstBuffer * buffer)
{
  auto self = GST_DWRITE_D3D12_RENDER (render);
  auto priv = self->priv;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d12_memory (mem))
    return FALSE;

  auto dmem = GST_D3D12_MEMORY_CAST (mem);
  if (!gst_d3d12_device_is_equal (dmem->device, priv->device)) {
    priv->ClearResource ();
    priv->device = (GstD3D12Device *) gst_object_ref (dmem->device);
    gst_dwrite_d3d12_render_prepare (self);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_dwrite_d3d12_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query)
{
  auto self = GST_DWRITE_D3D12_RENDER (render);
  auto priv = self->priv;

  GstCaps *caps = nullptr;
  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps) {
    GST_WARNING_OBJECT (elem, "Query without caps");
    return FALSE;
  }

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (elem, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  auto features = gst_caps_get_features (caps, 0);
  if (!gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
    GST_DEBUG_OBJECT (elem, "Not a d3d12 caps");
    return TRUE;
  }

  gboolean update_pool = FALSE;
  guint min, max, size;
  GstBufferPool *pool = nullptr;
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    min = max = 0;
    size = info.size;
  }

  if (pool) {
    if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      auto dpool = GST_D3D12_BUFFER_POOL (pool);
      if (!gst_d3d12_device_is_equal (dpool->device, priv->device))
        gst_clear_object (&pool);
    }
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (priv->device);

  auto config = gst_buffer_pool_get_config (pool);
  auto params = gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!params) {
    params = gst_d3d12_allocation_params_new (priv->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_HEAP_FLAG_NONE);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (params,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    gst_d3d12_allocation_params_unset_resource_flags (params,
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set config");
    gst_object_unref (pool);
    return FALSE;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_dwrite_d3d12_render_can_inplace (GstDWriteRender * render,
    GstBuffer * buffer)
{
  auto self = GST_DWRITE_D3D12_RENDER (render);
  auto priv = self->priv;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d12_memory (mem))
    return FALSE;

  auto dmem = GST_D3D12_MEMORY_CAST (mem);
  if (!gst_d3d12_device_is_equal (dmem->device, priv->device))
    return FALSE;

  D3D12_RESOURCE_DESC desc;
  auto resource = gst_d3d12_memory_get_resource_handle (dmem);
  desc = resource->GetDesc ();

  if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0 ||
      (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_dwrite_d3d12_render_upload_d3d12 (GstDWriteD3D12Render * self,
    GstBuffer * dst, GstBuffer * src)
{
  auto priv = self->priv;

  GST_TRACE_OBJECT (self, "d3d12 copy");

  return gst_d3d12_buffer_copy_into (dst, src, &priv->info);
}

static gboolean
gst_dwrite_d3d12_render_upload (GstDWriteRender * render,
    const GstVideoInfo * info, GstBuffer * in_buf, GstBuffer * out_buf)
{
  auto self = GST_DWRITE_D3D12_RENDER (render);
  auto priv = self->priv;

  if (!priv->prepared) {
    GST_ERROR_OBJECT (self, "Not prepared");
    return FALSE;
  }

  auto mem = gst_buffer_peek_memory (in_buf, 0);
  if (gst_is_d3d12_memory (mem) &&
      gst_d3d12_device_is_equal (GST_D3D12_MEMORY_CAST (mem)->device,
          priv->device)) {
    return gst_dwrite_d3d12_render_upload_d3d12 (self, out_buf, in_buf);
  }

  return GST_DWRITE_RENDER_CLASS (parent_class)->upload (render,
      info, in_buf, out_buf);
}

static gboolean
is_subsampled_yuv (const GstVideoInfo * info)
{
  if (!GST_VIDEO_INFO_IS_YUV (info))
    return FALSE;

  for (guint i = 0; i < GST_VIDEO_MAX_COMPONENTS; i++) {
    if (info->finfo->w_sub[i] != 0 || info->finfo->h_sub[i] != 0)
      return TRUE;
  }

  return FALSE;
}

static GstD3D12Converter *
create_converter (GstDWriteD3D12Render * self, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, gboolean is_blend)
{
  auto priv = self->priv;
  D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  D3D12_BLEND_DESC blend_desc = { };
  blend_desc.AlphaToCoverageEnable = FALSE;
  blend_desc.IndependentBlendEnable = FALSE;
  blend_desc.RenderTarget[0].BlendEnable = FALSE;
  blend_desc.RenderTarget[0].LogicOpEnable = FALSE;
  blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
  blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend_desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
  blend_desc.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;

  if (is_subsampled_yuv (in_info) || is_subsampled_yuv (out_info))
    filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;

  auto config = gst_structure_new ("convert-config",
      GST_D3D12_CONVERTER_OPT_SAMPLER_FILTER,
      GST_TYPE_D3D12_CONVERTER_SAMPLER_FILTER, filter, nullptr);

  if (is_blend) {
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    gst_structure_set (config, GST_D3D11_CONVERTER_OPT_SRC_ALPHA_MODE,
        GST_TYPE_D3D12_CONVERTER_ALPHA_MODE,
        GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED, nullptr);
  }

  auto ret = gst_d3d12_converter_new (priv->device, nullptr, in_info, out_info,
      &blend_desc, nullptr, config);

  if (!ret)
    GST_ERROR_OBJECT (self, "Couldn't create converter");

  return ret;
}

static gboolean
gst_dwrite_d3d12_render_prepare (GstDWriteD3D12Render * self)
{
  auto priv = self->priv;
  GstVideoInfo bgra_info;
  gst_video_info_set_format (&bgra_info,
      GST_VIDEO_FORMAT_BGRA, priv->info.width, priv->info.height);

  if (priv->direct_blend) {
    priv->blend_conv = create_converter (self, &bgra_info, &priv->blend_info,
        TRUE);
  } else {
    priv->blend_pool = gst_dwrite_d3d12_render_create_pool (self,
        &priv->blend_info);
    if (!priv->blend_pool)
      return FALSE;

    priv->pre_conv = create_converter (self,
        &priv->info, &priv->blend_info, FALSE);
    if (!priv->pre_conv)
      return FALSE;

    priv->blend_conv = create_converter (self,
        &bgra_info, &priv->blend_info, TRUE);
    if (!priv->blend_conv)
      return FALSE;

    priv->post_conv = create_converter (self,
        &priv->blend_info, &priv->info, FALSE);
    if (!priv->post_conv)
      return FALSE;
  }

  ComPtr < IUnknown > unknown =
      gst_d3d12_device_get_11on12_handle (priv->device);
  if (!unknown) {
    GST_ERROR_OBJECT (self, "Couldn't get d3d11on12 device");
    return FALSE;
  }

  unknown.As (&priv->device11on12);
  priv->device11on12.As (&priv->device11);
  priv->device11->GetImmediateContext (&priv->d3d11_context);

  auto device = gst_d3d12_device_get_device_handle (priv->device);
  priv->ca_pool = gst_d3d12_command_allocator_pool_new (device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  GST_DEBUG_OBJECT (self, "Resource prepared");

  priv->prepared = TRUE;

  return TRUE;
}

GstDWriteRender *
gst_dwrite_d3d12_render_new (GstD3D12Device * device, const GstVideoInfo * info,
    ID2D1Factory * d2d_factory, IDWriteFactory * dwrite_factory)
{
  auto self = (GstDWriteD3D12Render *)
      g_object_new (GST_TYPE_DWRITE_D3D12_RENDER, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->info = *info;

  auto format = GST_VIDEO_INFO_FORMAT (info);
  switch (format) {
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_VUYA:
    case GST_VIDEO_FORMAT_RGBA64_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      priv->direct_blend = TRUE;
      gst_video_info_set_format (&priv->blend_info,
          format, info->width, info->height);
      break;
    default:
      priv->direct_blend = FALSE;
      if (GST_VIDEO_INFO_COMP_DEPTH (info, 0) > 8) {
        gst_video_info_set_format (&priv->blend_info,
            GST_VIDEO_FORMAT_RGBA64_LE, info->width, info->height);
      } else {
        gst_video_info_set_format (&priv->blend_info,
            GST_VIDEO_FORMAT_BGRA, info->width, info->height);
      }
      break;
  }

  if (!gst_dwrite_d3d12_render_prepare (self)) {
    gst_object_unref (self);
    return nullptr;
  }

  priv->d2d_factory = d2d_factory;
  priv->dwrite_factory = dwrite_factory;
  IGstDWriteTextRenderer::CreateInstance (dwrite_factory, &priv->renderer);

  return GST_DWRITE_RENDER (self);
}

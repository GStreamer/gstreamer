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

#include <gst/d3d11/gstd3d11-private.h>
#include "gstdwriterender_d3d11.h"
#include "gstdwrite-renderer.h"
#include <wrl.h>

GST_DEBUG_CATEGORY_EXTERN (dwrite_overlay_object_debug);
#define GST_CAT_DEFAULT dwrite_overlay_object_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct GstDWriteD3D11RenderPrivate
{
  ~GstDWriteD3D11RenderPrivate ()
  {
    renderer = nullptr;
    dwrite_factory = nullptr;
    d2d_factory = nullptr;
    ClearResource ();
  }

  void ClearResource ()
  {
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
  }

  GstD3D11Device *device = nullptr;
  ComPtr<ID2D1Factory> d2d_factory;
  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IGstDWriteTextRenderer> renderer;
  GstBufferPool *layout_pool = nullptr;
  GstBufferPool *blend_pool = nullptr;
  GstVideoInfo layout_info;
  GstVideoInfo blend_info;
  GstVideoInfo info;
  gboolean direct_blend = FALSE;
  gboolean prepared = FALSE;

  GstD3D11Converter *pre_conv = nullptr;
  GstD3D11Converter *blend_conv = nullptr;
  GstD3D11Converter *post_conv = nullptr;
};
/* *INDENT-ON* */

struct _GstDWriteD3D11Render
{
  GstDWriteRender parent;
  GstDWriteD3D11RenderPrivate *priv;
};

static void gst_dwrite_d3d11_render_finalize (GObject * object);
static GstBuffer *gst_dwrite_d3d11_render_draw_layout (GstDWriteRender * render,
    IDWriteTextLayout * layout, gint x, gint y);
static gboolean gst_dwrite_d3d11_render_blend (GstDWriteRender * render,
    GstBuffer * layout_buf, gint x, gint y, GstBuffer * output);
static gboolean
gst_dwrite_d3d11_render_update_device (GstDWriteRender * render,
    GstBuffer * buffer);
static gboolean
gst_dwrite_d3d11_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query);
static gboolean gst_dwrite_d3d11_render_can_inplace (GstDWriteRender * render,
    GstBuffer * buffer);
static gboolean gst_dwrite_d3d11_render_upload (GstDWriteRender * render,
    const GstVideoInfo * info, GstBuffer * in_buf, GstBuffer * out_buf);

static gboolean gst_dwrite_d3d11_render_prepare (GstDWriteD3D11Render * self);

#define gst_dwrite_d3d11_render_parent_class parent_class
G_DEFINE_FINAL_TYPE (GstDWriteD3D11Render, gst_dwrite_d3d11_render,
    GST_TYPE_DWRITE_RENDER);

static void
gst_dwrite_d3d11_render_class_init (GstDWriteD3D11RenderClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto render_class = GST_DWRITE_RENDER_CLASS (klass);

  object_class->finalize = gst_dwrite_d3d11_render_finalize;

  render_class->draw_layout = gst_dwrite_d3d11_render_draw_layout;
  render_class->blend = gst_dwrite_d3d11_render_blend;
  render_class->update_device = gst_dwrite_d3d11_render_update_device;
  render_class->handle_allocation_query =
      gst_dwrite_d3d11_render_handle_allocation_query;
  render_class->can_inplace = gst_dwrite_d3d11_render_can_inplace;
  render_class->upload = gst_dwrite_d3d11_render_upload;
}

static void
gst_dwrite_d3d11_render_init (GstDWriteD3D11Render * self)
{
  self->priv = new GstDWriteD3D11RenderPrivate ();
}

static void
gst_dwrite_d3d11_render_finalize (GObject * object)
{
  auto self = GST_DWRITE_D3D11_RENDER (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dwrite_d3d11_render_decide_bind_flags (GstDWriteD3D11Render * self,
    const GstVideoInfo * info, guint * bind_flags)
{
  auto priv = self->priv;

  GstD3D11Format d3d11_format;
  if (!gst_d3d11_device_get_format (priv->device,
          GST_VIDEO_INFO_FORMAT (info), &d3d11_format)) {
    GST_ERROR_OBJECT (self, "Unknown device format");
    return FALSE;
  }

  DXGI_FORMAT dxgi_format = d3d11_format.dxgi_format;
  if (dxgi_format == DXGI_FORMAT_UNKNOWN)
    dxgi_format = d3d11_format.resource_format[0];

  auto device = gst_d3d11_device_get_device_handle (priv->device);
  UINT support = 0;
  auto hr = device->CheckFormatSupport (dxgi_format, &support);
  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't query format support");
    return FALSE;
  }

  guint flags = 0;
  if ((support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0)
    flags |= D3D11_BIND_SHADER_RESOURCE;

  if ((support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0) {
    flags |= D3D11_BIND_RENDER_TARGET;
    if (d3d11_format.dxgi_format == DXGI_FORMAT_UNKNOWN &&
        (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0) {
      flags |= D3D11_BIND_UNORDERED_ACCESS;
    }
  } else if ((support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0) {
    flags |= D3D11_BIND_UNORDERED_ACCESS;
  }

  *bind_flags = flags;

  return TRUE;
}

static GstBufferPool *
gst_dwrite_d3d11_render_create_pool (GstDWriteD3D11Render * self,
    const GstVideoInfo * info, guint bind_flags)
{
  auto priv = self->priv;

  auto caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Invalid info");
    return nullptr;
  }

  auto pool = gst_d3d11_buffer_pool_new (priv->device);
  auto config = gst_buffer_pool_get_config (pool);
  auto params = gst_d3d11_allocation_params_new (priv->device, info,
      GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);
  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_d3d11_allocation_params_free (params);
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
gst_dwrite_d3d11_render_draw_layout (GstDWriteRender * render,
    IDWriteTextLayout * layout, gint x, gint y)
{
  auto self = GST_DWRITE_D3D11_RENDER (render);
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
  }

  if (!priv->layout_pool) {
    gst_video_info_set_format (&priv->layout_info, GST_VIDEO_FORMAT_BGRA,
        width, height);

    guint bind_flags = 0;
    if (!gst_dwrite_d3d11_render_decide_bind_flags (self, &priv->layout_info,
            &bind_flags)) {
      GST_ERROR_OBJECT (self, "Couldn't decide bind flags");
      return nullptr;
    }
    priv->layout_pool = gst_dwrite_d3d11_render_create_pool (self,
        &priv->layout_info, bind_flags);
    if (!priv->layout_pool) {
      GST_ERROR_OBJECT (self, "Couldn't create pool");
      return nullptr;
    }
  }

  GstBuffer *layout_buf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->layout_pool, &layout_buf, nullptr);
  if (!layout_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    return nullptr;
  }

  GstD3D11DeviceLockGuard lk (priv->device);
  ComPtr < IDXGISurface > surface;
  auto dmem = (GstD3D11Memory *) gst_buffer_peek_memory (layout_buf, 0);
  static const D2D1_RENDER_TARGET_PROPERTIES props = {
    D2D1_RENDER_TARGET_TYPE_DEFAULT, DXGI_FORMAT_B8G8R8A8_UNORM,
    D2D1_ALPHA_MODE_PREMULTIPLIED, 0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
    D2D1_FEATURE_LEVEL_DEFAULT
  };

  auto texture = gst_d3d11_memory_get_resource_handle (dmem);
  auto hr = texture->QueryInterface (IID_PPV_ARGS (&surface));
  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't get DXGI surface");
    gst_buffer_unref (layout_buf);
    return nullptr;
  }

  ComPtr < ID2D1RenderTarget > target;
  hr = priv->d2d_factory->CreateDxgiSurfaceRenderTarget (surface.Get (), props,
      &target);
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't create d2d render target");
    gst_buffer_unref (layout_buf);
    return nullptr;
  }

  target->BeginDraw ();
  target->Clear (D2D1::ColorF (D2D1::ColorF::Black, 0.0));
  priv->renderer->Draw (D2D1::Point2F (),
      D2D1::Rect (0, 0, width, height), layout, target.Get ());
  target->EndDraw ();
  target = nullptr;

  return layout_buf;
}

static gboolean
gst_dwrite_d3d11_render_blend (GstDWriteRender * render, GstBuffer * layout_buf,
    gint x, gint y, GstBuffer * output)
{
  auto self = GST_DWRITE_D3D11_RENDER (render);
  auto priv = self->priv;

  if (!priv->prepared) {
    GST_ERROR_OBJECT (self, "Not prepapred");
    return FALSE;
  }

  g_object_set (priv->blend_conv, "src-width", priv->layout_info.width,
      "src-height", priv->layout_info.height,
      "dest-x", x, "dest-y", y, "dest-width", priv->layout_info.width,
      "dest-height", priv->layout_info.height, nullptr);

  if (priv->direct_blend) {
    return gst_d3d11_converter_convert_buffer (priv->blend_conv,
        layout_buf, output);
  }

  GstBuffer *bgra_buf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->blend_pool, &bgra_buf, nullptr);
  if (!bgra_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire preconv buffer");
    return FALSE;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->pre_conv,
          output, bgra_buf)) {
    GST_ERROR_OBJECT (self, "pre-convert failed");
    goto error;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->blend_conv,
          layout_buf, bgra_buf)) {
    GST_ERROR_OBJECT (self, "blend-convert failed");
    goto error;
  }

  if (!gst_d3d11_converter_convert_buffer_unlocked (priv->post_conv,
          bgra_buf, output)) {
    GST_ERROR_OBJECT (self, "post-convert failed");
    goto error;
  }

  gst_buffer_unref (bgra_buf);

  return TRUE;

error:
  gst_clear_buffer (&bgra_buf);
  return FALSE;
}

static gboolean
gst_dwrite_d3d11_render_update_device (GstDWriteRender * render,
    GstBuffer * buffer)
{
  auto self = GST_DWRITE_D3D11_RENDER (render);
  auto priv = self->priv;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem))
    return FALSE;

  auto dmem = GST_D3D11_MEMORY_CAST (mem);
  if (dmem->device != priv->device) {
    priv->ClearResource ();
    priv->device = (GstD3D11Device *) gst_object_ref (dmem->device);
    gst_dwrite_d3d11_render_prepare (self);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_dwrite_d3d11_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query)
{
  auto self = GST_DWRITE_D3D11_RENDER (render);
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
          GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
    GST_DEBUG_OBJECT (elem, "Not a d3d11 caps");
    return TRUE;
  }

  guint bind_flags = 0;
  if (!gst_dwrite_d3d11_render_decide_bind_flags (self, &info, &bind_flags)) {
    GST_ERROR_OBJECT (self, "Couldn't decide bind flags");
    return FALSE;
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
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_clear_object (&pool);
    } else {
      auto dpool = GST_D3D11_BUFFER_POOL (pool);
      if (dpool->device != priv->device)
        gst_clear_object (&pool);
    }
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (priv->device);

  auto config = gst_buffer_pool_get_config (pool);
  auto params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!params) {
    params = gst_d3d11_allocation_params_new (priv->device, &info,
        GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, 0);
  } else {
    gst_d3d11_allocation_params_set_bind_flags (params, bind_flags);
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
  gst_d3d11_allocation_params_free (params);

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
gst_dwrite_d3d11_render_can_inplace (GstDWriteRender * render,
    GstBuffer * buffer)
{
  auto self = GST_DWRITE_D3D11_RENDER (render);
  auto priv = self->priv;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d11_memory (mem))
    return FALSE;

  auto dmem = GST_D3D11_MEMORY_CAST (mem);
  if (dmem->device != priv->device)
    return FALSE;

  D3D11_TEXTURE2D_DESC desc;
  gst_d3d11_memory_get_texture_desc (dmem, &desc);

  if ((desc.BindFlags & D3D11_BIND_DECODER) != 0)
    return FALSE;

  return TRUE;
}

static gboolean
gst_dwrite_d3d11_render_upload_d3d11 (GstDWriteD3D11Render * self,
    GstBuffer * dst, GstBuffer * src)
{
  auto priv = self->priv;

  GST_TRACE_OBJECT (self, "d3d11 copy");

  GstD3D11DeviceLockGuard lk (priv->device);

  for (guint i = 0; i < gst_buffer_n_memory (dst); i++) {
    GstMemory *dst_mem, *src_mem;
    GstD3D11Memory *dst_dmem, *src_dmem;
    GstMapInfo dst_info;
    GstMapInfo src_info;
    ID3D11Resource *dst_texture, *src_texture;
    ID3D11DeviceContext *device_context;
    GstD3D11Device *device;
    D3D11_BOX src_box = { 0, };
    D3D11_TEXTURE2D_DESC dst_desc, src_desc;
    guint dst_subidx, src_subidx;

    dst_mem = gst_buffer_peek_memory (dst, i);
    src_mem = gst_buffer_peek_memory (src, i);

    dst_dmem = (GstD3D11Memory *) dst_mem;
    src_dmem = (GstD3D11Memory *) src_mem;

    device = dst_dmem->device;

    gst_d3d11_memory_get_texture_desc (dst_dmem, &dst_desc);
    gst_d3d11_memory_get_texture_desc (src_dmem, &src_desc);

    device_context = gst_d3d11_device_get_device_context_handle (device);

    if (!gst_memory_map (dst_mem, &dst_info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Cannot map dst d3d11 memory");
      return FALSE;
    }

    if (!gst_memory_map (src_mem, &src_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D11))) {
      GST_ERROR_OBJECT (self, "Cannot map src d3d11 memory");
      gst_memory_unmap (dst_mem, &dst_info);
      return FALSE;
    }

    dst_texture = (ID3D11Resource *) dst_info.data;
    src_texture = (ID3D11Resource *) src_info.data;

    /* src/dst texture size might be different if padding was used.
     * select smaller size */
    src_box.left = 0;
    src_box.top = 0;
    src_box.front = 0;
    src_box.back = 1;
    src_box.right = MIN (src_desc.Width, dst_desc.Width);
    src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

    dst_subidx = gst_d3d11_memory_get_subresource_index (dst_dmem);
    src_subidx = gst_d3d11_memory_get_subresource_index (src_dmem);

    GstD3D11DeviceLockGuard lk (device);
    device_context->CopySubresourceRegion (dst_texture, dst_subidx, 0, 0, 0,
        src_texture, src_subidx, &src_box);

    gst_memory_unmap (src_mem, &src_info);
    gst_memory_unmap (dst_mem, &dst_info);
  }

  return TRUE;
}

static gboolean
gst_dwrite_d3d11_render_upload (GstDWriteRender * render,
    const GstVideoInfo * info, GstBuffer * in_buf, GstBuffer * out_buf)
{
  auto self = GST_DWRITE_D3D11_RENDER (render);
  auto priv = self->priv;

  if (!priv->prepared) {
    GST_ERROR_OBJECT (self, "Not prepared");
    return FALSE;
  }

  auto mem = gst_buffer_peek_memory (in_buf, 0);
  if (gst_is_d3d11_memory (mem) && GST_D3D11_MEMORY_CAST (mem)->device ==
      priv->device) {
    return gst_dwrite_d3d11_render_upload_d3d11 (self, out_buf, in_buf);
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

static GstD3D11Converter *
create_converter (GstDWriteD3D11Render * self, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, gboolean is_blend)
{
  auto priv = self->priv;
  D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_POINT;

  if (is_subsampled_yuv (in_info) || is_subsampled_yuv (out_info))
    filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;

  auto config = gst_structure_new ("convert-config",
      GST_D3D11_CONVERTER_OPT_BACKEND, GST_TYPE_D3D11_CONVERTER_BACKEND,
      GST_D3D11_CONVERTER_BACKEND_SHADER,
      GST_D3D11_CONVERTER_OPT_SAMPLER_FILTER,
      GST_TYPE_D3D11_CONVERTER_SAMPLER_FILTER, filter, nullptr);
  if (is_blend) {
    gst_structure_set (config, GST_D3D11_CONVERTER_OPT_SRC_ALPHA_MODE,
        GST_TYPE_D3D11_CONVERTER_ALPHA_MODE,
        GST_D3D11_CONVERTER_ALPHA_MODE_PREMULTIPLIED, nullptr);
  }

  auto ret = gst_d3d11_converter_new (priv->device, in_info, out_info, config);
  if (!ret)
    GST_ERROR_OBJECT (self, "Couldn't create converter");

  return ret;
}

static gboolean
gst_dwrite_d3d11_render_prepare (GstDWriteD3D11Render * self)
{
  auto priv = self->priv;
  GstVideoInfo bgra_info;
  gst_video_info_set_format (&bgra_info,
      GST_VIDEO_FORMAT_BGRA, priv->info.width, priv->info.height);

  if (priv->direct_blend) {
    priv->blend_conv = create_converter (self, &bgra_info, &priv->blend_info,
        TRUE);
  } else {
    guint bind_flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    priv->blend_pool = gst_dwrite_d3d11_render_create_pool (self,
        &priv->blend_info, bind_flags);
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

  D3D11_BLEND_DESC desc = { };
  ComPtr < ID3D11BlendState > blend;
  auto device = gst_d3d11_device_get_device_handle (priv->device);

  desc.AlphaToCoverageEnable = FALSE;
  desc.IndependentBlendEnable = FALSE;
  desc.RenderTarget[0].BlendEnable = TRUE;
  desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
  desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
  desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
  desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
  desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  auto hr = device->CreateBlendState (&desc, &blend);
  if (!gst_d3d11_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create blend state");
    return FALSE;
  }

  g_object_set (priv->blend_conv, "blend-state", blend.Get (), nullptr);

  GST_DEBUG_OBJECT (self, "Resource prepared");

  priv->prepared = TRUE;

  return TRUE;
}

GstDWriteRender *
gst_dwrite_d3d11_render_new (GstD3D11Device * device, const GstVideoInfo * info,
    ID2D1Factory * d2d_factory, IDWriteFactory * dwrite_factory)
{
  auto self = (GstDWriteD3D11Render *)
      g_object_new (GST_TYPE_DWRITE_D3D11_RENDER, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = (GstD3D11Device *) gst_object_ref (device);
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

  if (!gst_dwrite_d3d11_render_prepare (self)) {
    gst_object_unref (self);
    return nullptr;
  }

  priv->d2d_factory = d2d_factory;
  priv->dwrite_factory = dwrite_factory;
  IGstDWriteTextRenderer::CreateInstance (dwrite_factory, &priv->renderer);

  return GST_DWRITE_RENDER (self);
}

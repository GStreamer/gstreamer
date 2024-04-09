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

#include "gstdwriterender_bitmap.h"
#include "gstdwritebitmappool.h"
#include "gstdwrite-renderer.h"
#include <wrl.h>

GST_DEBUG_CATEGORY_EXTERN (dwrite_overlay_object_debug);
#define GST_CAT_DEFAULT dwrite_overlay_object_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct GstDWriteBitmapRenderPrivate
{
  ~GstDWriteBitmapRenderPrivate ()
  {
    renderer = nullptr;
    dwrite_factory = nullptr;
    d2d_factory = nullptr;
    if (layout_pool)
      gst_buffer_pool_set_active (layout_pool, FALSE);
    gst_clear_object (&layout_pool);
  }
  ComPtr<ID2D1Factory> d2d_factory;
  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IGstDWriteTextRenderer> renderer;
  GstBufferPool *layout_pool = nullptr;
  GstVideoInfo layout_info;
  GstVideoInfo info;
};
/* *INDENT-ON* */

struct _GstDWriteBitmapRender
{
  GstDWriteRender parent;
  GstDWriteBitmapRenderPrivate *priv;
};

static void gst_dwrite_bitmap_render_finalize (GObject * object);
static GstBuffer *gst_dwrite_bitmap_render_draw_layout (GstDWriteRender *
    render, IDWriteTextLayout * layout, gint x, gint y);
static gboolean gst_dwrite_bitmap_render_blend (GstDWriteRender * render,
    GstBuffer * layout_buf, gint x, gint y, GstBuffer * output);
static gboolean gst_dwrite_bitmap_render_update_device (GstDWriteRender *
    render, GstBuffer * buffer);
static gboolean
gst_dwrite_bitmap_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query);
static gboolean gst_dwrite_bitmap_render_can_inplace (GstDWriteRender * render,
    GstBuffer * buffer);

#define gst_dwrite_bitmap_render_parent_class parent_class
G_DEFINE_FINAL_TYPE (GstDWriteBitmapRender, gst_dwrite_bitmap_render,
    GST_TYPE_DWRITE_RENDER);

static void
gst_dwrite_bitmap_render_class_init (GstDWriteBitmapRenderClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto render_class = GST_DWRITE_RENDER_CLASS (klass);

  object_class->finalize = gst_dwrite_bitmap_render_finalize;

  render_class->draw_layout = gst_dwrite_bitmap_render_draw_layout;
  render_class->blend = gst_dwrite_bitmap_render_blend;
  render_class->update_device = gst_dwrite_bitmap_render_update_device;
  render_class->handle_allocation_query =
      gst_dwrite_bitmap_render_handle_allocation_query;
  render_class->can_inplace = gst_dwrite_bitmap_render_can_inplace;
}

static void
gst_dwrite_bitmap_render_init (GstDWriteBitmapRender * self)
{
  self->priv = new GstDWriteBitmapRenderPrivate ();
}

static void
gst_dwrite_bitmap_render_finalize (GObject * object)
{
  auto self = GST_DWRITE_BITMAP_RENDER (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstBufferPool *
gst_dwrite_bitmap_render_create_pool (GstDWriteBitmapRender * self,
    const GstVideoInfo * info)
{
  auto caps = gst_video_info_to_caps (info);
  if (!caps) {
    GST_ERROR_OBJECT (self, "Invalid info");
    return nullptr;
  }

  auto pool = gst_dwrite_bitmap_pool_new ();
  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);
  gst_caps_unref (caps);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    gst_object_unref (pool);
    return nullptr;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't activate pool");
    gst_object_unref (pool);
    return nullptr;
  }

  return pool;
}

static GstBuffer *
gst_dwrite_bitmap_render_draw_layout (GstDWriteRender * render,
    IDWriteTextLayout * layout, gint x, gint y)
{
  auto self = GST_DWRITE_BITMAP_RENDER (render);
  auto priv = self->priv;

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
    priv->layout_pool = gst_dwrite_bitmap_render_create_pool (self,
        &priv->layout_info);
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

  ComPtr < IDXGISurface > surface;
  auto bmem = (GstDWriteBitmapMemory *) gst_buffer_peek_memory (layout_buf, 0);
  static const D2D1_RENDER_TARGET_PROPERTIES props = {
    D2D1_RENDER_TARGET_TYPE_DEFAULT, DXGI_FORMAT_B8G8R8A8_UNORM,
    D2D1_ALPHA_MODE_PREMULTIPLIED, 0, 0, D2D1_RENDER_TARGET_USAGE_NONE,
    D2D1_FEATURE_LEVEL_DEFAULT
  };

  ComPtr < ID2D1RenderTarget > target;
  auto hr = priv->d2d_factory->CreateWicBitmapRenderTarget (bmem->bitmap, props,
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
gst_dwrite_bitmap_render_blend (GstDWriteRender * render,
    GstBuffer * layout_buf, gint x, gint y, GstBuffer * output)
{
  auto self = GST_DWRITE_BITMAP_RENDER (render);
  auto priv = self->priv;

  GstVideoFrame dst_frame, src_frame;
  gboolean ret;

  if (!gst_video_frame_map (&dst_frame, &priv->info, output, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    return FALSE;
  }

  if (!gst_video_frame_map (&src_frame, &priv->layout_info, layout_buf,
          GST_MAP_READ)) {
    gst_video_frame_unmap (&dst_frame);
    GST_ERROR_OBJECT (self, "Couldn't map layout buffer");
    return FALSE;
  }

  src_frame.info.flags = (GstVideoFlags)
      (src_frame.info.flags | GST_VIDEO_FLAG_PREMULTIPLIED_ALPHA);
  ret = gst_video_blend (&dst_frame, &src_frame, x, y, 1.0);
  gst_video_frame_unmap (&src_frame);
  gst_video_frame_unmap (&dst_frame);

  return ret;
}

static gboolean
gst_dwrite_bitmap_render_update_device (GstDWriteRender * render,
    GstBuffer * buffer)
{
  return FALSE;
}

static gboolean
gst_dwrite_bitmap_render_handle_allocation_query (GstDWriteRender * render,
    GstElement * elem, GstQuery * query)
{
  return TRUE;
}

static gboolean
gst_dwrite_bitmap_render_can_inplace (GstDWriteRender * render,
    GstBuffer * buffer)
{
  return TRUE;
}

GstDWriteRender *
gst_dwrite_bitmap_render_new (const GstVideoInfo * info,
    ID2D1Factory * d2d_factory, IDWriteFactory * dwrite_factory)
{
  auto self = (GstDWriteBitmapRender *)
      g_object_new (GST_TYPE_DWRITE_BITMAP_RENDER, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->info = *info;
  priv->d2d_factory = d2d_factory;
  priv->dwrite_factory = dwrite_factory;
  IGstDWriteTextRenderer::CreateInstance (dwrite_factory, &priv->renderer);

  return GST_DWRITE_RENDER (self);
}

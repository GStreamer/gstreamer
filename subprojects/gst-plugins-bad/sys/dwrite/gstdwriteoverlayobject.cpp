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

#include <gst/d3d11/gstd3d11-private.h>
#include "gstdwriteoverlayobject.h"
#include "gstdwrite-renderer.h"
#include "gstdwriterender_bitmap.h"
#include "gstdwriterender_d3d11.h"
#include <wrl.h>
#include <mutex>

#ifdef HAVE_GST_D3D12
#include "gstdwriterender_d3d12.h"
#endif

GST_DEBUG_CATEGORY (dwrite_overlay_object_debug);
#define GST_CAT_DEFAULT dwrite_overlay_object_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct GstDWriteOverlayObjectPrivate
{
  ~GstDWriteOverlayObjectPrivate ()
  {
    ClearResource (true);
    gst_clear_object (&device);
#ifdef HAVE_GST_D3D12
    gst_clear_object (&device12);
#endif
  }

  void ClearResource (bool hard)
  {
    g_clear_pointer (&overlay_rect, gst_video_overlay_rectangle_unref);
    gst_clear_buffer (&layout_buf);
    layout = nullptr;

    gst_clear_object (&render);
  }

  GstVideoInfo info;
  GstVideoInfo layout_info;

  GstD3D11Device *device = nullptr;
#ifdef HAVE_GST_D3D12
  GstD3D12Device *device12 = nullptr;
#endif

  ComPtr<ID2D1Factory> d2d_factory;
  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IDWriteTextLayout> layout;

  GstDWriteRender *render = nullptr;

  GstBuffer *layout_buf = nullptr;
  GstVideoOverlayRectangle *overlay_rect = nullptr;

  gboolean attach_meta = FALSE;

  std::recursive_mutex ctx_lock;
};
/* *INDENT-ON* */

struct _GstDWriteOverlayObject
{
  GstObject parent;

  GstDWriteOverlayObjectPrivate *priv;
};

static void gst_dwrite_overlay_object_finalize (GObject * object);

#define gst_dwrite_overlay_object_parent_class parent_class
G_DEFINE_TYPE (GstDWriteOverlayObject, gst_dwrite_overlay_object,
    GST_TYPE_OBJECT);

static void
gst_dwrite_overlay_object_class_init (GstDWriteOverlayObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_dwrite_overlay_object_finalize;

  GST_DEBUG_CATEGORY_INIT (dwrite_overlay_object_debug,
      "dwriteoverlayobject", 0, "dwriteoverlayobject");
}

static void
gst_dwrite_overlay_object_init (GstDWriteOverlayObject * self)
{
  self->priv = new GstDWriteOverlayObjectPrivate ();
}

static void
gst_dwrite_overlay_object_finalize (GObject * object)
{
  GstDWriteOverlayObject *self = GST_DWRITE_OVERLAY_OBJECT (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GstDWriteOverlayObject *
gst_dwrite_overlay_object_new (void)
{
  GstDWriteOverlayObject *self;

  self = (GstDWriteOverlayObject *)
      g_object_new (GST_TYPE_DWRITE_OVERLAY_OBJECT, nullptr);
  gst_object_ref_sink (self);

  return self;
}

gboolean
gst_dwrite_overlay_object_start (GstDWriteOverlayObject * object,
    IDWriteFactory * dwrite_factory)
{
  auto priv = object->priv;
  HRESULT hr;
  ComPtr < ID2D1Factory > d2d_factory;

  hr = D2D1CreateFactory (D2D1_FACTORY_TYPE_MULTI_THREADED,
      IID_PPV_ARGS (&d2d_factory));
  if (FAILED (hr)) {
    GST_ERROR_OBJECT (object, "Couldn't create d2d factory");
    return FALSE;
  }

  priv->d2d_factory = d2d_factory;
  priv->dwrite_factory = dwrite_factory;

  return TRUE;
}

gboolean
gst_dwrite_overlay_object_stop (GstDWriteOverlayObject * object)
{
  auto priv = object->priv;

  priv->ClearResource (true);
  priv->dwrite_factory = nullptr;
  priv->d2d_factory = nullptr;
  gst_clear_object (&priv->device);

  return TRUE;
}

void
gst_dwrite_overlay_object_set_context (GstDWriteOverlayObject * object,
    GstElement * elem, GstContext * context)
{
  auto priv = object->priv;
  std::lock_guard < std::recursive_mutex > lk (priv->ctx_lock);

  gst_d3d11_handle_set_context (elem, context, -1, &priv->device);
#ifdef HAVE_GST_D3D12
  gst_d3d12_handle_set_context (elem, context, -1, &priv->device12);
#endif
}

gboolean
gst_dwrite_overlay_object_handle_query (GstDWriteOverlayObject * object,
    GstElement * elem, GstQuery * query)
{
  auto priv = object->priv;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return FALSE;

  std::lock_guard < std::recursive_mutex > lk (priv->ctx_lock);
  if (gst_d3d11_handle_context_query (elem, query, priv->device))
    return TRUE;

#ifdef HAVE_GST_D3D12
  if (gst_d3d12_handle_context_query (elem, query, priv->device12))
    return TRUE;
#endif

  return FALSE;
}

gboolean
gst_dwrite_overlay_object_decide_allocation (GstDWriteOverlayObject * object,
    GstElement * elem, GstQuery * query)
{
  auto priv = object->priv;
  if (!priv->render) {
    GST_DEBUG_OBJECT (object, "Render object is not configured");
    return TRUE;
  }

  return gst_dwrite_render_handle_allocation_query (priv->render, elem, query);
}

gboolean
gst_dwrite_overlay_object_propose_allocation (GstDWriteOverlayObject * object,
    GstElement * elem, GstQuery * query)
{
  auto priv = object->priv;
  if (!priv->render) {
    GST_DEBUG_OBJECT (object, "Render object is not configured");
    return TRUE;
  }

  return gst_dwrite_render_handle_allocation_query (priv->render, elem, query);
}

gboolean
gst_dwrite_overlay_object_set_caps (GstDWriteOverlayObject * object,
    GstElement * elem, GstCaps * in_caps, GstCaps * out_caps,
    GstVideoInfo * info)
{
  auto priv = object->priv;
  gboolean is_system;
  GstCapsFeatures *features;

  priv->ClearResource (true);

  if (!gst_video_info_from_caps (info, in_caps)) {
    GST_WARNING_OBJECT (elem, "Invalid caps %" GST_PTR_FORMAT, in_caps);
    return FALSE;
  }

  if (!gst_video_info_from_caps (&priv->info, out_caps)) {
    GST_ERROR_OBJECT (elem, "Invalid caps %" GST_PTR_FORMAT, out_caps);
    return FALSE;
  }

  features = gst_caps_get_features (out_caps, 0);
  auto is_d3d11 = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);
#ifdef HAVE_GST_D3D12
  auto is_d3d12 = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);
#endif
  is_system = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
  priv->attach_meta = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

  if (is_d3d11) {
    std::lock_guard < std::recursive_mutex > lk (priv->ctx_lock);
    is_d3d11 = gst_d3d11_ensure_element_data (elem, -1, &priv->device);
  }
#ifdef HAVE_GST_D3D12
  if (is_d3d12) {
    std::lock_guard < std::recursive_mutex > lk (priv->ctx_lock);
    is_d3d12 = gst_d3d12_ensure_element_data (elem, -1, &priv->device12);
  }
#endif

  if (!is_d3d11 && !is_system && !priv->attach_meta
#ifdef HAVE_GST_D3D12
      && !is_d3d12
#endif
      ) {
    GST_WARNING_OBJECT (elem,
        "Not d3d11/system memory without composition meta support");
    return FALSE;
  }
#ifdef HAVE_GST_D3D12
  if (is_d3d12) {
    priv->render = gst_dwrite_d3d12_render_new (priv->device12, &priv->info,
        priv->d2d_factory.Get (), priv->dwrite_factory.Get ());
  }
#endif

  if (!priv->render && is_d3d11) {
    priv->render = gst_dwrite_d3d11_render_new (priv->device, &priv->info,
        priv->d2d_factory.Get (), priv->dwrite_factory.Get ());
  }

  if (!priv->render) {
    priv->render = gst_dwrite_bitmap_render_new (&priv->info,
        priv->d2d_factory.Get (), priv->dwrite_factory.Get ());
  }

  if (!priv->render) {
    GST_ERROR_OBJECT (elem, "Couldn't create render object");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_dwrite_overlay_object_update_device (GstDWriteOverlayObject * object,
    GstBuffer * buffer)
{
  auto priv = object->priv;

  if (!priv->render)
    return FALSE;

  return gst_dwrite_render_update_device (priv->render, buffer);
}

GstFlowReturn
gst_dwrite_overlay_object_prepare_output (GstDWriteOverlayObject * object,
    GstBaseTransform * trans, gpointer trans_class, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  auto priv = object->priv;
  GstFlowReturn ret;

  if (!priv->render) {
    GST_ERROR_OBJECT (object, "Render object is not configured");
    return GST_FLOW_ERROR;
  }

  if (priv->attach_meta || gst_dwrite_render_can_inplace (priv->render, inbuf))
    goto inplace;

  /* Needs to allocate new buffer */
  ret = GST_BASE_TRANSFORM_CLASS (trans_class)->prepare_output_buffer (trans,
      inbuf, outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_LOG_OBJECT (object, "Needs upload");

  if (!gst_dwrite_render_upload (priv->render, &priv->info, inbuf, *outbuf)) {
    gst_clear_buffer (outbuf);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;

inplace:
  GST_LOG_OBJECT (object, "Inplace render is possible");
  if (gst_buffer_is_writable (inbuf)) {
    *outbuf = inbuf;
  } else {
    *outbuf = gst_buffer_copy (inbuf);
  }

  return GST_FLOW_OK;
}

static gboolean
gst_dwrite_overlay_object_draw_layout (GstDWriteOverlayObject * self,
    IDWriteTextLayout * layout, gint x, gint y)
{
  auto priv = self->priv;
  gint width, height;

  if (priv->layout_buf) {
    if (priv->layout && priv->layout.Get () == layout)
      return TRUE;

    gst_clear_buffer (&priv->layout_buf);
    g_clear_pointer (&priv->overlay_rect, gst_video_overlay_rectangle_unref);
  }

  priv->layout = nullptr;
  priv->layout = layout;

  if (priv->layout_buf)
    return TRUE;

  priv->layout_buf = gst_dwrite_render_draw_layout (priv->render, layout, x, y);

  if (!priv->layout_buf) {
    GST_ERROR_OBJECT (self, "Couldn't create layout buffer");
    return FALSE;
  }

  width = (gint) layout->GetMaxWidth ();
  height = (gint) layout->GetMaxHeight ();

  priv->overlay_rect = gst_video_overlay_rectangle_new_raw (priv->layout_buf,
      x, y, width, height, GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

  return TRUE;
}

static gboolean
gst_dwrite_overlay_object_mode_attach (GstDWriteOverlayObject * self,
    GstBuffer * buffer)
{
  auto priv = self->priv;
  GstVideoOverlayCompositionMeta *meta;

  meta = gst_buffer_get_video_overlay_composition_meta (buffer);
  if (meta) {
    if (meta->overlay) {
      meta->overlay =
          gst_video_overlay_composition_make_writable (meta->overlay);
      gst_video_overlay_composition_add_rectangle (meta->overlay,
          priv->overlay_rect);
    } else {
      meta->overlay = gst_video_overlay_composition_new (priv->overlay_rect);
    }
  } else {
    GstVideoOverlayComposition *comp =
        gst_video_overlay_composition_new (priv->overlay_rect);
    meta = gst_buffer_add_video_overlay_composition_meta (buffer, comp);
    gst_video_overlay_composition_unref (comp);
  }

  return TRUE;
}

gboolean
gst_dwrite_overlay_object_draw (GstDWriteOverlayObject * object,
    GstBuffer * buffer, IDWriteTextLayout * layout, gint x, gint y)
{
  auto priv = object->priv;
  gboolean ret = FALSE;

  if (!gst_dwrite_overlay_object_draw_layout (object, layout, x, y))
    return FALSE;

  if (priv->attach_meta)
    ret = gst_dwrite_overlay_object_mode_attach (object, buffer);
  else
    ret = gst_dwrite_render_blend (priv->render, priv->layout_buf,
        x, y, buffer);

  return ret;
}

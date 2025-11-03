/* GStreamer * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12overlaycompositor.h"
#include "gstd3d12overlayblender.h"
#include "gstd3d12pluginutils.h"
#include <memory>
#include <wrl.h>
#include <directx/d3dx12.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_overlay_compositor_debug);
#define GST_CAT_DEFAULT gst_d3d12_overlay_compositor_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)));

enum BlendMode
{
  BLEND_MODE_PASSTHROUGH,
  BLEND_MODE_BLEND,
  BLEND_MODE_CONVERT_BLEND,
};

/* *INDENT-OFF* */
struct OverlayBlendCtx
{
  OverlayBlendCtx (GstD3D12Device * dev)
  {
    device = (GstD3D12Device *) gst_object_ref (dev);
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
  }

  ~OverlayBlendCtx ()
  {
    if (fence_val > 0) {
      gst_d3d12_device_fence_wait (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
          fence_val);
    }

    if (blend_pool)
      gst_buffer_pool_set_active (blend_pool, FALSE);

    gst_clear_object (&blend_pool);
    gst_clear_object (&ca_pool);
    gst_clear_object (&pre_conv);
    gst_clear_object (&post_conv);
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;
  ComPtr<ID3D12GraphicsCommandList> cl;
  GstD3D12CmdAllocPool *ca_pool;
  guint64 fence_val = 0;

  GstD3D12OverlayBlender *blender = nullptr;
  GstBufferPool *blend_pool = nullptr;
  GstVideoInfo origin_info;
  GstVideoInfo blend_info;
  GstD3D12Converter *pre_conv = nullptr;
  GstD3D12Converter *post_conv = nullptr;
};

struct GstD3D12OverlayCompositorPrivate
{
  GstD3D12OverlayCompositorPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12OverlayCompositorPrivate ()
  {
    gst_object_unref (fence_data_pool);
  }

  GstD3D12FenceDataPool *fence_data_pool;

  std::shared_ptr<OverlayBlendCtx> ctx;
  gboolean downstream_supports_meta = FALSE;
  BlendMode blend_mode = BLEND_MODE_PASSTHROUGH;
};
/* *INDENT-ON* */

struct _GstD3D12OverlayCompositor
{
  GstD3D12BaseFilter parent;

  GstD3D12OverlayCompositorPrivate *priv;
};

#define gst_d3d12_overlay_compositor_parent_class parent_class
G_DEFINE_TYPE (GstD3D12OverlayCompositor,
    gst_d3d12_overlay_compositor, GST_TYPE_D3D12_BASE_FILTER);

static void gst_d3d12_overlay_compositor_finalize (GObject * object);
static gboolean gst_d3d12_overlay_compositor_stop (GstBaseTransform * trans);
static GstCaps *gst_d3d12_overlay_compositor_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d12_overlay_compositor_fixate_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static GstFlowReturn gst_d3d12_overlay_compositor_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn
gst_d3d12_overlay_compositor_generate_output (GstBaseTransform * trans,
    GstBuffer ** buffer);
static gboolean gst_d3d12_overlay_compositor_set_info (GstD3D12BaseFilter *
    filter, GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info);
static gboolean
gst_d3d12_overlay_compositor_propose_allocation (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstQuery * decide_query, GstQuery * query);

static void
gst_d3d12_overlay_compositor_class_init (GstD3D12OverlayCompositorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_D3D12_BASE_FILTER_CLASS (klass);

  object_class->finalize = gst_d3d12_overlay_compositor_finalize;

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Overlay Compositor", "Filter/Effect/Video/Hardware",
      "Blend overlay into stream", "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class, &sink_template);

  trans_class->passthrough_on_same_caps = FALSE;

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_fixate_caps);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_transform);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_generate_output);

  filter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_set_info);
  filter_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_overlay_compositor_propose_allocation);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_overlay_compositor_debug,
      "d3d12overlaycompositor", 0, "d3d12overlaycompositor");
}

static void
gst_d3d12_overlay_compositor_init (GstD3D12OverlayCompositor * self)
{
  self->priv = new GstD3D12OverlayCompositorPrivate ();
}

static void
gst_d3d12_overlay_compositor_finalize (GObject * object)
{
  auto self = GST_D3D12_OVERLAY_COMPOSITOR (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_d3d12_overlay_compositor_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_OVERLAY_COMPOSITOR (trans);
  auto priv = self->priv;

  priv->ctx = nullptr;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_d3d12_overlay_compositor_propose_allocation (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstQuery * decide_query, GstQuery * query)
{
  if (!GST_D3D12_BASE_FILTER_CLASS (parent_class)->propose_allocation (filter,
          device, decide_query, query)) {
    return FALSE;
  }

  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  return TRUE;
}

static GstCaps *
add_feature (GstCaps * caps)
{
  auto new_caps = gst_caps_new_empty ();
  auto caps_size = gst_caps_get_size (caps);

  for (guint i = 0; i < caps_size; i++) {
    auto s = gst_caps_get_structure (caps, i);
    auto f = gst_caps_features_copy (gst_caps_get_features (caps, i));
    auto c = gst_caps_new_full (gst_structure_copy (s), nullptr);

    if (!gst_caps_features_is_any (f) &&
        !gst_caps_features_contains (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      gst_caps_features_add (f,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    }

    gst_caps_set_features (c, 0, f);
    gst_caps_append (new_caps, c);
  }

  return new_caps;
}

static GstCaps *
remove_feature (GstCaps * caps)
{
  auto new_caps = gst_caps_new_empty ();
  auto caps_size = gst_caps_get_size (caps);

  for (guint i = 0; i < caps_size; i++) {
    auto s = gst_caps_get_structure (caps, i);
    auto f = gst_caps_features_copy (gst_caps_get_features (caps, i));
    auto c = gst_caps_new_full (gst_structure_copy (s), nullptr);

    gst_caps_features_remove (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    gst_caps_set_features (c, 0, f);
    gst_caps_append (new_caps, c);
  }

  return new_caps;
}

static GstCaps *
gst_d3d12_overlay_compositor_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmp = remove_feature (caps);
    tmp = gst_caps_merge (tmp, gst_caps_ref (caps));
  } else {
    tmp = add_feature (caps);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  }

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_d3d12_overlay_compositor_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *overlay_caps = nullptr;
  auto caps_size = gst_caps_get_size (othercaps);
  GstCaps *ret;

  GST_DEBUG_OBJECT (trans, "Fixate caps in direction %s, caps %"
      GST_PTR_FORMAT ", other caps %" GST_PTR_FORMAT,
      (direction == GST_PAD_SINK) ? "sink" : "src", caps, othercaps);

  /* Prefer overlaycomposition caps */
  for (guint i = 0; i < caps_size; i++) {
    auto f = gst_caps_get_features (othercaps, i);

    if (f && !gst_caps_features_is_any (f) &&
        gst_caps_features_contains (f,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
      auto s = gst_caps_get_structure (othercaps, i);
      overlay_caps = gst_caps_new_full (gst_structure_copy (s), nullptr);
      gst_caps_set_features_simple (overlay_caps, gst_caps_features_copy (f));
      break;
    }
  }

  if (overlay_caps) {
    gst_caps_unref (othercaps);
    ret = gst_caps_fixate (overlay_caps);
  } else {
    ret = gst_caps_fixate (othercaps);
  }

  GST_DEBUG_OBJECT (trans, "Fixated caps %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_d3d12_overlay_compositor_set_info (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  auto self = GST_D3D12_OVERLAY_COMPOSITOR (filter);
  auto priv = self->priv;

  priv->ctx = nullptr;
  priv->blend_mode = BLEND_MODE_PASSTHROUGH;

  auto features = gst_caps_get_features (outcaps, 0);
  if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
    /* Let downstream blend */
    priv->blend_mode = BLEND_MODE_PASSTHROUGH;
  } else {
    auto format = GST_VIDEO_INFO_FORMAT (in_info);
    GstVideoFormat blend_format = GST_VIDEO_FORMAT_UNKNOWN;
    GstVideoColorRange range = GST_VIDEO_COLOR_RANGE_0_255;

    switch (format) {
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_RGBA64_LE:
      case GST_VIDEO_FORMAT_VUYA:
        priv->blend_mode = BLEND_MODE_BLEND;
        range = in_info->colorimetry.range;
        blend_format = format;
        break;
      default:
        priv->blend_mode = BLEND_MODE_CONVERT_BLEND;
        if (GST_VIDEO_INFO_IS_YUV (in_info)) {
          if (GST_VIDEO_INFO_COMP_DEPTH (in_info, 0) <= 8)
            blend_format = GST_VIDEO_FORMAT_VUYA;
          else
            blend_format = GST_VIDEO_FORMAT_RGBA64_LE;
        } else {
          if (GST_VIDEO_INFO_COMP_DEPTH (in_info, 0) <= 8)
            blend_format = GST_VIDEO_FORMAT_RGBA;
          else
            blend_format = GST_VIDEO_FORMAT_RGBA64_LE;
        }
        break;
    }

    auto ctx = std::make_shared < OverlayBlendCtx > (device);
    ctx->origin_info = *in_info;

    gst_video_info_set_format (&ctx->blend_info, blend_format,
        in_info->width, in_info->height);
    ctx->blend_info.colorimetry.range = range;

    ctx->blender = gst_d3d12_overlay_blender_new (device, &ctx->blend_info);
    if (priv->blend_mode == BLEND_MODE_CONVERT_BLEND) {
      ctx->pre_conv = gst_d3d12_converter_new (device,
          nullptr, &ctx->origin_info, &ctx->blend_info, nullptr, nullptr,
          nullptr);
      ctx->post_conv = gst_d3d12_converter_new (device,
          nullptr, &ctx->blend_info, &ctx->origin_info, nullptr, nullptr,
          nullptr);
    }

    auto blend_caps = gst_video_info_to_caps (&ctx->blend_info);

    ctx->blend_pool = gst_d3d12_buffer_pool_new (device);
    auto config = gst_buffer_pool_get_config (ctx->blend_pool);
    gst_buffer_pool_config_set_params (config, blend_caps, 0, 0, 0);
    gst_caps_unref (blend_caps);

    if (!gst_buffer_pool_set_config (ctx->blend_pool, config)) {
      GST_ERROR_OBJECT (self, "Couldn't set config");
      return FALSE;
    }

    if (!gst_buffer_pool_set_active (ctx->blend_pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Couldn't set config");
      return FALSE;
    }

    priv->ctx = ctx;
  }

  GST_DEBUG_OBJECT (self, "Selected blend mode: %d", priv->blend_mode);

  return TRUE;
}

static gboolean
foreach_meta (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  if ((*meta)->info->api == GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE)
    *meta = nullptr;

  return TRUE;
}

static gboolean
buffer_has_overlay_rect (GstBuffer * buf)
{
  gboolean has_rect = FALSE;
  gpointer state = nullptr;
  GstMeta *meta;
  while ((meta = gst_buffer_iterate_meta_filtered (buf, &state,
              GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE)) != nullptr) {
    auto ometa = (GstVideoOverlayCompositionMeta *) meta;
    if (gst_video_overlay_composition_n_rectangles (ometa->overlay) > 0) {
      has_rect = TRUE;
      break;
    }
  }

  return has_rect;
}

static GstFlowReturn
gst_d3d12_overlay_compositor_generate_output (GstBaseTransform * trans,
    GstBuffer ** buffer)
{
  auto self = GST_D3D12_OVERLAY_COMPOSITOR (trans);
  auto priv = self->priv;

  if (!trans->queued_buf)
    return GST_FLOW_OK;

  auto buf = trans->queued_buf;
  trans->queued_buf = nullptr;

  auto has_rect = buffer_has_overlay_rect (buf);
  if (priv->blend_mode == BLEND_MODE_PASSTHROUGH || !has_rect) {
    *buffer = buf;
    return GST_FLOW_OK;
  }

  auto & ctx = priv->ctx;
  gst_d3d12_overlay_blender_upload (ctx->blender, buf);

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (ctx->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_cmd_alloc_unref (gst_ca);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  if (!ctx->cl) {
    auto device = gst_d3d12_device_get_device_handle (ctx->device);
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&ctx->cl));
    if (!gst_d3d12_result (hr, priv->ctx->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create command list");
      gst_d3d12_cmd_alloc_unref (gst_ca);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }
  } else {
    hr = ctx->cl->Reset (ca, nullptr);
    if (!gst_d3d12_result (hr, ctx->device)) {
      GST_ERROR_OBJECT (self, "Couldn't reset command list");
      gst_d3d12_cmd_alloc_unref (gst_ca);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  buf = gst_buffer_make_writable (buf);
  if (priv->blend_mode == BLEND_MODE_BLEND) {
    /* Ensure writable memory */
    GstD3D12Frame frame;
    if (!gst_d3d12_frame_map (&frame, &priv->ctx->origin_info, buf,
            GST_MAP_WRITE_D3D12, GST_D3D12_FRAME_MAP_FLAG_RTV)) {
      GST_WARNING_OBJECT (self, "Couldn't map buffer");
      GstBuffer *fallback_buf = nullptr;
      gst_buffer_pool_acquire_buffer (ctx->blend_pool, &fallback_buf, nullptr);
      if (!fallback_buf) {
        GST_ERROR_OBJECT (self, "Couldn't acquire fallback buffer");
        ctx->cl->Close ();
        gst_d3d12_fence_data_unref (fence_data);
        gst_buffer_unref (buf);
        return GST_FLOW_ERROR;
      }

      if (!gst_d3d12_buffer_copy_into (fallback_buf, buf, &ctx->origin_info)) {
        GST_ERROR_OBJECT (self, "Couldn't copy to fallback buffer");
        ctx->cl->Close ();
        gst_d3d12_fence_data_unref (fence_data);
        gst_buffer_unref (buf);
        gst_buffer_unref (fallback_buf);
        return GST_FLOW_ERROR;
      }

      gst_buffer_copy_into (fallback_buf, buf, GST_BUFFER_COPY_METADATA, 0, -1);
      gst_buffer_unref (buf);
      buf = fallback_buf;
    } else {
      gst_d3d12_frame_unmap (&frame);
    }

    gst_d3d12_overlay_blender_draw (ctx->blender,
        buf, fence_data, ctx->cl.Get ());
  } else {
    GstBuffer *blend_buf = nullptr;
    GstBuffer *out_buf = nullptr;

    gst_buffer_pool_acquire_buffer (ctx->blend_pool, &blend_buf, nullptr);
    if (!blend_buf) {
      GST_ERROR_OBJECT (self, "Couldn't acquire blend buffer");
      ctx->cl->Close ();
      gst_d3d12_fence_data_unref (fence_data);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    auto ret =
        GST_BASE_TRANSFORM_CLASS (parent_class)->prepare_output_buffer (trans,
        buf, &out_buf);
    if (ret != GST_FLOW_OK) {
      ctx->cl->Close ();
      gst_d3d12_fence_data_unref (fence_data);
      gst_buffer_unref (buf);
      gst_buffer_unref (blend_buf);
      return ret;
    }

    gst_d3d12_converter_convert_buffer (ctx->pre_conv, buf, blend_buf,
        fence_data, ctx->cl.Get (), TRUE);
    gst_d3d12_overlay_blender_draw (ctx->blender,
        blend_buf, fence_data, ctx->cl.Get ());

    auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (blend_buf, 0);
    auto resource = gst_d3d12_memory_get_resource_handle (dmem);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition (resource,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ctx->cl->ResourceBarrier (1, &barrier);

    gst_d3d12_converter_convert_buffer (ctx->post_conv, blend_buf, out_buf,
        fence_data, ctx->cl.Get (), FALSE);

    /* fence data will hold all source buffers */
    gst_buffer_unref (buf);
    gst_buffer_unref (blend_buf);

    buf = out_buf;
  }

  hr = ctx->cl->Close ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cmd_list[] = { priv->ctx->cl.Get () };

  hr = gst_d3d12_device_execute_command_lists (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT, 1, cmd_list, &ctx->fence_val);
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }

  auto fence = gst_d3d12_device_get_fence_handle (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  gst_d3d12_buffer_set_fence (buf, fence, priv->ctx->fence_val, FALSE);
  gst_d3d12_device_set_fence_notify (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT, priv->ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  gst_buffer_foreach_meta (buf, foreach_meta, nullptr);

  *buffer = buf;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_overlay_compositor_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  g_assert_not_reached ();

  return GST_FLOW_ERROR;
}

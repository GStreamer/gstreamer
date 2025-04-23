/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12remap.h"
#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <memory>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_remap_debug);
#define GST_CAT_DEFAULT gst_d3d12_remap_debug

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

enum
{
  PROP_0,
  PROP_UV_REMAP,
};

/* *INDENT-OFF* */
struct RemapContext
{
  ~RemapContext()
  {
    if (fence_val) {
      gst_d3d12_device_fence_wait (device,
          D3D12_COMMAND_LIST_TYPE_DIRECT, fence_val);
    }

    gst_clear_object (&conv);
    gst_clear_object (&ca_pool);
    gst_clear_object (&device);
  }

  ComPtr<ID3D12GraphicsCommandList> cl;
  ID3D12Fence *cq_fence;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12Device *device = nullptr;
  GstD3D12CmdQueue *cq = nullptr;
  guint64 fence_val = 0;
  GstD3D12Converter *conv = nullptr;
};

struct GstD3D12RemapPrivate
{
  GstD3D12RemapPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12RemapPrivate ()
  {
    gst_clear_object (&fence_data_pool);
  }

  GstD3D12FenceDataPool *fence_data_pool;

  std::shared_ptr<RemapContext> ctx;
  ComPtr<ID3D12Resource> uv_remap;

  std::mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12Remap
{
  GstD3D12BaseFilter parent;

  GstD3D12RemapPrivate *priv;
};

static void gst_d3d12_remap_finalize (GObject * object);
static void gst_d3d12_remap_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_remap_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_d3d12_remap_stop (GstBaseTransform * trans);
static gboolean gst_d3d12_remap_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_remap_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_d3d12_remap_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_d3d12_remap_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d12_remap_set_info (GstD3D12BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

#define gst_d3d12_remap_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Remap, gst_d3d12_remap, GST_TYPE_D3D12_BASE_FILTER);

static void
gst_d3d12_remap_class_init (GstD3D12RemapClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_D3D12_BASE_FILTER_CLASS (klass);

  object_class->set_property = gst_d3d12_remap_set_property;
  object_class->get_property = gst_d3d12_remap_get_property;
  object_class->finalize = gst_d3d12_remap_finalize;

  g_object_class_install_property (object_class, PROP_UV_REMAP,
      g_param_spec_pointer ("uv-remap", "UV Remap",
          "ID3D12Resource for UV coordinates remapping. Valid formats are "
          "R8G8B8A8_UNORM and R16G16B16A16_UNORM. R -> U, "
          "G -> U, B -> unused, and A -> mask where A >= 0.5 "
          "applies remapping, otherwise fill background color",
          (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Remap", "Filter/Converter/Video/Hardware",
      "Remap pixels", "Seungha Yang <seungha@centricular.com>");

  trans_class->passthrough_on_same_caps = FALSE;

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_remap_stop);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_remap_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_remap_decide_allocation);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_remap_transform_meta);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_remap_transform);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_d3d12_remap_set_info);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_SAMPLING_METHOD,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_remap_debug, "d3d12remap", 0,
      "d3d12remap");
}

static void
gst_d3d12_remap_init (GstD3D12Remap * self)
{
  self->priv = new GstD3D12RemapPrivate ();
}

static void
gst_d3d12_remap_finalize (GObject * object)
{
  auto self = GST_D3D12_REMAP (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_remap_set_remap_resource (GstD3D12Remap * self)
{
  auto priv = self->priv;

  if (!priv->ctx)
    return;

  if (priv->uv_remap) {
    ComPtr < ID3D12Device > other_device;
    priv->uv_remap->GetDevice (IID_PPV_ARGS (&other_device));
    auto other_device_luid = GetAdapterLuid (other_device);

    auto device = gst_d3d12_device_get_device_handle (priv->ctx->device);
    auto device_luid = GetAdapterLuid (device);

    if (other_device_luid.HighPart != device_luid.HighPart ||
        other_device_luid.LowPart != device_luid.LowPart) {
      GST_ERROR_OBJECT (self, "Remap resource belongs to other device");
    } else {
      gst_d3d12_converter_set_remap (priv->ctx->conv, priv->uv_remap.Get ());
    }
  } else {
    gst_d3d12_converter_set_remap (priv->ctx->conv, nullptr);
  }
}

static void
gst_d3d12_remap_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_REMAP (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_UV_REMAP:
      priv->uv_remap = (ID3D12Resource *) g_value_get_pointer (value);
      if (priv->uv_remap) {
        auto desc = GetDesc (priv->uv_remap);
        if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM
            && desc.Format != DXGI_FORMAT_R16G16B16A16_UNORM) {
          GST_ERROR_OBJECT (self,
              "Not supported format %d", (guint) desc.Format);
          priv->uv_remap = nullptr;
        }
      }

      gst_d3d12_remap_set_remap_resource (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_remap_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_REMAP (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  switch (prop_id) {
    case PROP_UV_REMAP:
      g_value_set_pointer (value, priv->uv_remap.Get ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d12_remap_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_REMAP (trans);
  auto priv = self->priv;

  priv->ctx = nullptr;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static gboolean
gst_d3d12_remap_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto filter = GST_D3D12_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCaps *caps;
  guint n_pools, i;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query)) {
    return FALSE;
  }

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, nullptr, nullptr,
        nullptr);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, filter->device))
          gst_clear_object (&pool);
      }
    }
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (filter->device);

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (filter->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_NONE);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (d3d12_params,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    gst_d3d12_allocation_params_unset_resource_flags (d3d12_params,
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
  gst_d3d12_allocation_params_free (d3d12_params);

  /* size will be updated by d3d12 buffer pool */
  gst_buffer_pool_config_set_params (config, caps, 0, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_d3d12_remap_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  auto filter = GST_D3D12_BASE_FILTER (trans);
  GstCaps *outcaps = nullptr;
  GstBufferPool *pool = nullptr;
  guint size, min = 0, max = 0;
  GstStructure *config;
  gboolean update_pool = FALSE;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &outcaps, nullptr);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  GstD3D12Format device_format;
  if (!gst_d3d12_device_get_format (filter->device,
          GST_VIDEO_INFO_FORMAT (&info), &device_format)) {
    GST_ERROR_OBJECT (filter, "Couldn't get device foramt");
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, filter->device))
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  D3D12_RESOURCE_FLAGS resource_flags =
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  if ((device_format.format_flags & GST_D3D12_FORMAT_FLAG_OUTPUT_UAV)
      == GST_D3D12_FORMAT_FLAG_OUTPUT_UAV) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  if ((device_format.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (filter->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
        D3D12_HEAP_FLAG_SHARED);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (d3d12_params,
        resource_flags);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
  gst_d3d12_allocation_params_free (d3d12_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_d3d12_remap_set_info (GstD3D12BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  auto self = GST_D3D12_REMAP (filter);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  priv->ctx = nullptr;

  auto ctx = std::make_shared < RemapContext > ();
  ctx->device = (GstD3D12Device *) gst_object_ref (filter->device);
  auto device = gst_d3d12_device_get_device_handle (filter->device);
  ctx->ca_pool = gst_d3d12_cmd_alloc_pool_new (device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  ctx->cq = gst_d3d12_device_get_cmd_queue (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  ctx->cq_fence = gst_d3d12_cmd_queue_get_fence_handle (ctx->cq);
  ctx->conv = gst_d3d12_converter_new (ctx->device, nullptr,
      in_info, out_info, nullptr, nullptr, nullptr);

  priv->ctx = ctx;
  gst_d3d12_remap_set_remap_resource (self);

  return TRUE;
}

static gboolean
gst_d3d12_remap_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  if (meta->info->api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
      outbuf, meta, inbuf);
}

static GstFlowReturn
gst_d3d12_remap_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_D3D12_REMAP (trans);
  auto priv = self->priv;
  GstD3D12CmdAlloc *gst_ca;
  GstD3D12FenceData *fence_data;
  auto ctx = priv->ctx;
  HRESULT hr;

  if (!ctx) {
    GST_ERROR_OBJECT (self, "Context is not configured");
    return GST_FLOW_ERROR;
  }

  auto device = gst_d3d12_device_get_device_handle (ctx->device);

  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  if (!gst_d3d12_cmd_alloc_pool_acquire (ctx->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  hr = ca->Reset ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  if (!ctx->cl) {
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&priv->ctx->cl));
  } else {
    hr = ctx->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  if (!gst_d3d12_converter_convert_buffer (ctx->conv, inbuf, outbuf, fence_data,
          ctx->cl.Get (), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't convert buffer");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  hr = ctx->cl->Close ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    gst_d3d12_fence_data_unref (fence_data);
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cl[] = { ctx->cl.Get () };
  gst_d3d12_cmd_queue_execute_command_lists (ctx->cq, 1, cl, &ctx->fence_val);

  gst_d3d12_cmd_queue_set_notify (ctx->cq, ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  gst_d3d12_buffer_set_fence (outbuf, ctx->cq_fence, ctx->fence_val, FALSE);

  return GST_FLOW_OK;
}

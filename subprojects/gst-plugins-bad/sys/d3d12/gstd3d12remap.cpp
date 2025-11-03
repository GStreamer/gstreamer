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
static gboolean gst_d3d12_remap_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_d3d12_remap_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d12_remap_set_info (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info);
static gboolean gst_d3d12_remap_propose_allocation (GstD3D12BaseFilter *
    filter, GstD3D12Device * device, GstQuery * decide_query, GstQuery * query);

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
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_remap_transform_meta);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_remap_transform);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_d3d12_remap_set_info);
  filter_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_remap_propose_allocation);

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
gst_d3d12_remap_propose_allocation (GstD3D12BaseFilter * filter,
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

static gboolean
gst_d3d12_remap_set_info (GstD3D12BaseFilter * filter, GstD3D12Device * device,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  auto self = GST_D3D12_REMAP (filter);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  priv->ctx = nullptr;

  auto ctx = std::make_shared < RemapContext > ();
  ctx->device = (GstD3D12Device *) gst_object_ref (device);
  auto device_handle = gst_d3d12_device_get_device_handle (device);
  ctx->ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
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

/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

/**
 * SECTION:element-d3d12alphacombine
 * @title: d3d12alphacombine
 *
 * A Direct3D12-based element that combines a YUV stream and a separate
 * alpha stream into a single YUV + alpha output.
 *
 * This element is intended for internal use by codec alpha decoders,
 * such as d3d12vp9alphadecodebin
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>
#include "gstd3d12alphacombine.h"
#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <string.h>
#include <gst/d3dshader/gstd3dshader.h>
#include <DirectXMath.h>
#include <wrl.h>
#include <math.h>
#include <memory>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_alpha_combine_debug);
#define GST_CAT_DEFAULT gst_d3d12_alpha_combine_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
using namespace DirectX;
/* *INDENT-ON* */

/* *INDENT-OFF* */
struct CopyParams
{
  XMUINT2 AlphaTexSize;
  XMUINT2 AlphaViewSize;
  XMUINT2 DstSize;
  XMUINT2 MainTexSize;
  XMUINT2 MainViewSize;
  UINT FillAlpha;
  UINT padding;
};

G_STATIC_ASSERT (sizeof (CopyParams) % 16 == 0);

struct CombineContext
{
  ~CombineContext()
  {
    if (fence_val) {
      gst_d3d12_device_fence_wait (device,
          D3D12_COMMAND_LIST_TYPE_DIRECT, fence_val);
    }

    gst_clear_object (&ca_pool);
    gst_clear_object (&desc_pool);
    gst_clear_object (&device);
  }

  ComPtr<ID3D12GraphicsCommandList> cl;
  ComPtr<ID3D12RootSignature> rs_nv12;
  ComPtr<ID3D12PipelineState> pso_nv12_to_r8_load;
  ComPtr<ID3D12PipelineState> pso_nv12_to_r8_sample;

  ComPtr<ID3D12RootSignature> rs_p010;
  ComPtr<ID3D12PipelineState> pso_p010_to_a420_10_load;
  ComPtr<ID3D12PipelineState> pso_p010_to_a420_10_sample;

  CopyParams cbuf;

  ID3D12Fence *cq_fence;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12DescHeapPool *desc_pool = nullptr;
  GstD3D12Device *device = nullptr;
  GstD3D12CmdQueue *cq = nullptr;
  guint srv_inc_size;
  guint64 fence_val = 0;
};

struct GstD3D12AlphaCombinePrivate
{
  GstD3D12AlphaCombinePrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

   ~GstD3D12AlphaCombinePrivate ()
  {
    clear_pool ();
    gst_clear_caps (&caps);
    gst_clear_object (&device);
    gst_clear_object (&fence_data_pool);
  }

  void clear_pool ()
  {
    if (out_alloc)
      gst_d3d12_allocator_set_active (GST_D3D12_ALLOCATOR (out_alloc), FALSE);
    gst_clear_object (&out_alloc);

    if (out_pool)
      gst_buffer_pool_set_active (out_pool, FALSE);
    gst_clear_object (&out_pool);
  }

  std::recursive_mutex lock;
  GstD3D12PoolAllocator *out_alloc = nullptr;
  GstBufferPool *out_pool = nullptr;
  GstD3D12Device *device = nullptr;
  GstCaps *caps = nullptr;
  GstVideoInfo main_info;
  GstVideoInfo alpha_info;
  GstVideoInfo out_info;
  guint dispatch_x;
  guint dispatch_y;
  GstD3D12FenceDataPool *fence_data_pool;
  std::shared_ptr<CombineContext> ctx;
  D3D12_RESOURCE_DESC out_alpha_desc = { };
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT out_alpha_layout = { };
  GstClockTime timeout_advance;
};
/* *INDENT-ON* */

struct _GstD3D12AlphaCombine
{
  GstAggregator parent;

  GstAggregatorPad *sinkpad;
  GstAggregatorPad *alphapad;

  GstD3D12AlphaCombinePrivate *priv;
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, "{ NV12, P010_10LE }")));

static GstStaticPadTemplate alpha_template =
GST_STATIC_PAD_TEMPLATE ("alpha", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, "{ NV12, P010_10LE }")));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, "{ AV12, A420_10LE }")));

static void gst_d3d12_alpha_combine_finalize (GObject * object);

static void gst_d3d12_alpha_combine_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d12_alpha_combine_start (GstAggregator * agg);
static gboolean gst_d3d12_alpha_combine_stop (GstAggregator * agg);
static gboolean gst_d3d12_alpha_combine_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query);
static gboolean gst_d3d12_alpha_combine_src_query (GstAggregator * agg,
    GstQuery * query);
static gboolean
gst_d3d12_alpha_combine_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_alpha_combine_sink_event (GstAggregator * agg,
    GstAggregatorPad * agg_pad, GstEvent * event);
static gboolean gst_d3d12_alpha_combine_negotiate (GstAggregator * agg);
static GstFlowReturn
gst_d3d12_alpha_combine_aggregate (GstAggregator * agg, gboolean timeout);

#define gst_d3d12_alpha_combine_parent_class parent_class
G_DEFINE_TYPE (GstD3D12AlphaCombine, gst_d3d12_alpha_combine,
    GST_TYPE_AGGREGATOR);

static void
gst_d3d12_alpha_combine_class_init (GstD3D12AlphaCombineClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto agg_class = GST_AGGREGATOR_CLASS (klass);

  object_class->finalize = gst_d3d12_alpha_combine_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_set_context);

  agg_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_start);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_stop);
  agg_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_sink_query);
  agg_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_src_query);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_propose_allocation);
  agg_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_sink_event);
  agg_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_negotiate);
  agg_class->get_next_time =
      GST_DEBUG_FUNCPTR (gst_aggregator_simple_get_next_time);
  agg_class->aggregate = GST_DEBUG_FUNCPTR (gst_d3d12_alpha_combine_aggregate);

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_template, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &alpha_template, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Alpha Combine", "Filter/Editor/Video/Compositor",
      "A Direct3D12 alpha combiner", "Seungha Yang <seungha@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_alpha_combine_debug,
      "d3d12alphacombine", 0, "d3d12alphacombine");
}

static void
gst_d3d12_alpha_combine_init (GstD3D12AlphaCombine * self)
{
  self->priv = new GstD3D12AlphaCombinePrivate ();

  auto templ = gst_static_pad_template_get (&sink_template);
  self->sinkpad = (GstAggregatorPad *) g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", "sink", "direction", GST_PAD_SINK, "template", templ, NULL);
  gst_element_add_pad (GST_ELEMENT_CAST (self), GST_PAD (self->sinkpad));
  gst_object_unref (templ);

  templ = gst_static_pad_template_get (&alpha_template);
  self->alphapad = (GstAggregatorPad *) g_object_new (GST_TYPE_AGGREGATOR_PAD,
      "name", "alpha", "direction", GST_PAD_SINK, "template", templ, NULL);
  gst_element_add_pad (GST_ELEMENT_CAST (self), GST_PAD (self->alphapad));
  gst_object_unref (templ);
}

static void
gst_d3d12_alpha_combine_finalize (GObject * object)
{
  auto self = GST_D3D12_ALPHA_COMBINE (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_alpha_combine_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_D3D12_ALPHA_COMBINE (element);
  auto priv = self->priv;

  {
    /* TODO: use luid */
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_d3d12_handle_set_context (element, context, -1, &priv->device);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static HRESULT
gst_d3d12_alpha_combine_get_nv12_rs_blob (GstD3D12Device * device,
    ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob = nullptr;
  static HRESULT hr = S_OK;

  GST_D3D12_CALL_ONCE_BEGIN {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
    CD3DX12_ROOT_PARAMETER root_params[3];
    CD3DX12_DESCRIPTOR_RANGE range_srv;
    CD3DX12_DESCRIPTOR_RANGE range_uav;
    CD3DX12_STATIC_SAMPLER_DESC sampler;

    root_params[0].InitAsConstants (sizeof (CopyParams) / 4, 0);

    range_uav.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    root_params[1].InitAsDescriptorTable (1, &range_uav);

    range_srv.Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    root_params[2].InitAsDescriptorTable (1, &range_srv);

    sampler.Init (0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0, 1, D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0, D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 3, root_params,
        1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS);

    ComPtr < ID3DBlob > error_blob;
    hr = D3DX12SerializeVersionedRootSignature (&desc,
        D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &error_blob);
    if (!gst_d3d12_result (hr, device)) {
      const gchar *error_msg = nullptr;
      if (error_blob)
        error_msg = (const gchar *) error_blob->GetBufferPointer ();

      GST_ERROR_OBJECT (device,
          "Couldn't serialize rs, hr: 0x%x, error detail: %s",
          (guint) hr, GST_STR_NULL (error_msg));
    }
  } GST_D3D12_CALL_ONCE_END;

  if (rs_blob) {
    *blob = rs_blob;
    rs_blob->AddRef ();
  }

  return hr;
}

static HRESULT
gst_d3d12_alpha_combine_get_p010_rs_blob (GstD3D12Device * device,
    ID3DBlob ** blob)
{
  static ID3DBlob *rs_blob = nullptr;
  static HRESULT hr = S_OK;

  GST_D3D12_CALL_ONCE_BEGIN {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = { };
    CD3DX12_ROOT_PARAMETER root_params[3];
    CD3DX12_DESCRIPTOR_RANGE range_srv;
    CD3DX12_DESCRIPTOR_RANGE range_uav;
    CD3DX12_STATIC_SAMPLER_DESC sampler;

    root_params[0].InitAsConstants (sizeof (CopyParams) / 4, 0);

    range_uav.Init (D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0);
    root_params[1].InitAsDescriptorTable (1, &range_uav);

    range_srv.Init (D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
    root_params[2].InitAsDescriptorTable (1, &range_srv);

    sampler.Init (0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0, 1, D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0, D3D12_FLOAT32_MAX,
        D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC::Init_1_0 (desc, 3, root_params,
        1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS);

    ComPtr < ID3DBlob > error_blob;
    hr = D3DX12SerializeVersionedRootSignature (&desc,
        D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, &error_blob);
    if (!gst_d3d12_result (hr, device)) {
      const gchar *error_msg = nullptr;
      if (error_blob)
        error_msg = (const gchar *) error_blob->GetBufferPointer ();

      GST_ERROR_OBJECT (device,
          "Couldn't serialize rs, hr: 0x%x, error detail: %s",
          (guint) hr, GST_STR_NULL (error_msg));
    }
  } GST_D3D12_CALL_ONCE_END;

  if (rs_blob) {
    *blob = rs_blob;
    rs_blob->AddRef ();
  }

  return hr;
}

static gboolean
gst_d3d12_alpha_combine_start (GstAggregator * agg)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    /* TODO: use luid */
    if (!gst_d3d12_ensure_element_data (GST_ELEMENT_CAST (self),
            -1, &priv->device)) {
      GST_ERROR_OBJECT (self, "Failed to get D3D12 device");
      return FALSE;
    }
  }

  auto ctx = std::make_shared < CombineContext > ();
  ctx->device = (GstD3D12Device *) gst_object_ref (priv->device);
  auto device_handle = gst_d3d12_device_get_device_handle (ctx->device);
  ctx->ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  D3D12_DESCRIPTOR_HEAP_DESC desc_heap_desc = { };
  desc_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  /* Need up to 7 descriptors in case of P010, SRV 3 + UAV 4 */
  desc_heap_desc.NumDescriptors = 7;
  desc_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ctx->desc_pool =
      gst_d3d12_desc_heap_pool_new (device_handle, &desc_heap_desc);
  ctx->cq = gst_d3d12_device_get_cmd_queue (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  ctx->cq_fence = gst_d3d12_cmd_queue_get_fence_handle (ctx->cq);

  ComPtr < ID3DBlob > rs_blob;
  auto hr = gst_d3d12_alpha_combine_get_nv12_rs_blob (priv->device, &rs_blob);
  if (!gst_d3d12_result (hr, priv->device))
    return FALSE;

  hr = device_handle->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&ctx->rs_nv12));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create root signature");
    return FALSE;
  }

  rs_blob = nullptr;
  hr = gst_d3d12_alpha_combine_get_p010_rs_blob (priv->device, &rs_blob);
  if (!gst_d3d12_result (hr, priv->device))
    return FALSE;

  hr = device_handle->CreateRootSignature (0, rs_blob->GetBufferPointer (),
      rs_blob->GetBufferSize (), IID_PPV_ARGS (&ctx->rs_p010));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create root signature");
    return FALSE;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = { };
  GstD3DShaderByteCode cs_code;
  if (!gst_d3d_plugin_shader_get_cs_blob (GST_D3D_PLUGIN_CS_NV12_TO_R8_LOAD,
          GST_D3D_SM_5_0, &cs_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
    return FALSE;
  }

  pso_desc.pRootSignature = ctx->rs_nv12.Get ();
  pso_desc.CS.pShaderBytecode = cs_code.byte_code;
  pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
  hr = device_handle->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&ctx->pso_nv12_to_r8_load));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create PSO");
    return FALSE;
  }

  if (!gst_d3d_plugin_shader_get_cs_blob (GST_D3D_PLUGIN_CS_NV12_TO_R8_SAMPLE,
          GST_D3D_SM_5_0, &cs_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
    return FALSE;
  }

  pso_desc.CS.pShaderBytecode = cs_code.byte_code;
  pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
  hr = device_handle->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&ctx->pso_nv12_to_r8_sample));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create PSO");
    return FALSE;
  }

  if (!gst_d3d_plugin_shader_get_cs_blob
      (GST_D3D_PLUGIN_CS_P010_TO_A420_10_LOAD, GST_D3D_SM_5_0, &cs_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
    return FALSE;
  }

  pso_desc.pRootSignature = ctx->rs_p010.Get ();
  pso_desc.CS.pShaderBytecode = cs_code.byte_code;
  pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
  hr = device_handle->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&ctx->pso_p010_to_a420_10_load));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create PSO");
    return FALSE;
  }

  if (!gst_d3d_plugin_shader_get_cs_blob
      (GST_D3D_PLUGIN_CS_P010_TO_A420_10_SAMPLE, GST_D3D_SM_5_0, &cs_code)) {
    GST_ERROR_OBJECT (self, "Couldn't get compute shader bytecode");
    return FALSE;
  }

  pso_desc.CS.pShaderBytecode = cs_code.byte_code;
  pso_desc.CS.BytecodeLength = cs_code.byte_code_len;
  hr = device_handle->CreateComputePipelineState (&pso_desc,
      IID_PPV_ARGS (&ctx->pso_p010_to_a420_10_sample));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create PSO");
    return FALSE;
  }

  ctx->srv_inc_size = device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  priv->ctx = std::move (ctx);

  /* 25 fps duration by default */
  priv->timeout_advance = 40 * GST_MSECOND;

  return TRUE;
}

static gboolean
gst_d3d12_alpha_combine_stop (GstAggregator * agg)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;

  gst_clear_caps (&priv->caps);
  priv->clear_pool ();
  priv->ctx = nullptr;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_clear_object (&priv->device);
  }

  return TRUE;
}

static GstCaps *
gst_d3d12_alpha_combine_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *sinkcaps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *returned_caps;

  template_caps = gst_pad_get_pad_template_caps (pad);

  sinkcaps = gst_pad_get_current_caps (pad);
  if (sinkcaps == nullptr) {
    sinkcaps = gst_caps_ref (template_caps);
  } else {
    sinkcaps = gst_caps_merge (sinkcaps, gst_caps_ref (template_caps));
  }

  if (filter) {
    filtered_caps = gst_caps_intersect (sinkcaps, filter);
    gst_caps_unref (sinkcaps);
  } else {
    filtered_caps = sinkcaps;   /* pass ownership */
  }

  returned_caps = gst_caps_intersect (filtered_caps, template_caps);

  gst_caps_unref (template_caps);
  gst_caps_unref (filtered_caps);

  GST_DEBUG_OBJECT (pad, "returning %" GST_PTR_FORMAT, returned_caps);

  return returned_caps;
}

static gboolean
gst_d3d12_alpha_combine_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstCaps *template_caps;

  GST_DEBUG_OBJECT (pad, "try accept caps of %" GST_PTR_FORMAT, caps);

  template_caps = gst_pad_get_pad_template_caps (pad);
  template_caps = gst_caps_make_writable (template_caps);

  ret = gst_caps_can_intersect (caps, template_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (template_caps);

  return ret;
}

static gboolean
gst_d3d12_alpha_combine_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (gst_d3d12_handle_context_query (GST_ELEMENT (agg), query,
              priv->device)) {
        return TRUE;
      }
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_d3d12_alpha_combine_sink_getcaps (GST_PAD (pad), filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_d3d12_alpha_combine_sink_acceptcaps (GST_PAD (pad), caps);
      gst_query_set_accept_caps_result (query, ret);
      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, pad, query);
}

static gboolean
gst_d3d12_alpha_combine_src_query (GstAggregator * agg, GstQuery * query)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d12_handle_context_query (GST_ELEMENT (agg), query,
              priv->device)) {
        return TRUE;
      }
      break;
    case GST_QUERY_POSITION:
    case GST_QUERY_DURATION:
    case GST_QUERY_URI:
    case GST_QUERY_SEEKING:
      return gst_pad_peer_query (GST_PAD_CAST (self->sinkpad), query);
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}

static gboolean
gst_d3d12_alpha_combine_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;
  GstVideoInfo info;
  GstCaps *caps;
  guint size;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    auto pool = gst_d3d12_buffer_pool_new (priv->device);

    if (!pool) {
      GST_ERROR_OBJECT (self, "Failed to create buffer pool");
      return FALSE;
    }

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);
    auto params = gst_d3d12_allocation_params_new (priv->device,
        &info, GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_NONE);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);

    gst_buffer_pool_config_set_params (config, caps, (guint) size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (pool, "Couldn't set config");
      gst_object_unref (pool);

      return FALSE;
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config,
        nullptr, &size, nullptr, nullptr);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_d3d12_alpha_combine_sink_event (GstAggregator * agg, GstAggregatorPad * pad,
    GstEvent * event)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      if (pad == self->sinkpad) {
        GstSegment segment;
        gst_event_copy_segment (event, &segment);

        if (segment.format != GST_FORMAT_TIME) {
          GST_WARNING_OBJECT (self, "Time format segment is required");
          return FALSE;
        }

        if (segment.rate > 0)
          segment.position = segment.start;
        else
          segment.position = segment.stop;

        gst_aggregator_update_segment (agg, &segment);
      }
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      if (pad == self->sinkpad) {
        if (!priv->caps || !gst_caps_is_equal (priv->caps, caps)) {
          gst_video_info_from_caps (&priv->main_info, caps);

          if (priv->main_info.fps_n > 0 && priv->main_info.fps_d > 0) {
            priv->timeout_advance = gst_util_uint64_scale (GST_SECOND,
                priv->main_info.fps_d, priv->main_info.fps_n);
            GST_DEBUG_OBJECT (self, "Timeout advance: %" GST_TIME_FORMAT,
                GST_TIME_ARGS (priv->timeout_advance));
            gst_aggregator_set_latency (agg, priv->timeout_advance,
                priv->timeout_advance);
          }

          gst_caps_replace (&priv->caps, caps);
          gst_aggregator_negotiate (agg);
        }
      } else {
        gst_video_info_from_caps (&priv->alpha_info, caps);
      }
      break;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, pad, event);
}

static GstBuffer *
gst_d3d12_alpha_combine_convert_nv12 (GstD3D12AlphaCombine * self,
    GstBuffer * main_buf, GstBuffer * alpha_buf)
{
  auto priv = self->priv;

  auto main_dmem = (GstD3D12Memory *) gst_buffer_peek_memory (main_buf, 0);
  auto main_resource = gst_d3d12_memory_get_resource_handle (main_dmem);
  auto main_desc = GetDesc (main_resource);

  auto device = gst_d3d12_device_get_device_handle (priv->device);

  if (priv->out_alloc) {
    if (priv->out_alpha_desc.Width != main_desc.Width ||
        priv->out_alpha_desc.Height != main_desc.Height) {
      priv->clear_pool ();
    }
  }

  if (!priv->out_alloc) {
    priv->out_alpha_desc = CD3DX12_RESOURCE_DESC::Tex2D (DXGI_FORMAT_R8_UNORM,
        main_desc.Width, main_desc.Height, 1, 1, 1, 0,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    device->GetCopyableFootprints (&priv->out_alpha_desc, 0, 1, 0,
        &priv->out_alpha_layout, nullptr, nullptr, nullptr);

    D3D12_HEAP_PROPERTIES heap_props =
        CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
    priv->out_alloc = gst_d3d12_pool_allocator_new (priv->device, &heap_props,
        D3D12_HEAP_FLAG_SHARED, &priv->out_alpha_desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr);
    if (!priv->out_alloc) {
      GST_ERROR_OBJECT (self, "Couldn't create output pool");
      return nullptr;
    }

    gst_d3d12_allocator_set_active (GST_D3D12_ALLOCATOR (priv->out_alloc),
        TRUE);
  }

  auto ctx = priv->ctx;

  GstMemory *out_mem = nullptr;
  gst_d3d12_pool_allocator_acquire_memory (priv->out_alloc, &out_mem);
  if (!out_mem) {
    GST_ERROR_OBJECT (self, "Couldn't acquire output memory");
    return nullptr;
  }

  auto out_dmem = (GstD3D12Memory *) out_mem;
  auto out_resource = gst_d3d12_memory_get_resource_handle (out_dmem);

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (ctx->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    gst_memory_unref (out_mem);

    return nullptr;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    gst_memory_unref (out_mem);

    return nullptr;
  }

  if (!ctx->cl) {
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&ctx->cl));
  } else {
    hr = ctx->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_memory_unref (out_mem);
    return nullptr;
  }

  if (alpha_buf) {
    GstD3D12Frame alpha_frame;

    if (!gst_d3d12_frame_map (&alpha_frame, &priv->alpha_info, alpha_buf,
            GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
      GST_ERROR_OBJECT (self, "Couldn't map alpha buffer");
      gst_d3d12_fence_data_unref (fence_data);
      gst_memory_unref (out_mem);
      return nullptr;
    }

    auto alpha_desc = GetDesc (alpha_frame.data[0]);

    GstD3D12DescHeap *heap;
    if (!gst_d3d12_desc_heap_pool_acquire (ctx->desc_pool, &heap)) {
      GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
      gst_d3d12_fence_data_unref (fence_data);
      gst_memory_unref (out_mem);
      gst_d3d12_frame_unmap (&alpha_frame);
      return nullptr;
    }

    auto heap_handle = gst_d3d12_desc_heap_get_handle (heap);
    gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (heap));

    auto cpu_handle =
        CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
        (heap_handle));

    /* UAV */
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R8_UNORM;
    device->CreateUnorderedAccessView (out_resource,
        nullptr, &uav_desc, cpu_handle);
    cpu_handle.Offset (ctx->srv_inc_size);

    /* SRV */
    device->CopyDescriptorsSimple (1, cpu_handle,
        alpha_frame.srv_desc_handle[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    /* Update cbuf */
    ctx->cbuf.AlphaTexSize.x = (UINT) alpha_desc.Width;
    ctx->cbuf.AlphaTexSize.y = alpha_desc.Height;
    ctx->cbuf.AlphaViewSize.x = priv->alpha_info.width;
    ctx->cbuf.AlphaViewSize.y = priv->alpha_info.height;
    ctx->cbuf.DstSize.x = priv->out_info.width;
    ctx->cbuf.DstSize.y = priv->out_info.height;

    /* Record cmd */
    ID3D12DescriptorHeap *heaps[] = { heap_handle };

    ctx->cl->SetComputeRootSignature (ctx->rs_nv12.Get ());
    if (priv->main_info.width == priv->alpha_info.width &&
        priv->main_info.height == priv->alpha_info.height) {
      GST_TRACE_OBJECT (self, "Run NV12 to R8 Load shader");
      ctx->cl->SetPipelineState (ctx->pso_nv12_to_r8_load.Get ());
    } else {
      GST_TRACE_OBJECT (self, "Run NV12 to R8 Sample shader");
      ctx->cl->SetPipelineState (ctx->pso_nv12_to_r8_sample.Get ());
    }
    ctx->cl->SetComputeRoot32BitConstants (0, sizeof (CopyParams) / 4,
        &ctx->cbuf, 0);
    auto gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE
        (GetGPUDescriptorHandleForHeapStart (heap_handle));

    ctx->cl->SetDescriptorHeaps (1, heaps);

    /* UAV */
    ctx->cl->SetComputeRootDescriptorTable (1, gpu_handle);
    gpu_handle.Offset (ctx->srv_inc_size);

    /* SRV */
    ctx->cl->SetComputeRootDescriptorTable (2, gpu_handle);

    ctx->cl->Dispatch (priv->dispatch_x, priv->dispatch_y, 1);

    gst_d3d12_frame_fence_gpu_wait (&alpha_frame, ctx->cq);
    gst_d3d12_frame_unmap (&alpha_frame);

    /* To release source alpha buffer once copy done */
    gst_buffer_ref (alpha_buf);
    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (alpha_buf));
  } else {
    /* Clear alpha plane with ones */
    auto rtv_heap = gst_d3d12_memory_get_render_target_view_heap (out_dmem);
    if (!rtv_heap) {
      GST_ERROR_OBJECT (self, "No RTV available");
      gst_d3d12_fence_data_unref (fence_data);
      gst_memory_unref (out_mem);
      return nullptr;
    }

    FLOAT clear_color[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    auto cpu_handle = GetCPUDescriptorHandleForHeapStart (rtv_heap);
    ctx->cl->ClearRenderTargetView (cpu_handle, clear_color, 0, nullptr);
  }

  hr = ctx->cl->Close ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_memory_unref (out_mem);
    return nullptr;
  }

  ID3D12CommandList *cl[] = { ctx->cl.Get () };

  gst_d3d12_cmd_queue_execute_command_lists (ctx->cq, 1, cl, &ctx->fence_val);
  gst_d3d12_memory_set_fence (out_dmem, ctx->cq_fence, ctx->fence_val, FALSE);
  GST_MINI_OBJECT_FLAG_SET (out_dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
  GST_MINI_OBJECT_FLAG_UNSET (out_dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  auto outbuf = gst_buffer_copy (main_buf);

  /* Update vmeta */
  auto prev_size = gst_buffer_get_size (outbuf);
  auto alpha_idx = GST_VIDEO_INFO_N_PLANES (&priv->main_info);
  auto vmeta = gst_buffer_get_video_meta (outbuf);

  vmeta->offset[alpha_idx] = prev_size;
  vmeta->stride[alpha_idx] = priv->out_alpha_layout.Footprint.RowPitch;
  vmeta->format = GST_VIDEO_INFO_FORMAT (&priv->out_info);
  vmeta->n_planes++;

  gst_buffer_append_memory (outbuf, out_mem);

  gst_d3d12_cmd_queue_set_notify (ctx->cq, ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  return outbuf;
}

static GstBuffer *
gst_d3d12_alpha_combine_convert_p010 (GstD3D12AlphaCombine * self,
    GstBuffer * main_buf, GstBuffer * alpha_buf)
{
  auto priv = self->priv;

  auto device = gst_d3d12_device_get_device_handle (priv->device);

  if (!priv->out_pool) {
    auto out_pool = gst_d3d12_buffer_pool_new (priv->device);
    auto config = gst_buffer_pool_get_config (out_pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    auto d3d12_params = gst_d3d12_allocation_params_new (priv->device,
        &priv->out_info, GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_HEAP_FLAG_SHARED);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
    gst_d3d12_allocation_params_free (d3d12_params);

    auto caps = gst_video_info_to_caps (&priv->out_info);
    gst_buffer_pool_config_set_params (config, caps, priv->out_info.size, 0, 0);
    gst_caps_unref (caps);
    gst_buffer_pool_set_config (out_pool, config);
    if (!gst_buffer_pool_set_active (out_pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Pool active failed");
      gst_object_unref (out_pool);
      return nullptr;
    }

    priv->out_pool = out_pool;
  }

  auto ctx = priv->ctx;

  GstBuffer *out_buf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->out_pool, &out_buf, nullptr);
  if (!out_buf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire output buffer");
    return nullptr;
  }

  GstD3D12Frame main_frame, out_frame, alpha_frame;
  if (!gst_d3d12_frame_map (&main_frame, &priv->main_info, main_buf,
          GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
    GST_ERROR_OBJECT (self, "Couldn't map main frame");
    gst_buffer_unref (out_buf);
    return nullptr;
  }

  if (alpha_buf) {
    if (!gst_d3d12_frame_map (&alpha_frame, &priv->alpha_info, alpha_buf,
            GST_MAP_READ, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
      GST_ERROR_OBJECT (self, "Couldn't map alpha frame");
      gst_d3d12_frame_unmap (&main_frame);
      gst_buffer_unref (out_buf);
      return nullptr;
    }
  }


  if (!gst_d3d12_frame_map (&out_frame, &priv->out_info, out_buf,
          GST_MAP_WRITE, GST_D3D12_FRAME_MAP_FLAG_RTV)) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    gst_d3d12_frame_unmap (&main_frame);
    if (alpha_buf)
      gst_d3d12_frame_unmap (&alpha_frame);
    gst_buffer_unref (out_buf);
    return nullptr;
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (ctx->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    gst_d3d12_frame_unmap (&main_frame);
    if (alpha_buf)
      gst_d3d12_frame_unmap (&alpha_frame);
    gst_d3d12_frame_unmap (&out_frame);
    gst_buffer_unref (out_buf);

    return nullptr;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    gst_d3d12_frame_unmap (&main_frame);
    if (alpha_buf)
      gst_d3d12_frame_unmap (&alpha_frame);
    gst_d3d12_frame_unmap (&out_frame);
    gst_buffer_unref (out_buf);

    return nullptr;
  }

  if (!ctx->cl) {
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&ctx->cl));
  } else {
    hr = ctx->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_d3d12_frame_unmap (&main_frame);
    if (alpha_buf)
      gst_d3d12_frame_unmap (&alpha_frame);
    gst_d3d12_frame_unmap (&out_frame);
    gst_buffer_unref (out_buf);
    return nullptr;
  }

  auto main_desc = GetDesc (main_frame.data[0]);

  /* Init cbuf */
  ctx->cbuf.MainTexSize.x = (UINT) main_desc.Width;
  ctx->cbuf.MainTexSize.y = main_desc.Height;
  ctx->cbuf.MainViewSize.x = priv->main_info.width;
  ctx->cbuf.MainViewSize.y = priv->main_info.height;
  ctx->cbuf.DstSize.x = priv->out_info.width;
  ctx->cbuf.DstSize.y = priv->out_info.height;
  ctx->cbuf.AlphaViewSize.x = priv->alpha_info.width;
  ctx->cbuf.AlphaViewSize.y = priv->alpha_info.height;

  if (alpha_buf) {
    auto alpha_desc = GetDesc (alpha_frame.data[0]);
    ctx->cbuf.AlphaTexSize.x = (UINT) alpha_desc.Width;
    ctx->cbuf.AlphaTexSize.y = alpha_desc.Height;
    ctx->cbuf.FillAlpha = FALSE;
  } else {
    ctx->cbuf.AlphaTexSize = ctx->cbuf.MainTexSize;
    ctx->cbuf.FillAlpha = TRUE;
  }

  GstD3D12DescHeap *heap;
  if (!gst_d3d12_desc_heap_pool_acquire (ctx->desc_pool, &heap)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire descriptor heap");
    gst_d3d12_fence_data_unref (fence_data);
    gst_d3d12_frame_unmap (&main_frame);
    if (alpha_buf)
      gst_d3d12_frame_unmap (&alpha_frame);
    gst_d3d12_frame_unmap (&out_frame);
    gst_buffer_unref (out_buf);
    return nullptr;
  }

  auto heap_handle = gst_d3d12_desc_heap_get_handle (heap);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (heap));

  auto cpu_handle =
      CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
      (heap_handle));

  /* UAV */
  for (guint i = 0; i < 4; i++) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Format = DXGI_FORMAT_R16_UNORM;
    device->CreateUnorderedAccessView (out_frame.data[i],
        nullptr, &uav_desc, cpu_handle);
    cpu_handle.Offset (ctx->srv_inc_size);
  }

  /* SRV */
  for (guint i = 0; i < 2; i++) {
    device->CopyDescriptorsSimple (1, cpu_handle,
        main_frame.srv_desc_handle[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu_handle.Offset (ctx->srv_inc_size);
  }

  if (alpha_buf) {
    device->CopyDescriptorsSimple (1, cpu_handle,
        alpha_frame.srv_desc_handle[0], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

  /* Record cmd */
  ID3D12DescriptorHeap *heaps[] = { heap_handle };

  ctx->cl->SetComputeRootSignature (ctx->rs_p010.Get ());
  if ((priv->main_info.width == priv->alpha_info.width &&
          priv->main_info.height == priv->alpha_info.height) || !alpha_buf) {
    GST_TRACE_OBJECT (self, "Run P010 to A420_10LE Load shader");
    ctx->cl->SetPipelineState (ctx->pso_p010_to_a420_10_load.Get ());
  } else {
    GST_TRACE_OBJECT (self, "Run P010 to A420_10LE Sample shader");
    ctx->cl->SetPipelineState (ctx->pso_p010_to_a420_10_sample.Get ());
  }

  ctx->cl->SetComputeRoot32BitConstants (0, sizeof (CopyParams) / 4,
      &ctx->cbuf, 0);
  auto gpu_handle = CD3DX12_GPU_DESCRIPTOR_HANDLE
      (GetGPUDescriptorHandleForHeapStart (heap_handle));

  ctx->cl->SetDescriptorHeaps (1, heaps);

  /* UAV */
  ctx->cl->SetComputeRootDescriptorTable (1, gpu_handle);
  gpu_handle.Offset (4, ctx->srv_inc_size);

  /* SRV */
  ctx->cl->SetComputeRootDescriptorTable (2, gpu_handle);

  ctx->cl->Dispatch (priv->dispatch_x, priv->dispatch_y, 1);

  gst_d3d12_frame_fence_gpu_wait (&main_frame, ctx->cq);
  gst_d3d12_frame_unmap (&main_frame);

  if (alpha_buf) {
    gst_d3d12_frame_fence_gpu_wait (&alpha_frame, ctx->cq);
    gst_d3d12_frame_unmap (&alpha_frame);
  }

  gst_d3d12_frame_unmap (&out_frame);

  hr = ctx->cl->Close ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (out_buf);
    return nullptr;
  }

  /* Hold ref to source for in flight command */
  gst_buffer_ref (main_buf);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (main_buf));
  if (alpha_buf) {
    gst_buffer_ref (alpha_buf);
    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (alpha_buf));
  }

  ID3D12CommandList *cl[] = { ctx->cl.Get () };

  gst_d3d12_cmd_queue_execute_command_lists (ctx->cq, 1, cl, &ctx->fence_val);
  gst_d3d12_cmd_queue_set_notify (ctx->cq, ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  gst_buffer_copy_into (out_buf, main_buf, GST_BUFFER_COPY_METADATA, 0, -1);
  gst_d3d12_buffer_set_fence (out_buf, ctx->cq_fence, ctx->fence_val, FALSE);

  return out_buf;
}

static GstFlowReturn
gst_d3d12_alpha_combine_aggregate (GstAggregator * agg, gboolean timeout)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;
  auto srcpad = (GstAggregatorPad *) GST_AGGREGATOR_SRC_PAD (agg);
  auto segment = &srcpad->segment;
  GstFlowReturn ret = GST_FLOW_OK;

  auto main_buf = gst_aggregator_pad_peek_buffer (self->sinkpad);
  if (!main_buf) {
    if (gst_aggregator_pad_is_eos (self->sinkpad)) {
      GST_DEBUG_OBJECT (self, "No queued main frame but EOS");
      ret = GST_FLOW_EOS;
    } else {
      GST_DEBUG_OBJECT (self, "Need main frame");
      ret = GST_AGGREGATOR_FLOW_NEED_DATA;

      if (timeout) {
        if (!priv->caps) {
          if (!GST_CLOCK_TIME_IS_VALID (segment->position)) {
            GST_DEBUG_OBJECT (self, "Timeout before negotiate");
            if (segment->rate > 0.0)
              segment->position = segment->start;
            else
              segment->position = segment->stop;
          }
        }

        if (GST_CLOCK_TIME_IS_VALID (segment->position)) {
          if (segment->rate > 0.0)
            segment->position += priv->timeout_advance;
          else if (segment->position > priv->timeout_advance)
            segment->position -= priv->timeout_advance;
          else
            segment->position = 0;

          GST_LOG_OBJECT (self, "Timeout, updating position %"
              GST_TIME_FORMAT, GST_TIME_ARGS (segment->position));
        }
      }
    }

    return ret;
  }

  auto alpha_buf = gst_aggregator_pad_peek_buffer (self->alphapad);
  if (!alpha_buf) {
    if (gst_aggregator_pad_is_eos (self->alphapad)) {
      GST_DEBUG_OBJECT (self, "No queued alpha frame but EOS");
    } else {
      GST_DEBUG_OBJECT (self, "Need alpha frame");
      gst_buffer_unref (main_buf);
      return GST_AGGREGATOR_FLOW_NEED_DATA;
    }
  }

  GstBuffer *outbuf = nullptr;
  auto out_format = GST_VIDEO_INFO_FORMAT (&priv->out_info);
  switch (out_format) {
    case GST_VIDEO_FORMAT_AV12:
      outbuf = gst_d3d12_alpha_combine_convert_nv12 (self, main_buf, alpha_buf);
      break;
    case GST_VIDEO_FORMAT_A420_10LE:
      outbuf = gst_d3d12_alpha_combine_convert_p010 (self, main_buf, alpha_buf);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected output format %s",
          gst_video_format_to_string (out_format));
      break;
  }

  gst_buffer_unref (main_buf);
  gst_clear_buffer (&alpha_buf);

  if (!outbuf)
    return GST_FLOW_ERROR;

  gst_aggregator_pad_drop_buffer (self->sinkpad);
  gst_aggregator_pad_drop_buffer (self->alphapad);

  srcpad->segment.position = GST_BUFFER_PTS (outbuf);
  if (GST_CLOCK_TIME_IS_VALID (srcpad->segment.position) &&
      GST_BUFFER_DURATION_IS_VALID (outbuf) && srcpad->segment.rate > 0) {
    srcpad->segment.position += GST_BUFFER_DURATION (outbuf);
  }

  return gst_aggregator_finish_buffer (agg, outbuf);
}

static gboolean
gst_d3d12_alpha_combine_negotiate (GstAggregator * agg)
{
  auto self = GST_D3D12_ALPHA_COMBINE (agg);
  auto priv = self->priv;

  if (!priv->caps) {
    GST_DEBUG_OBJECT (self, "No input caps yet");
    return FALSE;
  }

  auto main_format = GST_VIDEO_INFO_FORMAT (&priv->main_info);
  const gchar *out_format = "AV12";
  switch (main_format) {
    case GST_VIDEO_FORMAT_NV12:
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      out_format = "A420_10LE";
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected main format %s",
          gst_video_format_to_string (main_format));
      return FALSE;
  }

  auto caps = gst_caps_copy (priv->caps);
  gst_caps_set_simple (caps, "format", G_TYPE_STRING, out_format, nullptr);

  GST_DEBUG_OBJECT (self, "Negotiated caps %" GST_PTR_FORMAT, caps);

  gst_aggregator_set_src_caps (agg, caps);
  gst_video_info_from_caps (&priv->out_info, caps);

  priv->clear_pool ();

  priv->dispatch_x = (priv->out_info.width + 7) / 8;
  priv->dispatch_y = (priv->out_info.height + 7) / 8;

  return TRUE;
}

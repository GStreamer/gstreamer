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

/**
 * SECTION:element-d3d12mipmapping
 * @title: d3d12mipmapping
 * @short_description: Direct3D12 Mipmap generator element
 *
 * d3d12mipmapping element generates mipmap enabled Direct3D12 textures
 * from input textures
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12mipmapping.h"
#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <memory>
#include <queue>
#include <wrl.h>
#include <atomic>
#include <math.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_mip_mapping_debug);
#define GST_CAT_DEFAULT gst_d3d12_mip_mapping_debug

#define OUTPUT_FORMATS "{ VUYA, RGBA, AYUV64, RGBA64_LE, GRAY8, GRAY16_LE }"

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
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, OUTPUT_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            OUTPUT_FORMATS)));

enum
{
  PROP_0,
  PROP_ASYNC_DEPTH,
  PROP_MIP_LEVELS,
};

#define DEFAULT_ASYNC_DEPTH 0
#define DEFAULT_MIP_LEVELS 0

/* *INDENT-OFF* */
struct MipMappingContext
{
  MipMappingContext (GstD3D12Device * dev)
  {
    device = (GstD3D12Device *) gst_object_ref (dev);
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
  }

   ~MipMappingContext ()
  {
    gst_d3d12_device_fence_wait (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        fence_val);

    gst_clear_object (&ca_pool);
    gst_clear_object (&conv);
    gst_clear_object (&gen);
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;
  GstD3D12Converter *conv = nullptr;
  GstD3D12MipGen *gen = nullptr;
  ComPtr<ID3D12GraphicsCommandList> cl;
  std::queue<guint64> scheduled;
  GstD3D12CmdAllocPool *ca_pool;
  guint64 fence_val = 0;
};

struct GstD3D12MipMappingPrivate
{
  GstD3D12MipMappingPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12MipMappingPrivate ()
  {
    gst_clear_object (&fence_data_pool);
  }

  std::unique_ptr < MipMappingContext > ctx;
  GstD3D12FenceDataPool *fence_data_pool;
  D3D12_BOX in_rect = { };
  D3D12_BOX prev_in_rect = { };

  std::atomic<guint> async_depth = { DEFAULT_ASYNC_DEPTH };
  std::atomic<guint> mip_levels = { DEFAULT_MIP_LEVELS };

  std::mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12MipMapping
{
  GstD3D12BaseFilter parent;

  GstD3D12MipMappingPrivate *priv;
};

static void gst_d3d12_mip_mapping_finalize (GObject * object);
static void gst_d3d12_mip_mapping_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_mip_mapping_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_d3d12_mip_mapping_stop (GstBaseTransform * trans);
static GstCaps *gst_d3d12_mip_mapping_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d12_mip_mapping_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d12_mip_mapping_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_d3d12_mip_mapping_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d12_mip_mapping_set_info (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info);
static gboolean gst_d3d12_mip_mapping_propose_allocation (GstD3D12BaseFilter *
    filter, GstD3D12Device * device, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_mip_mapping_decide_allocation (GstD3D12BaseFilter *
    filter, GstD3D12Device * device, GstQuery * query);

#define gst_d3d12_mip_mapping_parent_class parent_class
G_DEFINE_TYPE (GstD3D12MipMapping, gst_d3d12_mip_mapping,
    GST_TYPE_D3D12_BASE_FILTER);

static void
gst_d3d12_mip_mapping_class_init (GstD3D12MipMappingClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_D3D12_BASE_FILTER_CLASS (klass);

  object_class->set_property = gst_d3d12_mip_mapping_set_property;
  object_class->get_property = gst_d3d12_mip_mapping_get_property;
  object_class->finalize = gst_d3d12_mip_mapping_finalize;

  g_object_class_install_property (object_class, PROP_ASYNC_DEPTH,
      g_param_spec_uint ("async-depth", "Async Depth",
          "Number of in-flight GPU commands which can be scheduled without "
          "synchronization (0 = unlimited)", 0, G_MAXINT, DEFAULT_ASYNC_DEPTH,
          (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_MIP_LEVELS,
      g_param_spec_uint ("mip-levels", "Mip Levels",
          "Mipmap levels to use (0 = maximum level)",
          0, G_MAXUINT16, DEFAULT_MIP_LEVELS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 MipMapping",
      "Filter/Converter/Video/Hardware",
      "Generates RGBA MipMap texture from input",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  trans_class->passthrough_on_same_caps = FALSE;

  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_fixate_caps);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_transform_meta);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_transform);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_set_info);
  filter_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_propose_allocation);
  filter_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_mip_mapping_decide_allocation);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_SAMPLING_METHOD,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_mip_mapping_debug, "d3d12mipmapping", 0,
      "d3d12mipmapping");
}

static void
gst_d3d12_mip_mapping_init (GstD3D12MipMapping * self)
{
  self->priv = new GstD3D12MipMappingPrivate ();
}

static void
gst_d3d12_mip_mapping_finalize (GObject * object)
{
  auto self = GST_D3D12_MIP_MAPPING (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_mip_mapping_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_MIP_MAPPING (object);
  auto priv = self->priv;

  switch (prop_id) {
    case PROP_ASYNC_DEPTH:
      priv->async_depth = g_value_get_uint (value);
      break;
    case PROP_MIP_LEVELS:
      priv->mip_levels = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_mip_mapping_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_MIP_MAPPING (object);
  auto priv = self->priv;

  switch (prop_id) {
    case PROP_ASYNC_DEPTH:
      g_value_set_uint (value, priv->async_depth);
      break;
    case PROP_MIP_LEVELS:
      g_value_set_uint (value, priv->mip_levels);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d12_mip_mapping_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_MIP_MAPPING (trans);
  auto priv = self->priv;

  priv->ctx = nullptr;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static GstCaps *
gst_d3d12_mip_mapping_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          NULL);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

static GstCaps *
gst_d3d12_mip_mapping_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_d3d12_mip_mapping_caps_remove_format_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_d3d12_mip_mapping_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_D3D12_MIP_MAPPING (trans);
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      GST_VIDEO_ORIENTATION_IDENTITY, NULL,
      GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT, caps, othercaps, NULL);
  if (!result)
    return othercaps;

  gst_clear_caps (&othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result))
      gst_caps_replace (&result, caps);
  }

  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_d3d12_mip_mapping_propose_allocation (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstQuery * decide_query, GstQuery * query)
{
  if (!GST_D3D12_BASE_FILTER_CLASS (parent_class)->propose_allocation (filter,
          device, decide_query, query)) {
    return FALSE;
  }

  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_d3d12_mip_mapping_decide_allocation (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstQuery * query)
{
  auto self = GST_D3D12_MIP_MAPPING (filter);
  auto priv = self->priv;
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

  size = GST_VIDEO_INFO_SIZE (&info);
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, device))
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  D3D12_RESOURCE_FLAGS resource_flags =
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  auto d3d12_params = gst_d3d12_allocation_params_new (device, &info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
      D3D12_HEAP_FLAG_SHARED);

  guint mip_levels = priv->mip_levels;
  if (mip_levels != 0) {
    guint max_levels = 1 + (guint) floor (log2 (MAX (info.width, info.height)));
    GST_DEBUG_OBJECT (self, "Requested mip levels %d, max levels %d",
        mip_levels, max_levels);

    if (max_levels <= mip_levels)
      mip_levels = 0;
  }

  /* Auto generate mip maps */
  gst_d3d12_allocation_params_set_mip_levels (d3d12_params, mip_levels);

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

  return TRUE;
}

static gboolean
gst_d3d12_mip_mapping_set_info (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  auto self = GST_D3D12_MIP_MAPPING (filter);
  auto priv = self->priv;

  priv->ctx = nullptr;

  GST_DEBUG_OBJECT (self, "Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  /* if present, these must match */
  if (in_info->interlace_mode != out_info->interlace_mode) {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }

  auto ctx = std::make_unique < MipMappingContext > (device);

  ctx->conv = gst_d3d12_converter_new (device, nullptr, in_info,
      out_info, nullptr, nullptr, nullptr);
  if (!ctx->conv) {
    GST_ERROR_OBJECT (self, "Couldn't create converter");
    return FALSE;
  }

  GstD3DPluginCS cs_type = GST_D3D_PLUGIN_CS_MIP_GEN;
  if (GST_VIDEO_INFO_IS_GRAY (out_info)) {
    GST_DEBUG_OBJECT (self, "Use GRAY shader");
    cs_type = GST_D3D_PLUGIN_CS_MIP_GEN_GRAY;
  } else if (!GST_VIDEO_INFO_HAS_ALPHA (in_info)) {
    GST_DEBUG_OBJECT (self, "Use VUYA shader");
    if (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_AYUV64)
      cs_type = GST_D3D_PLUGIN_CS_MIP_GEN_AYUV;
    else
      cs_type = GST_D3D_PLUGIN_CS_MIP_GEN_VUYA;
  }

  ctx->gen = gst_d3d12_mip_gen_new (device, cs_type);
  if (!ctx->gen) {
    GST_ERROR_OBJECT (self, "Couldn't create mip generator");
    return FALSE;
  }

  priv->in_rect = CD3DX12_BOX (0, 0,
      GST_VIDEO_INFO_WIDTH (in_info), GST_VIDEO_INFO_HEIGHT (in_info));
  priv->prev_in_rect = priv->in_rect;

  priv->ctx = std::move (ctx);

  return TRUE;
}

static gboolean
gst_d3d12_mip_mapping_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  if (meta->info->api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
      outbuf, meta, inbuf);
}

static GstFlowReturn
gst_d3d12_mip_mapping_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_D3D12_MIP_MAPPING (trans);
  auto priv = self->priv;
  D3D12_BOX in_rect;

  auto crop_meta = gst_buffer_get_video_crop_meta (inbuf);
  if (crop_meta) {
    GST_LOG_OBJECT (self, "Have crop rect, x:y:w:h = %d:%d:%d:%d",
        crop_meta->x, crop_meta->y, crop_meta->width, crop_meta->height);

    in_rect = CD3DX12_BOX (crop_meta->x, crop_meta->y,
        crop_meta->x + crop_meta->width, crop_meta->y + crop_meta->height);
  } else {
    in_rect = priv->in_rect;
  }

  if (in_rect != priv->in_rect) {
    priv->prev_in_rect = in_rect;
    g_object_set (priv->ctx->conv, "src-x", (gint) in_rect.left,
        "src-y", (gint) in_rect.top,
        "src-width", (gint) in_rect.right - in_rect.left,
        "src-height", (gint) in_rect.bottom - in_rect.top, nullptr);
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  if (!gst_d3d12_device_prepare_graphics_cmd_list (priv->ctx->device,
          priv->ctx->cl.GetAddressOf (), priv->ctx->ca_pool, nullptr,
          fence_data)) {
    GST_ERROR_OBJECT (self, "Couldn't prepare command list");
    gst_d3d12_fence_data_unref (fence_data);

    return GST_FLOW_ERROR;
  }

  auto cq = gst_d3d12_device_get_cmd_queue (priv->ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  auto fence = gst_d3d12_cmd_queue_get_fence_handle (cq);
  if (!gst_d3d12_converter_convert_buffer (priv->ctx->conv,
          inbuf, outbuf, fence_data, priv->ctx->cl.Get (), TRUE)) {
    GST_ERROR_OBJECT (self, "Couldn't build command list");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  auto dmem = (GstD3D12Memory *) gst_buffer_peek_memory (outbuf, 0);
  auto tex = gst_d3d12_memory_get_resource_handle (dmem);

  D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition
      (tex, D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
  priv->ctx->cl->ResourceBarrier (1, &barrier);

  if (!gst_d3d12_mip_gen_execute (priv->ctx->gen, tex, fence_data,
          priv->ctx->cl.Get ())) {
    GST_ERROR_OBJECT (self, "Couldn't build mip gen command");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  auto hr = priv->ctx->cl->Close ();
  if (!gst_d3d12_result (hr, priv->ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cmd_list[] = { priv->ctx->cl.Get () };

  hr = gst_d3d12_cmd_queue_execute_command_lists (cq,
      1, cmd_list, &priv->ctx->fence_val);
  if (!gst_d3d12_result (hr, priv->ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  gst_d3d12_buffer_set_fence (outbuf, fence, priv->ctx->fence_val, FALSE);
  gst_d3d12_cmd_queue_set_notify (cq, priv->ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  priv->ctx->scheduled.push (priv->ctx->fence_val);

  auto completed = gst_d3d12_device_get_completed_value (priv->ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  while (!priv->ctx->scheduled.empty ()) {
    if (priv->ctx->scheduled.front () > completed)
      break;

    priv->ctx->scheduled.pop ();
  }

  auto async_depth = priv->async_depth.load ();
  if (async_depth > 0 && priv->ctx->scheduled.size () > async_depth) {
    auto fence_to_wait = priv->ctx->scheduled.front ();
    priv->ctx->scheduled.pop ();
    gst_d3d12_device_fence_wait (priv->ctx->device,
        D3D12_COMMAND_LIST_TYPE_DIRECT, fence_to_wait);
  }

  return GST_FLOW_OK;
}

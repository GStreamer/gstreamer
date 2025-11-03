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

/**
 * SECTION:element-d3d12interlace
 * @title: d3d12interlace
 *
 * A Direct3D12 based interlacing element
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12interlace.h"
#include "gstd3d12pluginutils.h"
#include "gstd3d12weaveinterlace.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <memory>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_interlace_debug);
#define GST_CAT_DEFAULT gst_d3d12_interlace_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS)
        ", interlace-mode = progressive; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)
        ", interlace-mode = progressive; "));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, GST_D3D12_ALL_FORMATS)
        ", interlace-mode = interleaved; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D12_ALL_FORMATS)
        ", interlace-mode = interleaved; "));


enum GstD3D12InterlacePattern
{
  GST_D3D12_INTERLACE_PATTERN_1_1,
  GST_D3D12_INTERLACE_PATTERN_2_2,
};

/**
 * GstD3D12InterlacePattern:
 *
 * Since: 1.28
 */
#define GST_TYPE_D3D12_INTERLACE_PATTERN (gst_d3d12_interlace_pattern_get_type())
static GType
gst_d3d12_interlace_pattern_get_type (void)
{
  static GType type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GEnumValue types[] = {
      /**
       * GstD3D12InterlacePattern::1:1:
       *
       * Since: 1.28
       */
      {GST_D3D12_INTERLACE_PATTERN_1_1, "1:1 (e.g. 60p -> 60i)", "1:1"},

      /**
       * GstD3D12InterlaceFields::2:2:
       *
       * Since: 1.28
       */
      {GST_D3D12_INTERLACE_PATTERN_2_2, "2:2 (e.g. 30p -> 60i)", "2:2"},


      {0, nullptr, nullptr},
    };

    type = g_enum_register_static ("GstD3D12InterlacePattern", types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum GstD3D12InterlaceEngine
{
  GST_D3D12_INTERLACE_ENGINE_AUTO,
  GST_D3D12_INTERLACE_ENGINE_3D,
  GST_D3D12_INTERLACE_ENGINE_COMPUTE,
};

/**
 * GstD3D12InterlaceEngine:
 *
 * Since: 1.28
 */
#define GST_TYPE_D3D12_INTERLACE_ENGINE (gst_d3d12_interlace_engine_get_type())
static GType
gst_d3d12_interlace_engine_get_type (void)
{
  static GType type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GEnumValue types[] = {
      /**
       * GstD3D12InterlaceEngine::auto:
       *
       * Since: 1.28
       */
      {GST_D3D12_INTERLACE_ENGINE_AUTO,
          "iGPU uses 3D engine, dGPU uses compute engine", "auto"},

      /**
       * GstD3D12InterlaceEngine::3d:
       *
       * Since: 1.28
       */
      {GST_D3D12_INTERLACE_ENGINE_3D, "3D", "3d"},

      /**
       * GstD3D12InterlaceEngine::compute:
       *
       * Since: 1.28
       */
      {GST_D3D12_INTERLACE_ENGINE_COMPUTE, "Compute", "compute"},
      {0, nullptr, nullptr},
    };

    type = g_enum_register_static ("GstD3D12InterlaceEngine", types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum
{
  PROP_0,
  PROP_TFF,
  PROP_FIELD_PATTERN,
  PROP_ENGINE,
};

#define DEFAULT_TFF FALSE
#define DEFAULT_FIELD_PATTERN GST_D3D12_INTERLACE_PATTERN_1_1
#define DEFAULT_ENGINE GST_D3D12_INTERLACE_ENGINE_AUTO


/* *INDENT-OFF* */
struct InterlaceConvCtx
{
  InterlaceConvCtx (GstD3D12Device * dev)
  {
    device = (GstD3D12Device *) gst_object_ref (dev);
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
  }

  ~InterlaceConvCtx ()
  {
    gst_d3d12_device_fence_wait (device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        fence_val);

    if (pre_pool)
      gst_buffer_pool_set_active (pre_pool, FALSE);
    if (post_pool)
      gst_buffer_pool_set_active (post_pool, FALSE);
    cl = nullptr;
    gst_clear_object (&pre_pool);
    gst_clear_object (&post_pool);
    gst_clear_object (&pre_conv);
    gst_clear_object (&post_conv);
    gst_clear_object (&ca_pool);
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;
  GstD3D12Converter *pre_conv = nullptr;
  GstD3D12Converter *post_conv = nullptr;
  GstBufferPool *pre_pool = nullptr;
  GstBufferPool *post_pool = nullptr;
  ComPtr<ID3D12GraphicsCommandList> cl;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  guint64 fence_val = 0;
};

struct GstD3D12InterlacePrivate
{
  GstD3D12InterlacePrivate ()
  {
    fence_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12InterlacePrivate ()
  {
    gst_clear_object (&weave);
    gst_clear_object (&fence_pool);
  }

  std::mutex lock;
  GstD3D12WeaveInterlace *weave = nullptr;
  GstD3D12FenceDataPool *fence_pool = nullptr;
  std::shared_ptr<InterlaceConvCtx> conv_ctx;
  GstVideoInfo in_info;
  GstVideoInfo weave_info;
  GstClockTime latency = 0;
  gboolean use_compute = FALSE;
  gboolean tff = DEFAULT_TFF;
  GstD3D12InterlacePattern pattern = DEFAULT_FIELD_PATTERN;
  GstD3D12InterlaceEngine engine = DEFAULT_ENGINE;
};
/* *INDENT-ON* */

struct _GstD3D12Interlace
{
  GstD3D12BaseFilter parent;

  GstD3D12InterlacePrivate *priv;
};

static void gst_d3d12_interlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_interlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d12_interlace_finalize (GObject * object);
static gboolean gst_d3d12_interlace_start (GstBaseTransform * trans);
static gboolean gst_d3d12_interlace_stop (GstBaseTransform * trans);
static GstCaps *gst_d3d12_interlace_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d12_interlace_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d12_interlace_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_d3d12_interlace_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static GstFlowReturn gst_d3d12_interlace_generate_output (GstBaseTransform *
    trans, GstBuffer ** buffer);
static GstFlowReturn gst_d3d12_interlace_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_d3d12_interlace_submit_input_buffer (GstBaseTransform
    * trans, gboolean is_discont, GstBuffer * input);
static gboolean gst_d3d12_interlace_set_info (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info);
static gboolean gst_d3d12_interlace_propose_allocation (GstD3D12BaseFilter *
    filter, GstD3D12Device * device, GstQuery * decide_query, GstQuery * query);

#define gst_d3d12_interlace_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Interlace, gst_d3d12_interlace,
    GST_TYPE_D3D12_BASE_FILTER);

static void
gst_d3d12_interlace_class_init (GstD3D12InterlaceClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_D3D12_BASE_FILTER_CLASS (klass);

  object_class->set_property = gst_d3d12_interlace_set_property;
  object_class->get_property = gst_d3d12_interlace_get_property;
  object_class->finalize = gst_d3d12_interlace_finalize;

  g_object_class_install_property (object_class, PROP_TFF,
      g_param_spec_boolean ("top-field-first", "top field first",
          "Interlaced stream should be top field first", DEFAULT_TFF,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_FIELD_PATTERN,
      g_param_spec_enum ("field-pattern", "Field pattern",
          "The output field pattern", GST_TYPE_D3D12_INTERLACE_PATTERN,
          DEFAULT_FIELD_PATTERN,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_ENGINE,
      g_param_spec_enum ("engine", "Engine", "Engine to use",
          GST_TYPE_D3D12_INTERLACE_ENGINE, DEFAULT_ENGINE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Interlacer",
      "Filter/Interlace/Effect/Video/Hardware",
      "A Direct3D12 interlacer element",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_interlace_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_interlace_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_interlace_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_interlace_fixate_caps);
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_d3d12_interlace_sink_event);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_interlace_query);
  trans_class->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_d3d12_interlace_submit_input_buffer);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_d3d12_interlace_generate_output);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_interlace_transform);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_d3d12_interlace_set_info);
  filter_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_interlace_propose_allocation);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_INTERLACE_PATTERN,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_INTERLACE_ENGINE,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_interlace_debug, "d3d12interlace", 0,
      "d3d12interlace");
}

static void
gst_d3d12_interlace_init (GstD3D12Interlace * self)
{
  self->priv = new GstD3D12InterlacePrivate ();
}

static void
gst_d3d12_interlace_finalize (GObject * object)
{
  auto self = GST_D3D12_INTERLACE (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
is_half_framerate (GstD3D12InterlacePattern pattern)
{
  if (pattern == GST_D3D12_INTERLACE_PATTERN_1_1)
    return TRUE;

  return FALSE;
}

static void
gst_d3d12_interlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_INTERLACE (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_TFF:
      priv->tff = g_value_get_boolean (value);
      break;
    case PROP_FIELD_PATTERN:
      priv->pattern = (GstD3D12InterlacePattern) g_value_get_enum (value);
      break;
    case PROP_ENGINE:
      priv->engine = (GstD3D12InterlaceEngine) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_interlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_INTERLACE (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_TFF:
      g_value_set_boolean (value, priv->tff);
      break;
    case PROP_FIELD_PATTERN:
      g_value_set_enum (value, priv->pattern);
      break;
    case PROP_ENGINE:
      g_value_set_enum (value, priv->engine);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d12_interlace_start (GstBaseTransform * trans)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  priv->latency = 0;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_d3d12_interlace_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    gst_clear_object (&priv->weave);
    priv->conv_ctx = nullptr;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static GstCaps *
gst_d3d12_interlace_remove_interlace_info (GstCaps * caps,
    gboolean remove_framerate)
{
  auto res = gst_caps_new_empty ();
  auto n = gst_caps_get_size (caps);
  for (guint i = 0; i < n; i++) {
    auto s = gst_caps_get_structure (caps, i);
    auto f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, s, f))
      continue;

    s = gst_structure_copy (s);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_contains (f, GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
      if (remove_framerate) {
        gst_structure_remove_fields (s,
            "interlace-mode", "field-order", "framerate", nullptr);
      } else {
        gst_structure_remove_fields (s,
            "interlace-mode", "field-order", nullptr);
      }
    }

    gst_caps_append_structure_full (res, s, gst_caps_features_copy (f));
  }

  return res;
}

static GstCaps *
gst_d3d12_interlace_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  /* Get all possible caps that we can transform to */
  auto ret = gst_d3d12_interlace_remove_interlace_info (caps,
      is_half_framerate (priv->pattern));

  if (filter) {
    auto tmp = gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, ret);

  return ret;
}

static GstCaps *
gst_d3d12_interlace_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  GST_DEBUG_OBJECT (self,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  if (direction == GST_PAD_SRC)
    return gst_caps_fixate (othercaps);

  auto tmp = gst_caps_copy (caps);
  tmp = gst_caps_fixate (tmp);

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, tmp)) {
    GST_WARNING_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    gst_caps_unref (tmp);

    return gst_caps_fixate (othercaps);
  }

  auto s = gst_caps_get_structure (tmp, 0);

  gint fps_n, fps_d;
  if (is_half_framerate (priv->pattern) &&
      gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d) &&
      fps_n > 0 && fps_d > 0) {
    fps_d *= 2;

    gst_caps_set_simple (othercaps,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d, nullptr);
  }

  gst_caps_set_simple (othercaps,
      "interlace-mode", G_TYPE_STRING, "interleaved",
      "field-order", G_TYPE_STRING, priv->tff ? "top-field-first" :
      "bottom-field-first", nullptr);
  gst_caps_unref (tmp);

  return gst_caps_fixate (othercaps);
}

static gboolean
gst_d3d12_interlace_propose_allocation (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstQuery * decide_query, GstQuery * query)
{
  /* passthrough, we're done */
  if (!decide_query)
    return TRUE;

  if (!GST_D3D12_BASE_FILTER_CLASS (parent_class)->propose_allocation (filter,
          device, decide_query, query)) {
    return FALSE;
  }

  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  return TRUE;
}

static void
gst_d3d12_interlace_drain (GstD3D12Interlace * self)
{
  auto trans = GST_BASE_TRANSFORM (self);
  auto priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = nullptr;

  if (!priv->weave)
    return;

  if (gst_base_transform_is_passthrough (trans)) {
    gst_d3d12_weave_interlace_flush (priv->weave);
    return;
  }

  gst_d3d12_weave_interlace_drain (priv->weave);
  do {
    outbuf = nullptr;
    ret = gst_d3d12_weave_interlace_pop (priv->weave, &outbuf);
    if (ret == GST_D3D12_WEAVE_INTERLACE_FLOW_NEED_DATA)
      ret = GST_FLOW_OK;

    if (outbuf)
      ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (trans), outbuf);
  } while (ret == GST_FLOW_OK && outbuf);
}

static gboolean
gst_d3d12_interlace_prepare_convert (GstD3D12Interlace * self,
    GstD3D12Device * device, GstCaps * in_caps, const GstVideoInfo * in_info,
    GstVideoInfo * weave_info)
{
  auto priv = self->priv;

  auto format = GST_VIDEO_INFO_FORMAT (in_info);
  switch (format) {
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
      break;
    default:
      *weave_info = *in_info;
      return TRUE;
  }

  gst_video_info_set_interlaced_format (weave_info, GST_VIDEO_FORMAT_RGBA,
      in_info->interlace_mode, in_info->width, in_info->height);
  GST_VIDEO_INFO_FIELD_ORDER (weave_info) =
      GST_VIDEO_INFO_FIELD_ORDER (in_info);

  GstCaps *caps = gst_video_info_to_caps (weave_info);

  auto ctx = std::make_shared < InterlaceConvCtx > (device);
  ctx->pre_pool = gst_d3d12_buffer_pool_new (device);
  ctx->post_pool = gst_d3d12_buffer_pool_new (device);

  auto config = gst_buffer_pool_get_config (ctx->pre_pool);
  gst_buffer_pool_config_set_params (config, caps, weave_info->size, 0, 0);
  gst_caps_unref (caps);
  if (!gst_buffer_pool_set_config (ctx->pre_pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (ctx->pre_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Pool active failed");
    return FALSE;
  }

  config = gst_buffer_pool_get_config (ctx->post_pool);
  gst_buffer_pool_config_set_params (config, in_caps, in_info->size, 0, 0);

  if (!gst_buffer_pool_set_config (ctx->post_pool, config)) {
    GST_ERROR_OBJECT (self, "Couldn't set pool config");
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (ctx->post_pool, TRUE)) {
    GST_ERROR_OBJECT (self, "Pool active failed");
    return FALSE;
  }

  config = gst_structure_new ("convert-config",
      GST_D3D12_CONVERTER_OPT_SAMPLER_FILTER,
      GST_TYPE_D3D12_CONVERTER_SAMPLER_FILTER,
      D3D12_FILTER_MIN_MAG_MIP_POINT, nullptr);

  ctx->pre_conv = gst_d3d12_converter_new (device,
      nullptr, in_info, weave_info, nullptr, nullptr,
      gst_structure_copy (config));
  if (!ctx->pre_conv) {
    GST_ERROR_OBJECT (self, "Couldn't create pre converter");
    gst_structure_free (config);
    return FALSE;
  }

  ctx->post_conv = gst_d3d12_converter_new (device,
      nullptr, weave_info, in_info, nullptr, nullptr, config);
  if (!ctx->post_conv) {
    GST_ERROR_OBJECT (self, "Couldn't create post converter");
    return FALSE;
  }

  priv->conv_ctx = ctx;

  return TRUE;
}

static gboolean
gst_d3d12_interlace_set_info (GstD3D12BaseFilter * filter,
    GstD3D12Device * device, GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  auto trans = GST_BASE_TRANSFORM (filter);
  auto self = GST_D3D12_INTERLACE (filter);
  auto priv = self->priv;

  gboolean post_msg = FALSE;

  {
    std::lock_guard < std::mutex > lk (priv->lock);

    GstClockTime latency = 0;
    if (priv->pattern == GST_D3D12_INTERLACE_PATTERN_1_1) {
      auto fps_n = in_info->fps_n;
      auto fps_d = in_info->fps_d;
      if (fps_n <= 0 || fps_d <= 0) {
        fps_n = 25;
        fps_d = 1;
      }

      /* We have one frame latency in 1:1 pattern */
      latency = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
    }

    if (latency != priv->latency) {
      priv->latency = latency;
      post_msg = TRUE;
    }

    gst_clear_object (&priv->weave);
    priv->conv_ctx = nullptr;

    priv->in_info = *in_info;

    if (!gst_d3d12_interlace_prepare_convert (self, device, incaps,
            &priv->in_info, &priv->weave_info)) {
      return FALSE;
    }

    priv->use_compute = FALSE;
    if (priv->engine == GST_D3D12_INTERLACE_ENGINE_COMPUTE) {
      priv->use_compute = TRUE;
    } else if (priv->engine == GST_D3D12_INTERLACE_ENGINE_AUTO &&
        !gst_d3d12_device_is_uma (device) && !priv->conv_ctx) {
      /* Since weave shader is full compute shader, in case of dGPU,
       * prefer compute queue so that task can be overlapped with other 3D tasks
       */
      priv->use_compute = TRUE;
    }

    GST_DEBUG_OBJECT (self, "Use compute engine: %d", priv->use_compute);

    priv->weave = gst_d3d12_weave_interlace_new (device,
        &priv->weave_info, (GstD3D12WeaveInterlacPattern) priv->pattern,
        !priv->tff, priv->use_compute);

    if (!priv->weave) {
      GST_ERROR_OBJECT (self, "Couldn't create weave object");
      priv->conv_ctx = nullptr;
      return FALSE;
    }

    gst_d3d12_weave_interlace_set_direction (priv->weave,
        trans->segment.rate >= 0);
  }

  if (post_msg) {
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_message_new_latency (GST_OBJECT_CAST (self)));
  }

  return TRUE;
}

static gboolean
gst_d3d12_interlace_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_d3d12_interlace_drain (self);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (priv->weave)
        gst_d3d12_weave_interlace_flush (priv->weave);
      break;
    case GST_EVENT_SEGMENT:
      if (priv->weave) {
        const GstSegment *segment;
        gst_event_parse_segment (event, &segment);
        if (segment->format == GST_FORMAT_TIME) {
          std::lock_guard < std::mutex > lk (priv->lock);
          gst_d3d12_weave_interlace_set_direction (priv->weave,
              segment->rate >= 0);
        }
      }
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_d3d12_interlace_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime latency;

      {
        std::lock_guard < std::mutex > lk (priv->lock);
        latency = priv->latency;
      }

      if (latency != 0 && GST_CLOCK_TIME_IS_VALID (latency) &&
          !gst_base_transform_is_passthrough (trans)) {
        auto otherpad = (direction == GST_PAD_SRC) ?
            GST_BASE_TRANSFORM_SINK_PAD (trans) :
            GST_BASE_TRANSFORM_SRC_PAD (trans);

        auto ret = gst_pad_peer_query (otherpad, query);
        if (ret) {
          gboolean live;
          GstClockTime min_latency, max_latency;
          gst_query_parse_latency (query, &live, &min_latency, &max_latency);

          GST_DEBUG_OBJECT (self, "peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT
              ", our latency: %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency),
              GST_TIME_ARGS (latency));

          min_latency += latency;
          if (GST_CLOCK_TIME_IS_VALID (max_latency))
            max_latency += latency;

          gst_query_set_latency (query, live, min_latency, max_latency);
        }

        return ret;
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static GstBuffer *
gst_d3d12_interlace_convert (GstD3D12Interlace * self, GstBuffer * buffer,
    gboolean is_preproc)
{
  auto priv = self->priv;
  if (!priv->conv_ctx)
    return buffer;

  GstBuffer *outbuf = nullptr;
  auto ctx = priv->conv_ctx;
  GstBufferPool *pool;
  GstD3D12Converter *conv;
  if (is_preproc) {
    pool = ctx->pre_pool;
    conv = ctx->pre_conv;
  } else {
    pool = ctx->post_pool;
    conv = ctx->post_conv;
  }

  gst_buffer_pool_acquire_buffer (pool, &outbuf, nullptr);
  if (!outbuf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    gst_buffer_unref (buffer);
    return nullptr;
  }

  gst_buffer_copy_into (outbuf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (buffer));

  GstD3D12CmdAlloc *gst_ca;
  if (!gst_d3d12_cmd_alloc_pool_acquire (ctx->ca_pool, &gst_ca)) {
    GST_ERROR_OBJECT (self, "Couldn't acquire command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  auto device = gst_d3d12_device_get_device_handle (ctx->device);
  if (!ctx->cl) {
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        ca, nullptr, IID_PPV_ARGS (&ctx->cl));
  } else {
    hr = ctx->cl->Reset (ca, nullptr);
  }

  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't reset command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  if (!gst_d3d12_converter_convert_buffer (conv, buffer, outbuf,
          fence_data, ctx->cl.Get (), is_preproc ? TRUE : priv->use_compute)) {
    GST_ERROR_OBJECT (self, "Couldn't convert buffer");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  hr = ctx->cl->Close ();
  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  ID3D12CommandList *cmd_list[] = { ctx->cl.Get () };
  hr = gst_d3d12_device_execute_command_lists (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT, 1, cmd_list, &ctx->fence_val);

  if (!gst_d3d12_result (hr, ctx->device)) {
    GST_ERROR_OBJECT (self, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  gst_d3d12_device_set_fence_notify (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT, ctx->fence_val,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  auto fence = gst_d3d12_device_get_fence_handle (ctx->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  gst_d3d12_buffer_set_fence (outbuf, fence, ctx->fence_val, FALSE);

  return outbuf;
}

static GstFlowReturn
gst_d3d12_interlace_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * input)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  /* Let baseclass handle QoS first */
  auto ret = GST_BASE_TRANSFORM_CLASS (parent_class)->submit_input_buffer
      (trans, is_discont, input);
  if (ret != GST_FLOW_OK)
    return ret;

  /* at this moment, baseclass must hold queued_buf */
  g_assert (trans->queued_buf != NULL);
  auto buf = trans->queued_buf;
  trans->queued_buf = nullptr;

  buf = gst_d3d12_interlace_convert (self, buf, TRUE);
  if (!buf)
    return GST_FLOW_ERROR;

  ret = gst_d3d12_weave_interlace_push (priv->weave, buf);
  if (ret == GST_D3D12_WEAVE_INTERLACE_FLOW_NEED_DATA)
    ret = GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_d3d12_interlace_generate_output (GstBaseTransform * trans,
    GstBuffer ** buffer)
{
  auto self = GST_D3D12_INTERLACE (trans);
  auto priv = self->priv;

  if (gst_base_transform_is_passthrough (trans)) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->generate_output (trans,
        buffer);
  }

  GstBuffer *outbuf = nullptr;
  auto ret = gst_d3d12_weave_interlace_pop (priv->weave, &outbuf);
  if (ret == GST_D3D12_WEAVE_INTERLACE_FLOW_NEED_DATA)
    ret = GST_FLOW_OK;

  if (outbuf) {
    outbuf = gst_d3d12_interlace_convert (self, outbuf, FALSE);
    if (!outbuf)
      ret = GST_FLOW_ERROR;
  }

  *buffer = outbuf;

  return ret;
}

static GstFlowReturn
gst_d3d12_interlace_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  /* generate_output() will do actual process */
  return GST_FLOW_OK;
}

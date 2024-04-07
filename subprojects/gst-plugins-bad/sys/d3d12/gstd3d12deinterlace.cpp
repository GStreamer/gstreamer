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

#include "gstd3d12deinterlace.h"
#include "gstd3d12pluginutils.h"
#include "gstd3d12yadif.h"
#include <directx/d3dx12.h>
#include <mutex>
#include <memory>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_deinterlace_debug);
#define GST_CAT_DEFAULT gst_d3d12_deinterlace_debug

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


enum GstD3D12DeinterlaceFields
{
  GST_D3D12_DEINTERLACE_FIELDS_ALL,
  GST_D3D12_DEINTERLACE_FIELDS_TOP,
  GST_D3D12_DEINTERLACE_FIELDS_BOTTOM,
};

#define GST_TYPE_D3D12_DEINTERLACE_FIELDS (gst_d3d12_deinterlace_fields_get_type())
static GType
gst_d3d12_deinterlace_fields_get_type (void)
{
  static GType type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GEnumValue types[] = {
      {GST_D3D12_DEINTERLACE_FIELDS_ALL, "All fields", "all"},
      {GST_D3D12_DEINTERLACE_FIELDS_TOP, "Top fields only", "top"},
      {GST_D3D12_DEINTERLACE_FIELDS_BOTTOM, "Bottom fields only", "bottom"},
      {0, nullptr, nullptr},
    };

    type = g_enum_register_static ("GstD3D12DeinterlaceFields", types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum GstD3D12DeinterlaceEngine
{
  GST_D3D12_DEINTERLACE_ENGINE_AUTO,
  GST_D3D12_DEINTERLACE_ENGINE_3D,
  GST_D3D12_DEINTERLACE_ENGINE_COMPUTE,
};

#define GST_TYPE_D3D12_DEINTERLACE_ENGINE (gst_d3d12_deinterlace_engine_get_type())
static GType
gst_d3d12_deinterlace_engine_get_type (void)
{
  static GType type = 0;

  GST_D3D12_CALL_ONCE_BEGIN {
    static const GEnumValue types[] = {
      {GST_D3D12_DEINTERLACE_ENGINE_AUTO,
          "iGPU uses 3D engine, dGPU uses compute engine", "auto"},
      {GST_D3D12_DEINTERLACE_ENGINE_3D, "3D", "3d"},
      {GST_D3D12_DEINTERLACE_ENGINE_COMPUTE, "Compute", "compute"},
      {0, nullptr, nullptr},
    };

    type = g_enum_register_static ("GstD3D12DeinterlaceEngine", types);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

enum
{
  PROP_0,
  PROP_FIELDS,
  PROP_ENGINE,
};

#define DEFAULT_FIELDS GST_D3D12_DEINTERLACE_FIELDS_ALL
#define DEFAULT_ENGINE GST_D3D12_DEINTERLACE_ENGINE_AUTO


/* *INDENT-OFF* */
struct DeinterlaceConvCtx
{
  DeinterlaceConvCtx (GstD3D12Device * dev)
  {
    device = (GstD3D12Device *) gst_object_ref (dev);
    auto device_handle = gst_d3d12_device_get_device_handle (device);
    ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
        D3D12_COMMAND_LIST_TYPE_DIRECT);
  }

  ~DeinterlaceConvCtx ()
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

struct GstD3D12DeinterlacePrivate
{
  GstD3D12DeinterlacePrivate ()
  {
    fence_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12DeinterlacePrivate ()
  {
    gst_clear_object (&yadif);
    gst_clear_object (&fence_pool);
  }

  std::mutex lock;
  GstD3D12Yadif *yadif = nullptr;
  GstD3D12FenceDataPool *fence_pool = nullptr;
  std::shared_ptr<DeinterlaceConvCtx> conv_ctx;
  GstVideoInfo in_info;
  GstVideoInfo yadif_info;
  GstClockTime latency = 0;
  gboolean use_compute = FALSE;
  GstD3D12DeinterlaceFields fields = DEFAULT_FIELDS;
  GstD3D12DeinterlaceEngine engine = DEFAULT_ENGINE;
};
/* *INDENT-ON* */

struct _GstD3D12Deinterlace
{
  GstD3D12BaseFilter parent;

  GstD3D12DeinterlacePrivate *priv;
};

static void gst_d3d12_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_deinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d12_deinterlace_finalize (GObject * object);
static gboolean gst_d3d12_deinterlace_start (GstBaseTransform * trans);
static gboolean gst_d3d12_deinterlace_stop (GstBaseTransform * trans);
static GstCaps *gst_d3d12_deinterlace_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d12_deinterlace_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d12_deinterlace_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_deinterlace_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_d3d12_deinterlace_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_d3d12_deinterlace_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean gst_d3d12_deinterlace_set_info (GstD3D12BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_d3d12_deinterlace_generate_output (GstBaseTransform *
    trans, GstBuffer ** buffer);
static GstFlowReturn gst_d3d12_deinterlace_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_d3d12_deinterlace_submit_input_buffer (GstBaseTransform
    * trans, gboolean is_discont, GstBuffer * input);

#define gst_d3d12_deinterlace_parent_class parent_class
G_DEFINE_TYPE (GstD3D12Deinterlace, gst_d3d12_deinterlace,
    GST_TYPE_D3D12_BASE_FILTER);

static void
gst_d3d12_deinterlace_class_init (GstD3D12DeinterlaceClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_D3D12_BASE_FILTER_CLASS (klass);

  object_class->set_property = gst_d3d12_deinterlace_set_property;
  object_class->get_property = gst_d3d12_deinterlace_get_property;
  object_class->finalize = gst_d3d12_deinterlace_finalize;

  g_object_class_install_property (object_class, PROP_FIELDS,
      g_param_spec_enum ("fields", "Fields", "Fields to use for deinterlacing",
          GST_TYPE_D3D12_DEINTERLACE_FIELDS, DEFAULT_FIELDS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_ENGINE,
      g_param_spec_enum ("engine", "Engine", "Engine to use",
          GST_TYPE_D3D12_DEINTERLACE_ENGINE, DEFAULT_ENGINE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D12 Deinterlacer",
      "Filter/Deinterlace/Effect/Video/Hardware",
      "A Direct3D12 deinterlacer element",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_stop);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_fixate_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_decide_allocation);
  trans_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_sink_event);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_query);
  trans_class->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_submit_input_buffer);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_generate_output);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_transform);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_d3d12_deinterlace_set_info);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_SAMPLING_METHOD,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_deinterlace_debug, "d3d12deinterlace", 0,
      "d3d12deinterlace");
}

static void
gst_d3d12_deinterlace_init (GstD3D12Deinterlace * self)
{
  self->priv = new GstD3D12DeinterlacePrivate ();
}

static void
gst_d3d12_deinterlace_finalize (GObject * object)
{
  auto self = GST_D3D12_DEINTERLACE (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
is_double_framerate (GstD3D12DeinterlaceFields fields)
{
  if (fields == GST_D3D12_DEINTERLACE_FIELDS_ALL)
    return TRUE;

  return FALSE;
}

static void
gst_d3d12_deinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_DEINTERLACE (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_FIELDS:
    {
      auto fields = (GstD3D12DeinterlaceFields) g_value_get_enum (value);

      if (priv->fields != fields) {
        gboolean reconfig = FALSE;
        if (is_double_framerate (priv->fields) != is_double_framerate (fields))
          reconfig = TRUE;

        priv->fields = fields;
        if (priv->yadif) {
          gst_d3d12_yadif_set_fields (priv->yadif,
              (GstD3D12YadifFields) fields);
        }

        if (reconfig)
          gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (self));
      }
      break;
    }
    case PROP_ENGINE:
      priv->engine = (GstD3D12DeinterlaceEngine) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_deinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_DEINTERLACE (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_FIELDS:
      g_value_set_enum (value, priv->fields);
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
gst_d3d12_deinterlace_start (GstBaseTransform * trans)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
  auto priv = self->priv;

  priv->latency = 0;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans);
}

static gboolean
gst_d3d12_deinterlace_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
  auto priv = self->priv;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    gst_clear_object (&priv->yadif);
    priv->conv_ctx = nullptr;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static GstCaps *
gst_d3d12_deinterlace_remove_interlace_info (GstCaps * caps,
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
gst_d3d12_deinterlace_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
  auto priv = self->priv;

  /* Get all possible caps that we can transform to */
  auto ret = gst_d3d12_deinterlace_remove_interlace_info (caps,
      is_double_framerate (priv->fields));

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
gst_d3d12_deinterlace_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
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
  if (is_double_framerate (priv->fields) &&
      gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d) &&
      fps_n > 0 && fps_d > 0) {
    /* for non-blend method, output framerate will be doubled */
    if (GST_VIDEO_INFO_IS_INTERLACED (&info)) {
      fps_n *= 2;
    }

    gst_caps_set_simple (othercaps,
        "framerate", GST_TYPE_FRACTION, fps_n, fps_d, nullptr);
  }

  auto interlace_mode = gst_structure_get_string (s, "interlace-mode");
  if (g_strcmp0 ("progressive", interlace_mode) == 0) {
    /* Just forward interlace-mode=progressive.
     * By this way, basetransform will enable passthrough for non-interlaced
     * stream*/
    gst_caps_set_simple (othercaps,
        "interlace-mode", G_TYPE_STRING, "progressive", nullptr);
  }

  gst_caps_unref (tmp);

  return gst_caps_fixate (othercaps);
}

static gboolean
gst_d3d12_deinterlace_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto filter = GST_D3D12_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;
  gboolean is_d3d12 = FALSE;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (!decide_query)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_WARNING_OBJECT (filter, "Allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstCapsFeatures *features;
    GstStructure *config;

    features = gst_caps_get_features (caps, 0);

    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
      GST_DEBUG_OBJECT (filter, "upstream support d3d12 memory");
      pool = gst_d3d12_buffer_pool_new (filter->device);
      is_d3d12 = TRUE;
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!is_d3d12) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (filter, "Bufferpool config failed");
      gst_object_unref (pool);
      return FALSE;
    }

    /* d3d12 buffer pool will update buffer size based on allocated texture,
     * get size from config again */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config,
        nullptr, &size, nullptr, nullptr);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_d3d12_deinterlace_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
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

static void
gst_d3d12_deinterlace_drain (GstD3D12Deinterlace * self)
{
  auto trans = GST_BASE_TRANSFORM (self);
  auto priv = self->priv;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf = nullptr;

  if (!priv->yadif)
    return;

  if (gst_base_transform_is_passthrough (trans)) {
    gst_d3d12_yadif_flush (priv->yadif);
    return;
  }

  gst_d3d12_yadif_drain (priv->yadif);
  do {
    outbuf = nullptr;
    ret = gst_d3d12_yadif_pop (priv->yadif, &outbuf);
    if (ret == GST_D3D12_YADIF_FLOW_NEED_DATA)
      ret = GST_FLOW_OK;

    if (outbuf)
      ret = gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (trans), outbuf);
  } while (ret == GST_FLOW_OK && outbuf);
}

static gboolean
gst_d3d12_deinterlace_prepare_convert (GstD3D12Deinterlace * self,
    GstCaps * in_caps, const GstVideoInfo * in_info, GstVideoInfo * yadif_info)
{
  auto filter = GST_D3D12_BASE_FILTER (self);
  auto priv = self->priv;

  auto format = GST_VIDEO_INFO_FORMAT (in_info);
  switch (format) {
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
    case GST_VIDEO_FORMAT_RGB15:
    case GST_VIDEO_FORMAT_BGR15:
      break;
    default:
      *yadif_info = *in_info;
      return TRUE;
  }

  gst_video_info_set_interlaced_format (yadif_info, GST_VIDEO_FORMAT_RGBA,
      in_info->interlace_mode, in_info->width, in_info->height);
  GST_VIDEO_INFO_FIELD_ORDER (yadif_info) =
      GST_VIDEO_INFO_FIELD_ORDER (in_info);

  GstCaps *caps = gst_video_info_to_caps (yadif_info);

  auto ctx = std::make_shared < DeinterlaceConvCtx > (filter->device);
  ctx->pre_pool = gst_d3d12_buffer_pool_new (filter->device);
  ctx->post_pool = gst_d3d12_buffer_pool_new (filter->device);

  auto config = gst_buffer_pool_get_config (ctx->pre_pool);
  gst_buffer_pool_config_set_params (config, caps, yadif_info->size, 0, 0);
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

  ctx->pre_conv = gst_d3d12_converter_new (filter->device,
      nullptr, in_info, yadif_info, nullptr, nullptr,
      gst_structure_copy (config));
  if (!ctx->pre_conv) {
    GST_ERROR_OBJECT (self, "Couldn't create pre converter");
    gst_structure_free (config);
    return FALSE;
  }

  ctx->post_conv = gst_d3d12_converter_new (filter->device,
      nullptr, yadif_info, in_info, nullptr, nullptr, config);
  if (!ctx->post_conv) {
    GST_ERROR_OBJECT (self, "Couldn't create post converter");
    return FALSE;
  }

  priv->conv_ctx = ctx;

  return TRUE;
}

static gboolean
gst_d3d12_deinterlace_set_info (GstD3D12BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  auto trans = GST_BASE_TRANSFORM (filter);
  auto self = GST_D3D12_DEINTERLACE (filter);
  auto priv = self->priv;

  gboolean post_msg = FALSE;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    auto fps_n = in_info->fps_n;
    auto fps_d = in_info->fps_d;
    if (fps_n <= 0 || fps_d <= 0) {
      fps_n = 25;
      fps_d = 1;
    }
    GstClockTime latency = gst_util_uint64_scale (GST_SECOND, fps_d, fps_n);
    if (latency != priv->latency) {
      priv->latency = latency;
      post_msg = TRUE;
    }

    gst_clear_object (&priv->yadif);
    priv->conv_ctx = nullptr;

    if (!GST_VIDEO_INFO_IS_INTERLACED (in_info)) {
      gst_base_transform_set_passthrough (trans, TRUE);
    } else {
      gst_base_transform_set_passthrough (trans, FALSE);
    }

    if (gst_base_transform_is_passthrough (trans))
      return TRUE;

    priv->in_info = *in_info;
    if (!gst_d3d12_deinterlace_prepare_convert (self, incaps, &priv->in_info,
            &priv->yadif_info)) {
      return FALSE;
    }

    priv->use_compute = FALSE;
    if (priv->engine == GST_D3D12_DEINTERLACE_ENGINE_COMPUTE) {
      priv->use_compute = TRUE;
    } else if (priv->engine == GST_D3D12_DEINTERLACE_ENGINE_AUTO &&
        !gst_d3d12_device_is_uma (filter->device) && !priv->conv_ctx) {
      /* Since yadif shader is full compute shader, in case of dGPU,
       * prefer compute queue so that task can be overlapped with other 3D tasks
       */
      priv->use_compute = TRUE;
    }

    GST_DEBUG_OBJECT (self, "Use compute engine: %d", priv->use_compute);

    priv->yadif = gst_d3d12_yadif_new (filter->device, &priv->yadif_info,
        priv->use_compute);
    if (!priv->yadif) {
      GST_ERROR_OBJECT (self, "Couldn't create yadif object");
      priv->conv_ctx = nullptr;
      return FALSE;
    }

    gst_d3d12_yadif_set_direction (priv->yadif, trans->segment.rate >= 0);
    gst_d3d12_yadif_set_fields (priv->yadif,
        (GstD3D12YadifFields) priv->fields);
  }

  if (post_msg) {
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_message_new_latency (GST_OBJECT_CAST (self)));
  }

  return TRUE;
}

static gboolean
gst_d3d12_deinterlace_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
  auto priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_d3d12_deinterlace_drain (self);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (priv->yadif)
        gst_d3d12_yadif_flush (priv->yadif);
      break;
    case GST_EVENT_SEGMENT:
      if (priv->yadif) {
        const GstSegment *segment;
        gst_event_parse_segment (event, &segment);
        if (segment->format == GST_FORMAT_TIME) {
          std::lock_guard < std::mutex > lk (priv->lock);
          gst_d3d12_yadif_set_direction (priv->yadif, segment->rate >= 0);
        }
      }
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_d3d12_deinterlace_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
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
gst_d3d12_deinterlace_convert (GstD3D12Deinterlace * self, GstBuffer * buffer,
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
gst_d3d12_deinterlace_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * input)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
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

  buf = gst_d3d12_deinterlace_convert (self, buf, TRUE);
  if (!buf)
    return GST_FLOW_ERROR;

  ret = gst_d3d12_yadif_push (priv->yadif, buf);
  if (ret == GST_D3D12_YADIF_FLOW_NEED_DATA)
    ret = GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_d3d12_deinterlace_generate_output (GstBaseTransform * trans,
    GstBuffer ** buffer)
{
  auto self = GST_D3D12_DEINTERLACE (trans);
  auto priv = self->priv;

  if (gst_base_transform_is_passthrough (trans)) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->generate_output (trans,
        buffer);
  }

  GstBuffer *outbuf = nullptr;
  auto ret = gst_d3d12_yadif_pop (priv->yadif, &outbuf);
  if (ret == GST_D3D12_YADIF_FLOW_NEED_DATA)
    ret = GST_FLOW_OK;

  if (outbuf) {
    outbuf = gst_d3d12_deinterlace_convert (self, outbuf, FALSE);
    if (!outbuf)
      ret = GST_FLOW_ERROR;
  }

  *buffer = outbuf;

  return ret;
}

static GstFlowReturn
gst_d3d12_deinterlace_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  /* generate_output() will do actual process */
  return GST_FLOW_OK;
}

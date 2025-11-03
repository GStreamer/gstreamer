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
#  include <config.h>
#endif

#include "gstd3d12basefilter.h"
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_base_filter_debug);
#define GST_CAT_DEFAULT gst_d3d12_base_filter_debug

#define META_TAG_VIDEO meta_tag_video_quark
static GQuark meta_tag_video_quark;

enum
{
  PROP_0,
  PROP_ADAPTER,
};

#define DEFAULT_ADAPTER -1

struct _GstD3D12BaseFilterPrivate
{
  ~_GstD3D12BaseFilterPrivate ()
  {
    gst_clear_object (&device);
  }

  GstD3D12Device *device = nullptr;

  gint adapter = DEFAULT_ADAPTER;
  std::recursive_mutex lock;
};

#define gst_d3d12_base_filter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstD3D12BaseFilter, gst_d3d12_base_filter,
    GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_d3d12_base_filter_debug,
        "d3d12basefilter", 0, "d3d12 basefilter"));

static void gst_d3d12_base_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_base_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d12_base_filter_finalize (GObject * object);
static void gst_d3d12_base_filter_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_d3d12_base_filter_start (GstBaseTransform * trans);
static gboolean gst_d3d12_base_filter_stop (GstBaseTransform * trans);
static gboolean gst_d3d12_base_filter_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean
gst_d3d12_base_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static void gst_d3d12_base_filter_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);
static gboolean gst_d3d12_base_filter_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static gboolean
gst_d3d12_base_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_d3d12_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean
gst_d3d12_base_filter_default_decide_allocation (GstD3D12BaseFilter * self,
    GstD3D12Device * device, GstQuery * query);

static gboolean
gst_d3d12_base_filter_default_propose_allocation (GstD3D12BaseFilter * self,
    GstD3D12Device * device, GstQuery * decide_query, GstQuery * query);

static void
gst_d3d12_base_filter_class_init (GstD3D12BaseFilterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_d3d12_base_filter_finalize;
  gobject_class->set_property = gst_d3d12_base_filter_set_property;
  gobject_class->get_property = gst_d3d12_base_filter_get_property;

  /**
   * GstD3D12BaseFilter:adapter:
   *
   * Adapter index for creating device (-1 for default)
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_int ("adapter", "Adapter",
          "Adapter index for creating device (-1 for default)",
          -1, G_MAXINT32, DEFAULT_ADAPTER,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_set_context);

  trans_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_stop);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_set_caps);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_query);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_before_transform);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_transform_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_decide_allocation);

  klass->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_default_propose_allocation);
  klass->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_base_filter_default_decide_allocation);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D12_BASE_FILTER,
      (GstPluginAPIFlags) 0);
  meta_tag_video_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_STR);
}

static void
gst_d3d12_base_filter_init (GstD3D12BaseFilter * self)
{
  self->priv = new GstD3D12BaseFilterPrivate ();
}

static void
gst_d3d12_base_filter_finalize (GObject * object)
{
  auto self = GST_D3D12_BASE_FILTER (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_base_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_BASE_FILTER (object);
  auto priv = self->priv;

  switch (prop_id) {
    case PROP_ADAPTER:
      priv->adapter = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_base_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_BASE_FILTER (object);
  auto priv = self->priv;

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_int (value, priv->adapter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_base_filter_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_D3D12_BASE_FILTER (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_d3d12_handle_set_context (element, context, -1, &priv->device);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d12_base_filter_start (GstBaseTransform * trans)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!gst_d3d12_ensure_element_data (GST_ELEMENT_CAST (self),
          priv->adapter, &priv->device)) {
    GST_ERROR_OBJECT (self, "Failed to get D3D12 device");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_base_filter_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  gst_clear_object (&priv->device);

  return TRUE;
}

static gboolean
gst_d3d12_base_filter_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;
  GstVideoInfo in_info, out_info;
  GstD3D12BaseFilterClass *klass;
  gboolean res;

  if (!gst_video_info_from_caps (&in_info, incaps)) {
    GST_ERROR_OBJECT (self, "Invalid input caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  if (!gst_video_info_from_caps (&out_info, outcaps)) {
    GST_ERROR_OBJECT (self, "Invalid output caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  klass = GST_D3D12_BASE_FILTER_GET_CLASS (self);
  if (klass->set_info) {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!priv->device) {
      GST_ERROR_OBJECT (self, "No available D3D12 device");
      return FALSE;
    }

    res = klass->set_info (self,
        priv->device, incaps, &in_info, outcaps, &out_info);
  } else {
    res = TRUE;
  }

  if (res) {
    self->in_info = in_info;
    self->out_info = out_info;
  }

  return res;
}

static gboolean
gst_d3d12_base_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (gst_d3d12_handle_context_query (GST_ELEMENT (self), query,
              priv->device)) {
        return TRUE;
      }
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static void
gst_d3d12_base_filter_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;
  GstD3D12Memory *dmem;
  GstMemory *mem;
  GstCaps *in_caps = nullptr;
  GstCaps *out_caps = nullptr;

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!gst_is_d3d12_memory (mem))
    return;

  dmem = GST_D3D12_MEMORY_CAST (mem);

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  /* d3d12 devices are singletons per adapter */
  if (gst_d3d12_device_is_equal (dmem->device, priv->device))
    return;

  GST_INFO_OBJECT (self, "Updating device %" GST_PTR_FORMAT " -> %"
      GST_PTR_FORMAT, priv->device, dmem->device);

  gst_object_unref (priv->device);
  priv->device = (GstD3D12Device *) gst_object_ref (dmem->device);

  in_caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM_SINK_PAD (trans));
  if (!in_caps) {
    GST_WARNING_OBJECT (self, "sinkpad has null caps");
    goto out;
  }

  out_caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM_SRC_PAD (trans));
  if (!out_caps) {
    GST_WARNING_OBJECT (self, "Has no configured output caps");
    goto out;
  }

  /* subclass will update internal object.
   * Note that gst_base_transform_reconfigure() might not trigger this
   * unless caps was changed meanwhile */
  gst_d3d12_base_filter_set_caps (trans, in_caps, out_caps);

  /* Mark reconfigure so that we can update pool */
  gst_base_transform_reconfigure_src (trans);

out:
  gst_clear_caps (&in_caps);
  gst_clear_caps (&out_caps);

  return;
}

static gboolean
gst_d3d12_base_filter_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  const GstMetaInfo *info = meta->info;
  const gchar *const *tags;

  tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api, META_TAG_VIDEO))) {
    return TRUE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans, outbuf,
      meta, inbuf);
}

static gboolean
gst_d3d12_base_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;
  auto klass = GST_D3D12_BASE_FILTER_GET_CLASS (self);

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query)) {
    return FALSE;
  }

  if (klass->propose_allocation) {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!priv->device) {
      GST_DEBUG_OBJECT (self, "No device configured");
      return FALSE;
    }

    return klass->propose_allocation (self, priv->device, decide_query, query);
  }

  return TRUE;
}

static gboolean
gst_d3d12_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  auto self = GST_D3D12_BASE_FILTER (trans);
  auto priv = self->priv;
  auto klass = GST_D3D12_BASE_FILTER_GET_CLASS (self);

  if (klass->decide_allocation) {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!priv->device) {
      GST_DEBUG_OBJECT (self, "No device configured");
      return FALSE;
    }

    if (!klass->decide_allocation (self, priv->device, query))
      return FALSE;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_d3d12_base_filter_default_propose_allocation (GstD3D12BaseFilter * self,
    GstD3D12Device * device, GstQuery * decide_query, GstQuery * query)
{
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCaps *caps;
  guint n_pools, i;
  guint size;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
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
        if (!gst_d3d12_device_is_equal (dpool->device, device))
          gst_clear_object (&pool);
      }
    }
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (device);

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (device, &info,
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
    GST_ERROR_OBJECT (self, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

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
gst_d3d12_base_filter_default_decide_allocation (GstD3D12BaseFilter * self,
    GstD3D12Device * device, GstQuery * query)
{
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
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  GstD3D12Format device_format;
  if (!gst_d3d12_device_get_format (device,
          GST_VIDEO_INFO_FORMAT (&info), &device_format)) {
    GST_ERROR_OBJECT (self, "Couldn't get device foramt");
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
    d3d12_params = gst_d3d12_allocation_params_new (device, &info,
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

  return TRUE;
}

GstD3D12Device *
gst_d3d12_base_filter_get_device (GstD3D12BaseFilter * filter)
{
  auto priv = filter->priv;
  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!priv->device)
    return nullptr;

  return (GstD3D12Device *) gst_object_ref (priv->device);
}

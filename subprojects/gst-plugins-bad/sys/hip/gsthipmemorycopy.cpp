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
#include <config.h>
#endif

#include "gsthip.h"
#include "gsthipmemorycopy.h"
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_hip_memory_copy_debug);
#define GST_CAT_DEFAULT gst_hip_memory_copy_debug

#define GST_HIP_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, YUY2, UYVY, RGBP, " \
    "BGRP, GBR, GBR_10LE, GBR_12LE, GBR_16LE, GBRA, VUYA }"

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_HIP_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE (GST_HIP_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_HIP_FORMATS)));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_HIP_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE (GST_HIP_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_HIP_FORMATS)));

struct _GstHipMemoryCopyPrivate
{
  gboolean is_uploader;
    std::recursive_mutex lock;
};

static void gst_hip_memory_copy_finalize (GObject * object);

static GstCaps *gst_hip_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_hip_memory_copy_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_hip_memory_copy_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static GstFlowReturn gst_hip_memory_copy_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

#define gst_hip_memory_copy_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstHipMemoryCopy, gst_hip_memory_copy,
    GST_TYPE_HIP_BASE_FILTER);

static void
gst_hip_memory_copy_class_init (GstHipMemoryCopyClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->finalize = gst_hip_memory_copy_finalize;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_transform_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_memory_copy_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_hip_memory_copy_transform);

  gst_type_mark_as_plugin_api (GST_TYPE_HIP_MEMORY_COPY, (GstPluginAPIFlags) 0);
  GST_DEBUG_CATEGORY_INIT (gst_hip_memory_copy_debug,
      "hipmemorycopy", 0, "hipmemorycopy");
}

static void
gst_hip_memory_copy_init (GstHipMemoryCopy * self)
{
  self->priv = new GstHipMemoryCopyPrivate ();
}

static void
gst_hip_memory_copy_finalize (GObject * object)
{
  auto self = GST_HIP_MEMORY_COPY (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++) {
    gst_caps_set_features (tmp, i,
        gst_caps_features_new_single_static_str (feature_name));
  }

  return tmp;
}

static GstCaps *
gst_hip_memory_copy_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto self = GST_HIP_MEMORY_COPY (trans);
  auto priv = self->priv;
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (self,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    if (priv->is_uploader) {
      auto caps_hip =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY);
      tmp = gst_caps_merge (caps_hip, gst_caps_ref (caps));
    } else {
      auto caps_sys =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
      tmp = gst_caps_merge (caps_sys, gst_caps_ref (caps));
    }
  } else {
    if (priv->is_uploader) {
      auto caps_sys =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
      tmp = gst_caps_merge (caps_sys, gst_caps_ref (caps));
    } else {
      auto caps_hip =
          _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY);
      tmp = gst_caps_merge (caps_hip, gst_caps_ref (caps));
    }
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

static gboolean
gst_hip_memory_copy_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto filter = GST_HIP_BASE_FILTER (trans);
  auto self = GST_HIP_MEMORY_COPY (trans);
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCaps *caps;
  guint size;
  bool is_hip = false;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (!decide_query)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_WARNING_OBJECT (self, "Allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) == 0) {
    auto features = gst_caps_get_features (caps, 0);
    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
      GST_DEBUG_OBJECT (self, "upstream support hip memory");
      pool = gst_hip_buffer_pool_new (filter->device);
      is_hip = true;
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (!is_hip) {
      gst_buffer_pool_config_add_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    }

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (self, "Bufferpool config failed");
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
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_hip_memory_copy_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  auto filter = GST_HIP_BASE_FILTER (trans);
  auto self = GST_HIP_MEMORY_COPY (trans);
  GstBufferPool *pool = nullptr;
  GstVideoInfo info;
  guint min, max, size;
  GstCaps *caps = nullptr;
  bool update_pool = false;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_WARNING_OBJECT (self, "Allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = true;
  } else {
    size = info.size;
    min = max = 0;
  }

  auto features = gst_caps_get_features (caps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
    GST_DEBUG_OBJECT (self, "upstream support hip memory");
    if (pool) {
      if (!GST_IS_HIP_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto hpool = GST_HIP_BUFFER_POOL (pool);
        if (!gst_hip_device_is_equal (hpool->device, filter->device))
          gst_clear_object (&pool);
      }
    }

    if (!pool)
      pool = gst_hip_buffer_pool_new (filter->device);
  } else {
    pool = gst_video_buffer_pool_new ();
  }

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

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

static GstFlowReturn
gst_hip_memory_copy_system_copy (GstHipMemoryCopy * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  auto filter = GST_HIP_BASE_FILTER (self);
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_video_frame_map (&in_frame, &filter->in_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame, &filter->out_info, outbuf,
          GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    gst_video_frame_unmap (&in_frame);
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_copy (&out_frame, &in_frame)) {
    GST_ERROR_OBJECT (self, "Copy failed");
    ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;
}

static GstFlowReturn
gst_hip_memory_copy_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_HIP_MEMORY_COPY (trans);

  return gst_hip_memory_copy_system_copy (self, inbuf, outbuf);
}

struct _GstHipUpload
{
  GstHipMemoryCopy parent;
};

G_DEFINE_TYPE (GstHipUpload, gst_hip_upload, GST_TYPE_HIP_MEMORY_COPY);

static void
gst_hip_upload_class_init (GstHipUploadClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "HIP Uploader", "Filter/Video",
      "Uploads system memory into HIP device memory",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_hip_upload_init (GstHipUpload * self)
{
  auto memcpy = GST_HIP_MEMORY_COPY (self);
  memcpy->priv->is_uploader = true;
}

struct _GstHipDownload
{
  GstHipMemoryCopy parent;
};

G_DEFINE_TYPE (GstHipDownload, gst_hip_download, GST_TYPE_HIP_MEMORY_COPY);

static void
gst_hip_download_class_init (GstHipDownloadClass * klass)
{
  auto element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "HIP Downloader", "Filter/Video",
      "Downloads HIP device memory into system memory",
      "Seungha Yang <seungha@centricular.com>");
}

static void
gst_hip_download_init (GstHipDownload * self)
{
  auto memcpy = GST_HIP_MEMORY_COPY (self);
  memcpy->priv->is_uploader = false;
}

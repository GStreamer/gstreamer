/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
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
 * element-cudaupload:
 *
 * Uploads data to NVIDA GPU via CUDA APIs
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstcudaupload.h"
#include "gstcudaformat.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_upload_debug);
#define GST_CAT_DEFAULT gst_cuda_upload_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_CUDA_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY,
            GST_CUDA_FORMATS)));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY,
            GST_CUDA_FORMATS) ";" GST_VIDEO_CAPS_MAKE (GST_CUDA_FORMATS)));
struct _GstCudaUpload
{
  GstCudaBaseTransform parent;
};

#define gst_cuda_upload_parent_class parent_class
G_DEFINE_TYPE (GstCudaUpload, gst_cuda_upload, GST_TYPE_CUDA_BASE_TRANSFORM);

static GstCaps *gst_cuda_upload_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_cuda_upload_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_cuda_upload_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static GstFlowReturn gst_cuda_upload_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static void
gst_cuda_upload_class_init (GstCudaUploadClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "CUDA uploader", "Filter/Video",
      "Uploads data into NVIDA GPU via CUDA APIs",
      "Seungha Yang <seungha.yang@navercorp.com>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_upload_transform_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_upload_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_upload_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cuda_upload_transform);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_upload_debug,
      "cudaupload", 0, "cudaupload Element");
}

static void
gst_cuda_upload_init (GstCudaUpload * upload)
{
}

static GstCaps *
_set_caps_features (const GstCaps * caps, const gchar * feature_name)
{
  GstCaps *tmp = gst_caps_copy (caps);
  guint n = gst_caps_get_size (tmp);
  guint i = 0;

  for (i = 0; i < n; i++)
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (feature_name));

  return tmp;
}

static GstCaps *
gst_cuda_upload_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  GST_DEBUG_OBJECT (trans,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  if (direction == GST_PAD_SINK) {
    tmp = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);
    tmp = gst_caps_merge (gst_caps_ref (caps), tmp);
  } else {
    GstCaps *newcaps;
    tmp = gst_caps_ref (caps);

    newcaps = _set_caps_features (caps, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
    tmp = gst_caps_merge (tmp, newcaps);
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
gst_cuda_upload_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstCapsFeatures *features;
    GstStructure *config;

    features = gst_caps_get_features (caps, 0);

    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
      GST_DEBUG_OBJECT (ctrans, "upstream support CUDA memory");
      pool = gst_cuda_buffer_pool_new (ctrans->context);
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (ctrans, "failed to set config");
      gst_object_unref (pool);
      return FALSE;
    }

    /* Get updated size by cuda buffer pool */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);

    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_cuda_upload_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;
  gboolean need_cuda = FALSE;
  GstCapsFeatures *features;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  features = gst_caps_get_features (outcaps, 0);
  if (features && gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    need_cuda = TRUE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (need_cuda && pool) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        GstCudaBufferPool *cpool = GST_CUDA_BUFFER_POOL (pool);

        if (cpool->context != ctrans->context) {
          gst_clear_object (&pool);
        }
      }
    }

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;
    gst_video_info_from_caps (&vinfo, outcaps);
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = max = 0;
  }

  if (!pool) {
    GST_DEBUG_OBJECT (ctrans, "create our pool");

    if (need_cuda)
      pool = gst_cuda_buffer_pool_new (ctrans->context);
    else
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* Get updated size by cuda buffer pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
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
gst_cuda_upload_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, &ctrans->in_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (ctrans, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame,
          &ctrans->out_info, outbuf, GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);

    GST_ERROR_OBJECT (ctrans, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!ret) {
    GST_ERROR_OBJECT (ctrans, "Failed to copy frame");
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

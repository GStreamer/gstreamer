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
 * GstCudaBaseTransform:
 *
 * Base class for CUDA transformers
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstcudabasetransform.h"
#include "gstcudautils.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_base_transform_debug);
#define GST_CAT_DEFAULT gst_cuda_base_transform_debug

enum
{
  PROP_0,
  PROP_DEVICE_ID,
};

#define DEFAULT_DEVICE_ID -1

#define gst_cuda_base_transform_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstCudaBaseTransform, gst_cuda_base_transform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_cuda_base_transform_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cuda_base_transform_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_cuda_base_transform_dispose (GObject * object);
static void gst_cuda_base_transform_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_cuda_base_transform_start (GstBaseTransform * trans);
static gboolean gst_cuda_base_transform_stop (GstBaseTransform * trans);
static gboolean gst_cuda_base_transform_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_cuda_base_transform_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_cuda_base_transform_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_cuda_base_transform_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_cuda_base_transform_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_cuda_base_transform_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static GstFlowReturn
gst_cuda_base_transform_transform_frame_default (GstCudaBaseTransform * filter,
    GstVideoFrame * in_frame, GstCudaMemory * in_cuda_mem,
    GstVideoFrame * out_frame, GstCudaMemory * out_cuda_mem);

static void
gst_cuda_base_transform_class_init (GstCudaBaseTransformClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_cuda_base_transform_set_property;
  gobject_class->get_property = gst_cuda_base_transform_get_property;
  gobject_class->dispose = gst_cuda_base_transform_dispose;

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_int ("cuda-device-id",
          "Cuda Device ID",
          "Set the GPU device to use for operations (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
          G_PARAM_STATIC_STRINGS));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_cuda_base_transform_set_context);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_cuda_base_transform_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_cuda_base_transform_stop);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_cuda_base_transform_set_caps);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_cuda_base_transform_transform);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_cuda_base_transform_get_unit_size);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_base_transform_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_base_transform_decide_allocation);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_cuda_base_transform_query);

  klass->transform_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_base_transform_transform_frame_default);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_base_transform_debug,
      "cudabasefilter", 0, "cudabasefilter Element");
}

static void
gst_cuda_base_transform_init (GstCudaBaseTransform * filter)
{
  filter->device_id = DEFAULT_DEVICE_ID;

  filter->negotiated = FALSE;
}

static void
gst_cuda_base_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (object);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      filter->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_base_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (object);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, filter->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_base_transform_dispose (GObject * object)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (object);

  gst_clear_object (&filter->context);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_cuda_base_transform_set_context (GstElement * element, GstContext * context)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (element);

  gst_cuda_handle_set_context (element,
      context, filter->device_id, &filter->context);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_cuda_base_transform_start (GstBaseTransform * trans)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);
  CUresult cuda_ret;

  if (!gst_cuda_ensure_element_context (GST_ELEMENT_CAST (filter),
          filter->device_id, &filter->context)) {
    GST_ERROR_OBJECT (filter, "Failed to get CUDA context");
    return FALSE;
  }

  if (gst_cuda_context_push (filter->context)) {
    cuda_ret = CuStreamCreate (&filter->cuda_stream, CU_STREAM_DEFAULT);
    if (!gst_cuda_result (cuda_ret)) {
      GST_WARNING_OBJECT (filter,
          "Could not create cuda stream, will use default stream");
      filter->cuda_stream = NULL;
    }
    gst_cuda_context_pop (NULL);
  }

  return TRUE;
}

static gboolean
gst_cuda_base_transform_stop (GstBaseTransform * trans)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);

  if (filter->context && filter->cuda_stream) {
    if (gst_cuda_context_push (filter->context)) {
      gst_cuda_result (CuStreamDestroy (filter->cuda_stream));
      gst_cuda_context_pop (NULL);
    }
  }

  gst_clear_object (&filter->context);
  filter->cuda_stream = NULL;

  return TRUE;
}

static gboolean
gst_cuda_base_transform_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoInfo in_info, out_info;
  GstCudaBaseTransformClass *klass;
  gboolean res;

  if (!filter->context) {
    GST_ERROR_OBJECT (filter, "No available CUDA context");
    return FALSE;
  }

  /* input caps */
  if (!gst_video_info_from_caps (&in_info, incaps))
    goto invalid_caps;

  /* output caps */
  if (!gst_video_info_from_caps (&out_info, outcaps))
    goto invalid_caps;

  klass = GST_CUDA_BASE_TRANSFORM_GET_CLASS (filter);
  if (klass->set_info)
    res = klass->set_info (filter, incaps, &in_info, outcaps, &out_info);
  else
    res = TRUE;

  if (res) {
    filter->in_info = in_info;
    filter->out_info = out_info;
  }

  filter->negotiated = res;

  return res;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (filter, "invalid caps");
    filter->negotiated = FALSE;
    return FALSE;
  }
}

static gboolean
gst_cuda_base_transform_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps (&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE (&info);

  return TRUE;
}

static GstFlowReturn
gst_cuda_base_transform_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);
  GstCudaBaseTransformClass *fclass =
      GST_CUDA_BASE_TRANSFORM_GET_CLASS (filter);
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapFlags in_map_flags, out_map_flags;
  GstMemory *mem;
  GstCudaMemory *in_cuda_mem = NULL;
  GstCudaMemory *out_cuda_mem = NULL;

  if (G_UNLIKELY (!filter->negotiated))
    goto unknown_format;

  in_map_flags = GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;
  out_map_flags = GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF;

  in_cuda_mem = out_cuda_mem = FALSE;

  if (gst_buffer_n_memory (inbuf) == 1 &&
      (mem = gst_buffer_peek_memory (inbuf, 0)) && gst_is_cuda_memory (mem)) {
    GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);

    if (cmem->context == filter->context ||
        gst_cuda_context_get_handle (cmem->context) ==
        gst_cuda_context_get_handle (filter->context) ||
        (gst_cuda_context_can_access_peer (cmem->context, filter->context) &&
            gst_cuda_context_can_access_peer (filter->context,
                cmem->context))) {
      in_map_flags |= GST_MAP_CUDA;
      in_cuda_mem = cmem;
    }
  }

  if (gst_buffer_n_memory (outbuf) == 1 &&
      (mem = gst_buffer_peek_memory (outbuf, 0)) && gst_is_cuda_memory (mem)) {
    GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);

    if (cmem->context == filter->context ||
        gst_cuda_context_get_handle (cmem->context) ==
        gst_cuda_context_get_handle (filter->context) ||
        (gst_cuda_context_can_access_peer (cmem->context, filter->context) &&
            gst_cuda_context_can_access_peer (filter->context,
                cmem->context))) {
      out_map_flags |= GST_MAP_CUDA;
      out_cuda_mem = cmem;
    }
  }

  if (!gst_video_frame_map (&in_frame, &filter->in_info, inbuf, in_map_flags))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, &filter->out_info, outbuf,
          out_map_flags)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  ret = fclass->transform_frame (filter, &in_frame, in_cuda_mem, &out_frame,
      out_cuda_mem);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;

  /* ERRORS */
unknown_format:
  {
    GST_ELEMENT_ERROR (filter, CORE, NOT_IMPLEMENTED, (NULL),
        ("unknown format"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
invalid_buffer:
  {
    GST_ELEMENT_WARNING (trans, CORE, NOT_IMPLEMENTED, (NULL),
        ("invalid video buffer received"));
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_cuda_base_transform_transform_frame_default (GstCudaBaseTransform * filter,
    GstVideoFrame * in_frame, GstCudaMemory * in_cuda_mem,
    GstVideoFrame * out_frame, GstCudaMemory * out_cuda_mem)
{
  gint i;
  GstFlowReturn ret = GST_FLOW_OK;

  if (in_cuda_mem || out_cuda_mem) {
    if (!gst_cuda_context_push (filter->context)) {
      GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
          ("Cannot push CUDA context"));

      return GST_FLOW_ERROR;
    }

    for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (in_frame); i++) {
      CUDA_MEMCPY2D param = { 0, };
      guint width, height;

      width = GST_VIDEO_FRAME_COMP_WIDTH (in_frame, i) *
          GST_VIDEO_FRAME_COMP_PSTRIDE (in_frame, i);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (in_frame, i);

      if (in_cuda_mem) {
        param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
        param.srcDevice = in_cuda_mem->data + in_cuda_mem->offset[i];
        param.srcPitch = in_cuda_mem->stride;
      } else {
        param.srcMemoryType = CU_MEMORYTYPE_HOST;
        param.srcHost = GST_VIDEO_FRAME_PLANE_DATA (in_frame, i);
        param.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, i);
      }

      if (out_cuda_mem) {
        param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        param.dstDevice = out_cuda_mem->data + out_cuda_mem->offset[i];
        param.dstPitch = out_cuda_mem->stride;
      } else {
        param.dstMemoryType = CU_MEMORYTYPE_HOST;
        param.dstHost = GST_VIDEO_FRAME_PLANE_DATA (out_frame, i);
        param.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, i);
      }

      param.WidthInBytes = width;
      param.Height = height;

      if (!gst_cuda_result (CuMemcpy2DAsync (&param, filter->cuda_stream))) {
        gst_cuda_context_pop (NULL);
        GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
            ("Cannot upload input video frame"));

        return GST_FLOW_ERROR;
      }
    }

    CuStreamSynchronize (filter->cuda_stream);

    gst_cuda_context_pop (NULL);
  } else {
    for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (in_frame); i++) {
      if (!gst_video_frame_copy_plane (out_frame, in_frame, i)) {
        GST_ERROR_OBJECT (filter, "Couldn't copy %dth plane", i);

        return GST_FLOW_ERROR;
      }
    }
  }

  return ret;
}

static gboolean
gst_cuda_base_transform_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);
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
    GstVideoAlignment align;
    GstAllocationParams params = { 0, 31, 0, 0, };
    GstAllocator *allocator = NULL;
    gint i;

    features = gst_caps_get_features (caps, 0);

    if (features && gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
      GST_DEBUG_OBJECT (filter, "upstream support CUDA memory");
      pool = gst_cuda_buffer_pool_new (filter->context);
    } else {
      pool = gst_video_buffer_pool_new ();
    }

    config = gst_buffer_pool_get_config (pool);

    gst_video_alignment_reset (&align);
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      align.stride_align[i] = 31;
    }
    gst_video_info_align (&info, &align);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

    gst_buffer_pool_config_set_video_alignment (config, &align);
    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_pool (query, pool, size, 0, 0);

    if (gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
      if (params.align < 31)
        params.align = 31;

      gst_query_add_allocation_param (query, allocator, &params);
      gst_buffer_pool_config_set_allocator (config, allocator, &params);
    }

    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;

    gst_object_unref (pool);
  }

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static gboolean
gst_cuda_base_transform_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);
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
    if (need_cuda && pool && !GST_IS_CUDA_BUFFER_POOL (pool)) {
      /* when cuda device memory is supported, but pool is not cudabufferpool */
      gst_object_unref (pool);
      pool = NULL;
    }

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;
    gst_video_info_from_caps (&vinfo, outcaps);
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = max = 0;
  }

  if (!pool) {
    GST_DEBUG_OBJECT (filter, "create our pool");

    if (need_cuda)
      pool = gst_cuda_buffer_pool_new (filter->context);
    else
      pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);
  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
gst_cuda_base_transform_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
  GstCudaBaseTransform *filter = GST_CUDA_BASE_TRANSFORM (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      gboolean ret;
      ret = gst_cuda_handle_context_query (GST_ELEMENT (filter), query,
          filter->context);
      if (ret)
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
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
 * GstCudaBaseFilter:
 *
 * Base class for CUDA filters
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstcudabasefilter.h"
#include "gstcudaformat.h"
#include <gst/cuda/gstcudautils.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_cuda_base_filter_debug);
#define GST_CAT_DEFAULT gst_cuda_base_filter_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_FORMATS))
    );

#define gst_cuda_base_filter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstCudaBaseFilter,
    gst_cuda_base_filter, GST_TYPE_CUDA_BASE_TRANSFORM);

static void gst_cuda_base_filter_dispose (GObject * object);
static gboolean
gst_cuda_base_filter_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_cuda_base_filter_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static GstFlowReturn gst_cuda_base_filter_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_cuda_base_filter_set_info (GstCudaBaseTransform * btrans,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

static void
gst_cuda_base_filter_class_init (GstCudaBaseFilterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstCudaBaseTransformClass *btrans_class =
      GST_CUDA_BASE_TRANSFORM_CLASS (klass);

  gobject_class->dispose = gst_cuda_base_filter_dispose;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_base_filter_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_base_filter_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cuda_base_filter_transform);

  btrans_class->set_info = GST_DEBUG_FUNCPTR (gst_cuda_base_filter_set_info);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_base_filter_debug,
      "cudabasefilter", 0, "CUDA Base Filter");

  gst_type_mark_as_plugin_api (GST_TYPE_CUDA_BASE_FILTER, 0);
}

static void
gst_cuda_base_filter_init (GstCudaBaseFilter * convert)
{
}

static void
gst_cuda_base_filter_dispose (GObject * object)
{
  GstCudaBaseFilter *filter = GST_CUDA_BASE_FILTER (object);

  if (filter->converter) {
    gst_cuda_converter_free (filter->converter);
    filter->converter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_cuda_base_filter_set_info (GstCudaBaseTransform * btrans, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstCudaBaseFilter *filter = GST_CUDA_BASE_FILTER (btrans);

  if (filter->converter)
    gst_cuda_converter_free (filter->converter);

  filter->converter =
      gst_cuda_converter_new (in_info, out_info, btrans->context);

  if (!filter->converter) {
    GST_ERROR_OBJECT (filter, "could not create converter");
    return FALSE;
  }

  GST_DEBUG_OBJECT (filter, "reconfigured %d %d",
      GST_VIDEO_INFO_FORMAT (in_info), GST_VIDEO_INFO_FORMAT (out_info));

  return TRUE;
}

static gboolean
gst_cuda_base_filter_propose_allocation (GstBaseTransform * trans,
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
    GstStructure *config;

    pool = gst_cuda_buffer_pool_new (ctrans->context);

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
gst_cuda_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
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

    pool = gst_cuda_buffer_pool_new (ctrans->context);
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
gst_cuda_base_filter_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstCudaBaseFilter *self = GST_CUDA_BASE_FILTER (trans);
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory *mem;

  if (gst_buffer_n_memory (inbuf) != 1) {
    GST_ERROR_OBJECT (self, "Invalid input buffer");
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_ERROR_OBJECT (self, "Input buffer is not CUDA");
    return GST_FLOW_ERROR;
  }

  if (gst_buffer_n_memory (outbuf) != 1) {
    GST_ERROR_OBJECT (self, "Invalid output buffer");
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_ERROR_OBJECT (self, "Input buffer is not CUDA");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&in_frame, &ctrans->in_info, inbuf,
          GST_MAP_READ | GST_MAP_CUDA)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame, &ctrans->out_info, outbuf,
          GST_MAP_WRITE | GST_MAP_CUDA)) {
    gst_video_frame_unmap (&in_frame);
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_cuda_converter_convert_frame (self->converter, &in_frame, &out_frame,
          ctrans->cuda_stream)) {
    GST_ERROR_OBJECT (self, "Failed to convert frame");
    ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;
}

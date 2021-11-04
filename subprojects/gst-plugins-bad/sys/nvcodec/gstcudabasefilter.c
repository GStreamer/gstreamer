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
#include "gstcudautils.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_cuda_base_filter_debug);
#define GST_CAT_DEFAULT gst_cuda_base_filter_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_CONVERTER_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_CONVERTER_FORMATS))
    );

#define gst_cuda_base_filter_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstCudaBaseFilter,
    gst_cuda_base_filter, GST_TYPE_CUDA_BASE_TRANSFORM);

static void gst_cuda_base_filter_dispose (GObject * object);
static GstFlowReturn
gst_cuda_base_filter_transform_frame (GstCudaBaseTransform * btrans,
    GstVideoFrame * in_frame, GstCudaMemory * in_cuda_mem,
    GstVideoFrame * out_frame, GstCudaMemory * out_cuda_mem);
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

  btrans_class->set_info = GST_DEBUG_FUNCPTR (gst_cuda_base_filter_set_info);
  btrans_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_base_filter_transform_frame);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_base_filter_debug,
      "cudabasefilter", 0, "CUDA Base Filter");
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

  if (filter->in_fallback) {
    gst_memory_unref (GST_MEMORY_CAST (filter->in_fallback));
    filter->in_fallback = NULL;
  }

  if (filter->out_fallback) {
    gst_memory_unref (GST_MEMORY_CAST (filter->out_fallback));
    filter->out_fallback = NULL;
  }

  gst_clear_object (&filter->allocator);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_cuda_base_filter_configure (GstCudaBaseFilter * filter,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  GstCudaBaseTransform *btrans = GST_CUDA_BASE_TRANSFORM (filter);

  /* cleanup internal pool */
  if (filter->in_fallback) {
    gst_memory_unref (GST_MEMORY_CAST (filter->in_fallback));
    filter->in_fallback = NULL;
  }

  if (filter->out_fallback) {
    gst_memory_unref (GST_MEMORY_CAST (filter->out_fallback));
    filter->out_fallback = NULL;
  }

  if (!filter->allocator)
    filter->allocator = gst_cuda_allocator_new (btrans->context);

  if (!filter->allocator) {
    GST_ERROR_OBJECT (filter, "Failed to create CUDA allocator");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_cuda_base_filter_set_info (GstCudaBaseTransform * btrans, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstCudaBaseFilter *filter = GST_CUDA_BASE_FILTER (btrans);

  if (!gst_cuda_base_filter_configure (filter, in_info, out_info)) {
    return FALSE;
  }

  if (filter->converter)
    gst_cuda_converter_free (filter->converter);

  filter->converter =
      gst_cuda_converter_new (in_info, out_info, btrans->context);

  if (filter->converter == NULL)
    goto no_converter;

  GST_DEBUG_OBJECT (filter, "reconfigured %d %d",
      GST_VIDEO_INFO_FORMAT (in_info), GST_VIDEO_INFO_FORMAT (out_info));

  return TRUE;

no_converter:
  {
    GST_ERROR_OBJECT (filter, "could not create converter");
    return FALSE;
  }
}

static GstFlowReturn
gst_cuda_base_filter_transform_frame (GstCudaBaseTransform * btrans,
    GstVideoFrame * in_frame, GstCudaMemory * in_cuda_mem,
    GstVideoFrame * out_frame, GstCudaMemory * out_cuda_mem)
{
  GstCudaBaseFilter *filter = GST_CUDA_BASE_FILTER (btrans);
  gboolean conv_ret;
  GstCudaMemory *in_mem;
  GstCudaMemory *out_mem;
  gint i;

  if (in_cuda_mem) {
    in_mem = in_cuda_mem;
  } else {
    if (!filter->in_fallback) {
      GstCudaAllocationParams params;

      memset (&params, 0, sizeof (GstCudaAllocationParams));
      params.info = btrans->in_info;

      filter->in_fallback =
          (GstCudaMemory *) gst_cuda_allocator_alloc (filter->allocator,
          GST_VIDEO_INFO_SIZE (&params.info), &params);
    }

    if (!filter->in_fallback) {
      GST_ERROR_OBJECT (filter, "Couldn't allocate fallback memory");
      return GST_FLOW_ERROR;
    }

    GST_TRACE_OBJECT (filter, "use CUDA fallback memory input");

    if (!gst_cuda_context_push (btrans->context)) {
      GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
          ("Cannot push CUDA context"));
      return FALSE;
    }

    /* upload frame to device memory */
    for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (in_frame); i++) {
      CUDA_MEMCPY2D param = { 0, };
      guint width, height;

      width = GST_VIDEO_FRAME_COMP_WIDTH (in_frame, i) *
          GST_VIDEO_FRAME_COMP_PSTRIDE (in_frame, i);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (in_frame, i);

      param.srcMemoryType = CU_MEMORYTYPE_HOST;
      param.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (in_frame, i);
      param.srcHost = GST_VIDEO_FRAME_PLANE_DATA (in_frame, i);
      param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
      param.dstPitch = filter->in_fallback->stride;
      param.dstDevice =
          filter->in_fallback->data + filter->in_fallback->offset[i];
      param.WidthInBytes = width;
      param.Height = height;

      if (!gst_cuda_result (CuMemcpy2DAsync (&param, btrans->cuda_stream))) {
        gst_cuda_context_pop (NULL);
        GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
            ("Cannot upload input video frame"));
        return GST_FLOW_ERROR;
      }
    }

    gst_cuda_result (CuStreamSynchronize (btrans->cuda_stream));
    gst_cuda_context_pop (NULL);

    in_mem = filter->in_fallback;
  }

  if (out_cuda_mem) {
    out_mem = out_cuda_mem;
  } else {
    if (!filter->out_fallback) {
      GstCudaAllocationParams params;

      memset (&params, 0, sizeof (GstCudaAllocationParams));
      params.info = btrans->out_info;

      filter->out_fallback =
          (GstCudaMemory *) gst_cuda_allocator_alloc (filter->allocator,
          GST_VIDEO_INFO_SIZE (&params.info), &params);
    }

    if (!filter->out_fallback) {
      GST_ERROR_OBJECT (filter, "Couldn't allocate fallback memory");
      return GST_FLOW_ERROR;
    }

    out_mem = filter->out_fallback;
  }

  conv_ret =
      gst_cuda_converter_frame (filter->converter, in_mem, &btrans->in_info,
      out_mem, &btrans->out_info, btrans->cuda_stream);

  if (!conv_ret) {
    GST_ERROR_OBJECT (filter, "Failed to convert frame");
    return GST_FLOW_ERROR;
  }

  if (!out_cuda_mem) {
    if (!gst_cuda_context_push (btrans->context)) {
      GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
          ("Cannot push CUDA context"));
      return FALSE;
    }

    for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (out_frame); i++) {
      CUDA_MEMCPY2D param = { 0, };
      guint width, height;

      width = GST_VIDEO_FRAME_COMP_WIDTH (out_frame, i) *
          GST_VIDEO_FRAME_COMP_PSTRIDE (out_frame, i);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (out_frame, i);

      param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
      param.srcPitch = out_mem->stride;
      param.srcDevice =
          filter->out_fallback->data + filter->out_fallback->offset[i];
      param.dstMemoryType = CU_MEMORYTYPE_HOST;
      param.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (out_frame, i);
      param.dstHost = GST_VIDEO_FRAME_PLANE_DATA (out_frame, i);
      param.WidthInBytes = width;
      param.Height = height;

      if (!gst_cuda_result (CuMemcpy2DAsync (&param, btrans->cuda_stream))) {
        gst_cuda_context_pop (NULL);
        GST_ELEMENT_ERROR (filter, LIBRARY, FAILED, (NULL),
            ("Cannot upload input video frame"));
        return GST_FLOW_ERROR;
      }
    }

    gst_cuda_result (CuStreamSynchronize (btrans->cuda_stream));
    gst_cuda_context_pop (NULL);
  }

  return GST_FLOW_OK;
}

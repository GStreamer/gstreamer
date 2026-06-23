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

#pragma once

#include <gst/video/video.h>
#include <gst/cuda/gstcuda_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_CUDA_AGGREGATOR_PAD            (gst_cuda_aggregator_pad_get_type())
#define GST_CUDA_AGGREGATOR_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_AGGREGATOR_PAD, GstCudaAggregatorPad))
#define GST_CUDA_AGGREGATOR_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CUDA_AGGREGATOR_PAD, GstCudaAggregatorPadClass))
#define GST_IS_CUDA_AGGREGATOR_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_AGGREGATOR_PAD))
#define GST_IS_CUDA_AGGREGATOR_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CUDA_AGGREGATOR_PAD))
#define GST_CUDA_AGGREGATOR_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_CUDA_AGGREGATOR_PAD,GstCudaAggregatorPadClass))

#define GST_TYPE_CUDA_AGGREGATOR_CONVERT_PAD            (gst_cuda_aggregator_convert_pad_get_type())
#define GST_CUDA_AGGREGATOR_CONVERT_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_AGGREGATOR_CONVERT_PAD, GstCudaAggregatorConvertPad))
#define GST_CUDA_AGGREGATOR_CONVERT_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CUDA_AGGREGATOR_CONVERT_PAD, GstCudaAggregatorConvertPadClass))
#define GST_IS_CUDA_AGGREGATOR_CONVERT_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_AGGREGATOR_CONVERT_PAD))
#define GST_IS_CUDA_AGGREGATOR_CONVERT_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CUDA_AGGREGATOR_CONVERT_PAD))
#define GST_CUDA_AGGREGATOR_CONVERT_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_CUDA_AGGREGATOR_CONVERT_PAD,GstCudaAggregatorConvertPadClass))

#define GST_TYPE_CUDA_AGGREGATOR            (gst_cuda_aggregator_get_type())
#define GST_CUDA_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_AGGREGATOR, GstCudaAggregator))
#define GST_CUDA_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CUDA_AGGREGATOR, GstCudaAggregatorClass))
#define GST_IS_CUDA_AGGREGATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_AGGREGATOR))
#define GST_IS_CUDA_AGGREGATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CUDA_AGGREGATOR))
#define GST_CUDA_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_CUDA_AGGREGATOR,GstCudaAggregatorClass))

/**
 * GstCudaAggregatorPad:
 *
 * Since: 1.30
 */
struct _GstCudaAggregatorPad
{
  GstVideoAggregatorPad parent;

  /* < private > */
  GstCudaAggregatorPadPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstCudaAggregatorPadClass:
 *
 * Since: 1.30
 */
struct _GstCudaAggregatorPadClass
{
  GstVideoAggregatorPadClass parent_class;

  /* < private > */
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_CUDA_API
GType gst_cuda_aggregator_pad_get_type (void);

/**
 * GstCudaAggregatorConvertPad:
 *
 * Since: 1.30
 */
struct _GstCudaAggregatorConvertPad
{
  GstCudaAggregatorPad parent;

  /* < private > */
  GstCudaAggregatorConvertPadPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstCudaAggregatorConvertPadClass:
 *
 * Since: 1.30
 */
struct _GstCudaAggregatorConvertPadClass
{
  GstCudaAggregatorPadClass parent_class;

  void (*create_conversion_info) (GstCudaAggregatorConvertPad *pad,
                                  GstCudaAggregator *agg,
                                  GstVideoInfo *conversion_info);

  /* < private > */
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_CUDA_API
GType gst_cuda_aggregator_convert_pad_get_type (void);

/**
 * GstCudaAggregator:
 *
 * Since: 1.30
 */
struct _GstCudaAggregator
{
  GstVideoAggregator parent;

  GstCudaContext *context;

  /* < private > */
  GstCudaAggregatorPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstCudaAggregatorClass:
 *
 * Since: 1.30
 */
struct _GstCudaAggregatorClass
{
  GstVideoAggregatorClass parent_class;

  /**
   * GstCudaAggregatorClass::aggregate_cuda_frames:
   * @aggregator: a #GstCudaAggregator
   * @stream: (transfer none): a #GstCudaStream that subclass can submit commands on
   * @outbuffer: (transfer none): output buffer that subclass can update
   *
   * The subclass performs operations on aggregated video frames.
   * #GstCudaAggregator performs synchronization before this call,
   * if a prepared frame is associated with different #GstCudaStream.
   *
   * #GstCudaAggregator does not perform synchronization after this call.
   * Therefore, the subclass is responsible for any required synchronization
   * (e.g., by calling cuStreamSynchronize or setting the NEED_SYNC flag
   * on #GstCudaMemory).
   *
   * Since: 1.30
   */
  GstFlowReturn (*aggregate_cuda_frames)  (GstCudaAggregator * aggregator,
                                           GstCudaStream * stream,
                                           GstBuffer * outbuffer);

  /* < private > */
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_CUDA_API
GType gst_cuda_aggregator_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstCudaAggregator, gst_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstCudaAggregatorPad, gst_object_unref)

G_END_DECLS


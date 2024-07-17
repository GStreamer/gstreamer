/*
 * GStreamer gstreamer-tensor
 * Copyright (C) 2024 Collabora Ltd
 *
 * gsttensor.h
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

#ifndef __GST_TENSOR_H__
#define __GST_TENSOR_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

/**
 * GstTensorDataType:
 * @GST_TENSOR_TYPE_INT4 signed 4 bit integer tensor data
 * @GST_TENSOR_TYPE_INT8 signed 8 bit integer tensor data
 * @GST_TENSOR_TYPE_INT16 signed 16 bit integer tensor data
 * @GST_TENSOR_TYPE_INT32 signed 32 bit integer tensor data
 * @GST_TENSOR_TYPE_INT64 signed 64 bit integer tensor data
 * @GST_TENSOR_TYPE_UINT4 unsigned 4 bit integer tensor data
 * @GST_TENSOR_TYPE_UINT8 unsigned 8 bit integer tensor data
 * @GST_TENSOR_TYPE_UINT16 unsigned 16 bit integer tensor data
 * @GST_TENSOR_TYPE_UINT32 unsigned 32 bit integer tensor data
 * @GST_TENSOR_TYPE_UINT64 unsigned 64 bit integer tensor data
 * @GST_TENSOR_TYPE_FLOAT16 16 bit floating point tensor data
 * @GST_TENSOR_TYPE_FLOAT32 32 bit floating point tensor data
 * @GST_TENSOR_TYPE_FLOAT64 64 bit floating point tensor data
 * @GST_TENSOR_TYPE_BFLOAT16 "brain" 16 bit floating point tensor data
 *
 * Describe the type of data contain in the tensor.
 *
 * Since: 1.26
 */
typedef enum _GstTensorDataType
{
  GST_TENSOR_TYPE_INT4,
  GST_TENSOR_TYPE_INT8,
  GST_TENSOR_TYPE_INT16,
  GST_TENSOR_TYPE_INT32,
  GST_TENSOR_TYPE_INT64,
  GST_TENSOR_TYPE_UINT4,
  GST_TENSOR_TYPE_UINT8,
  GST_TENSOR_TYPE_UINT16,
  GST_TENSOR_TYPE_UINT32,
  GST_TENSOR_TYPE_UINT64,
  GST_TENSOR_TYPE_FLOAT16,
  GST_TENSOR_TYPE_FLOAT32,
  GST_TENSOR_TYPE_FLOAT64,
  GST_TENSOR_TYPE_BFLOAT16,
} GstTensorDataType;

/**
 * GstTensorDimOrder:
 * @GST_TENSOR_DIM_ORDER_ROW_MAJOR: elements along a row are consecutive in memory
 * @GST_TENSOR_DIM_ORDER_COL_MAJOR: elements along a column are consecutive in memory
 *
 * Indicate to read tensor from memory in row-major or column-major.
 *
 * Since: 1.26
 */
typedef enum _GstTensorDimOrder
{
  GST_TENSOR_DIM_ORDER_ROW_MAJOR,
  GST_TENSOR_DIM_ORDER_COL_MAJOR
} GstTensorDimOrder;

/**
 * GstTensorLayout:
 * @GST_TENSOR_LAYOUT_STRIDED: indicate the tensor is stored in a dense format in memory
 *
 * Indicate tensor storage in memory.
 *
 * Since: 1.26
 */
typedef enum _GstTensorLayout
{
  GST_TENSOR_LAYOUT_STRIDED
} GstTensorLayout;

/**
 * GstTensor:
 * @id: semantically identify the contents of the tensor
 * @num_dims: number of tensor dimensions
 * @dims: tensor dimensions
 * @dims_order: Indicate tensor elements layout in memory.
 * @layout: Indicate tensor layout
 * @type: #GstTensorDataType of tensor data
 * @batch_size: Model batch size
 * @data: #GstBuffer holding tensor data
 *
 * Hold tensor data
 *
 * Since: 1.26
 */
typedef struct _GstTensor
{
  GQuark id;
  gsize num_dims;
  gsize *dims;
  GstTensorDimOrder dims_order;
  GstTensorLayout layout;
  GstTensorDataType data_type;
  gsize batch_size;
  GstBuffer *data;
} GstTensor;

#define GST_TENSOR_MISSING_ID -1

#endif /* __GST_TENSOR_H__ */

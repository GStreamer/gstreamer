/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalyticsmeta.c
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

#include "gsttensor.h"

#define GST_TENSOR_SIZE(num_dims) \
  (sizeof (GstTensor) + (sizeof (GstTensorDim) * num_dims))

G_DEFINE_BOXED_TYPE (GstTensor, gst_tensor,
    (GBoxedCopyFunc) gst_tensor_copy, (GBoxedFreeFunc) gst_tensor_free);

/**
 * gst_tensor_alloc: (constructor)
 * @num_dims: Number of dimension of the tensors
 *
 * Allocate a tensor with @num_dims dimensions.
 *
 * Returns: (transfer full) (not nullable): tensor allocated
 *
 * Since: 1.26
 */
GstTensor *
gst_tensor_alloc (gsize num_dims)
{
  GstTensor *tensor = g_malloc0 (GST_TENSOR_SIZE (num_dims));

  tensor->num_dims = num_dims;

  return tensor;
}

static gsize
size_for_elements (GstTensorDataType data_type, gsize elements)
{
  switch (data_type) {
    case GST_TENSOR_DATA_TYPE_INT4:
    case GST_TENSOR_DATA_TYPE_UINT4:
      return (elements / 2) + (elements % 2);

    case GST_TENSOR_DATA_TYPE_INT8:
    case GST_TENSOR_DATA_TYPE_UINT8:
      return elements;

    case GST_TENSOR_DATA_TYPE_INT16:
    case GST_TENSOR_DATA_TYPE_UINT16:
    case GST_TENSOR_DATA_TYPE_FLOAT16:
    case GST_TENSOR_DATA_TYPE_BFLOAT16:
      return elements * 2;

    case GST_TENSOR_DATA_TYPE_INT32:
    case GST_TENSOR_DATA_TYPE_UINT32:
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      return elements * 4;

    case GST_TENSOR_DATA_TYPE_INT64:
    case GST_TENSOR_DATA_TYPE_UINT64:
    case GST_TENSOR_DATA_TYPE_FLOAT64:
      return elements * 8;

    default:
      g_assert_not_reached ();
      return 0;
  }
}

/**
 * gst_tensor_new_simple:
 * @id: semantically identify the contents of the tensor
 * @data_type: #GstTensorDataType of tensor data
 * @batch_size: Model batch size
 * @data: (transfer full): #GstBuffer holding tensor data
 * @dims_order: Indicate tensor dimension indexing order
 * @num_dims: number of tensor dimensions
 * @dims: (array length=num_dims): tensor dimensions
 *
 * Allocates a new #GstTensor of @dims_order ROW_MAJOR or COLUMN_MAJOR and
 * with an interleaved layout
 *
 * Returns: A newly allocated #GstTensor
 *
 * Since: 1.26
 */
GstTensor *
gst_tensor_new_simple (GQuark id, GstTensorDataType data_type,
    gsize batch_size, GstBuffer * data,
    GstTensorDimOrder dims_order, gsize num_dims, gsize * dims)
{
  GstTensor *tensor;
  gsize num_elements = 1;
  gsize i;

  /* Update this if adding more to GstTensorDataType */
  g_return_val_if_fail (data_type <= GST_TENSOR_DATA_TYPE_BFLOAT16, NULL);

  g_return_val_if_fail (batch_size > 0, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (data), NULL);
  g_return_val_if_fail (dims_order == GST_TENSOR_DIM_ORDER_ROW_MAJOR ||
      dims_order == GST_TENSOR_DIM_ORDER_COL_MAJOR, NULL);
  g_return_val_if_fail (num_dims > 0, NULL);


  for (i = 0; i < num_dims; i++) {
    g_return_val_if_fail (dims[i] > 0, NULL);
    num_elements *= dims[i];
  }
  num_elements *= batch_size;

  if (gst_buffer_get_size (data) != size_for_elements (data_type, num_elements)) {
    g_critical ("Expected buffer of size %zu (%zu elements),"
        " but buffer has size %zu",
        size_for_elements (data_type, num_elements), num_elements,
        gst_buffer_get_size (data));
    return NULL;
  }

  tensor = gst_tensor_alloc (num_dims);
  tensor->id = id;
  tensor->layout = GST_TENSOR_LAYOUT_STRIDED;
  tensor->data_type = data_type;
  tensor->batch_size = batch_size;
  tensor->data = data;
  tensor->dims_order = dims_order;
  tensor->num_dims = num_dims;
  for (i = 0; i < num_dims; i++) {
    tensor->dims[i].size = dims[i];
    if (dims_order == GST_TENSOR_DIM_ORDER_COL_MAJOR)
      tensor->dims[i].order_index = i;
    else if (dims_order == GST_TENSOR_DIM_ORDER_ROW_MAJOR)
      tensor->dims[i].order_index = num_dims - i - 1;
  }

  return tensor;
}

/**
 * gst_tensor_free:
 * @tensor: (in) (transfer full): pointer to tensor to free
 *
 * Free tensor
 *
 * Since: 1.26
 */
void
gst_tensor_free (GstTensor * tensor)
{
  if (tensor->data != NULL) {
    gst_buffer_unref (tensor->data);
  }
  g_free (tensor);
}

/**
 * gst_tensor_copy:
 * @tensor: (transfer none) (nullable): a #GstTensor to be copied
 *
 * Create a copy of @tensor.
 *
 * Returns: (transfer full) (nullable): a new #GstTensor
 *
 * Since: 1.26
 */
GstTensor *
gst_tensor_copy (const GstTensor * tensor)
{
  GstTensor *copy = NULL;
  if (tensor) {
    copy = (GstTensor *) g_memdup2 (tensor, GST_TENSOR_SIZE (tensor->num_dims));
    if (copy->data)
      gst_buffer_ref (copy->data);
  }

  return copy;
}

/**
 * gst_tensor_get_dims:
 * @tensor: a #GstTensor
 * @num_dims: (out): The number of dimensions
 *
 * Gets the dimensions of the tensor.
 *
 * Returns: (array length=num_dims) (transfer none): The dims array form the tensor
 *
 * Since: 1.26
 */

GstTensorDim *
gst_tensor_get_dims (GstTensor * tensor, gsize * num_dims)
{
  if (num_dims)
    *num_dims = tensor->num_dims;
  return tensor->dims;
}

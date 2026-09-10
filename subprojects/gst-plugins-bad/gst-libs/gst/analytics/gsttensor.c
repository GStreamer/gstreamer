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
  (sizeof (GstTensor) + (sizeof (gsize) * num_dims))

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
 * @data: (transfer full): #GstBuffer holding tensor data
 * @dims_order: Indicate tensor dimension indexing order
 * @num_dims: number of tensor dimensions
 * @dims: (array length=num_dims): size of tensor in each dimension.
 *     A value of 0 means the dimension is dynamic.
 *
 * Allocates a new #GstTensor of @dims_order ROW_MAJOR or COLUMN_MAJOR and
 * with an interleaved layout.
 *
 * For example, a two-dimensional tensor with 32 rows and 4 columns, @dims would
 * be the two element array `[32, 4]`.
 *
 * Returns: A newly allocated #GstTensor
 *
 * Since: 1.26
 */
GstTensor *
gst_tensor_new_simple (GQuark id, GstTensorDataType data_type, GstBuffer * data,
    GstTensorDimOrder dims_order, gsize num_dims, gsize * dims)
{
  GstTensor *tensor;

  tensor = gst_tensor_alloc (num_dims);
  if (!gst_tensor_set_simple (tensor, id,
          data_type, data, dims_order, num_dims, dims)) {
    g_free (tensor);
    return NULL;
  }

  return tensor;
}


/**
 * gst_tensor_set_simple:
 * @tensor: a #GstTensor
 * @id: semantically identify the contents of the tensor
 * @data_type: #GstTensorDataType of tensor data
 * @data: (transfer full): #GstBuffer holding tensor data
 * @dims_order: Indicate tensor dimension indexing order
 * @num_dims: number of tensor dimensions
 * @dims: (array length=num_dims): size of tensor in each dimension.
 *     A value of 0 means the dimension is dynamic.
 *
 * Sets the content of a #GstTensor of @dims_order ROW_MAJOR or COLUMN_MAJOR and
 * with an interleaved layout. The #GstTensor must have exactly num_dims.
 *
 * For example, a two-dimensional tensor with 32 rows and 4 columns, @dims would
 * be the two element array `[32, 4]`.
 *
 * Returns: TRUE if it coudl be set correctly
 *
 * Since: 1.28
 */
gboolean
gst_tensor_set_simple (GstTensor * tensor, GQuark id,
    GstTensorDataType data_type, GstBuffer * data,
    GstTensorDimOrder dims_order, gsize num_dims, gsize * dims)
{
  gsize num_elements = 1;
  gsize i;
  gboolean dynamic_tensor_size = FALSE;

  /* Update this if adding more to GstTensorDataType */
  g_return_val_if_fail (data_type <= GST_TENSOR_DATA_TYPE_BFLOAT16, FALSE);

  g_return_val_if_fail (GST_IS_BUFFER (data), FALSE);
  g_return_val_if_fail (dims_order == GST_TENSOR_DIM_ORDER_ROW_MAJOR ||
      dims_order == GST_TENSOR_DIM_ORDER_COL_MAJOR, FALSE);
  g_return_val_if_fail (num_dims > 0, FALSE);
  g_return_val_if_fail (tensor->num_dims == num_dims, FALSE);

  for (i = 0; i < num_dims; i++) {
    dynamic_tensor_size = dims[i] == 0;

    if (dynamic_tensor_size == FALSE)
      num_elements *= dims[i];
    else
      break;
  }

  /* We can't do this validation if the tensor size is dynamic */
  if (dynamic_tensor_size == FALSE &&
      gst_buffer_get_size (data) !=
      size_for_elements (data_type, num_elements)) {
    g_critical ("Expected buffer of size %zu (%zu elements),"
        " but buffer has size %zu",
        size_for_elements (data_type, num_elements), num_elements,
        gst_buffer_get_size (data));
    return FALSE;
  }

  memset (tensor, 0, sizeof (GstTensor));

  tensor->id = id;
  tensor->layout = GST_TENSOR_LAYOUT_CONTIGUOUS;
  tensor->data_type = data_type;
  tensor->data = data;
  tensor->dims_order = dims_order;
  tensor->num_dims = num_dims;
  memcpy (tensor->dims, dims, sizeof (gsize) * num_dims);

  return TRUE;
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

gsize *
gst_tensor_get_dims (GstTensor * tensor, gsize * num_dims)
{
  if (num_dims)
    *num_dims = tensor->num_dims;
  return tensor->dims;
}

/**
 * gst_tensor_data_type_get_name:
 * @data_type: a #GstTensorDataType
 *
 * Get a string version of the data type
 *
 * Returns: a constant string with the name of the data type
 *
 * Since: 1.28
 */
const gchar *
gst_tensor_data_type_get_name (GstTensorDataType data_type)
{
  switch (data_type) {
    case GST_TENSOR_DATA_TYPE_INT4:
      return "int4";
    case GST_TENSOR_DATA_TYPE_INT8:
      return "int8";
    case GST_TENSOR_DATA_TYPE_INT16:
      return "int16";
    case GST_TENSOR_DATA_TYPE_INT32:
      return "int32";
    case GST_TENSOR_DATA_TYPE_INT64:
      return "int64";
    case GST_TENSOR_DATA_TYPE_UINT4:
      return "uint4";
    case GST_TENSOR_DATA_TYPE_UINT8:
      return "uint8";
    case GST_TENSOR_DATA_TYPE_UINT16:
      return "uint16";
    case GST_TENSOR_DATA_TYPE_UINT32:
      return "uint32";
    case GST_TENSOR_DATA_TYPE_UINT64:
      return "uint64";
    case GST_TENSOR_DATA_TYPE_FLOAT16:
      return "float16";
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      return "float32";
    case GST_TENSOR_DATA_TYPE_FLOAT64:
      return "float64";
    case GST_TENSOR_DATA_TYPE_BFLOAT16:
      return "bfloat16";
    default:
      return NULL;
  }
}

/**
 * gst_tensor_data_type_from_name:
 * @name: a data type name.
 * @data_type: (out): set to the matching #GstTensorDataType when found
 *
 * Parse @name, into a #GstTensorDataType.
 *
 * Returns: %TRUE if @name matched a known data type and @data_type was set,
 *    %FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_tensor_data_type_from_name (const gchar * name,
    GstTensorDataType * data_type)
{
  static const GstTensorDataType types[] = {
    GST_TENSOR_DATA_TYPE_INT4,
    GST_TENSOR_DATA_TYPE_INT8,
    GST_TENSOR_DATA_TYPE_INT16,
    GST_TENSOR_DATA_TYPE_INT32,
    GST_TENSOR_DATA_TYPE_INT64,
    GST_TENSOR_DATA_TYPE_UINT4,
    GST_TENSOR_DATA_TYPE_UINT8,
    GST_TENSOR_DATA_TYPE_UINT16,
    GST_TENSOR_DATA_TYPE_UINT32,
    GST_TENSOR_DATA_TYPE_UINT64,
    GST_TENSOR_DATA_TYPE_FLOAT16,
    GST_TENSOR_DATA_TYPE_FLOAT32,
    GST_TENSOR_DATA_TYPE_FLOAT64,
    GST_TENSOR_DATA_TYPE_BFLOAT16,
  };
  gsize i;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (data_type != NULL, FALSE);

  for (i = 0; i < G_N_ELEMENTS (types); i++) {
    if (!g_strcmp0 (name, gst_tensor_data_type_get_name (types[i]))) {
      *data_type = types[i];
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * gst_tensor_data_type_is_float:
 * @data_type: a #GstTensorDataType
 *
 * Check whether @data_type is a floating-point type.
 *
 * Returns: %TRUE if @data_type is a floating-point type, %FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_tensor_data_type_is_float (GstTensorDataType data_type)
{
  switch (data_type) {
    case GST_TENSOR_DATA_TYPE_FLOAT16:
    case GST_TENSOR_DATA_TYPE_FLOAT32:
    case GST_TENSOR_DATA_TYPE_FLOAT64:
    case GST_TENSOR_DATA_TYPE_BFLOAT16:
    case GST_TENSOR_DATA_TYPE_FLOAT8E4M3FN:
    case GST_TENSOR_DATA_TYPE_FLOAT8E4M3FNUZ:
    case GST_TENSOR_DATA_TYPE_FLOAT8E5M2:
    case GST_TENSOR_DATA_TYPE_FLOAT8E5M2FNUZ:
      return TRUE;
    default:
      return FALSE;
  }
}

/**
 * gst_tensor_data_type_get_bit_depth:
 * @data_type: a #GstTensorDataType
 *
 * Get the number of bits used to store a single element of @data_type.
 *
 * Returns: the bit depth of @data_type, or 0 for dynamic if @data_type has no
 * fixed bit depth.
 *
 * Since: 1.30
 */
guint
gst_tensor_data_type_get_bit_depth (GstTensorDataType data_type)
{
  switch (data_type) {
    case GST_TENSOR_DATA_TYPE_INT4:
    case GST_TENSOR_DATA_TYPE_UINT4:
      return 4;

    case GST_TENSOR_DATA_TYPE_INT8:
    case GST_TENSOR_DATA_TYPE_UINT8:
    case GST_TENSOR_DATA_TYPE_BOOL:
    case GST_TENSOR_DATA_TYPE_FLOAT8E4M3FN:
    case GST_TENSOR_DATA_TYPE_FLOAT8E4M3FNUZ:
    case GST_TENSOR_DATA_TYPE_FLOAT8E5M2:
    case GST_TENSOR_DATA_TYPE_FLOAT8E5M2FNUZ:
      return 8;

    case GST_TENSOR_DATA_TYPE_INT16:
    case GST_TENSOR_DATA_TYPE_UINT16:
    case GST_TENSOR_DATA_TYPE_FLOAT16:
    case GST_TENSOR_DATA_TYPE_BFLOAT16:
      return 16;

    case GST_TENSOR_DATA_TYPE_INT32:
    case GST_TENSOR_DATA_TYPE_UINT32:
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      return 32;

    case GST_TENSOR_DATA_TYPE_INT64:
    case GST_TENSOR_DATA_TYPE_UINT64:
    case GST_TENSOR_DATA_TYPE_FLOAT64:
    case GST_TENSOR_DATA_TYPE_COMPLEX64:
      return 64;

    case GST_TENSOR_DATA_TYPE_COMPLEX128:
      return 128;

    case GST_TENSOR_DATA_TYPE_STRING:
      return 0;

    default:
      g_assert_not_reached ();
      return 0;
  }
}

/**
 * gst_tensor_check_type:
 * @tensor: A #GstTensor
 * @data_type: The data type of the tensor
 * @order: The order of the tensor to read from the memory
 * @num_dims: The number of dimensions that the tensor can have
 * @dims: (array length=num_dims)(nullable): An optional array of dimensions, where G_MAXSIZE means ANY.
 *
 * Validate the tensor whether it mathces the reading order, dimensions and the data type.
 * Validate whether the #GstBuffer has enough size to hold the tensor data.
 *
 * Returns: TRUE if the #GstTensor has the reading order from the memory matching @order,
 * dimensions matching @num_dims, data type matching @data_type
 * Otherwise FALSE will be returned.
 *
 * Since: 1.28
 */
gboolean
gst_tensor_check_type (const GstTensor * tensor, GstTensorDataType data_type,
    GstTensorDimOrder order, gsize num_dims, const gsize * dims)
{
  gsize num_elements = 1, tensor_size, i;

  if (tensor->dims_order != order) {
    GST_DEBUG ("Tensor \"%s\" has order %d, expected %d",
        g_quark_to_string (tensor->id), tensor->dims_order, order);
    return FALSE;
  }

  if (tensor->num_dims != num_dims) {
    GST_DEBUG ("Tensor \"%s\" has %zu dimensions, expected %zu",
        g_quark_to_string (tensor->id), tensor->num_dims, num_dims);
    return FALSE;
  }

  if (tensor->data_type != data_type) {
    GST_DEBUG ("Tensor \"%s\" has data type \"%s\", expected \"%s\".",
        g_quark_to_string (tensor->id),
        gst_tensor_data_type_get_name (tensor->data_type),
        gst_tensor_data_type_get_name (data_type));
    return FALSE;
  }

  for (i = 0; i < tensor->num_dims; i++) {
    num_elements *= tensor->dims[i];

    if (dims) {
      if (dims[i] != G_MAXSIZE && dims[i] != tensor->dims[i]) {
        GST_DEBUG ("Tensor \"%s\" has dim[%zu]=%zu but expect dim[%zu]=%zu",
            g_quark_to_string (tensor->id), i, tensor->dims[i], i, dims[i]);
        return FALSE;
      }
    }
  }

  tensor_size = size_for_elements (tensor->data_type, num_elements);

  if (gst_buffer_get_size (tensor->data) < tensor_size) {
    GST_ERROR ("Expected tensor \"%s\" buffer of size %zu (%zu elements),"
        " but buffer has size %zu", g_quark_to_string (tensor->id),
        tensor_size, num_elements, gst_buffer_get_size (tensor->data));
    return FALSE;
  }

  return TRUE;
}

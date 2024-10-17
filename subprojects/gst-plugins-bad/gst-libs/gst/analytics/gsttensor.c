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

/* GStreamer
 * Copyright (C) 2024 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticssegmentmeta.c
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

#include "gstanalyticstensormtd.h"
#include <gst/video/video.h>

/**
 * SECTION: gstanalyticstensormtd
 * @title: GstAnalyticsTensorMtd
 * @short_description: Analytics metadata to store tensor inside a
 * #GstAnalyticsRelationMeta
 * @symbols:
 * - GstAnalyticsTensorMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This type of metadata holds a tensor. It can be used to store tensor as
 * analytics-meta for their ability to relate to each others. For example
 * in a multi-model analytics pipeline, we sometime have one model input match
 * the output of the other model. In this context it can be useful to keep the
 * ancestry relation between first tensor, output of first inference, and the
 * second tensor, output from second inference. Another use-case for
 * #GstAnalyticsTensorMtd is to transport tensors from inference element to a
 * post-processing element using a computing graph framework, like ONNX.
 * Essentially #GstAnalyticsTensorMtd is a GstBuffer encapsulated by a
 * analytics-meta with additional parameters describing the tensor.
 *
 * Since 1.28
 */

static void gst_analytics_tensor_mtd_clear (GstBuffer * buffer,
    GstAnalyticsMtd * mtd);

static gboolean
gst_analytics_tensor_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data);

static const GstAnalyticsMtdImpl tensor_impl = {
  "tensor",
  gst_analytics_tensor_mtd_transform,
  gst_analytics_tensor_mtd_clear
};


typedef GstTensor GstAnalyticsTensorMtdData;

/**
 * gst_analytics_tensor_mtd_get_mtd_type:
 *
 * Get an id that represent tensor metadata type
 *
 * Returns: Opaque id of the #GstAnalyticsMtd type
 *
 * Since: 1.28
 */
GstAnalyticsMtdType
gst_analytics_tensor_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & tensor_impl;
}

/**
 * gst_analytics_tensor_mtd_get_tensor:
 * @instance: Instance of #GstAnalyticsTensorMtd
 *
 * Get tensor
 *
 * Returns: (transfer none): a #GstTensor
 *
 * Since: 1.28
 */
GstTensor *
gst_analytics_tensor_mtd_get_tensor (const GstAnalyticsTensorMtd * instance)
{
  GstAnalyticsTensorMtdData *mtddata;

  g_return_val_if_fail (instance, NULL);

  mtddata =
      gst_analytics_relation_meta_get_mtd_data (instance->meta, instance->id);
  g_return_val_if_fail (mtddata != NULL, NULL);

  return mtddata;
}

/**
 * gst_analytics_relation_meta_add_tensor_mtd:
 * @instance: Instance
 * @num_dims: The number of dimensions in the tensor
 * @tensor_mtd: (out)(nullable): Handle update with newly added tensor mtd.
 *  Add tensor mtd to @instance.
 *
 * Add a new #GstAnalyticsTensorMtd holding a #GstTensor to @instance. The
 * #GstTensor needs to be filled.
 *
 * Returns: Added successfully
 *
 * Since: 1.28
 */
gboolean
gst_analytics_relation_meta_add_tensor_mtd (GstAnalyticsRelationMeta
    * meta, gsize num_dims, GstAnalyticsTensorMtd * tensor_mtd)
{
  GstTensor *tensor;

  tensor = gst_analytics_relation_meta_add_mtd (meta, &tensor_impl,
      sizeof (GstAnalyticsTensorMtdData) + (num_dims * sizeof (gsize)),
      tensor_mtd);

  if (tensor == NULL)
    return FALSE;

  memset (tensor, 0, sizeof (GstTensor));
  tensor->num_dims = num_dims;

  return TRUE;
}

/**
 * gst_analytics_relation_meta_add_tensor_mtd_simple:
 * @instance: Instance
 * @id: semantically identify the contents of the tensor
 * @data_type: #GstTensorDataType of tensor data
 * @data: (transfer full): #GstBuffer holding tensor data
 * @dims_order: Indicate tensor dimension indexing order
 * @num_dims: number of tensor dimensions
 * @dims: (array length=num_dims): size of tensor in each dimension.
 *     A value of 0 means the dimension is dynamic.
 * @tensor_mtd: (out)(nullable): Handle update with newly added tensor mtd.
 *  Add tensor mtd to @instance.
 *
 * Add a new #GstAnalyticsTensorMtd holding a #GstTensor to @instance.
 *
 * Returns: Added successfully
 *
 * Since: 1.28
 */
gboolean
gst_analytics_relation_meta_add_tensor_mtd_simple (GstAnalyticsRelationMeta
    * meta, GQuark id, GstTensorDataType data_type, GstBuffer * data,
    GstTensorDimOrder dims_order, gsize num_dims, gsize * dims,
    GstAnalyticsTensorMtd * tensor_mtd)
{
  GstTensor *tensor;
  GstAnalyticsTensorMtd mtd;

  if (!gst_analytics_relation_meta_add_tensor_mtd (meta, num_dims, &mtd))
    return FALSE;

  tensor = gst_analytics_relation_meta_get_mtd_data (meta, mtd.id);

  if (!gst_tensor_set_simple (tensor, id, data_type, data,
          dims_order, num_dims, dims))
    return FALSE;

  if (tensor_mtd)
    *tensor_mtd = mtd;

  return TRUE;
}

static void
gst_analytics_tensor_mtd_clear (GstBuffer * buffer, GstAnalyticsMtd * mtd)
{
  GstAnalyticsTensorMtdData *tensor;
  gsize num_dims;

  tensor = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_assert (tensor);

  num_dims = tensor->num_dims;
  if (tensor->data)
    gst_buffer_unref (tensor->data);

  memset (tensor, 0, sizeof (GstTensor));
  tensor->num_dims = num_dims;
}

static gboolean
gst_analytics_tensor_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstTensor *tensor;

  tensor = gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
      transmtd->id);

  if (tensor->data)
    tensor->data = gst_buffer_ref (tensor->data);

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_tensor_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsTensorMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with relatable
 *    meta
 *
 * Fill @rlt if a analytics-meta with id == @an_meta_id exist in @meta instance,
 * otherwise this method return FALSE and @rlt is invalid.
 *
 * Returns: TRUE if successful.
 *
 * Since: 1.28
 */
gboolean
gst_analytics_relation_meta_get_tensor_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsTensorMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_tensor_mtd_get_mtd_type (), (GstAnalyticsTensorMtd *) rlt);
}

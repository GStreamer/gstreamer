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

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gsttensor.h>

G_BEGIN_DECLS

/**
 * GST_TENSOR_LIST_META_API_TYPE:
 *
 * The tensor list meta API type.
 *
 * Since: 1.30
 */
#define GST_TENSOR_LIST_META_API_TYPE (gst_tensor_list_meta_api_get_type())

/**
 * GST_TENSOR_LIST_META_INFO:
 *
 * The tensor list meta information.
 *
 * Since: 1.30
 */
#define GST_TENSOR_LIST_META_INFO (gst_tensor_list_meta_get_info())

typedef struct _GstTensorProvenance GstTensorProvenance;

/**
 * GstTensorListItem:
 *
 * An opaque item containing a tensor set and its provenance information.
 *
 * Since: 1.30
 */
typedef struct _GstTensorListItem GstTensorListItem;

typedef struct _GstTensorListMeta GstTensorListMeta;

/**
 * GstTensorProvenance:
 * @key: non-zero key, typically the ID of the originating input tensor
 * @num_correlation_ids: number of correlation IDs
 * @correlation_ids: (array length=num_correlation_ids) (nullable): ordered
 *   correlation IDs, or %NULL when @num_correlation_ids is zero
 *
 * Caller-owned descriptor for a provenance entry. Neither the descriptor
 * nor its correlation ID array is transferred to #GstTensorListMeta.
 *
 * #GstTensorListMeta copies the IDs when appending an item.
 *
 * A preprocessing element typically uses the input tensor's ID as the key.
 * The key is preserved when inference replaces the input tensors with outputs,
 * so it need not identify a tensor in that item.
 *
 * When filled by gst_tensor_list_meta_get_provenance(), the correlation ID
 * array belongs to #GstTensorListMeta and remains valid until that meta is freed.
 *
 * Since: 1.30
 */
struct _GstTensorProvenance
{
  GQuark key;
  gsize num_correlation_ids;
  const gint64 *correlation_ids;
};

/**
 * GstTensorListMeta:
 * @meta: parent
 * @group_id: the representative tensor group ID
 * @num_items: number of #GstTensorListItem
 * @items: (array length=num_items): a #GstTensorListItem list
 *
 * Each item represents an instance of the same tensor group. All items are
 * expected to contain the same set of tensors, although tensor dimensions
 * may differ for dynamic dimensions. This is a producer/consumer contract.
 *
 * Each item contains one tensor set and its own provenance information.
 * Provenance keys do not need to identify tensors currently stored in the
 * item. Correlation IDs can be used to associate tensor data with
 * #GstAnalyticsTensorProvenanceMtd.
 *
 * gst_tensor_list_meta_append_item_from() replaces a tensor set while keeping
 * the source item's provenance.
 *
 * Since: 1.30
 */
struct _GstTensorListMeta
{
  GstMeta meta;

  GQuark group_id;

  gsize num_items;
  GstTensorListItem **items;
};

GST_ANALYTICS_META_API
GType gst_tensor_list_meta_api_get_type (void);

GST_ANALYTICS_META_API
const GstMetaInfo * gst_tensor_list_meta_get_info (void);

GST_ANALYTICS_META_API
GstTensorListMeta * gst_buffer_add_tensor_list_meta (GstBuffer * buffer,
                                                     GQuark group_id);

GST_ANALYTICS_META_API
GstTensorListMeta * gst_buffer_get_tensor_list_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
gboolean  gst_tensor_list_meta_append_item (GstTensorListMeta * meta,
                                            gsize num_tensors,
                                            GstTensor ** tensors);

GST_ANALYTICS_META_API
gboolean  gst_tensor_list_meta_append_item_full (GstTensorListMeta * meta,
                                                 gsize num_tensors,
                                                 GstTensor ** tensors,
                                                 gsize num_provenance,
                                                 const GstTensorProvenance * provenance);

GST_ANALYTICS_META_API
gboolean  gst_tensor_list_meta_append_item_from (GstTensorListMeta * meta,
                                                 gsize num_tensors,
                                                 GstTensor ** tensors,
                                                 const GstTensorListMeta * source,
                                                 gsize source_item_index);

GST_ANALYTICS_META_API
gsize   gst_tensor_list_meta_get_num_provenance (GstTensorListMeta * meta,
                                                 gsize item_index);

GST_ANALYTICS_META_API
gboolean  gst_tensor_list_meta_get_provenance (GstTensorListMeta * meta,
                                               gsize item_index,
                                               gsize provenance_index,
                                               GstTensorProvenance * provenance);

GST_ANALYTICS_META_API
const GstTensor * gst_tensor_list_meta_get_typed_tensor (GstTensorListMeta * meta,
                                                         gsize item_index,
                                                         GQuark tensor_id,
                                                         GstTensorDataType data_type,
                                                         GstTensorDimOrder order,
                                                         gsize num_dims,
                                                         const gsize * dims);

G_END_DECLS


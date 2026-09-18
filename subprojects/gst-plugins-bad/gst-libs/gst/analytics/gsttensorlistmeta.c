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

/**
 * SECTION:gsttensorlistmeta
 * @title: GstTensorListMeta
 * @short_description: Metadata for a list of tensor sets
 * @see_also: #GstAnalyticsTensorProvenanceMtd
 *
 * While #GstTensorMeta holds a single tensor set, which may contain multiple
 * tensors (e.g., boxes, scores and classes from one inference run),
 * #GstTensorListMeta holds one or more items, each containing a tensor set
 * (e.g., one item for each inference run on a different ROI).
 *
 * Each item can also carry optional provenance, so downstream elements do not
 * need to rely on the ordering of separate tensor metadata to associate results
 * with their origins.
 *
 * |[
 *   GstTensorListMeta
 *     |
 *     +-- item 0
 *     |     +-- tensor A
 *     |     +-- tensor B
 *     |
 *     +-- item 1
 *           +-- tensor A
 *           +-- tensor B
 * ]|
 *
 * Items are expected to represent instances of the same tensor group,
 * although tensor dimensions may differ for dynamic dimensions. The number
 * of items is independent of a batch or other dimension of the tensors.
 *
 * Each item can also carry provenance independently of its current tensors.
 * For example, a preprocessing element can create one batched input tensor
 * from three regions of a video buffer:
 *
 * |[
 *   item 0
 *     |
 *     +-- tensor "image" [3, C, H, W]
 *     |
 *     +-- provenance
 *           +-- "image" -> [ 10, 11, 12 ]
 * ]|
 *
 * Here 10, 11 and 12 can identify the #GstAnalyticsTensorProvenanceMtd instances
 * for the three input regions. The "image" key is the input tensor's ID, also
 * stored in those provenance metadata instances.
 *
 * An inference element can replace the input tensor with its output while
 * keeping the provenance unchanged:
 *
 * |[
 *   item 0
 *     |
 *     +-- tensor "logits" [3, T, C]
 *     |
 *     +-- provenance
 *           +-- "image" -> [ 10, 11, 12 ]
 * ]|
 *
 * The provenance key therefore does not need to identify a tensor currently
 * stored in the item. Correlation IDs are kept in producer-defined order.
 * How that order maps to tensor data is a producer/consumer contract and does
 * not imply a particular tensor dimension.
 *
 * This allows inference to preserve provenance without understanding the
 * semantic mapping between its input and output tensors.
 * gst_tensor_list_meta_append_item_from() can be used to replace a tensor set
 * while copying the provenance from an existing item.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttensorlistmeta.h"

typedef struct
{
  GQuark id;
  gsize num_ids;
  gint64 *ids;
} GstTensorProvenanceEntry;

struct _GstTensorListItem
{
  gsize num_tensors;
  GstTensor **tensors;
  gsize num_provenance;
  GstTensorProvenanceEntry *provenance;
};

static gboolean
gst_tensor_list_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstTensorListMeta *dmeta, *smeta;

  smeta = (GstTensorListMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dmeta = gst_buffer_add_tensor_list_meta (dest, smeta->group_id);
    if (!dmeta)
      return FALSE;

    dmeta->num_items = smeta->num_items;
    dmeta->items = g_new0 (GstTensorListItem *, smeta->num_items);

    for (gsize i = 0; i < smeta->num_items; i++) {
      const GstTensorListItem *sitem = smeta->items[i];
      GstTensorListItem *ditem = g_new0 (GstTensorListItem, 1);

      ditem->num_tensors = sitem->num_tensors;
      ditem->tensors = g_new0 (GstTensor *, sitem->num_tensors);
      ditem->num_provenance = sitem->num_provenance;
      ditem->provenance =
          g_new0 (GstTensorProvenanceEntry, sitem->num_provenance);
      dmeta->items[i] = ditem;

      for (gsize j = 0; j < sitem->num_tensors; j++) {
        ditem->tensors[j] = gst_tensor_copy (sitem->tensors[j]);
        if (!ditem->tensors[j]) {
          gst_buffer_remove_meta (dest, &dmeta->meta);
          return FALSE;
        }
      }
      for (gsize j = 0; j < sitem->num_provenance; j++) {
        ditem->provenance[j].id = sitem->provenance[j].id;
        ditem->provenance[j].num_ids = sitem->provenance[j].num_ids;
        ditem->provenance[j].ids = g_memdup2 (sitem->provenance[j].ids,
            sitem->provenance[j].num_ids * sizeof (gint64));
      }
    }
  } else {
    GST_WARNING
        ("gst_tensor_list_meta_transform: transform type %u not supported",
        type);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_tensor_list_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstTensorListMeta *tmeta = (GstTensorListMeta *) meta;

  tmeta->group_id = 0;
  tmeta->num_items = 0;
  tmeta->items = NULL;

  return TRUE;
}

static void
gst_tensor_list_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstTensorListMeta *tmeta = (GstTensorListMeta *) meta;

  for (gsize i = 0; i < tmeta->num_items; i++) {
    GstTensorListItem *item = tmeta->items[i];

    if (!item)
      continue;

    for (gsize j = 0; j < item->num_tensors; j++) {
      if (item->tensors[j])
        gst_tensor_free (item->tensors[j]);

    }

    for (gsize j = 0; j < item->num_provenance; j++)
      g_free (item->provenance[j].ids);
    g_free (item->provenance);
    g_free (item->tensors);
    g_free (item);
  }

  g_free (tmeta->items);
}

/**
 * gst_tensor_list_meta_api_get_type: (skip)
 *
 * Returns: the tensor list meta API type
 *
 * Since: 1.30
 */
GType
gst_tensor_list_meta_api_get_type (void)
{
  static gsize type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstTensorListMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

/**
 * gst_tensor_list_meta_get_info: (skip)
 *
 * Returns: (transfer none): the tensor list meta information
 *
 * Since: 1.30
 */
const GstMetaInfo *
gst_tensor_list_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_TENSOR_LIST_META_API_TYPE,
        "GstTensorListMeta", sizeof (GstTensorListMeta),
        gst_tensor_list_meta_init, gst_tensor_list_meta_free,
        gst_tensor_list_meta_transform);
    g_once_init_leave (&meta_info, mi);
  }

  return meta_info;
}

/**
 * gst_buffer_add_tensor_list_meta:
 * @buffer: A #GstBuffer
 * @group_id: A representative group ID of the GstTensorListMeta
 *
 * Adds a #GstTensorListMeta to a buffer
 *
 * Returns: (transfer none): The new #GstTensorListMeta
 *
 * Since: 1.30
 */
GstTensorListMeta *
gst_buffer_add_tensor_list_meta (GstBuffer * buffer, GQuark group_id)
{
  GstTensorListMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = (GstTensorListMeta *) gst_buffer_add_meta (buffer,
      GST_TENSOR_LIST_META_INFO, NULL);
  if (!meta)
    return NULL;

  meta->group_id = group_id;

  return meta;
}

/**
 * gst_buffer_get_tensor_list_meta:
 * @buffer: A #GstBuffer
 *
 * Gets the #GstTensorListMeta from a buffer
 *
 * Returns: (nullable) (transfer none): The #GstTensorListMeta
 *
 * Since: 1.30
 */
GstTensorListMeta *
gst_buffer_get_tensor_list_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  return (GstTensorListMeta *) gst_buffer_get_meta (buffer,
      GST_TENSOR_LIST_META_API_TYPE);
}

static GstTensorListItem *
gst_tensor_list_item_new (gsize num_tensors, GstTensor ** tensors,
    gsize num_provenance)
{
  GstTensorListItem *item;

  item = g_new0 (GstTensorListItem, 1);
  item->num_tensors = num_tensors;
  item->tensors = tensors;
  item->num_provenance = num_provenance;
  item->provenance = g_new0 (GstTensorProvenanceEntry, num_provenance);

  return item;
}

static void
gst_tensor_list_meta_append (GstTensorListMeta * meta, GstTensorListItem * item)
{
  meta->items = g_renew (GstTensorListItem *, meta->items, meta->num_items + 1);
  meta->items[meta->num_items] = item;
  meta->num_items++;
}

/**
 * gst_tensor_list_meta_append_item:
 * @meta: a #GstTensorListMeta
 * @num_tensors: number of tensors
 * @tensors: (array length=num_tensors) (transfer full): tensor set
 *
 * Appends a tensor set without correlation information. Ownership of @tensors
 * and its tensors is transferred only on success.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.30
 */
gboolean
gst_tensor_list_meta_append_item (GstTensorListMeta * meta,
    gsize num_tensors, GstTensor ** tensors)
{
  return gst_tensor_list_meta_append_item_full (meta, num_tensors, tensors,
      0, NULL);
}

/**
 * gst_tensor_list_meta_append_item_full:
 * @meta: a #GstTensorListMeta
 * @num_tensors: number of tensors
 * @tensors: (array length=num_tensors) (transfer full): tensor set
 * @num_provenance: number of provenance entries
 * @provenance: (array length=num_provenance) (nullable) (transfer none):
 *   caller-owned provenance descriptors
 *
 * Appends a tensor set with provenance information. Keys are typically the
 * originating input tensor IDs, but need not match any tensor in @tensors.
 * Keys must be non-zero and unique within the item, and correlation IDs
 * must be non-zero.
 *
 * The meta copies the provenance information. Ownership of @tensors and
 * its tensors transfers only on success.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.30
 */
gboolean
gst_tensor_list_meta_append_item_full (GstTensorListMeta * meta,
    gsize num_tensors, GstTensor ** tensors, gsize num_provenance,
    const GstTensorProvenance * provenance)
{
  GstTensorListItem *item;

  g_return_val_if_fail (meta, FALSE);
  g_return_val_if_fail (num_tensors > 0, FALSE);
  g_return_val_if_fail (tensors, FALSE);
  g_return_val_if_fail (num_provenance == 0 || provenance, FALSE);
  g_return_val_if_fail (num_provenance <=
      G_MAXSIZE / sizeof (GstTensorProvenanceEntry), FALSE);

  for (gsize i = 0; i < num_tensors; i++)
    g_return_val_if_fail (tensors[i], FALSE);

  for (gsize i = 0; i < num_provenance; i++) {
    const GstTensorProvenance *entry = &provenance[i];

    g_return_val_if_fail (entry->key != 0, FALSE);
    g_return_val_if_fail (entry->num_correlation_ids <=
        G_MAXSIZE / sizeof (gint64), FALSE);
    g_return_val_if_fail (entry->num_correlation_ids == 0 ||
        entry->correlation_ids, FALSE);
    for (gsize j = 0; j < i; j++)
      g_return_val_if_fail (entry->key != provenance[j].key, FALSE);
    for (gsize j = 0; j < entry->num_correlation_ids; j++)
      g_return_val_if_fail (entry->correlation_ids[j] != 0, FALSE);
  }

  item = gst_tensor_list_item_new (num_tensors, tensors, num_provenance);
  for (gsize i = 0; i < num_provenance; i++) {
    item->provenance[i].id = provenance[i].key;
    item->provenance[i].num_ids = provenance[i].num_correlation_ids;
    item->provenance[i].ids = g_memdup2 (provenance[i].correlation_ids,
        provenance[i].num_correlation_ids * sizeof (gint64));
  }

  gst_tensor_list_meta_append (meta, item);

  return TRUE;
}

/**
 * gst_tensor_list_meta_append_item_from:
 * @meta: a #GstTensorListMeta
 * @num_tensors: number of tensors
 * @tensors: (array length=num_tensors) (transfer full): replacement tensor set
 * @source: (transfer none): source #GstTensorListMeta
 * @source_item_index: item whose provenance should be copied
 *
 * Appends a tensor set with provenance copied from the source item.
 * Ownership of @tensors and its tensors transfers only on success.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.30
 */
gboolean
gst_tensor_list_meta_append_item_from (GstTensorListMeta * meta,
    gsize num_tensors, GstTensor ** tensors, const GstTensorListMeta * source,
    gsize source_item_index)
{
  const GstTensorListItem *source_item;
  GstTensorListItem *item;

  g_return_val_if_fail (meta, FALSE);
  g_return_val_if_fail (num_tensors > 0, FALSE);
  g_return_val_if_fail (tensors, FALSE);
  g_return_val_if_fail (source, FALSE);
  g_return_val_if_fail (source_item_index < source->num_items, FALSE);

  for (gsize i = 0; i < num_tensors; i++)
    g_return_val_if_fail (tensors[i], FALSE);

  source_item = source->items[source_item_index];
  item = gst_tensor_list_item_new (num_tensors, tensors,
      source_item->num_provenance);
  for (gsize i = 0; i < source_item->num_provenance; i++) {
    item->provenance[i].id = source_item->provenance[i].id;
    item->provenance[i].num_ids = source_item->provenance[i].num_ids;
    item->provenance[i].ids = g_memdup2 (source_item->provenance[i].ids,
        source_item->provenance[i].num_ids * sizeof (gint64));
  }

  gst_tensor_list_meta_append (meta, item);

  return TRUE;
}

/**
 * gst_tensor_list_meta_get_num_provenance:
 * @meta: a #GstTensorListMeta
 * @item_index: item index
 *
 * Gets the number of provenance entries for the specified item
 *
 * Returns: the number of provenance entries
 *
 * Since: 1.30
 */
gsize
gst_tensor_list_meta_get_num_provenance (GstTensorListMeta * meta,
    gsize item_index)
{
  g_return_val_if_fail (meta, 0);
  g_return_val_if_fail (item_index < meta->num_items, 0);

  return meta->items[item_index]->num_provenance;
}

/**
 * gst_tensor_list_meta_get_provenance:
 * @meta: a #GstTensorListMeta
 * @item_index: item index
 * @provenance_index: provenance entry index
 * @provenance: (out caller-allocates): location for the provenance descriptor
 *
 * Gets the provenance entry at the specified index. The returned correlation
 * ID array belongs to @meta and remains valid until @meta is freed.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_tensor_list_meta_get_provenance (GstTensorListMeta * meta,
    gsize item_index, gsize provenance_index, GstTensorProvenance * provenance)
{
  GstTensorListItem *item;
  const GstTensorProvenanceEntry *entry;

  g_return_val_if_fail (meta, FALSE);
  g_return_val_if_fail (item_index < meta->num_items, FALSE);
  g_return_val_if_fail (provenance, FALSE);

  item = meta->items[item_index];
  g_return_val_if_fail (provenance_index < item->num_provenance, FALSE);

  entry = &item->provenance[provenance_index];
  provenance->key = entry->id;
  provenance->num_correlation_ids = entry->num_ids;
  provenance->correlation_ids = entry->ids;

  return TRUE;
}

/**
 * gst_tensor_list_meta_get_typed_tensor:
 * @meta: a #GstTensorListMeta
 * @item_index: item index
 * @tensor_id: tensor ID
 * @data_type: expected tensor data type
 * @order: expected tensor dimension order
 * @num_dims: expected number of dimensions
 * @dims: (array length=num_dims): expected tensor dimensions
 *
 * Gets the tensor identified by @tensor_id from the specified @item_index
 * and verifies that it matches the expected type and dimensions.
 *
 * Returns: (transfer none) (nullable): the matching #GstTensor, or %NULL
 *
 * Since: 1.30
 */
const GstTensor *
gst_tensor_list_meta_get_typed_tensor (GstTensorListMeta * meta,
    gsize item_index, GQuark tensor_id, GstTensorDataType data_type,
    GstTensorDimOrder order, gsize num_dims, const gsize * dims)
{
  GstTensorListItem *item;

  g_return_val_if_fail (meta != NULL, NULL);
  g_return_val_if_fail (item_index < meta->num_items, NULL);

  item = meta->items[item_index];

  for (gsize i = 0; i < item->num_tensors; i++) {
    const GstTensor *tensor = item->tensors[i];

    if (tensor->id != tensor_id)
      continue;

    if (!gst_tensor_check_type (tensor, data_type, order, num_dims, dims))
      return NULL;

    return tensor;
  }

  return NULL;
}

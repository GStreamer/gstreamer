/* GStreamer
 * Copyright (C) 2025 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticsgroupmtd.c
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

#include "gstanalyticsgroupmtd.h"

GST_DEBUG_CATEGORY_EXTERN (gst_analytics_relation_meta_debug);
#define GST_CAT_DEFAULT gst_analytics_relation_meta_debug

/**
 * SECTION:gstanalyticsgroupmtd
 * @title: GstAnalyticsGroupMtd
 * @short_description: An analytics metadata for grouping #GstAnalyticsMtd
 * @symbols:
 * - GstAnalyticsGroupMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This type of metadata define a group of #GstAnalyticsMtd. It allows to
 * describe results as composition. Group members are ordered within the group
 * and the @semantic_tag allow to define a specific group meaning.
 * For example with a semantic_tag = "posture/hand-21-kp", first
 * member represent the "wrist", second is the thumb first joint.
 * Since: 1.30
 */

typedef struct _GstAnalyticsGroupMtdData GstAnalyticsGroupMtdData;

/*
 * GstAnalyticsGroupMtdData:
 * @semantic_tag: Semantic meaning of this grouping where a value of NULL means
 *    no semantic is defined for the group.
 * @members_len: Number of allocated member slots
 * @members_count: Number of assigned member slot
 * @members_allocated: TRUE if members was dynamically allocated, FALSE if using inplace storage
 * @members: Array of member IDs
 * @members_inplace: In-place member ID storage
 *
 * Store grouping details. Members are stored as IDs only since the meta
 * pointer is the same as the group's meta.
 *
 * Since 1.30
 */
struct _GstAnalyticsGroupMtdData
{
  GstIdStr semantic_tag;
  gsize members_len;
  gsize members_count;
  gboolean members_allocated;
  guint *members;
  guint members_inplace[];
};

static gboolean gst_analytics_group_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data);
static void gst_analytics_group_mtd_clear (GstBuffer * buffer,
    GstAnalyticsMtd * mtd);

static const GstAnalyticsMtdImpl group_impl = {
  "grouping-mtd",
  gst_analytics_group_mtd_transform,
  gst_analytics_group_mtd_clear
};

/**
 * gst_analytics_group_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsMtd type.
 *
 * Returns: opaque id of #GstAnalyticsMtd type
 *
 * Since: 1.30
 */
GstAnalyticsMtdType
gst_analytics_group_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & group_impl;
}

/**
 * gst_analytics_group_mtd_has_semantic_tag:
 * @handle: handle
 * @tag: tag string to compare against
 *
 * Check if the group's semantic tag matches @tag. An empty or unset tag never
 * matches.
 *
 * Returns: %TRUE if the group's semantic tag equals @tag.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_group_mtd_has_semantic_tag (const GstAnalyticsGroupMtd * handle,
    const gchar * tag)
{
  GstAnalyticsGroupMtdData *mtddata;
  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (tag, FALSE);

  mtddata = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (mtddata != NULL, FALSE);

  return gst_id_str_is_equal_to_str (&mtddata->semantic_tag, tag);
}

/**
 * gst_analytics_group_mtd_semantic_tag_has_prefix:
 * @handle: handle
 * @prefix: prefix string to test against
 *
 * Check if the group's semantic tag starts with @prefix. An empty or unset tag
 * never matches.
 *
 * Returns: %TRUE if the group's semantic tag starts with @prefix.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_group_mtd_semantic_tag_has_prefix (const GstAnalyticsGroupMtd *
    handle, const gchar * prefix)
{
  GstAnalyticsGroupMtdData *mtddata;
  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (prefix, FALSE);

  mtddata = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (mtddata != NULL, FALSE);

  return g_str_has_prefix (gst_id_str_as_str (&mtddata->semantic_tag), prefix);
}

/**
 * gst_analytics_group_mtd_get_member_count:
 * @handle: handle
 *
 * Get the number of members in the group.
 *
 * Returns: Number of members in the group, or 0 on error.
 *
 * Since: 1.30
 */
gsize
gst_analytics_group_mtd_get_member_count (const GstAnalyticsGroupMtd * handle)
{
  GstAnalyticsGroupMtdData *mtddata;
  g_return_val_if_fail (handle, 0);

  mtddata = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (mtddata != NULL, 0);

  return mtddata->members_count;
}

/**
 * gst_analytics_group_mtd_get_member:
 * @handle: handle
 * @index: Index of the member to retrieve
 * @member: (out) (not nullable): Handle updated to the member at @index
 *
 * Get a member from the group by index. Members are ordered within the group
 * according to the semantic tag meaning.
 *
 * Returns: TRUE if member was retrieved successfully, FALSE otherwise (invalid index or error).
 *
 * Since: 1.30
 */
gboolean
gst_analytics_group_mtd_get_member (const GstAnalyticsGroupMtd * handle,
    gsize index, GstAnalyticsMtd * member)
{
  GstAnalyticsGroupMtdData *mtddata;
  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (member, FALSE);

  mtddata = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (mtddata != NULL, FALSE);

  /* Fix members pointer if using in-place storage after potential reallocation */
  if (!mtddata->members_allocated) {
    mtddata->members = mtddata->members_inplace;
  }

  if (index >= mtddata->members_count) {
    return FALSE;
  }

  /* Reconstruct handle from stored ID and group's meta */
  member->id = mtddata->members[index];
  member->meta = handle->meta;
  return TRUE;
}

/**
 * gst_analytics_group_mtd_iterate:
 * @handle: Instance of GstAnalyticsGroupMtd to iterate
 * @state: (inout): Opaque data to store iteration state, initialize to NULL,
 *    no need to free it.
 * @type: Type of GstAnalyticsMtd to iterate on or use
 *    %GST_ANALYTICS_MTD_TYPE_ANY for any.
 * @member: (out caller-allocates)(not nullable): Handle updated to iterated
 *    group member.
 *
 * Iterate over members in the group. If @type is specified, only members
 * matching that type are returned. Initialize @state to NULL to start iteration.
 *
 * Returns: TRUE if a member was retrieved, FALSE when iteration is complete
 *    or an error occurred.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_group_mtd_iterate (const GstAnalyticsGroupMtd * handle,
    gpointer * state, GstAnalyticsMtdType type, GstAnalyticsMtd * member)
{
  GstAnalyticsGroupMtdData *mtddata;
  gsize index;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (state, FALSE);
  g_return_val_if_fail (member, FALSE);

  mtddata = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (mtddata != NULL, FALSE);

  /* Fix members pointer if using in-place storage after potential reallocation */
  if (!mtddata->members_allocated) {
    mtddata->members = mtddata->members_inplace;
  }

  /* Decode state: use MSB as flag, rest as index */
  if (*state) {
    index = ~G_MINSSIZE & (GPOINTER_TO_SIZE (*state) + 1);
  } else {
    index = 0;
    *state = GSIZE_TO_POINTER (G_MINSSIZE | index);
  }

  /* Iterate through members looking for matching type */
  for (; index < mtddata->members_count; index++) {
    /* Reconstruct handle from stored ID and group's meta */
    member->id = mtddata->members[index];
    member->meta = handle->meta;

    if (type == GST_ANALYTICS_MTD_TYPE_ANY ||
        gst_analytics_mtd_get_mtd_type (member) == type) {
      *state = GSIZE_TO_POINTER (G_MINSSIZE | index);
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * gst_analytics_relation_meta_add_group_mtd:
 * @instance: Instance of @GstAnalyticsRelationMeta where to add group_mtd
 *    instance
 * @pre_alloc_size: Slots allocated for group members
 * @group_mtd: (out) (not nullable): Handle to updated to newly added grouping
 *    mtd
 *
 * Add mtd grouping metadata to @inscance. Use this API when the number of members
 * need to be dynamic, otherwise prefer
 * @gst_analytics_relation_meta_add_group_mtd_with_size.
 *
 * Returns: TRUE is added successfully, otherwise FALSE.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_group_mtd (GstAnalyticsRelationMeta * instance,
    gsize pre_alloc_size, GstAnalyticsGroupMtd * group_mtd)
{
  g_return_val_if_fail (instance, FALSE);
  g_return_val_if_fail (group_mtd, FALSE);

  GstAnalyticsGroupMtdData *group_mtd_data = (GstAnalyticsGroupMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, &group_impl,
      sizeof (GstAnalyticsGroupMtdData), group_mtd);

  if (group_mtd_data) {
    group_mtd_data->members_len = pre_alloc_size;
    group_mtd_data->members = g_malloc_n (pre_alloc_size, sizeof (guint));
    group_mtd_data->members_count = 0;
    group_mtd_data->members_allocated = TRUE;
    gst_id_str_init (&group_mtd_data->semantic_tag);
  } else {
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_analytics_relation_meta_add_group_mtd_with_size:
 * @instance: Instance of @GstAnalyticsRelationMeta where to add group_mtd
 *    instance
 * @group_size: Slots reserved for group members
 * @group_mtd: (out) (not nullable): Handle to updated to newly added grouping
 *    mtd
 *
 * Add mtd grouping metadata to @inscance. Use this API when the number of
 * members is known when adding the group.
 *
 * Returns: TRUE is added successfully, otherwise FALSE.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_group_mtd_with_size (GstAnalyticsRelationMeta *
    instance, gsize group_size, GstAnalyticsGroupMtd * group_mtd)
{
  g_return_val_if_fail (instance, FALSE);
  g_return_val_if_fail (group_mtd, FALSE);

  gsize members_size = sizeof (guint) * group_size;
  gsize size = sizeof (GstAnalyticsGroupMtdData) + members_size;
  GstAnalyticsGroupMtdData *group_data = (GstAnalyticsGroupMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, &group_impl, size,
      group_mtd);

  if (group_data) {
    group_data->members_len = group_size;
    group_data->members_count = 0;
    group_data->members = group_data->members_inplace;
    group_data->members_allocated = FALSE;
    gst_id_str_init (&group_data->semantic_tag);
  } else {
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_analytics_group_mtd_add_member:
 * @handle: handle of #GstAnalyticsMtd where to add a member
 * @an_meta_id: ID of #GstAnalyticsMtd to add as a member
 *
 * Adding a member to the group.
 *
 * Returns: Member added successfully
 *
 * Since: 1.30
 */
gboolean
gst_analytics_group_mtd_add_member (GstAnalyticsGroupMtd * handle,
    guint an_meta_id)
{
  gboolean ret = TRUE;
  g_return_val_if_fail (handle, FALSE);

  GstAnalyticsGroupMtdData *group_data =
      gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (group_data != NULL, FALSE);

  /* Fix members pointer if using in-place storage after potential reallocation */
  if (!group_data->members_allocated) {
    group_data->members = group_data->members_inplace;
  }

  if (group_data->members_len > group_data->members_count) {
    /* We can use a slot */
    group_data->members[group_data->members_count++] = an_meta_id;
  } else if (group_data->members == group_data->members_inplace) {
    /* Group size was underestimated and since the members in-place slots be
     * allocated only when adding the group, we will use dynamic allocation. */
    gsize new_len = group_data->members_len + 1;
    GST_WARNING ("Group size under estimated, growing from %zu to %zu fallback"
        " to dynamic allocation.", group_data->members_count, new_len);

    group_data->members = g_malloc_n (new_len, sizeof (guint));
    memcpy (group_data->members, group_data->members_inplace,
        group_data->members_count * sizeof (guint));

    group_data->members_len = new_len;
    group_data->members_allocated = TRUE;
    group_data->members[group_data->members_count++] = an_meta_id;
  } else {
    gsize new_len = group_data->members_len + 1;
    GST_DEBUG ("Group size under estimated, growing from %zu to %zu",
        group_data->members_count, new_len);

    group_data->members = g_realloc_n (group_data->members,
        new_len, sizeof (guint));
    group_data->members_len = new_len;
    group_data->members[group_data->members_count++] = an_meta_id;
  }

  ret = gst_analytics_relation_meta_set_relation (handle->meta,
      GST_ANALYTICS_REL_TYPE_IS_PART_OF, an_meta_id, handle->id);
  if (!ret) {
    GST_WARNING ("Failed to create relation from %u to %u", an_meta_id,
        handle->id);
  }

  ret = gst_analytics_relation_meta_set_relation (handle->meta,
      GST_ANALYTICS_REL_TYPE_CONTAIN, handle->id, an_meta_id);
  if (!ret) {
    GST_WARNING ("Failed to create relation from %u to %u", handle->id,
        an_meta_id);
  }

  return ret;
}

/**
 * gst_analytics_group_mtd_set_semantic_tag:
 * @handle: handle of #GstAnalyticsMtd where to set semantic_tag
 * @tag: (nullable): string representing a semantic type of grouping, or %NULL
 *   to clear the tag
 *
 * Set grouping semantic tag. This gives a context to understand the grouping.
 * For example "hand-21-kp", "human-pose-17-kp" that give a specific context
 * allowing to understand the grouping semantic. Pass %NULL to clear the tag.
 *
 * Returns: TRUE is semantic_tag was set successfully
 *
 * Since: 1.30
 */
gboolean
gst_analytics_group_mtd_set_semantic_tag (GstAnalyticsGroupMtd * handle,
    const gchar * tag)
{
  g_return_val_if_fail (handle, FALSE);

  GstAnalyticsGroupMtdData *group_data =
      gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (group_data != NULL, FALSE);

  if (tag)
    gst_id_str_set (&group_data->semantic_tag, tag);
  else
    gst_id_str_clear (&group_data->semantic_tag);

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_group_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsGroupMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with group mtd
 *
 * Retrieve group metadata instance from @meta.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_get_group_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsGroupMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_group_mtd_get_mtd_type (), (GstAnalyticsGroupMtd *) rlt);
}

static gboolean
gst_analytics_group_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsGroupMtdData *dst_data;

  dst_data =
      gst_analytics_relation_meta_get_mtd_data (transmtd->meta, transmtd->id);
  g_return_val_if_fail (dst_data != NULL, FALSE);

  GstIdStr tmp = GST_ID_STR_INIT;
  gst_id_str_copy_into (&tmp, &dst_data->semantic_tag);
  gst_id_str_init (&dst_data->semantic_tag);
  gst_id_str_move (&dst_data->semantic_tag, &tmp);

  /* For dynamically allocated members we need to make an independent copy.
   * For in-place storage, fix up the pointer to the new location since it
   * was memcpy'd from the source struct. */
  if (dst_data->members_allocated) {
    guint *src_members = dst_data->members;
    dst_data->members = g_malloc_n (dst_data->members_count, sizeof (guint));
    memcpy (dst_data->members, src_members,
        dst_data->members_count * sizeof (guint));
    dst_data->members_len = dst_data->members_count;
  } else {
    dst_data->members = dst_data->members_inplace;
  }

  return TRUE;
}

static void
gst_analytics_group_mtd_clear (GstBuffer * buffer, GstAnalyticsMtd * mtd)
{
  GstAnalyticsGroupMtdData *groupdata;
  groupdata = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_if_fail (groupdata != NULL);

  /* Free members array if it was dynamically allocated.
     Use the members_allocated flag instead of pointer comparison because
     after metadata buffer reallocation, members pointer may point to old
     inplace location. */
  if (groupdata->members_allocated) {
    g_free (groupdata->members);
  }

  gst_id_str_clear (&groupdata->semantic_tag);
}

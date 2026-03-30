/* GStreamer
 * Copyright (C) 2025 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticsgroupmtd.h
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

#ifndef __GST_ANALYTICS_GROUP_MTD_H__
#define __GST_ANALYTICS_GROUP_MTD_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsGroupMtd:
 * @id: Instance identifier
 * @meta: Instance of #GstAnalyticsRelationMeta where the analytics-metadata
 * identified by @id is stored
 *
 * Handle containing data required by gst_analytics_group_mtd APIs. This type
 * is generally expected to be allocated on the stack.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsMtd GstAnalyticsGroupMtd;


GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_group_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
gboolean gst_analytics_group_mtd_has_semantic_tag (const GstAnalyticsGroupMtd *
    handle, const gchar * tag);

GST_ANALYTICS_META_API
gboolean gst_analytics_group_mtd_semantic_tag_has_prefix (const GstAnalyticsGroupMtd *
    handle, const gchar * prefix);

GST_ANALYTICS_META_API
gsize gst_analytics_group_mtd_get_member_count (const GstAnalyticsGroupMtd *
    handle);

GST_ANALYTICS_META_API
gboolean gst_analytics_group_mtd_get_member (const GstAnalyticsGroupMtd *
    handle, gsize index, GstAnalyticsMtd * member);

GST_ANALYTICS_META_API
gboolean gst_analytics_group_mtd_iterate (const GstAnalyticsGroupMtd * handle,
    gpointer * state, GstAnalyticsMtdType type, GstAnalyticsMtd * member);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_group_mtd (GstAnalyticsRelationMeta *
    instance, gsize pre_alloc_size, GstAnalyticsGroupMtd * group_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_group_mtd_with_size (GstAnalyticsRelationMeta *
    instance, gsize group_size, GstAnalyticsGroupMtd * group_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_group_mtd_add_member (GstAnalyticsGroupMtd * handle,
    guint an_meta_id);

GST_ANALYTICS_META_API
gboolean gst_analytics_group_mtd_set_semantic_tag (GstAnalyticsGroupMtd * handle,
    const char * tag);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_group_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsGroupMtd * rlt);

GST_ANALYTICS_META_API
gchar * gst_analytics_group_mtd_get_semantic_tag (const GstAnalyticsGroupMtd *
    handle);

G_END_DECLS
#endif /* __GST_ANALYTICS_GROUP_MTD_H__ */

/* GStreamer
 * Copyright (C) 2024 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticssegmentationmtd.h
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

#ifndef __GST_ANALYTICS_SEGMENTATION_META_H__
#define __GST_ANALYTICS_SEGMENTATION_META_H__

#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsSegmentationMtd:
 * @id: Instance identifier
 * @meta: Instance of #GstAnalyticsRelationMeta where the analytics-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_segmentation_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.26
 */
typedef struct _GstAnalyticsMtd GstAnalyticsSegmentationMtd;

/**
 * GstSegmentationType:
 * @GST_SEGMENTATION_TYPE_SEMANTIC: Segmentation where the belonging of each
 * pixel to a class of objects is identified.
 * @GST_SEGMENTATION_TYPE_INSTANCE: Segmentation where the belonging of each
 * pixel to instance of an object is identified.
 *
 * Enum value describing supported segmentation type
 *
 * Since: 1.26
 */
typedef enum
{
  GST_SEGMENTATION_TYPE_SEMANTIC,
  GST_SEGMENTATION_TYPE_INSTANCE
} GstSegmentationType;

GST_ANALYTICS_META_API
GstAnalyticsMtdType
gst_analytics_segmentation_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
GstBuffer *
gst_analytics_segmentation_mtd_get_mask (const GstAnalyticsSegmentationMtd *
    handle, gint * masks_loc_x, gint * masks_loc_y, guint * masks_loc_w, guint *
    masks_loc_h) G_GNUC_WARN_UNUSED_RESULT;

GST_ANALYTICS_META_API
gboolean
gst_analytics_segmentation_mtd_get_region_index (
    const GstAnalyticsSegmentationMtd * handle, gsize * index, guint id);

GST_ANALYTICS_META_API
guint
gst_analytics_segmentation_mtd_get_region_id (
    const GstAnalyticsSegmentationMtd * handle, gsize index);

GST_ANALYTICS_META_API
gsize
gst_analytics_segmentation_mtd_get_region_count (
    const GstAnalyticsSegmentationMtd * handle);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_add_segmentation_mtd (GstAnalyticsRelationMeta *
    instance, GstBuffer * buffer, GstSegmentationType segmentation_type,
    gsize region_count, guint * region_ids, gint masks_loc_x, gint masks_loc_y,
    guint masks_loc_w, guint masks_loc_h, GstAnalyticsSegmentationMtd *
    segmentation_mtd);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_get_segmentation_mtd (GstAnalyticsRelationMeta *
    meta, guint an_meta_id, GstAnalyticsSegmentationMtd * rlt);

G_END_DECLS
#endif // __GST_ANALYTICS_SEGMENTATION_META_H__

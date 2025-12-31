/* GStreamer
 * Copyright (C) 2024 Intel Corporation
 *  @author: Tomasz Janczak <tomasz.janczak@intel.com>
 * Copyright (C) 2026 Collabora
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticskeypointmtd.h
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

#ifndef __GST_ANALYTICS_KEYPOINT_MTD_H__
#define __GST_ANALYTICS_KEYPOINT_MTD_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gstanalyticsgroupmtd.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsKeypointMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analytics-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_keypoint_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsMtd GstAnalyticsKeypointMtd;

/**
 * GstAnalyticsKeypointDimensions:
 * @GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D: keypoints in 2D space with (x,y) coordinates.
 * @GST_ANALYTICS_KEYPOINT_DIMENSIONS_3D: keypoints in 3D space with (x,y,z) coordinates.
 *
 * Enum value describing supported keypoint dimension.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D = 2,
  GST_ANALYTICS_KEYPOINT_DIMENSIONS_3D = 3,
} GstAnalyticsKeypointDimensions;

/**
 * GstAnalyticsKeypointVisibility:
 * @GST_ANALYTICS_KEYPOINT_VISIBILITY_UNKNOWN: Visibility is not known.
 * @GST_ANALYTICS_KEYPOINT_VISIBILITY_VISIBLE: Keypoint is fully visible.
 * @GST_ANALYTICS_KEYPOINT_VISIBILITY_OCCLUDED: Keypoint exists but is
 *   occluded.
 * @GST_ANALYTICS_KEYPOINT_VISIBILITY_PROJECTED: Keypoint position was
 *   projected by the analysis.
 *
 * Flags describing the visibility state of a keypoint.
 *
 * Since: 1.30
 */
typedef enum {
  GST_ANALYTICS_KEYPOINT_VISIBILITY_UNKNOWN = 0,
  GST_ANALYTICS_KEYPOINT_VISIBILITY_VISIBLE = 1,
  GST_ANALYTICS_KEYPOINT_VISIBILITY_OCCLUDED = 1 << 2,
  GST_ANALYTICS_KEYPOINT_VISIBILITY_PROJECTED = 1 << 3,
} GstAnalyticsKeypointVisibility;


GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_keypoint_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_keypoint_mtd (
    GstAnalyticsRelationMeta *instance,
    GstAnalyticsKeypointDimensions dimension,
    gint x, gint y, gint z,
    guint8 visibility_flags,
    gfloat confidence,
    GstAnalyticsKeypointMtd *keypoint_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_mtd_get_position (
    const GstAnalyticsKeypointMtd *handle,
    gint *x, gint *y, gint *z,
    GstAnalyticsKeypointDimensions *dimension);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_mtd_get_confidence (
    const GstAnalyticsKeypointMtd *handle,
    gfloat *confidence);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_mtd_get_visibility_flags (
    const GstAnalyticsKeypointMtd *handle,
    guint8 *visibility_flags);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypoint_mtd (
    GstAnalyticsRelationMeta *meta,
    guint an_meta_id,
    GstAnalyticsKeypointMtd *rlt);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_keypoints_group (
    GstAnalyticsRelationMeta *instance,
    const gchar *semantic_tag,
    GstAnalyticsKeypointDimensions dimension,
    gsize positions_len,
    const gint *positions,
    gsize keypoint_count,
    const gfloat *confidences,
    const guint8 *visibilities,
    gsize skeleton_pairs_len,
    const gint *skeleton_pairs,
    GstAnalyticsGroupMtd *group_mtd);

G_END_DECLS

#endif /* __GST_ANALYTICS_KEYPOINT_MTD_H__ */

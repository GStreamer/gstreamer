/* GStreamer
 * Copyright (C) 2026 Collabora
 *  @author: Olivier Crête <olivier.crete@collabora.com>
 *
 * gstanalyticsorientationmtd.h
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

#ifndef __GST_ANALYTICS_ORIENTATION_MTD_H__
#define __GST_ANALYTICS_ORIENTATION_MTD_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsOrientationMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analytics-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_orientation_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsMtd GstAnalyticsOrientationMtd;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_orientation_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_orientation_mtd (
    GstAnalyticsRelationMeta *instance,
    gfloat yaw,
    gfloat pitch,
    gfloat confidence,
    GstAnalyticsOrientationMtd *orientation_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_orientation_mtd_get_angles (
    const GstAnalyticsOrientationMtd *handle,
    gfloat * yaw, gfloat * pitch);

GST_ANALYTICS_META_API
gboolean gst_analytics_orientation_mtd_get_confidence (
    const GstAnalyticsOrientationMtd *handle,
    gfloat *confidence);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_orientation_mtd (
    GstAnalyticsRelationMeta *meta,
    guint an_meta_id,
    GstAnalyticsOrientationMtd *rlt);

G_END_DECLS

#endif /* __GST_ANALYTICS_ORIENTATION_MTD_H__ */

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
#include <gst/video/video.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gsttensor.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsTensorProvenanceMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analytics-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_tensor_provenance_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsMtd GstAnalyticsTensorProvenanceMtd;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_tensor_provenance_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
gboolean  gst_analytics_relation_meta_add_tensor_provenance_mtd (GstAnalyticsRelationMeta * meta,
                                                                 GQuark group_id,
                                                                 const GstTensor * tensor,
                                                                 GstAnalyticsTensorProvenanceMtd * tensor_provenance_mtd);

GST_ANALYTICS_META_API
gboolean  gst_analytics_relation_meta_get_tensor_provenance_mtd (GstAnalyticsRelationMeta * meta,
                                                                 guint an_meta_id,
                                                                 GstAnalyticsTensorProvenanceMtd * rlt);

GST_ANALYTICS_META_API
GQuark    gst_analytics_tensor_provenance_mtd_get_group_id (const GstAnalyticsTensorProvenanceMtd * mtd);

GST_ANALYTICS_META_API
GQuark    gst_analytics_tensor_provenance_mtd_get_tensor_id (const GstAnalyticsTensorProvenanceMtd * mtd);

GST_ANALYTICS_META_API
gint64    gst_analytics_tensor_provenance_mtd_get_correlation_id (const GstAnalyticsTensorProvenanceMtd * mtd);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_has_spatial_transform (const GstAnalyticsTensorProvenanceMtd * mtd);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_set_spatial_transform (GstAnalyticsTensorProvenanceMtd * mtd,
                                                                     const gfloat transform[9]);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_set_spatial_transform_from_rectangles (GstAnalyticsTensorProvenanceMtd * mtd,
                                                                                     const GstVideoRectangle * tensor_rect,
                                                                                     const GstVideoRectangle * video_rect);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_get_spatial_transform (const GstAnalyticsTensorProvenanceMtd * mtd,
                                                                     gfloat transform[9]);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_transform_point (const GstAnalyticsTensorProvenanceMtd * mtd,
                                                               gfloat * x,
                                                               gfloat * y);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_transform_rectangle (const GstAnalyticsTensorProvenanceMtd * mtd,
                                                                   gfloat * x,
                                                                   gfloat * y,
                                                                   gfloat * width,
                                                                   gfloat * height);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_set_timestamp (GstAnalyticsTensorProvenanceMtd * mtd,
                                                             GstClockTime timestamp,
                                                             GstClockTime duration);

GST_ANALYTICS_META_API
gboolean  gst_analytics_tensor_provenance_mtd_get_timestamp (const GstAnalyticsTensorProvenanceMtd * mtd,
                                                             GstClockTime * timestamp,
                                                             GstClockTime * duration);

G_END_DECLS

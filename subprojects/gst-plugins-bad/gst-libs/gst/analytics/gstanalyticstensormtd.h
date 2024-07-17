/* GStreamer
 * Copyright (C) 2024 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticstensormtd.h
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

#ifndef __GST_ANALYTICS_TENSOR_MTD_H__
#define __GST_ANALYTICS_TENSOR_MTD_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/analytics/gsttensor.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsTensorMtd:
 * @id: Instance identifier
 * @meta: Instance of #GstAnalyticsRelationMeta where the analytics-metadata
 * identified by @id is stored
 *
 * Handle containing data required to use gst_analytics_tensor_mtd APIs.
 * This type is generally expected to be allocated on stack.
 *
 * Since: 1.28
 */
typedef struct _GstAnalyticsMtd GstAnalyticsTensorMtd;


GST_ANALYTICS_META_API
GstAnalyticsMtdType
gst_analytics_tensor_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
GstTensor *
gst_analytics_tensor_mtd_get_tensor (const GstAnalyticsTensorMtd * instance);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_add_tensor_mtd (GstAnalyticsRelationMeta *
    instance, gsize num_dims, GstAnalyticsTensorMtd * tensor_mtd);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_add_tensor_mtd_simple (GstAnalyticsRelationMeta *
    instance, GQuark id, GstTensorDataType data_type,
    GstBuffer * data, GstTensorDimOrder dims_order, gsize num_dims,
    gsize * dims, GstAnalyticsTensorMtd * tensor_mtd);

GST_ANALYTICS_META_API
gboolean
gst_analytics_relation_meta_get_tensor_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsTensorMtd * rlt);

G_END_DECLS
#endif /* __GST_ANALYTICS_TENSOR_MTD_H__ */

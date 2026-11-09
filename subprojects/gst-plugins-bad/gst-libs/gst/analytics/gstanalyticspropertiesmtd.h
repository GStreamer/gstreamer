/* GStreamer
 * Copyright (C) 2026 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticspropertiesmtd.h
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
#include <gst/analytics/gstanalyticsmeta.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsPropertiesMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where this Mtd is stored.
 *
 * Handle containing data required to use gst_analytics_properties_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsMtd GstAnalyticsPropertiesMtd;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_properties_mtd_get_mtd_type (void);

GST_ANALYTICS_META_API
gboolean    gst_analytics_properties_mtd_set_value (GstAnalyticsPropertiesMtd * mtd,
                                                    const gchar * key,
                                                    const GValue * value);

GST_ANALYTICS_META_API
gboolean    gst_analytics_properties_mtd_set (GstAnalyticsPropertiesMtd * mtd,
                                              const gchar * first_key,
                                              ...) G_GNUC_NULL_TERMINATED;

GST_ANALYTICS_META_API
gboolean    gst_analytics_properties_mtd_set_string (GstAnalyticsPropertiesMtd * mtd,
                                                     const gchar * key,
                                                     const gchar * value);

GST_ANALYTICS_META_API
const GValue * gst_analytics_properties_mtd_get_value (const GstAnalyticsPropertiesMtd * mtd,
                                                       const gchar * key);

GST_ANALYTICS_META_API
const gchar * gst_analytics_properties_mtd_get_string (const GstAnalyticsPropertiesMtd * mtd,
                                                       const gchar * key);

GST_ANALYTICS_META_API
gboolean    gst_analytics_properties_mtd_has_key (const GstAnalyticsPropertiesMtd * mtd,
                                                  const gchar * key);

GST_ANALYTICS_META_API
gboolean    gst_analytics_properties_mtd_remove_key (GstAnalyticsPropertiesMtd * mtd,
                                                     const gchar * key);

GST_ANALYTICS_META_API
guint       gst_analytics_properties_mtd_get_length (const GstAnalyticsPropertiesMtd * mtd);

GST_ANALYTICS_META_API
const gchar * gst_analytics_properties_mtd_get_nth_key (const GstAnalyticsPropertiesMtd * mtd,
                                                        guint index);

GST_ANALYTICS_META_API
const GstStructure * gst_analytics_properties_mtd_get_structure (const GstAnalyticsPropertiesMtd * mtd);

GST_ANALYTICS_META_API
gboolean    gst_analytics_relation_meta_add_properties_mtd (GstAnalyticsRelationMeta * meta,
                                                            GstAnalyticsPropertiesMtd * properties_mtd);

GST_ANALYTICS_META_API
gboolean    gst_analytics_relation_meta_get_properties_mtd (GstAnalyticsRelationMeta * meta,
                                                            guint an_meta_id,
                                                            GstAnalyticsPropertiesMtd * rlt);

G_END_DECLS

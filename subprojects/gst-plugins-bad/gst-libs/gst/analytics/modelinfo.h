/*
 * GStreamer
 * Copyright (C) 2025 Collabora Ltd.
 * @author: Olivier Crete <olivier.crete@collabora.com>
 *
 * modeinfo.c
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


#include <glib.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gsttensor.h>

#pragma once

/**
 * GST_MODELINFO_VERSION_MAJOR:
 *
 * The current major version of the modelinfo format
 *
 * Since: 1.28
 */
#define GST_MODELINFO_VERSION_MAJOR (1)

/**
 * GST_MODELINFO_VERSION_MINOR:
 *
 * The current minor version of the modelinfo format
 *
 * Since: 1.28
 */
#define GST_MODELINFO_VERSION_MINOR (0)

/**
 * GST_MODELINFO_VERSION_STR:
 *
 * The current version string for the modelinfo format.
 * This MUST be updated whenever the format changes.
 *
 * Since: 1.28
 */
#define GST_MODELINFO_VERSION_STR "1.0"

/**
 * GST_MODELINFO_SECTION_NAME:
 *
 * The name of the modelinfo header section
 *
 * Since: 1.28
 */
#define GST_MODELINFO_SECTION_NAME "modelinfo"

G_BEGIN_DECLS

/**
 * GstAnalyticsModelInfoTensorDirection:
 * @MODELINFO_DIRECTION_UNKNOWN: Tensor location is unknown
 * @MODELINFO_DIRECTION_INPUT: Input tensor
 * @MODELINFO_DIRECTION_OUTPUT: Output tensor
 *
 * Since: 1.28
 */
typedef enum {
  MODELINFO_DIRECTION_UNKNOWN,
  MODELINFO_DIRECTION_INPUT,
  MODELINFO_DIRECTION_OUTPUT,
} GstAnalyticsModelInfoTensorDirection;

/**
 * GstAnalyticsModelInfo:
 *
 * The #GstAnalyticsModelInfo is an object storing artifical neural network
 * model metadata describing the input and output tensors. These information's
 * are required by inference elements.
 *
 * Since: 1.28
 */
typedef struct _ModelInfo GstAnalyticsModelInfo;

GST_ANALYTICS_META_API
GType gst_analytics_modelinfo_get_type (void);

/**
 * GST_ANALYTICS_MODELINFO_TYPE:
 *
 * The model info type
 *
 * Since: 1.28
 */
#define GST_ANALYTICS_MODELINFO_TYPE (gst_analytics_modelinfo_get_type())

GST_ANALYTICS_META_API
GstAnalyticsModelInfo *
gst_analytics_modelinfo_load (const gchar *model_filename);

GST_ANALYTICS_META_API
gchar *
gst_analytics_modelinfo_find_tensor_name (GstAnalyticsModelInfo * modelinfo,
    GstAnalyticsModelInfoTensorDirection dir, gsize index, const gchar *in_tensor_name,
    GstTensorDataType data_type, gsize num_dims, const gsize * dims);

GST_ANALYTICS_META_API
gchar *
gst_analytics_modelinfo_get_id (GstAnalyticsModelInfo *modelinfo, const gchar * tensor_name);

GST_ANALYTICS_META_API
gchar *
gst_analytics_modelinfo_get_group_id (GstAnalyticsModelInfo * modelinfo);

GST_ANALYTICS_META_API
GQuark
gst_analytics_modelinfo_get_quark_id (GstAnalyticsModelInfo *modelinfo, const gchar * tensor_name);

GST_ANALYTICS_META_API
GQuark
gst_analytics_modelinfo_get_quark_group_id (GstAnalyticsModelInfo * modelinfo);

GST_ANALYTICS_META_API
gboolean
gst_analytics_modelinfo_get_target_ranges (GstAnalyticsModelInfo * modelinfo,
    const gchar *tensor_name, gsize *num_ranges, gdouble **mins, gdouble **maxs);

GST_ANALYTICS_META_API
gboolean
gst_analytics_modelinfo_get_input_scales_offsets (GstAnalyticsModelInfo * modelinfo,
    const gchar *tensor_name, gsize num_input_ranges, const gdouble *input_mins,
    const gdouble *input_maxs, gsize *num_output_ranges, gdouble **output_scales,
    gdouble **output_offsets);

GST_ANALYTICS_META_API
GstTensorDimOrder
gst_analytics_modelinfo_get_dims_order (GstAnalyticsModelInfo * modelinfo,
    const gchar * tensor_name);

GST_ANALYTICS_META_API
gchar *
gst_analytics_modelinfo_get_version (GstAnalyticsModelInfo * modelinfo);

GST_ANALYTICS_META_API
void
gst_analytics_modelinfo_free (GstAnalyticsModelInfo *model_info);

G_END_DECLS

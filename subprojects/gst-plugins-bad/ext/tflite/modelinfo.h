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
#include <gst/analytics/analytics.h>

#pragma once

G_BEGIN_DECLS


typedef enum {
  MODELINFO_DIRECTION_UNKNOWN,
  MODELINFO_DIRECTION_INPUT,
  MODELINFO_DIRECTION_OUTPUT,
} ModelInfoTensorDirection;

typedef struct _ModelInfo ModelInfo;

ModelInfo *
modelinfo_load (const gchar *model_filename);

gchar *
modelinfo_find_tensor_name (ModelInfo * modelinfo,
    ModelInfoTensorDirection dir, gsize index, const gchar *in_tensor_name,
    GstTensorDataType data_type, gsize num_dims, const gsize * dims);

gchar *
modelinfo_get_id (ModelInfo *modelinfo, const gchar * tensor_name);

GQuark
modelinfo_get_quark_id (ModelInfo *modelinfo, const gchar * tensor_name);

gsize
modelinfo_get_normalization_means (ModelInfo * modelinfo,
    const gchar *tensor_name, gsize num_channels, gdouble ** mean);

gsize
modelinfo_get_normalization_stddevs (ModelInfo * modelinfo,
    const gchar *tensor_name, gsize num_channels, gdouble ** stddev);

void
modelinfo_free (ModelInfo *model_info);

G_END_DECLS

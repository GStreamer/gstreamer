/*
 * GStreamer gstreamer-tensormeta
 * Copyright (C) 2023 Collabora Ltd
 *
 * gsttensormeta.h
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

#ifndef __GST_TENSOR_META_H__
#define __GST_TENSOR_META_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gsttensor.h>

/**
 * GstTensorMeta:
 * @meta base GstMeta
 * @num_tensors number of tensors
 * @tensor @ref GstTensor for each tensor
 * @batch_size model batch size
 *
 * Since: 1.26
 */
typedef struct _GstTensorMeta
{
  GstMeta meta;

  gsize num_tensors;
  GstTensor *tensor;
  gsize batch_size;
} GstTensorMeta;

G_BEGIN_DECLS

#define GST_TENSOR_META_API_TYPE \
  (gst_tensor_meta_api_get_type())

#define GST_TENSOR_META_INFO \
  (gst_tensor_meta_get_info())

GST_ANALYTICS_META_API
GType gst_tensor_meta_api_get_type (void);

GST_ANALYTICS_META_API
const GstMetaInfo *gst_tensor_meta_get_info (void);

GST_ANALYTICS_META_API
gint gst_tensor_meta_get_index_from_id(GstTensorMeta *meta, GQuark id);

G_END_DECLS

#endif

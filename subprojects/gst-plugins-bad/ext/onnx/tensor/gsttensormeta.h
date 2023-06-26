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
#ifndef __GST_TENSOR_META_H__
#define __GST_TENSOR_META_H__

#include <gst/gst.h>
#include "gsttensor.h"

/**
 * GstTensorMeta:
 *
 * @meta base GstMeta
 * @num_tensors number of tensors
 * @tensor @ref GstTensor for each tensor
 * @batch_size model batch size
 *
 * Since: 1.24
 */
typedef struct _GstTensorMeta
{
  GstMeta meta;

  gint num_tensors;
  GstTensor *tensor;
  int batch_size;
} GstTensorMeta;

G_BEGIN_DECLS

GType gst_tensor_meta_api_get_type (void);
const GstMetaInfo *gst_tensor_meta_get_info (void);
GList *gst_tensor_meta_get_all_from_buffer (GstBuffer * buffer);
gint gst_tensor_meta_get_index_from_id(GstTensorMeta *meta, GQuark id);

G_END_DECLS

#endif

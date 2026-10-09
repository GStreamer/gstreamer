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
#include <gst/hip/gsthip_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_HIP_FENCE_DATA_POOL            (gst_hip_fence_data_pool_get_type ())
#define GST_HIP_FENCE_DATA_POOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HIP_FENCE_DATA_POOL, GstHipFenceDataPool))
#define GST_HIP_FENCE_DATA_POOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HIP_FENCE_DATA_POOL, GstHipFenceDataPoolClass))
#define GST_IS_HIP_FENCE_DATA_POOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HIP_FENCE_DATA_POOL))
#define GST_IS_HIP_FENCE_DATA_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HIP_FENCE_DATA_POOL))
#define GST_HIP_FENCE_DATA_POOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HIP_FENCE_DATA_POOL, GstHipFenceDataPoolClass))
#define GST_HIP_FENCE_DATA_POOL_CAST(obj)       ((GstHipFenceDataPool*)(obj))

/**
 * GstHipFenceDataPool:
 *
 * Opaque GstHipFenceDataPool struct
 *
 * Since: 1.30
 */
struct _GstHipFenceDataPool
{
  GstObject parent;

  /*< private >*/
  GstHipFenceDataPoolPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstHipFenceDataPoolClass:
 *
 * Opaque GstHipFenceDataPoolClass struct
 *
 * Since: 1.30
 */
struct _GstHipFenceDataPoolClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_HIP_API
GType                 gst_hip_fence_data_pool_get_type (void);

GST_HIP_API
GstHipFenceDataPool * gst_hip_fence_data_pool_new (void);

GST_HIP_API
gboolean              gst_hip_fence_data_pool_acquire (GstHipFenceDataPool * pool,
                                                       GstHipFenceData ** data);

GST_HIP_API
GType                 gst_hip_fence_data_get_type (void);

GST_HIP_API
void                  gst_hip_fence_data_push (GstHipFenceData * data,
                                               gpointer user_data,
                                               GDestroyNotify notify);

GST_HIP_API
GstHipFenceData *     gst_hip_fence_data_ref (GstHipFenceData * data);

GST_HIP_API
void                  gst_hip_fence_data_unref (GstHipFenceData * data);

GST_HIP_API
void                  gst_clear_hip_fence_data (GstHipFenceData ** data);

G_END_DECLS


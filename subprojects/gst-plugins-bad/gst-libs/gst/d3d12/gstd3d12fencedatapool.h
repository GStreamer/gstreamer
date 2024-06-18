/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_FENCE_DATA_POOL            (gst_d3d12_fence_data_pool_get_type ())
#define GST_D3D12_FENCE_DATA_POOL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_FENCE_DATA_POOL, GstD3D12FenceDataPool))
#define GST_D3D12_FENCE_DATA_POOL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_FENCE_DATA_POOL, GstD3D12FenceDataPoolClass))
#define GST_IS_D3D12_FENCE_DATA_POOL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_FENCE_DATA_POOL))
#define GST_IS_D3D12_FENCE_DATA_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_FENCE_DATA_POOL))
#define GST_D3D12_FENCE_DATA_POOL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_FENCE_DATA_POOL, GstD3D12FenceDataPoolClass))
#define GST_D3D12_FENCE_DATA_POOL_CAST(obj)       ((GstD3D12FenceDataPool*)(obj))

/**
 * GstD3D12FenceDataPool:
 *
 * Opaque GstD3D12FenceDataPool struct
 *
 * Since: 1.26
 */
struct _GstD3D12FenceDataPool
{
  GstObject parent;

  /*< private >*/
  GstD3D12FenceDataPoolPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12FenceDataPoolClass:
 *
 * Opaque GstD3D12FenceDataPoolClass struct
 *
 * Since: 1.26
 */
struct _GstD3D12FenceDataPoolClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
GType                   gst_d3d12_fence_data_pool_get_type (void);

GST_D3D12_API
GType                   gst_d3d12_fence_data_get_type (void);

GST_D3D12_API
GstD3D12FenceDataPool * gst_d3d12_fence_data_pool_new (void);

GST_D3D12_API
gboolean                gst_d3d12_fence_data_pool_acquire (GstD3D12FenceDataPool * pool,
                                                           GstD3D12FenceData ** data);

GST_D3D12_API
void                    gst_d3d12_fence_data_push (GstD3D12FenceData * data,
                                                   gpointer user_data,
                                                   GDestroyNotify notify);

GST_D3D12_API
GstD3D12FenceData *     gst_d3d12_fence_data_ref (GstD3D12FenceData * data);

GST_D3D12_API
void                    gst_d3d12_fence_data_unref (GstD3D12FenceData * data);

GST_D3D12_API
void                    gst_clear_d3d12_fence_data (GstD3D12FenceData ** data);

G_END_DECLS


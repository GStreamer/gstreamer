/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#define GST_TYPE_D3D12_STAGING_BUFFER_POOL                (gst_d3d12_staging_buffer_pool_get_type ())
#define GST_D3D12_STAGING_BUFFER_POOL(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_STAGING_BUFFER_POOL, GstD3D12StagingBufferPool))
#define GST_D3D12_STAGING_BUFFER_POOL_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_STAGING_BUFFER_POOL, GstD3D12StagingBufferPoolClass))
#define GST_IS_D3D12_STAGING_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_STAGING_BUFFER_POOL))
#define GST_IS_D3D12_STAGING_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_STAGING_BUFFER_POOL))
#define GST_D3D12_STAGING_BUFFER_POOL_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_STAGING_BUFFER_POOL, GstD3D12StagingBufferPoolClass))
#define GST_D3D12_STAGING_BUFFER_POOL_CAST(obj)           ((GstD3D12StagingBufferPool*)(obj))

/**
 * GstD3D12StagingBufferPool:
 *
 * Opaque GstD3D12StagingBufferPool struct
 *
 * Since: 1.28
 */
struct _GstD3D12StagingBufferPool
{
  GstBufferPool parent;

  GstD3D12Device *device;

  /*< private >*/
  GstD3D12StagingBufferPoolPrivate *priv;
};

/**
 * GstD3D12StagingBufferPoolClass:
 *
 * Opaque GstD3D12StagingBufferPoolClass struct
 *
 * Since: 1.28
 */
struct _GstD3D12StagingBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GST_D3D12_API
GType           gst_d3d12_staging_buffer_pool_get_type (void);

GST_D3D12_API
GstBufferPool * gst_d3d12_staging_buffer_pool_new (GstD3D12Device * device);

G_END_DECLS


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

#define GST_TYPE_D3D12_BUFFER_POOL                (gst_d3d12_buffer_pool_get_type ())
#define GST_D3D12_BUFFER_POOL(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_BUFFER_POOL, GstD3D12BufferPool))
#define GST_D3D12_BUFFER_POOL_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_BUFFER_POOL, GstD3D12BufferPoolClass))
#define GST_IS_D3D12_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_BUFFER_POOL))
#define GST_IS_D3D12_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_BUFFER_POOL))
#define GST_D3D12_BUFFER_POOL_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_BUFFER_POOL, GstD3D12BufferPoolClass))
#define GST_D3D12_BUFFER_POOL_CAST(obj)           ((GstD3D12BufferPool*)(obj))

/**
 * GstD3D12BufferPool:
 *
 * Opaque GstD3D12BufferPool struct
 *
 * Since: 1.26
 */
struct _GstD3D12BufferPool
{
  GstBufferPool parent;

  GstD3D12Device *device;

  /*< private >*/
  GstD3D12BufferPoolPrivate *priv;
};

/**
 * GstD3D12BufferPoolClass:
 *
 * Opaque GstD3D12BufferPoolClass struct
 *
 * Since: 1.26
 */
struct _GstD3D12BufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GST_D3D12_API
GType           gst_d3d12_buffer_pool_get_type (void);

GST_D3D12_API
GstBufferPool * gst_d3d12_buffer_pool_new (GstD3D12Device * device);

GST_D3D12_API
GstD3D12AllocationParams * gst_buffer_pool_config_get_d3d12_allocation_params (GstStructure * config);

GST_D3D12_API
void            gst_buffer_pool_config_set_d3d12_allocation_params (GstStructure * config,
                                                                    GstD3D12AllocationParams * params);

G_END_DECLS


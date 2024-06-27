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

#define GST_TYPE_D3D12_COMMAND_ALLOCATOR_POOL                (gst_d3d12_command_allocator_pool_get_type ())
#define GST_D3D12_COMMAND_ALLOCATOR_POOL(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_COMMAND_ALLOCATOR_POOL, GstD3D12CommandAllocatorPool))
#define GST_D3D12_COMMAND_ALLOCATOR_POOL_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_COMMAND_ALLOCATOR_POOL, GstD3D12CommandAllocatorPoolClass))
#define GST_IS_D3D12_COMMAND_ALLOCATOR_POOL(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_COMMAND_ALLOCATOR_POOL))
#define GST_IS_D3D12_COMMAND_ALLOCATOR_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_COMMAND_ALLOCATOR_POOL))
#define GST_D3D12_COMMAND_ALLOCATOR_POOL_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_COMMAND_ALLOCATOR_POOL, GstD3D12CommandAllocatorPoolClass))
#define GST_D3D12_COMMAND_ALLOCATOR_POOL_CAST(obj)           ((GstD3D12CommandAllocatorPool*)(obj))

/**
 * GstD3D12CommandAllocatorPool:
 *
 * Opaque GstD3D12CommandAllocatorPool struct
 *
 * Since: 1.26
 */
struct _GstD3D12CommandAllocatorPool
{
  GstObject parent;

  /*< private >*/
  GstD3D12CommandAllocatorPoolPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12CommandAllocatorPoolClass:
 *
 * Opaque GstD3D12CommandAllocatorPoolClass struct
 *
 * Since: 1.26
 */
struct _GstD3D12CommandAllocatorPoolClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
GType                          gst_d3d12_command_allocator_pool_get_type (void);

GST_D3D12_API
GType                          gst_d3d12_command_allocator_get_type (void);

GST_D3D12_API
GstD3D12CommandAllocatorPool * gst_d3d12_command_allocator_pool_new (ID3D12Device * device,
                                                                     D3D12_COMMAND_LIST_TYPE type);

GST_D3D12_API
gboolean                       gst_d3d12_command_allocator_pool_acquire (GstD3D12CommandAllocatorPool * pool,
                                                                         GstD3D12CommandAllocator ** cmd);

GST_D3D12_API
GstD3D12CommandAllocator *     gst_d3d12_command_allocator_ref (GstD3D12CommandAllocator * cmd);

GST_D3D12_API
void                           gst_d3d12_command_allocator_unref (GstD3D12CommandAllocator * cmd);

GST_D3D12_API
void                           gst_clear_d3d12_command_allocator (GstD3D12CommandAllocator ** cmd);

GST_D3D12_API
ID3D12CommandAllocator *       gst_d3d12_command_allocator_get_handle (GstD3D12CommandAllocator * cmd);

G_END_DECLS


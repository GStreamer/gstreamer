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

#define GST_D3D12_STAGING_MEMORY_CAST(obj)          ((GstD3D12StagingMemory *)obj)

#define GST_TYPE_D3D12_STAGING_ALLOCATOR            (gst_d3d12_staging_allocator_get_type())
#define GST_D3D12_STAGING_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_D3D12_STAGING_ALLOCATOR, GstD3D12Allocator))
#define GST_D3D12_STAGING_ALLOCATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_D3D12_STAGING_ALLOCATOR, GstD3D12AllocatorClass))
#define GST_IS_D3D12_STAGING_ALLOCATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_D3D12_STAGING_ALLOCATOR))
#define GST_IS_D3D12_STAGING_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D12_STAGING_ALLOCATOR))
#define GST_D3D12_STAGING_ALLOCATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D12_STAGING_ALLOCATOR, GstD3D12AllocatorClass))
#define GST_D3D12_STAGING_ALLOCATOR_CAST(obj)       ((GstD3D12Allocator *)obj)

/**
 * GST_D3D12_STAGING_MEMORY_NAME:
 *
 * The name of the Direct3D12 staging memory
 *
 * Since: 1.28
 */
#define GST_D3D12_STAGING_MEMORY_NAME "D3D12StagingMemory"

/**
 * GstD3D12StagingMemory:
 *
 * Opaque GstD3D12StagingMemory struct
 *
 * Since: 1.28
 */
struct _GstD3D12StagingMemory
{
  GstMemory mem;

  GstD3D12Device *device;

  /*< private >*/
  GstD3D12StagingMemoryPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
gboolean          gst_is_d3d12_staging_memory        (GstMemory * mem);

GST_D3D12_API
gboolean          gst_d3d12_staging_memory_sync      (GstD3D12StagingMemory * mem);

GST_D3D12_API
gboolean          gst_d3d12_staging_memory_get_layout (GstD3D12StagingMemory * mem,
                                                       guint index,
                                                       D3D12_PLACED_SUBRESOURCE_FOOTPRINT * layout);

GST_D3D12_API
void              gst_d3d12_staging_memory_set_fence (GstD3D12StagingMemory * mem,
                                                      ID3D12Fence * fence,
                                                      guint64 fence_value,
                                                      gboolean wait);

GST_D3D12_API
gboolean          gst_d3d12_staging_memory_get_fence (GstD3D12StagingMemory * mem,
                                                      ID3D12Fence ** fence,
                                                      guint64 * fence_value);

/**
 * GstD3D12StagingAllocator:
 *
 * Opaque GstD3D12StagingAllocator struct
 *
 * Since: 1.28
 */
struct _GstD3D12StagingAllocator
{
  GstAllocator allocator;

  /*< private >*/
  GstD3D12StagingAllocatorPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12StagingAllocatorClass:
 *
 * Opaque GstD3D12AllocatorClass struct
 *
 * Since: 1.28
 */
struct _GstD3D12StagingAllocatorClass
{
  GstAllocatorClass allocator_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_D3D12_API
GType       gst_d3d12_staging_allocator_get_type  (void);

GST_D3D12_API
GstMemory * gst_d3d12_staging_allocator_alloc (GstD3D12StagingAllocator * allocator,
                                               GstD3D12Device * device,
                                               guint num_layouts,
                                               const D3D12_PLACED_SUBRESOURCE_FOOTPRINT * layouts,
                                               gsize total_bytes);

G_END_DECLS


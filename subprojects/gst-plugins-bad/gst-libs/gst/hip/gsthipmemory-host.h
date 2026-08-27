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

#define GST_HIP_HOST_MEMORY_CAST(obj)          ((GstHipHostMemory *)obj)

#define GST_TYPE_HIP_HOST_ALLOCATOR            (gst_hip_host_allocator_get_type())
#define GST_HIP_HOST_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HIP_HOST_ALLOCATOR, GstHipHostAllocator))
#define GST_HIP_HOST_ALLOCATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HIP_HOST_ALLOCATOR, GstHipHostAllocatorClass))
#define GST_IS_HIP_HOST_ALLOCATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HIP_HOST_ALLOCATOR))
#define GST_IS_HIP_HOST_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HIP_HOST_ALLOCATOR))
#define GST_HIP_HOST_ALLOCATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HIP_HOST_ALLOCATOR, GstHipHostAllocatorClass))

#define GST_TYPE_HIP_HOST_POOL_ALLOCATOR            (gst_hip_host_pool_allocator_get_type())
#define GST_HIP_HOST_POOL_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HIP_HOST_POOL_ALLOCATOR, GstHipHostPoolAllocator))
#define GST_HIP_HOST_POOL_ALLOCATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HIP_HOST_POOL_ALLOCATOR, GstHipHostPoolAllocatorClass))
#define GST_IS_HIP_HOST_POOL_ALLOCATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HIP_HOST_POOL_ALLOCATOR))
#define GST_IS_HIP_HOST_POOL_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HIP_HOST_POOL_ALLOCATOR))
#define GST_HIP_HOST_POOL_ALLOCATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HIP_HOST_POOL_ALLOCATOR, GstHipHostPoolAllocatorClass))
#define GST_HIP_HOST_POOL_ALLOCATOR_CAST(obj)       ((GstHipHostPoolAllocator *)obj)

/**
 * GST_HIP_HOST_MEMORY_NAME:
 *
 * The name of the HIP host-pinned memory
 *
 * Since: 1.30
 */
#define GST_HIP_HOST_MEMORY_NAME "HIPHostMemory"

/**
 * GstHipHostMemory:
 *
 * Opaque GstHipHostMemory struct
 *
 * Since: 1.30
 */
struct _GstHipHostMemory
{
  GstMemory mem;

  GstHipDevice *device;

  /*< private >*/
  GstHipHostMemoryPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

GST_HIP_API
gboolean  gst_is_hip_host_memory (GstMemory * mem);

GST_HIP_API
gpointer  gst_hip_host_memory_get_device_pointer (GstHipHostMemory * mem);

/**
 * GstHipHostAllocator:
 *
 * Opaque GstHipHostAllocator struct
 *
 * Since: 1.30
 */
struct _GstHipHostAllocator
{
  GstAllocator allocator;

  /*< private >*/
  GstHipHostAllocatorPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstHipHostAllocatorClass:
 *
 * Opaque GstHipHostAllocatorClass struct
 *
 * Since: 1.30
 */
struct _GstHipHostAllocatorClass
{
  GstAllocatorClass allocator_class;

  gboolean (*set_active)   (GstHipHostAllocator * allocator,
                            gboolean active);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GST_HIP_API
GType       gst_hip_host_allocator_get_type  (void);

GST_HIP_API
GstMemory * gst_hip_host_allocator_alloc (GstHipHostAllocator * allocator,
                                          GstHipDevice * device,
                                          gsize size,
                                          guint flags);

GST_HIP_API
gboolean    gst_hip_host_allocator_set_active (GstHipHostAllocator * allocator,
                                               gboolean active);

/**
 * GstHipHostPoolAllocator:
 *
 * Opaque GstHipHostPoolAllocator struct
 *
 * Since: 1.30
 */
struct _GstHipHostPoolAllocator
{
  GstHipHostAllocator parent;

  GstHipDevice *device;

  /*< private >*/
  GstHipHostPoolAllocatorPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstHipHostPoolAllocatorClass:
 *
 * Opaque GstHipHostPoolAllocatorClass struct
 *
 * Since: 1.30
 */
struct _GstHipHostPoolAllocatorClass
{
  GstHipHostAllocatorClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_HIP_API
GType                 gst_hip_host_pool_allocator_get_type (void);

GST_HIP_API
GstHipHostPoolAllocator * gst_hip_host_pool_allocator_new (GstHipDevice * device,
                                                           gsize size,
                                                           guint flags);

GST_HIP_API
GstFlowReturn         gst_hip_host_pool_allocator_acquire_memory (GstHipHostPoolAllocator * allocator,
                                                                   GstMemory ** memory);

G_END_DECLS


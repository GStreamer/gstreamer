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
#include "gsthip_fwd.h"

G_BEGIN_DECLS

#define GST_HIP_MEMORY_CAST(obj)          ((GstHipMemory *)obj)

#define GST_TYPE_HIP_ALLOCATOR            (gst_hip_allocator_get_type())
#define GST_HIP_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HIP_ALLOCATOR, GstHipAllocator))
#define GST_HIP_ALLOCATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HIP_ALLOCATOR, GstHipAllocatorClass))
#define GST_IS_HIP_ALLOCATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HIP_ALLOCATOR))
#define GST_IS_HIP_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HIP_ALLOCATOR))
#define GST_HIP_ALLOCATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HIP_ALLOCATOR, GstHipAllocatorClass))
#define GST_HIP_ALLOCATOR_CAST(obj)       ((GstHipAllocator *)obj)

#define GST_TYPE_HIP_POOL_ALLOCATOR            (gst_hip_pool_allocator_get_type())
#define GST_HIP_POOL_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_HIP_POOL_ALLOCATOR, GstHipPoolAllocator))
#define GST_HIP_POOL_ALLOCATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_HIP_POOL_ALLOCATOR, GstHipPoolAllocatorClass))
#define GST_IS_HIP_POOL_ALLOCATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_HIP_POOL_ALLOCATOR))
#define GST_IS_HIP_POOL_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_HIP_POOL_ALLOCATOR))
#define GST_HIP_POOL_ALLOCATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_HIP_POOL_ALLOCATOR, GstHipPoolAllocatorClass))
#define GST_HIP_POOL_ALLOCATOR_CAST(obj)       ((GstHipPoolAllocator *)obj)

#define GST_HIP_MEMORY_NAME "HIPMemory"
#define GST_CAPS_FEATURE_MEMORY_HIP_MEMORY "memory:HIPMemory"
#define GST_MAP_HIP ((GstMapFlags) (GST_MAP_FLAG_LAST << 1))
#define GST_MAP_READ_HIP ((GstMapFlags) (GST_MAP_READ | GST_MAP_HIP))
#define GST_MAP_WRITE_HIP ((GstMapFlags) (GST_MAP_WRITE | GST_MAP_HIP))

typedef enum
{
  GST_HIP_MEMORY_TRANSFER_NEED_DOWNLOAD = (GST_MEMORY_FLAG_LAST << 0),
  GST_HIP_MEMORY_TRANSFER_NEED_UPLOAD = (GST_MEMORY_FLAG_LAST << 1)
} GstHipMemoryTransfer;

struct _GstHipMemory
{
  GstMemory mem;

  /*< public >*/
  GstHipDevice *device;
  GstVideoInfo info;

  /*< private >*/
  GstHipMemoryPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

void      gst_hip_memory_init_once (void);

gboolean  gst_is_hip_memory   (GstMemory * mem);

gboolean gst_hip_memory_get_texture (GstHipMemory * mem,
                                     guint plane,
                                     guint filter_mode,
                                     guint address_mode,
                                     hipTextureObject_t * texture);

struct _GstHipAllocator
{
  GstAllocator allocator;

  /*< private >*/
  GstHipAllocatorPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstHipAllocatorClass
{
  GstAllocatorClass allocator_class;

  gboolean (*set_active)   (GstHipAllocator * allocator,
                            gboolean active);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

GType       gst_hip_allocator_get_type (void);

GstMemory * gst_hip_allocator_alloc (GstHipAllocator * allocator,
                                     GstHipDevice * device,
                                     const GstVideoInfo * info);

gboolean    gst_hip_allocator_set_active (GstHipAllocator * allocator,
                                          gboolean active);

struct _GstHipPoolAllocator
{
  GstHipAllocator parent;

  GstHipDevice *device;
  GstVideoInfo info;

  /*< private >*/
  GstHipPoolAllocatorPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstHipPoolAllocatorClass
{
  GstHipAllocatorClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType                 gst_hip_pool_allocator_get_type (void);

GstHipPoolAllocator * gst_hip_pool_allocator_new (GstHipDevice * device,
                                                   const GstVideoInfo * info,
                                                   GstStructure * config);

GstFlowReturn         gst_hip_pool_allocator_acquire_memory (GstHipPoolAllocator * allocator,
                                                               GstMemory ** memory);

G_END_DECLS


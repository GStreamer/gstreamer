/*
 * GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_MEMORY_H__
#define __GST_D3D11_MEMORY_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_ALLOCATION_PARAMS    (gst_d3d11_allocation_params_get_type())
#define GST_TYPE_D3D11_ALLOCATOR            (gst_d3d11_allocator_get_type())
#define GST_D3D11_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_D3D11_ALLOCATOR, GstD3D11Allocator))
#define GST_D3D11_ALLOCATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS((klass), GST_TYPE_D3D11_ALLOCATOR, GstD3D11AllocatorClass))
#define GST_IS_D3D11_ALLOCATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_D3D11_ALLOCATOR))
#define GST_IS_D3D11_ALLOCATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_D3D11_ALLOCATOR))
#define GST_D3D11_ALLOCATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_D3D11_ALLOCATOR, GstD3D11AllocatorClass))

#define GST_D3D11_MEMORY_NAME "D3D11Memory"

/**
 * GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY:
 *
 * Name of the caps feature for indicating the use of #GstD3D11Memory
 */
#define GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "memory:D3D11Memory"

/**
 * GST_MAP_D3D11:
 *
 * Flag indicating that we should map the D3D11 resource instead of to system memory.
 */
#define GST_MAP_D3D11 (GST_MAP_FLAG_LAST << 1)

struct _GstD3D11Memory
{
  GstMemory mem;

  GstMapFlags map_flags;
  gint cpu_map_count;

  ID3D11Texture2D *texture;
  ID3D11Texture2D *staging;

  D3D11_TEXTURE2D_DESC desc;
  D3D11_MAPPED_SUBRESOURCE map;
  gboolean need_upload;

  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride[GST_VIDEO_MAX_PLANES];
};

struct _GstD3D11AllocationParams
{
  GstAllocationParams parent;

  D3D11_TEXTURE2D_DESC desc;

  GstVideoInfo info;
  GstVideoAlignment align;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct _GstD3D11Allocator
{
  GstAllocator parent;

  /*< private >*/
  GstD3D11Device *device;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstD3D11AllocatorClass
{
  GstAllocatorClass allocator_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType               gst_d3d11_allocation_params_get_type (void);

GstD3D11AllocationParams * gst_d3d11_allocation_params_new (GstAllocationParams * alloc_params,
                                                            GstVideoInfo * info,
                                                            GstVideoAlignment * align);

GstD3D11AllocationParams * gst_d3d11_allocation_params_copy (GstD3D11AllocationParams * src);

void                       gst_d3d11_allocation_params_free (GstD3D11AllocationParams * parms);

GType               gst_d3d11_allocator_get_type  (void);

GstD3D11Allocator * gst_d3d11_allocator_new       (GstD3D11Device *device);

GstMemory *         gst_d3d11_allocator_alloc     (GstD3D11Allocator * allocator,
                                                   GstD3D11AllocationParams * params);

G_END_DECLS

#endif /* __GST_D3D11_MEMORY_H__ */

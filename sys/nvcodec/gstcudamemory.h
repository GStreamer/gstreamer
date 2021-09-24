/* GStreamer
 * Copyright (C) <2018-2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_CUDA_MEMORY_H__
#define __GST_CUDA_MEMORY_H__

#include <gst/gst.h>
#include <gst/gstallocator.h>
#include <gst/video/video.h>
#include "gstcudaloader.h"
#include "gstcudacontext.h"

G_BEGIN_DECLS

#define GST_TYPE_CUDA_ALLOCATOR             (gst_cuda_allocator_get_type())
#define GST_CUDA_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_ALLOCATOR,GstCudaAllocator))
#define GST_CUDA_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CUDA_ALLOCATOR,GstCudaAllocatorClass))
#define GST_CUDA_ALLOCATOR_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_CUDA_ALLOCATOR,GstCudaAllocatorClass))
#define GST_IS_CUDA_ALLOCATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_ALLOCATOR))
#define GST_IS_CUDA_ALLOCATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CUDA_ALLOCATOR))
#define GST_CUDA_ALLOCATOR_CAST(obj)        ((GstCudaAllocator *)(obj))
#define GST_CUDA_MEMORY_CAST(mem)           ((GstCudaMemory *) (mem))

typedef struct _GstCudaAllocationParams GstCudaAllocationParams;
typedef struct _GstCudaAllocator GstCudaAllocator;
typedef struct _GstCudaAllocatorClass GstCudaAllocatorClass;
typedef struct _GstCudaMemory GstCudaMemory;

/**
 * GST_MAP_CUDA:
 *
 * Flag indicating that we should map the CUDA device memory
 * instead of to system memory.
 *
 * Combining #GST_MAP_CUDA with #GST_MAP_WRITE has the same semantics as though
 * you are writing to CUDA device/host memory.
 * Conversely, combining #GST_MAP_CUDA with
 * #GST_MAP_READ has the same semantics as though you are reading from
 * CUDA device/host memory
 */
#define GST_MAP_CUDA (GST_MAP_FLAG_LAST << 1)

#define GST_CUDA_MEMORY_TYPE_NAME "gst.cuda.memory"

/**
 * GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY:
 *
 * Name of the caps feature for indicating the use of #GstCudaMemory
 */
#define GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY "memory:CUDAMemory"

struct _GstCudaAllocationParams
{
  GstAllocationParams parent;

  GstVideoInfo info;
};

struct _GstCudaAllocator
{
  GstAllocator parent;
  GstCudaContext *context;
};

struct _GstCudaAllocatorClass
{
  GstAllocatorClass parent_class;
};

GType          gst_cuda_allocator_get_type (void);

GstAllocator * gst_cuda_allocator_new (GstCudaContext * context);

GstMemory    * gst_cuda_allocator_alloc (GstAllocator * allocator,
                                         gsize size,
                                         GstCudaAllocationParams * params);

/**
 * GstCudaMemoryTransfer:
 * @GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD: the device memory needs downloading
 *                                          to the staging memory
 * @GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD:   the staging memory needs uploading
 *                                          to the device memory
 */
typedef enum
{
  GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD   = (GST_MEMORY_FLAG_LAST << 0),
  GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD     = (GST_MEMORY_FLAG_LAST << 1)
} GstCudaMemoryTransfer;

struct _GstCudaMemory
{
  GstMemory       mem;

  GstCudaContext *context;
  CUdeviceptr data;

  GstCudaAllocationParams alloc_params;

  /* offset and stride of CUDA device memory */
  gsize offset[GST_VIDEO_MAX_PLANES];
  gint stride;

  /* allocated CUDA Host memory */
  gpointer map_alloc_data;

  /* aligned CUDA Host memory */
  guint8 *align_data;

  /* pointing align_data if the memory is mapped */
  gpointer map_data;

  gint map_count;

  GMutex lock;
};

gboolean        gst_is_cuda_memory        (GstMemory * mem);

G_END_DECLS

#endif /* __GST_CUDA_MEMORY_H__ */

/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
#include <gst/cuda/cuda-prelude.h>
#include <gst/cuda/gstcudacontext.h>
#include <cuda.h>

G_BEGIN_DECLS

#define GST_TYPE_CUDA_MEMORY_POOL         (gst_cuda_memory_pool_get_type())
#define GST_IS_CUDA_MEMORY_POOL(obj)      (GST_IS_MINI_OBJECT_TYPE(obj, GST_TYPE_CUDA_MEMORY_POOL))
#define GST_CUDA_MEMORY_POOL(obj)         ((GstCudaMemoryPool *)obj)

typedef struct _GstCudaMemoryPool GstCudaMemoryPool;
typedef struct _GstCudaMemoryPoolPrivate GstCudaMemoryPoolPrivate;

/**
 * GstCudaMemoryPool:
 *
 * Since: 1.26
 */
struct _GstCudaMemoryPool
{
  GstMiniObject parent;

  GstCudaContext *context;

  /*< private >*/
  GstCudaMemoryPoolPrivate *priv;
};

GST_CUDA_API
GType               gst_cuda_memory_pool_get_type (void);

GST_CUDA_API
GstCudaMemoryPool * gst_cuda_memory_pool_new (GstCudaContext * context,
                                              const CUmemPoolProps * props) G_GNUC_WARN_UNUSED_RESULT;

GST_CUDA_API
CUmemoryPool        gst_cuda_memory_pool_get_handle (GstCudaMemoryPool * pool);

GST_CUDA_API
GstCudaMemoryPool * gst_cuda_memory_pool_ref (GstCudaMemoryPool * pool);

GST_CUDA_API
void                gst_cuda_memory_pool_unref (GstCudaMemoryPool * pool);

GST_CUDA_API
void                gst_clear_cuda_memory_pool (GstCudaMemoryPool ** pool);

G_END_DECLS

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

#ifndef __GST_CUDA_BUFFER_POOL_H__
#define __GST_CUDA_BUFFER_POOL_H__

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstcudamemory.h"

G_BEGIN_DECLS

#define GST_TYPE_CUDA_BUFFER_POOL             (gst_cuda_buffer_pool_get_type ())
#define GST_CUDA_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),GST_TYPE_CUDA_BUFFER_POOL,GstCudaBufferPool))
#define GST_CUDA_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_CUDA_BUFFER_POOL,GstCudaBufferPoolClass))
#define GST_CUDA_BUFFER_POOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj),  GST_TYPE_CUDA_BUFFER_POOL,GstCudaBufferPoolClass))
#define GST_IS_CUDA_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),GST_TYPE_CUDA_BUFFER_POOL))
#define GST_IS_CUDA_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_CUDA_BUFFER_POOL))
#define GST_CUDA_BUFFER_POOL_CAST(obj)        ((GstCudaBufferPool*)(obj))

typedef struct _GstCudaBufferPool GstCudaBufferPool;
typedef struct _GstCudaBufferPoolClass GstCudaBufferPoolClass;
typedef struct _GstCudaBufferPoolPrivate GstCudaBufferPoolPrivate;

/*
 * GstCudaBufferPool:
 */
struct _GstCudaBufferPool
{
  GstBufferPool parent;

  GstCudaBufferPoolPrivate *priv;
};

/*
 * GstCudaBufferPoolClass:
 */
struct _GstCudaBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_cuda_buffer_pool_get_type (void);

GstBufferPool * gst_cuda_buffer_pool_new (GstCudaContext * context);

G_END_DECLS

#endif /* __GST_CUDA_BUFFER_POOL_H__ */

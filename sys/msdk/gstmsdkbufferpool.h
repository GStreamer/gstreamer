/* GStreamer Intel MSDK plugin
 * Copyright (c) 2018, Intel Corporation
 * Copyright (c) 2018, Igalia S.L.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGDECE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef GST_MSDK_BUFFER_POOL_H
#define GST_MSDK_BUFFER_POOL_H

#include "msdk.h"
#include "gstmsdkcontext.h"

G_BEGIN_DECLS

#define GST_TYPE_MSDK_BUFFER_POOL \
  (gst_msdk_buffer_pool_get_type ())
#define GST_MSDK_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MSDK_BUFFER_POOL, \
      GstMsdkBufferPool))
#define GST_MSDK_BUFFER_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MSDK_BUFFER_POOL, \
      GstMsdkBufferPoolClass))
#define GST_IS_MSDK_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MSDK_BUFFER_POOL))
#define GST_IS_MSDK_BUFFER_POOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MSDK_BUFFER_POOL))
#define GST_MSDK_BUFFER_POOL_CAST(obj) ((GstMsdkBufferPool*)(obj))

typedef struct _GstMsdkBufferPool GstMsdkBufferPool;
typedef struct _GstMsdkBufferPoolClass GstMsdkBufferPoolClass;
typedef struct _GstMsdkBufferPoolPrivate GstMsdkBufferPoolPrivate;

/*
 * GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY:
 *
 * An option that presents if the bufferpool will use
 * MsdkSystemAllocator or MsdkVideoAllocator.
 */
#define GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY "GstBufferPoolOptionMsdkUseVideoMemory"

/**
 * GstMsdkBufferPool:
 *
 * A MSDK buffer pool object.
 */
struct _GstMsdkBufferPool
{
  GstVideoBufferPool parent_instance;
  GstMsdkBufferPoolPrivate *priv;
};

/**
 * GstMsdkBufferPoolClass:
 *
 * A MSDK buffer pool class.
 */
struct _GstMsdkBufferPoolClass
{
  GstVideoBufferPoolClass parent_class;
};

GType gst_msdk_buffer_pool_get_type (void);

GstBufferPool * gst_msdk_buffer_pool_new (GstMsdkContext * context,
    mfxFrameAllocResponse * alloc_resp);

G_END_DECLS

#endif /* GST_MSDK_BUFFER_POOL_H */

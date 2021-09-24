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

#ifndef GST_MSDK_SYSTEM_MEMORY_H
#define GST_MSDK_SYSTEM_MEMORY_H

#include "msdk.h"

G_BEGIN_DECLS

typedef struct _GstMsdkSystemMemory GstMsdkSystemMemory;
typedef struct _GstMsdkSystemAllocator GstMsdkSystemAllocator;
typedef struct _GstMsdkSystemAllocatorClass GstMsdkSystemAllocatorClass;

/* ---------------------------------------------------------------------*/
/* GstMsdkSystemMemory                                                        */
/* ---------------------------------------------------------------------*/

#define GST_MSDK_SYSTEM_MEMORY_CAST(mem) \
  ((GstMsdkSystemMemory *) (mem))

#define GST_IS_MSDK_SYSTEM_MEMORY(mem) \
  ((mem) && (mem)->allocator && GST_IS_MSDK_SYSTEM_ALLOCATOR((mem)->allocator))

#define GST_MSDK_SYSTEM_MEMORY_NAME             "GstMsdkSystemMemory"

/**
 * GstMsdkSystemMemory:
 *
 * A MSDK memory object holder, including mfxFrameSurface,
 * video info of the surface.
 */
struct _GstMsdkSystemMemory
{
  GstMemory parent_instance;

  mfxFrameSurface1 *surface;

  guint8           *cache;
  mfxU8            *cached_data[4];
  guint            destination_pitches[4];
};

GstMemory *
gst_msdk_system_memory_new (GstAllocator * base_allocator);

/* ---------------------------------------------------------------------*/
/* GstMsdkSystemAllocator                                                     */
/* ---------------------------------------------------------------------*/

#define GST_MSDK_SYSTEM_ALLOCATOR_CAST(allocator) \
  ((GstMsdkSystemAllocator *) (allocator))

#define GST_TYPE_MSDK_SYSTEM_ALLOCATOR \
  (gst_msdk_system_allocator_get_type ())
#define GST_MSDK_SYSTEM_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MSDK_SYSTEM_ALLOCATOR, \
      GstMsdkSystemAllocator))
#define GST_IS_MSDK_SYSTEM_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MSDK_SYSTEM_ALLOCATOR))

/**
 * GstMsdkSystemAllocator:
 *
 * A MSDK memory allocator object.
 */
struct _GstMsdkSystemAllocator
{
  GstAllocator parent_instance;

  GstVideoInfo image_info;
};

/**
 * GstMsdkSystemAllocatorClass:
 *
 * A MSDK memory allocator class.
 */
struct _GstMsdkSystemAllocatorClass
{
  GstAllocatorClass parent_class;
};

GType gst_msdk_system_allocator_get_type (void);

GstAllocator * gst_msdk_system_allocator_new (GstVideoInfo *image_info);

G_END_DECLS

#endif /* GST_MSDK_SYSTEM_MEMORY_H */

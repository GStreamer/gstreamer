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

#ifndef GST_MSDK_VIDEO_MEMORY_H
#define GST_MSDK_VIDEO_MEMORY_H

#include "msdk.h"
#include "gstmsdkcontext.h"
#include "gstmsdkallocator.h"

G_BEGIN_DECLS

typedef struct _GstMsdkVideoMemory GstMsdkVideoMemory;
typedef struct _GstMsdkVideoAllocator GstMsdkVideoAllocator;
typedef struct _GstMsdkVideoAllocatorClass GstMsdkVideoAllocatorClass;

/* ---------------------------------------------------------------------*/
/* GstMsdkVideoMemory                                                        */
/* ---------------------------------------------------------------------*/

#define GST_MSDK_VIDEO_MEMORY_CAST(mem) \
  ((GstMsdkVideoMemory *) (mem))

#define GST_IS_MSDK_VIDEO_MEMORY(mem) \
  ((mem) && (mem)->allocator && GST_IS_MSDK_VIDEO_ALLOCATOR((mem)->allocator))

#define GST_MSDK_VIDEO_MEMORY_NAME             "GstMsdkVideoMemory"

/*
 * GstMsdkVideoMemory:
 *
 * A MSDK memory object holder, including mfxFrameSurface,
 * video info of the surface.
 */
struct _GstMsdkVideoMemory
{
  GstMemory parent_instance;

  mfxFrameSurface1 *surface;
  guint mapped;
};

GstMemory *
gst_msdk_video_memory_new (GstAllocator * allocator);

gboolean
gst_msdk_video_memory_get_surface_available (GstMsdkVideoMemory * mem);

void
gst_msdk_video_memory_release_surface (GstMsdkVideoMemory * mem);

gboolean
gst_video_meta_map_msdk_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info, gpointer * data, gint * stride, GstMapFlags flags);

gboolean
gst_video_meta_unmap_msdk_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info);


/* ---------------------------------------------------------------------*/
/* GstMsdkVideoAllocator                                                */
/* ---------------------------------------------------------------------*/

#define GST_MSDK_VIDEO_ALLOCATOR_CAST(allocator) \
  ((GstMsdkVideoAllocator *) (allocator))

#define GST_TYPE_MSDK_VIDEO_ALLOCATOR \
  (gst_msdk_video_allocator_get_type ())
#define GST_MSDK_VIDEO_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MSDK_VIDEO_ALLOCATOR, \
      GstMsdkVideoAllocator))
#define GST_IS_MSDK_VIDEO_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MSDK_VIDEO_ALLOCATOR))

/*
 * GstMsdkVideoAllocator:
 *
 * A MSDK video memory allocator object.
 */
struct _GstMsdkVideoAllocator
{
  GstAllocator parent_instance;

  GstMsdkContext *context;
  GstVideoInfo image_info;
  mfxFrameAllocResponse *alloc_response;
};

/*
 * GstMsdkVideoAllocatorClass:
 *
 * A MSDK video memory allocator class.
 */
struct _GstMsdkVideoAllocatorClass
{
  GstAllocatorClass parent_class;
};

GType gst_msdk_video_allocator_get_type (void);

GstAllocator * gst_msdk_video_allocator_new (GstMsdkContext * context,
    GstVideoInfo *image_info, mfxFrameAllocResponse * alloc_resp);

G_END_DECLS

#endif /* GST_MSDK_VIDEO_MEMORY_H */

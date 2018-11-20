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

#ifndef _WIN32
#include <unistd.h>
#include <va/va.h>
#endif
#include <stdlib.h>
#include "gstmsdkvideomemory.h"
#include "gstmsdkallocator.h"

#define GST_MSDK_BUFFER_SURFACE gst_msdk_buffer_surface_quark_get ()
static GQuark
gst_msdk_buffer_surface_quark_get (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstMsdkBufferSurface");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

static mfxFrameSurface1 *
gst_msdk_video_allocator_get_surface (GstAllocator * allocator)
{
  mfxFrameInfo frame_info = { {0,}, 0, };
  mfxFrameSurface1 *surface;
  GstMsdkContext *context = NULL;
  mfxFrameAllocResponse *resp = NULL;
  GstVideoInfo *vinfo = NULL;

  if (GST_IS_MSDK_VIDEO_ALLOCATOR (allocator)) {
    context = GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator)->context;
    resp = GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator)->alloc_response;
    vinfo = &GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator)->image_info;
  } else if (GST_IS_MSDK_DMABUF_ALLOCATOR (allocator)) {
    context = GST_MSDK_DMABUF_ALLOCATOR_CAST (allocator)->context;
    resp = GST_MSDK_DMABUF_ALLOCATOR_CAST (allocator)->alloc_response;
    vinfo = &GST_MSDK_DMABUF_ALLOCATOR_CAST (allocator)->image_info;
  } else {
    return NULL;
  }

  surface = gst_msdk_context_get_surface_available (context, resp);
  if (!surface) {
    GST_ERROR ("failed to get surface available");
    return NULL;
  }

  gst_msdk_set_mfx_frame_info_from_video_info (&frame_info, vinfo);
  surface->Info = frame_info;

  return surface;
}

gboolean
gst_msdk_video_memory_get_surface_available (GstMemory * mem)
{
  GstAllocator *allocator;
  mfxFrameSurface1 *surface;

  g_return_val_if_fail (mem, FALSE);

  allocator = mem->allocator;
  surface = gst_msdk_video_allocator_get_surface (allocator);

  if (GST_IS_MSDK_VIDEO_ALLOCATOR (allocator)) {
    GST_MSDK_VIDEO_MEMORY_CAST (mem)->surface = surface;
  } else if (GST_IS_MSDK_DMABUF_ALLOCATOR (allocator)) {
    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
        GST_MSDK_BUFFER_SURFACE, surface, NULL);
  }

  return surface ? TRUE : FALSE;
}

/*
 * Every time releasing a gst buffer, we need to check the status of surface's lock,
 * so that we could manage locked surfaces seperatedly in the context.
 * Otherwise, we put the surface to the available list.
 */
void
gst_msdk_video_memory_release_surface (GstMemory * mem)
{
  mfxFrameSurface1 *surface = NULL;
  GstMsdkContext *context = NULL;
  mfxFrameAllocResponse *alloc_response = NULL;

  g_return_if_fail (mem);

  if (GST_IS_MSDK_VIDEO_ALLOCATOR (mem->allocator)) {
    surface = GST_MSDK_VIDEO_MEMORY_CAST (mem)->surface;
    context = GST_MSDK_VIDEO_ALLOCATOR_CAST (mem->allocator)->context;
    alloc_response =
        GST_MSDK_VIDEO_ALLOCATOR_CAST (mem->allocator)->alloc_response;
  } else if (GST_IS_MSDK_DMABUF_ALLOCATOR (mem->allocator)) {
    surface =
        gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        GST_MSDK_BUFFER_SURFACE);
    context = GST_MSDK_DMABUF_ALLOCATOR_CAST (mem->allocator)->context;
    alloc_response =
        GST_MSDK_DMABUF_ALLOCATOR_CAST (mem->allocator)->alloc_response;
  } else {
    return;
  }

  if (surface->Data.Locked > 0)
    gst_msdk_context_put_surface_locked (context, alloc_response, surface);
  else
    gst_msdk_context_put_surface_available (context, alloc_response, surface);

  if (GST_IS_MSDK_VIDEO_ALLOCATOR (mem->allocator))
    GST_MSDK_VIDEO_MEMORY_CAST (mem)->surface = NULL;
  else if (GST_IS_MSDK_DMABUF_ALLOCATOR (mem->allocator))
    gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
        GST_MSDK_BUFFER_SURFACE, NULL, NULL);

  return;
}

GstMemory *
gst_msdk_video_memory_new (GstAllocator * base_allocator)
{
  GstMsdkVideoAllocator *allocator;
  GstVideoInfo *vip;
  GstMsdkVideoMemory *mem;

  g_return_val_if_fail (base_allocator, NULL);
  g_return_val_if_fail (GST_IS_MSDK_VIDEO_ALLOCATOR (base_allocator), NULL);

  allocator = GST_MSDK_VIDEO_ALLOCATOR_CAST (base_allocator);

  mem = g_slice_new0 (GstMsdkVideoMemory);
  if (!mem)
    return NULL;

  mem->surface = gst_msdk_video_allocator_get_surface (base_allocator);
  if (!mem->surface)
    return NULL;

  vip = &allocator->image_info;
  gst_memory_init (&mem->parent_instance, GST_MEMORY_FLAG_NO_SHARE,
      base_allocator, NULL, GST_VIDEO_INFO_SIZE (vip), 0, 0,
      GST_VIDEO_INFO_SIZE (vip));

  return GST_MEMORY_CAST (mem);
}

gboolean
gst_video_meta_map_msdk_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info, gpointer * data, gint * stride, GstMapFlags flags)
{
  gboolean ret = FALSE;
  GstAllocator *allocator;
  GstMsdkVideoAllocator *msdk_video_allocator;
  GstMsdkVideoMemory *mem =
      GST_MSDK_VIDEO_MEMORY_CAST (gst_buffer_peek_memory (meta->buffer, 0));
  GstMsdkMemoryID *mem_id;
  guint offset = 0;
  gint pitch = 0;
  guint plane_id = plane;

  g_return_val_if_fail (mem, FALSE);

  allocator = GST_MEMORY_CAST (mem)->allocator;
  msdk_video_allocator = GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator);

  if (!GST_IS_MSDK_VIDEO_ALLOCATOR (allocator)) {
    GST_WARNING ("The allocator is not MSDK video allocator");
    return FALSE;
  }

  if (!mem->surface) {
    GST_WARNING ("The surface is not allocated");
    return FALSE;
  }

  if ((flags & GST_MAP_WRITE) && mem->surface && mem->surface->Data.Locked) {
    GST_WARNING ("The surface in memory %p is not still avaliable", mem);
    return FALSE;
  }

  if (!mem->mapped) {
    gst_msdk_frame_lock (msdk_video_allocator->context,
        mem->surface->Data.MemId, &mem->surface->Data);
  }

  mem->mapped++;
  mem_id = mem->surface->Data.MemId;

  /* msdk doesn't support I420 format and we used YV12 internally
   * So we need to swap U/V planes for mapping */
  if (meta->format == GST_VIDEO_FORMAT_I420)
    plane_id = plane ? (plane == 1 ? 2 : 1) : plane;

#ifndef _WIN32
  offset = mem_id->image.offsets[plane_id];
  pitch = mem_id->image.pitches[plane_id];
#else
  /* TODO: This is just to avoid compile errors on Windows.
   * Implement handling Windows-specific video-memory.
   */
  offset = mem_id->offset;
  pitch = mem_id->pitch;
#endif

  *data = mem->surface->Data.Y + offset;
  *stride = pitch;

  info->flags = flags;
  ret = (*data != NULL);

  return ret;
}

gboolean
gst_video_meta_unmap_msdk_memory (GstVideoMeta * meta, guint plane,
    GstMapInfo * info)
{
  GstAllocator *allocator;
  GstMsdkVideoAllocator *msdk_video_allocator;
  GstMsdkVideoMemory *mem =
      GST_MSDK_VIDEO_MEMORY_CAST (gst_buffer_peek_memory (meta->buffer, 0));

  g_return_val_if_fail (mem, FALSE);

  allocator = GST_MEMORY_CAST (mem)->allocator;
  msdk_video_allocator = GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator);

  if (mem->mapped == 1)
    gst_msdk_frame_unlock (msdk_video_allocator->context,
        mem->surface->Data.MemId, &mem->surface->Data);

  mem->mapped--;

  return TRUE;
}


static gpointer
gst_msdk_video_memory_map_full (GstMemory * base_mem, GstMapInfo * info,
    gsize maxsize)
{
  GstMsdkVideoMemory *const mem = GST_MSDK_VIDEO_MEMORY_CAST (base_mem);
  GstAllocator *allocator = base_mem->allocator;
  GstMsdkVideoAllocator *msdk_video_allocator =
      GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator);

  g_return_val_if_fail (mem, NULL);

  if (!mem->surface) {
    GST_WARNING ("The surface is not allocated");
    return NULL;
  }

  if ((info->flags & GST_MAP_WRITE) && mem->surface
      && mem->surface->Data.Locked) {
    GST_WARNING ("The surface in memory %p is not still avaliable", mem);
    return NULL;
  }

  gst_msdk_frame_lock (msdk_video_allocator->context, mem->surface->Data.MemId,
      &mem->surface->Data);
  return mem->surface->Data.Y;
}

static void
gst_msdk_video_memory_unmap (GstMemory * base_mem)
{
  GstMsdkVideoMemory *const mem = GST_MSDK_VIDEO_MEMORY_CAST (base_mem);
  GstAllocator *allocator = base_mem->allocator;
  GstMsdkVideoAllocator *msdk_video_allocator =
      GST_MSDK_VIDEO_ALLOCATOR_CAST (allocator);

  gst_msdk_frame_unlock (msdk_video_allocator->context,
      mem->surface->Data.MemId, &mem->surface->Data);
}

/* GstMsdkVideoAllocator */
G_DEFINE_TYPE (GstMsdkVideoAllocator, gst_msdk_video_allocator,
    GST_TYPE_ALLOCATOR);

static GstMemory *
gst_msdk_video_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  return gst_msdk_video_memory_new (allocator);
}

static void
gst_msdk_video_allocator_finalize (GObject * object)
{
  GstMsdkVideoAllocator *allocator = GST_MSDK_VIDEO_ALLOCATOR_CAST (object);

  gst_object_unref (allocator->context);
  G_OBJECT_CLASS (gst_msdk_video_allocator_parent_class)->finalize (object);
}

static void
gst_msdk_video_allocator_class_init (GstMsdkVideoAllocatorClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *const allocator_class = GST_ALLOCATOR_CLASS (klass);

  object_class->finalize = gst_msdk_video_allocator_finalize;

  allocator_class->alloc = gst_msdk_video_allocator_alloc;
}

static void
gst_msdk_video_allocator_init (GstMsdkVideoAllocator * allocator)
{
  GstAllocator *const base_allocator = GST_ALLOCATOR_CAST (allocator);

  base_allocator->mem_type = GST_MSDK_VIDEO_MEMORY_NAME;
  base_allocator->mem_map_full = gst_msdk_video_memory_map_full;
  base_allocator->mem_unmap = gst_msdk_video_memory_unmap;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_msdk_video_allocator_new (GstMsdkContext * context,
    GstVideoInfo * image_info, mfxFrameAllocResponse * alloc_resp)
{
  GstMsdkVideoAllocator *allocator;

  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (image_info != NULL, NULL);

  allocator = g_object_new (GST_TYPE_MSDK_VIDEO_ALLOCATOR, NULL);
  if (!allocator)
    return NULL;

  allocator->context = gst_object_ref (context);
  allocator->image_info = *image_info;
  allocator->alloc_response = alloc_resp;

  return GST_ALLOCATOR_CAST (allocator);
}

/* GstMsdkDmaBufMemory */
GstMemory *
gst_msdk_dmabuf_memory_new (GstAllocator * base_allocator)
{
#ifndef _WIN32
  mfxFrameSurface1 *surface;

  g_return_val_if_fail (base_allocator, NULL);
  g_return_val_if_fail (GST_IS_MSDK_DMABUF_ALLOCATOR (base_allocator), NULL);

  surface = gst_msdk_video_allocator_get_surface (base_allocator);
  if (!surface)
    return NULL;

  return gst_msdk_dmabuf_memory_new_with_surface (base_allocator, surface);
#else
  return NULL;
#endif
}

GstMemory *
gst_msdk_dmabuf_memory_new_with_surface (GstAllocator * allocator,
    mfxFrameSurface1 * surface)
{
#ifndef _WIN32
  GstMemory *mem;
  GstMsdkMemoryID *mem_id;
  gint fd;
  gsize size;

  g_return_val_if_fail (allocator, NULL);
  g_return_val_if_fail (GST_IS_MSDK_DMABUF_ALLOCATOR (allocator), NULL);

  mem_id = surface->Data.MemId;
  fd = mem_id->info.handle;
  size = mem_id->info.mem_size;

  if (fd < 0 || (fd = dup (fd)) < 0) {
    GST_ERROR ("Failed to get dmabuf handle");
    return NULL;
  }

  mem = gst_dmabuf_allocator_alloc (allocator, fd, size);
  if (!mem) {
    GST_ERROR ("failed ! dmabuf fd: %d", fd);
    close (fd);
    return NULL;
  }

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (mem),
      GST_MSDK_BUFFER_SURFACE, surface, NULL);

  return mem;
#else
  return NULL;
#endif
}

/* GstMsdkDmaBufAllocator */
G_DEFINE_TYPE (GstMsdkDmaBufAllocator, gst_msdk_dmabuf_allocator,
    GST_TYPE_DMABUF_ALLOCATOR);

static void
gst_msdk_dmabuf_allocator_finalize (GObject * object)
{
  GstMsdkDmaBufAllocator *allocator = GST_MSDK_DMABUF_ALLOCATOR_CAST (object);

  gst_object_unref (allocator->context);
  G_OBJECT_CLASS (gst_msdk_dmabuf_allocator_parent_class)->finalize (object);
}

static void
gst_msdk_dmabuf_allocator_class_init (GstMsdkDmaBufAllocatorClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_msdk_dmabuf_allocator_finalize;
}

static void
gst_msdk_dmabuf_allocator_init (GstMsdkDmaBufAllocator * allocator)
{
  GstAllocator *const base_allocator = GST_ALLOCATOR_CAST (allocator);
  base_allocator->mem_type = GST_MSDK_DMABUF_MEMORY_NAME;
}

GstAllocator *
gst_msdk_dmabuf_allocator_new (GstMsdkContext * context,
    GstVideoInfo * image_info, mfxFrameAllocResponse * alloc_resp)
{
  GstMsdkDmaBufAllocator *allocator;

  g_return_val_if_fail (context != NULL, NULL);
  g_return_val_if_fail (image_info != NULL, NULL);

  allocator = g_object_new (GST_TYPE_MSDK_DMABUF_ALLOCATOR, NULL);
  if (!allocator)
    return NULL;

  allocator->context = gst_object_ref (context);
  allocator->image_info = *image_info;
  allocator->alloc_response = alloc_resp;

  return GST_ALLOCATOR_CAST (allocator);
}

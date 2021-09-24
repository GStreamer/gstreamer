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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcudamemory.h"
#include "gstcudautils.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (cudaallocator_debug);
#define GST_CAT_DEFAULT cudaallocator_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_MEMORY);

#define gst_cuda_allocator_parent_class parent_class
G_DEFINE_TYPE (GstCudaAllocator, gst_cuda_allocator, GST_TYPE_ALLOCATOR);

static void gst_cuda_allocator_dispose (GObject * object);
static void gst_cuda_allocator_free (GstAllocator * allocator,
    GstMemory * memory);

static gpointer cuda_mem_map (GstCudaMemory * mem, gsize maxsize,
    GstMapFlags flags);
static void cuda_mem_unmap_full (GstCudaMemory * mem, GstMapInfo * info);
static GstMemory *cuda_mem_copy (GstMemory * mem, gssize offset, gssize size);

static GstMemory *
gst_cuda_allocator_dummy_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (NULL);
}

static void
gst_cuda_allocator_class_init (GstCudaAllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  gobject_class->dispose = gst_cuda_allocator_dispose;

  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_cuda_allocator_dummy_alloc);
  allocator_class->free = GST_DEBUG_FUNCPTR (gst_cuda_allocator_free);

  GST_DEBUG_CATEGORY_INIT (cudaallocator_debug, "cudaallocator", 0,
      "CUDA Allocator");
  GST_DEBUG_CATEGORY_GET (GST_CAT_MEMORY, "GST_MEMORY");
}

static void
gst_cuda_allocator_init (GstCudaAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (allocator, "init");

  alloc->mem_type = GST_CUDA_MEMORY_TYPE_NAME;

  alloc->mem_map = (GstMemoryMapFunction) cuda_mem_map;
  alloc->mem_unmap_full = (GstMemoryUnmapFullFunction) cuda_mem_unmap_full;
  alloc->mem_copy = (GstMemoryCopyFunction) cuda_mem_copy;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_cuda_allocator_dispose (GObject * object)
{
  GstCudaAllocator *self = GST_CUDA_ALLOCATOR_CAST (object);

  GST_DEBUG_OBJECT (self, "dispose");

  gst_clear_object (&self->context);
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstMemory *
gst_cuda_allocator_alloc (GstAllocator * allocator, gsize size,
    GstCudaAllocationParams * params)
{
  GstCudaAllocator *self = GST_CUDA_ALLOCATOR_CAST (allocator);
  gsize maxsize = size + params->parent.prefix + params->parent.padding;
  gsize align = params->parent.align;
  gsize offset = params->parent.prefix;
  GstMemoryFlags flags = params->parent.flags;
  CUdeviceptr data;
  gboolean ret = FALSE;
  GstCudaMemory *mem;
  GstVideoInfo *info = &params->info;
  gint i;
  guint width, height;
  gsize stride, plane_offset;

  if (!gst_cuda_context_push (self->context))
    return NULL;

  /* ensure configured alignment */
  align |= gst_memory_alignment;
  /* allocate more to compensate for alignment */
  maxsize += align;

  GST_CAT_DEBUG_OBJECT (GST_CAT_MEMORY, self, "allocate new cuda memory");

  width = GST_VIDEO_INFO_COMP_WIDTH (info, 0) *
      GST_VIDEO_INFO_COMP_PSTRIDE (info, 0);
  height = 0;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++)
    height += GST_VIDEO_INFO_COMP_HEIGHT (info, i);

  ret = gst_cuda_result (CuMemAllocPitch (&data, &stride, width, height, 16));
  gst_cuda_context_pop (NULL);

  if (G_UNLIKELY (!ret)) {
    GST_CAT_ERROR_OBJECT (GST_CAT_MEMORY, self, "CUDA allocation failure");
    return NULL;
  }

  mem = g_new0 (GstCudaMemory, 1);
  g_mutex_init (&mem->lock);
  mem->data = data;
  mem->alloc_params = *params;
  mem->stride = stride;

  plane_offset = 0;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    mem->offset[i] = plane_offset;
    plane_offset += stride * GST_VIDEO_INFO_COMP_HEIGHT (info, i);
  }

  mem->context = gst_object_ref (self->context);

  gst_memory_init (GST_MEMORY_CAST (mem),
      flags, GST_ALLOCATOR_CAST (self), NULL, maxsize, align, offset, size);

  return GST_MEMORY_CAST (mem);
}

static void
gst_cuda_allocator_free (GstAllocator * allocator, GstMemory * memory)
{
  GstCudaAllocator *self = GST_CUDA_ALLOCATOR_CAST (allocator);
  GstCudaMemory *mem = GST_CUDA_MEMORY_CAST (memory);

  GST_CAT_DEBUG_OBJECT (GST_CAT_MEMORY, allocator, "free cuda memory");

  g_mutex_clear (&mem->lock);

  gst_cuda_context_push (self->context);
  if (mem->data)
    gst_cuda_result (CuMemFree (mem->data));

  if (mem->map_alloc_data)
    gst_cuda_result (CuMemFreeHost (mem->map_alloc_data));

  gst_cuda_context_pop (NULL);
  gst_object_unref (mem->context);

  g_free (mem);
}

/* called with lock */
static gboolean
gst_cuda_memory_upload_transfer (GstCudaMemory * mem)
{
  gint i;
  GstVideoInfo *info = &mem->alloc_params.info;
  gboolean ret = TRUE;

  if (!mem->map_data) {
    GST_CAT_ERROR (GST_CAT_MEMORY, "no staging memory to upload");
    return FALSE;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    CUDA_MEMCPY2D param = { 0, };

    param.srcMemoryType = CU_MEMORYTYPE_HOST;
    param.srcHost =
        (guint8 *) mem->map_data + GST_VIDEO_INFO_PLANE_OFFSET (info, i);
    param.srcPitch = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

    param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    param.dstDevice = mem->data + mem->offset[i];
    param.dstPitch = mem->stride;
    param.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (info, i) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, i);
    param.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&param, NULL))) {
      GST_CAT_ERROR (GST_CAT_MEMORY, "Failed to copy %dth plane", i);
      ret = FALSE;
      break;
    }
  }
  gst_cuda_result (CuStreamSynchronize (NULL));

  return ret;
}

/* called with lock */
static gboolean
gst_cuda_memory_download_transfer (GstCudaMemory * mem)
{
  gint i;
  GstVideoInfo *info = &mem->alloc_params.info;

  if (!mem->map_data) {
    GST_CAT_ERROR (GST_CAT_MEMORY, "no staging memory to upload");
    return FALSE;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    CUDA_MEMCPY2D param = { 0, };

    param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    param.srcDevice = mem->data + mem->offset[i];
    param.srcPitch = mem->stride;

    param.dstMemoryType = CU_MEMORYTYPE_HOST;
    param.dstHost =
        (guint8 *) mem->map_data + GST_VIDEO_INFO_PLANE_OFFSET (info, i);
    param.dstPitch = GST_VIDEO_INFO_PLANE_STRIDE (info, i);
    param.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (info, i) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, i);
    param.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&param, NULL))) {
      GST_CAT_ERROR (GST_CAT_MEMORY, "Failed to copy %dth plane", i);
      CuMemFreeHost (mem->map_alloc_data);
      mem->map_alloc_data = mem->map_data = mem->align_data = NULL;
      break;
    }
  }
  gst_cuda_result (CuStreamSynchronize (NULL));

  return ! !mem->map_data;
}

static gpointer
gst_cuda_memory_device_memory_map (GstCudaMemory * mem)
{
  GstMemory *memory = GST_MEMORY_CAST (mem);
  gpointer data;
  gsize aoffset;
  gsize align = memory->align;

  if (mem->map_data) {
    return mem->map_data;
  }

  GST_CAT_DEBUG (GST_CAT_MEMORY, "alloc host memory for map");

  if (!mem->map_alloc_data) {
    gsize maxsize;
    guint8 *align_data;

    maxsize = memory->maxsize + align;
    if (!gst_cuda_context_push (mem->context)) {
      GST_CAT_ERROR (GST_CAT_MEMORY, "cannot push cuda context");

      return NULL;
    }

    if (!gst_cuda_result (CuMemAllocHost (&data, maxsize))) {
      GST_CAT_ERROR (GST_CAT_MEMORY, "cannot alloc host memory");
      gst_cuda_context_pop (NULL);

      return NULL;
    }

    if (!gst_cuda_context_pop (NULL)) {
      GST_CAT_WARNING (GST_CAT_MEMORY, "cannot pop cuda context");
    }

    mem->map_alloc_data = data;
    align_data = data;

    /* do align */
    if ((aoffset = ((guintptr) align_data & align))) {
      aoffset = (align + 1) - aoffset;
      align_data += aoffset;
    }
    mem->align_data = align_data;

    /* first memory, always need download to staging */
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);
  }

  mem->map_data = mem->align_data;

  if (GST_MEMORY_FLAG_IS_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD)) {
    if (!gst_cuda_context_push (mem->context)) {
      GST_CAT_ERROR (GST_CAT_MEMORY, "cannot push cuda context");

      return NULL;
    }

    gst_cuda_memory_download_transfer (mem);

    if (!gst_cuda_context_pop (NULL)) {
      GST_CAT_WARNING (GST_CAT_MEMORY, "cannot pop cuda context");
    }
  }

  return mem->map_data;
}

static gpointer
cuda_mem_map (GstCudaMemory * mem, gsize maxsize, GstMapFlags flags)
{
  gpointer ret = NULL;

  g_mutex_lock (&mem->lock);
  mem->map_count++;

  if ((flags & GST_MAP_CUDA) == GST_MAP_CUDA) {
    /* upload from staging to device memory if necessary */
    if (GST_MEMORY_FLAG_IS_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD)) {
      if (!gst_cuda_context_push (mem->context)) {
        GST_CAT_ERROR (GST_CAT_MEMORY, "cannot push cuda context");
        g_mutex_unlock (&mem->lock);

        return NULL;
      }

      if (!gst_cuda_memory_upload_transfer (mem)) {
        g_mutex_unlock (&mem->lock);
        return NULL;
      }

      gst_cuda_context_pop (NULL);
    }

    GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

    g_mutex_unlock (&mem->lock);
    return (gpointer) mem->data;
  }

  ret = gst_cuda_memory_device_memory_map (mem);
  if (ret == NULL) {
    mem->map_count--;
    g_mutex_unlock (&mem->lock);
    return NULL;
  }

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

  GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

  g_mutex_unlock (&mem->lock);

  return ret;
}

static void
cuda_mem_unmap_full (GstCudaMemory * mem, GstMapInfo * info)
{
  g_mutex_lock (&mem->lock);
  mem->map_count--;
  GST_CAT_TRACE (GST_CAT_MEMORY,
      "unmap CUDA memory %p, map count %d, have map_data %s",
      mem, mem->map_count, mem->map_data ? "true" : "false");

  if ((info->flags & GST_MAP_CUDA) == GST_MAP_CUDA) {
    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

    g_mutex_unlock (&mem->lock);
    return;
  }

  if ((info->flags & GST_MAP_WRITE))
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

  if (mem->map_count > 0 || !mem->map_data) {
    g_mutex_unlock (&mem->lock);
    return;
  }

  mem->map_data = NULL;
  g_mutex_unlock (&mem->lock);

  return;
}

static GstMemory *
cuda_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstMemory *copy;
  GstCudaMemory *src_mem = GST_CUDA_MEMORY_CAST (mem);
  GstCudaMemory *dst_mem;
  GstCudaContext *ctx = GST_CUDA_ALLOCATOR_CAST (mem->allocator)->context;
  gint i;
  GstVideoInfo *info;

  /* offset and size are ignored */
  copy = gst_cuda_allocator_alloc (mem->allocator, mem->size,
      &src_mem->alloc_params);

  dst_mem = GST_CUDA_MEMORY_CAST (copy);

  info = &src_mem->alloc_params.info;

  if (!gst_cuda_context_push (ctx)) {
    GST_CAT_ERROR (GST_CAT_MEMORY, "cannot push cuda context");
    gst_cuda_allocator_free (mem->allocator, copy);

    return NULL;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    CUDA_MEMCPY2D param = { 0, };

    param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    param.srcDevice = src_mem->data + src_mem->offset[i];
    param.srcPitch = src_mem->stride;

    param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    param.dstDevice = dst_mem->data + dst_mem->offset[i];
    param.dstPitch = dst_mem->stride;
    param.WidthInBytes = GST_VIDEO_INFO_COMP_WIDTH (info, i) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, i);
    param.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

    if (!gst_cuda_result (CuMemcpy2DAsync (&param, NULL))) {
      GST_CAT_ERROR_OBJECT (GST_CAT_MEMORY,
          mem->allocator, "Failed to copy %dth plane", i);
      gst_cuda_context_pop (NULL);
      gst_cuda_allocator_free (mem->allocator, copy);

      return NULL;
    }
  }

  gst_cuda_result (CuStreamSynchronize (NULL));

  if (!gst_cuda_context_pop (NULL)) {
    GST_CAT_WARNING (GST_CAT_MEMORY, "cannot pop cuda context");
  }

  return copy;
}

GstAllocator *
gst_cuda_allocator_new (GstCudaContext * context)
{
  GstCudaAllocator *allocator;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), NULL);

  allocator = g_object_new (GST_TYPE_CUDA_ALLOCATOR, NULL);
  allocator->context = gst_object_ref (context);

  return GST_ALLOCATOR_CAST (allocator);
}

gboolean
gst_is_cuda_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      GST_IS_CUDA_ALLOCATOR (mem->allocator);
}

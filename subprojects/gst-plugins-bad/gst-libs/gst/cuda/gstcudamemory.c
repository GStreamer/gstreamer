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

GST_DEBUG_CATEGORY_STATIC (cuda_allocator_debug);
#define GST_CAT_DEFAULT cuda_allocator_debug

static GstAllocator *_gst_cuda_allocator = NULL;

struct _GstCudaMemoryPrivate
{
  CUdeviceptr data;
  void *staging;

  /* params used for cuMemAllocPitch */
  gsize pitch;
  guint width_in_bytes;
  guint height;

  GMutex lock;
};

#define gst_cuda_allocator_parent_class parent_class
G_DEFINE_TYPE (GstCudaAllocator, gst_cuda_allocator, GST_TYPE_ALLOCATOR);

static void gst_cuda_allocator_free (GstAllocator * allocator,
    GstMemory * memory);

static gpointer cuda_mem_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags);
static void cuda_mem_unmap_full (GstMemory * mem, GstMapInfo * info);
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
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_cuda_allocator_dummy_alloc);
  allocator_class->free = GST_DEBUG_FUNCPTR (gst_cuda_allocator_free);

  GST_DEBUG_CATEGORY_INIT (cuda_allocator_debug, "cudaallocator", 0,
      "CUDA Allocator");
}

static void
gst_cuda_allocator_init (GstCudaAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  GST_DEBUG_OBJECT (allocator, "init");

  alloc->mem_type = GST_CUDA_MEMORY_TYPE_NAME;

  alloc->mem_map = cuda_mem_map;
  alloc->mem_unmap_full = cuda_mem_unmap_full;
  alloc->mem_copy = cuda_mem_copy;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_cuda_allocator_alloc_internal (GstCudaAllocator * self,
    GstCudaContext * context, const GstVideoInfo * info,
    guint width_in_bytes, guint alloc_height)
{
  GstCudaMemoryPrivate *priv;
  GstCudaMemory *mem;
  CUdeviceptr data;
  gboolean ret = FALSE;
  gsize pitch;
  guint height = GST_VIDEO_INFO_HEIGHT (info);
  GstVideoInfo *alloc_info;

  if (!gst_cuda_context_push (context))
    return NULL;

  ret = gst_cuda_result (CuMemAllocPitch (&data, &pitch, width_in_bytes,
          alloc_height, 16));
  gst_cuda_context_pop (NULL);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to allocate CUDA memory");
    return NULL;
  }

  mem = g_new0 (GstCudaMemory, 1);
  mem->priv = priv = g_new0 (GstCudaMemoryPrivate, 1);

  priv->data = data;
  priv->pitch = pitch;
  priv->width_in_bytes = width_in_bytes;
  priv->height = alloc_height;
  g_mutex_init (&priv->lock);

  mem->context = gst_object_ref (context);
  mem->info = *info;
  mem->info.size = pitch * alloc_height;

  alloc_info = &mem->info;
  gst_memory_init (GST_MEMORY_CAST (mem), 0, GST_ALLOCATOR_CAST (self),
      NULL, alloc_info->size, 0, 0, alloc_info->size);

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
      /* we are wasting space yes, but required so that this memory
       * can be used in kernel function */
      alloc_info->stride[0] = pitch;
      alloc_info->stride[1] = pitch;
      alloc_info->stride[2] = pitch;
      alloc_info->offset[0] = 0;
      alloc_info->offset[1] = alloc_info->stride[0] * height;
      alloc_info->offset[2] = alloc_info->offset[1] +
          alloc_info->stride[1] * height / 2;
      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
      alloc_info->stride[0] = pitch;
      alloc_info->stride[1] = pitch;
      alloc_info->stride[2] = pitch;
      alloc_info->offset[0] = 0;
      alloc_info->offset[1] = alloc_info->stride[0] * height;
      alloc_info->offset[2] = alloc_info->offset[1] +
          alloc_info->stride[1] * height;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
      alloc_info->stride[0] = pitch;
      alloc_info->stride[1] = pitch;
      alloc_info->offset[0] = 0;
      alloc_info->offset[1] = alloc_info->stride[0] * height;
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_16LE:
      alloc_info->stride[0] = pitch;
      alloc_info->stride[1] = pitch;
      alloc_info->stride[2] = pitch;
      alloc_info->offset[0] = 0;
      alloc_info->offset[1] = alloc_info->stride[0] * height;
      alloc_info->offset[2] = alloc_info->offset[1] * 2;
      break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGR10A2_LE:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
      alloc_info->stride[0] = pitch;
      alloc_info->offset[0] = 0;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
      g_assert_not_reached ();
      gst_memory_unref (GST_MEMORY_CAST (mem));
      return NULL;
  }

  return GST_MEMORY_CAST (mem);
}

static void
gst_cuda_allocator_free (GstAllocator * allocator, GstMemory * memory)
{
  GstCudaMemory *mem = GST_CUDA_MEMORY_CAST (memory);
  GstCudaMemoryPrivate *priv = mem->priv;

  gst_cuda_context_push (mem->context);
  if (priv->data)
    gst_cuda_result (CuMemFree (priv->data));

  if (priv->staging)
    gst_cuda_result (CuMemFreeHost (priv->staging));
  gst_cuda_context_pop (NULL);

  gst_object_unref (mem->context);

  g_mutex_clear (&priv->lock);
  g_free (mem->priv);
  g_free (mem);
}

static gboolean
gst_cuda_memory_upload (GstCudaAllocator * self, GstCudaMemory * mem)
{
  GstCudaMemoryPrivate *priv = mem->priv;
  gboolean ret = TRUE;
  CUDA_MEMCPY2D param = { 0, };

  if (!priv->staging ||
      !GST_MEMORY_FLAG_IS_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD)) {
    return TRUE;
  }

  if (!gst_cuda_context_push (mem->context)) {
    GST_ERROR_OBJECT (self, "Failed to push cuda context");
    return FALSE;
  }

  param.srcMemoryType = CU_MEMORYTYPE_HOST;
  param.srcHost = priv->staging;
  param.srcPitch = priv->pitch;

  param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
  param.dstDevice = (CUdeviceptr) priv->data;
  param.dstPitch = priv->pitch;
  param.WidthInBytes = priv->width_in_bytes;
  param.Height = priv->height;

  ret = gst_cuda_result (CuMemcpy2D (&param));
  gst_cuda_context_pop (NULL);

  if (!ret)
    GST_ERROR_OBJECT (self, "Failed to upload memory");

  return ret;
}

static gboolean
gst_cuda_memory_download (GstCudaAllocator * self, GstCudaMemory * mem)
{
  GstCudaMemoryPrivate *priv = mem->priv;
  gboolean ret = TRUE;
  CUDA_MEMCPY2D param = { 0, };

  if (!GST_MEMORY_FLAG_IS_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD))
    return TRUE;

  if (!gst_cuda_context_push (mem->context)) {
    GST_ERROR_OBJECT (self, "Failed to push cuda context");
    return FALSE;
  }

  if (!priv->staging) {
    ret = gst_cuda_result (CuMemAllocHost (&priv->staging,
            GST_MEMORY_CAST (mem)->size));
    if (!ret) {
      GST_ERROR_OBJECT (self, "Failed to allocate staging memory");
      gst_cuda_context_pop (NULL);
      return FALSE;
    }
  }

  param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  param.srcDevice = (CUdeviceptr) priv->data;
  param.srcPitch = priv->pitch;

  param.dstMemoryType = CU_MEMORYTYPE_HOST;
  param.dstHost = priv->staging;
  param.dstPitch = priv->pitch;
  param.WidthInBytes = priv->width_in_bytes;
  param.Height = priv->height;

  ret = gst_cuda_result (CuMemcpy2D (&param));
  gst_cuda_context_pop (NULL);

  if (!ret)
    GST_ERROR_OBJECT (self, "Failed to upload memory");

  return ret;
}

static gpointer
cuda_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstCudaAllocator *self = GST_CUDA_ALLOCATOR (mem->allocator);
  GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);
  GstCudaMemoryPrivate *priv = cmem->priv;
  gpointer ret = NULL;

  g_mutex_lock (&priv->lock);
  if ((flags & GST_MAP_CUDA) == GST_MAP_CUDA) {
    if (!gst_cuda_memory_upload (self, cmem))
      goto out;

    GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

    ret = (gpointer) priv->data;
    goto out;
  }

  /* First CPU access, must be downloaded */
  if (!priv->staging)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

  if (!gst_cuda_memory_download (self, cmem))
    goto out;

  ret = priv->staging;

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

  GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

out:
  g_mutex_unlock (&priv->lock);

  return ret;
}

static void
cuda_mem_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);
  GstCudaMemoryPrivate *priv = cmem->priv;

  g_mutex_lock (&priv->lock);
  if ((info->flags & GST_MAP_CUDA) == GST_MAP_CUDA) {
    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

    goto out;
  }

  if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

out:
  g_mutex_unlock (&priv->lock);

  return;
}

static GstMemory *
cuda_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  GstCudaAllocator *self = GST_CUDA_ALLOCATOR (mem->allocator);
  GstCudaMemory *src_mem = GST_CUDA_MEMORY_CAST (mem);
  GstCudaContext *context = src_mem->context;
  GstMapInfo src_info, dst_info;
  CUDA_MEMCPY2D param = { 0, };
  GstMemory *copy;
  gboolean ret;

  /* offset and size are ignored */
  copy = gst_cuda_allocator_alloc_internal (self, context,
      &src_mem->info, src_mem->priv->width_in_bytes, src_mem->priv->height);

  if (!copy) {
    GST_ERROR_OBJECT (self, "Failed to allocate memory for copying");
    return NULL;
  }

  if (!gst_memory_map (mem, &src_info, GST_MAP_READ | GST_MAP_CUDA)) {
    GST_ERROR_OBJECT (self, "Failed to map src memory");
    gst_memory_unref (copy);
    return NULL;
  }

  if (!gst_memory_map (copy, &dst_info, GST_MAP_WRITE | GST_MAP_CUDA)) {
    GST_ERROR_OBJECT (self, "Failed to map dst memory");
    gst_memory_unmap (mem, &src_info);
    gst_memory_unref (copy);
    return NULL;
  }

  if (!gst_cuda_context_push (context)) {
    GST_ERROR_OBJECT (self, "Failed to push cuda context");
    gst_memory_unmap (mem, &src_info);
    gst_memory_unmap (copy, &dst_info);

    return NULL;
  }

  param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  param.srcDevice = (CUdeviceptr) src_info.data;
  param.srcPitch = src_mem->priv->pitch;

  param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
  param.dstDevice = (CUdeviceptr) dst_info.data;
  param.dstPitch = src_mem->priv->pitch;
  param.WidthInBytes = src_mem->priv->width_in_bytes;
  param.Height = src_mem->priv->height;

  ret = gst_cuda_result (CuMemcpy2D (&param));
  gst_cuda_context_pop (NULL);

  gst_memory_unmap (mem, &src_info);
  gst_memory_unmap (copy, &dst_info);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to copy memory");
    gst_memory_unref (copy);
    return NULL;
  }

  return copy;
}

/**
 * gst_cuda_memory_init_once:
 *
 * Ensures that the #GstCudaAllocator is initialized and ready to be used.
 *
 * Since: 1.22
 */
void
gst_cuda_memory_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    _gst_cuda_allocator =
        (GstAllocator *) g_object_new (GST_TYPE_CUDA_ALLOCATOR, NULL);
    gst_object_ref_sink (_gst_cuda_allocator);

    gst_allocator_register (GST_CUDA_MEMORY_TYPE_NAME, _gst_cuda_allocator);
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_is_cuda_memory:
 * @mem: A #GstMemory
 *
 * Check if @mem is a cuda memory
 *
 * Since: 1.22
 */
gboolean
gst_is_cuda_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      GST_IS_CUDA_ALLOCATOR (mem->allocator);
}

/**
 * gst_cuda_allocator_alloc:
 *
 * Since: 1.22
 */
GstMemory *
gst_cuda_allocator_alloc (GstCudaAllocator * allocator,
    GstCudaContext * context, const GstVideoInfo * info)
{
  guint alloc_height;

  g_return_val_if_fail (GST_IS_CUDA_ALLOCATOR (allocator), NULL);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), NULL);
  g_return_val_if_fail (info != NULL, NULL);

  alloc_height = GST_VIDEO_INFO_HEIGHT (info);

  /* make sure valid height for subsampled formats */
  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
    case GST_VIDEO_FORMAT_I420_10LE:
      alloc_height = GST_ROUND_UP_2 (alloc_height);
      break;
    default:
      break;
  }

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
      alloc_height *= 2;
      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_16LE:
      alloc_height *= 3;
      break;
    default:
      break;
  }

  return gst_cuda_allocator_alloc_internal (allocator, context,
      info, info->stride[0], alloc_height);
}

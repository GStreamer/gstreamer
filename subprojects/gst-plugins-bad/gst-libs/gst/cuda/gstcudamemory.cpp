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
#include "gstcuda-private.h"

#include <string.h>
#include <map>
#include <memory>

GST_DEBUG_CATEGORY_STATIC (cuda_allocator_debug);
#define GST_CAT_DEFAULT cuda_allocator_debug

static GstAllocator *_gst_cuda_allocator = nullptr;

/* *INDENT-OFF* */
struct GstCudaMemoryTokenData
{
  GstCudaMemoryTokenData (gpointer data, GDestroyNotify notify_func)
  :user_data (data), notify (notify_func)
  {
  }

   ~GstCudaMemoryTokenData ()
  {
    if (notify)
      notify (user_data);
  }

  gpointer user_data;
  GDestroyNotify notify;
};

struct _GstCudaMemoryPrivate
{
  _GstCudaMemoryPrivate ()
  {
    memset (&texture, 0, sizeof (texture));
  }

  CUdeviceptr data = 0;
  void *staging = nullptr;

  /* params used for cuMemAllocPitch */
  gsize pitch = 0;
  guint width_in_bytes = 0;
  guint height = 0;

  std::mutex lock;

  GstCudaStream *stream = nullptr;

  gint texture_align = 0;

  /* Per plane, and point/linear sampling textures respectively  */
  CUtexObject texture[GST_VIDEO_MAX_PLANES][2];

  gboolean saw_io = FALSE;

  gboolean from_fixed_pool = FALSE;

  std::map < gint64, std::unique_ptr < GstCudaMemoryTokenData >> token_map;

  gpointer user_data = nullptr;
  GDestroyNotify notify = nullptr;
};
/* *INDENT-ON* */

struct _GstCudaAllocatorPrivate
{
  GstMemoryCopyFunction fallback_copy;
};

#define gst_cuda_allocator_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstCudaAllocator, gst_cuda_allocator,
    GST_TYPE_ALLOCATOR);

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
  g_return_val_if_reached (nullptr);
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
  GstCudaAllocatorPrivate *priv;

  priv = allocator->priv = (GstCudaAllocatorPrivate *)
      gst_cuda_allocator_get_instance_private (allocator);

  alloc->mem_type = GST_CUDA_MEMORY_TYPE_NAME;

  alloc->mem_map = cuda_mem_map;
  alloc->mem_unmap_full = cuda_mem_unmap_full;

  /* Store pointer to default mem_copy method for fallback copy */
  priv->fallback_copy = alloc->mem_copy;
  alloc->mem_copy = cuda_mem_copy;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_cuda_allocator_alloc_internal (GstCudaAllocator * self,
    GstCudaContext * context, GstCudaStream * stream, const GstVideoInfo * info,
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
    return nullptr;

  ret = gst_cuda_result (CuMemAllocPitch (&data, &pitch, width_in_bytes,
          alloc_height, 16));
  gst_cuda_context_pop (nullptr);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to allocate CUDA memory");
    return nullptr;
  }

  mem = g_new0 (GstCudaMemory, 1);
  mem->priv = priv = new GstCudaMemoryPrivate ();

  priv->data = data;
  priv->pitch = pitch;
  priv->width_in_bytes = width_in_bytes;
  priv->height = alloc_height;
  priv->texture_align = gst_cuda_context_get_texture_alignment (context);
  if (stream)
    priv->stream = gst_cuda_stream_ref (stream);

  mem->context = (GstCudaContext *) gst_object_ref (context);
  mem->info = *info;
  mem->info.size = pitch * alloc_height;

  alloc_info = &mem->info;
  gst_memory_init (GST_MEMORY_CAST (mem), (GstMemoryFlags) 0,
      GST_ALLOCATOR_CAST (self), nullptr, alloc_info->size, 0, 0,
      alloc_info->size);

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
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
      alloc_info->stride[0] = pitch;
      alloc_info->stride[1] = pitch;
      alloc_info->stride[2] = pitch;
      alloc_info->offset[0] = 0;
      alloc_info->offset[1] = alloc_info->stride[0] * height;
      alloc_info->offset[2] = alloc_info->offset[1] * 2;
      break;
    case GST_VIDEO_FORMAT_GBRA:
      alloc_info->stride[0] = pitch;
      alloc_info->stride[1] = pitch;
      alloc_info->stride[2] = pitch;
      alloc_info->stride[3] = pitch;
      alloc_info->offset[0] = 0;
      alloc_info->offset[1] = alloc_info->stride[0] * height;
      alloc_info->offset[2] = alloc_info->offset[1] * 2;
      alloc_info->offset[3] = alloc_info->offset[1] * 3;
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
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      alloc_info->stride[0] = pitch;
      alloc_info->offset[0] = 0;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unexpected format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
      g_assert_not_reached ();
      gst_memory_unref (GST_MEMORY_CAST (mem));
      return nullptr;
  }

  return GST_MEMORY_CAST (mem);
}

static void
gst_cuda_allocator_free (GstAllocator * allocator, GstMemory * memory)
{
  GstCudaMemory *mem = GST_CUDA_MEMORY_CAST (memory);
  GstCudaMemoryPrivate *priv = mem->priv;

  gst_cuda_context_push (mem->context);
  /* Finish any pending operations before freeing */
  if (priv->stream && priv->saw_io &&
      GST_MEMORY_FLAG_IS_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC)) {
    CuStreamSynchronize (gst_cuda_stream_get_handle (priv->stream));
  }

  priv->token_map.clear ();

  for (guint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    for (guint j = 0; j < 2; j++) {
      if (priv->texture[i][j]) {
        CuTexObjectDestroy (priv->texture[i][j]);
      }
    }
  }

  if (priv->notify) {
    priv->notify (priv->user_data);
  } else if (priv->data) {
    gst_cuda_result (CuMemFree (priv->data));
  }

  if (priv->staging)
    gst_cuda_result (CuMemFreeHost (priv->staging));
  gst_cuda_context_pop (nullptr);

  gst_clear_cuda_stream (&priv->stream);
  gst_object_unref (mem->context);

  delete mem->priv;

  g_free (mem);
}

static gboolean
gst_cuda_memory_upload (GstCudaAllocator * self, GstCudaMemory * mem)
{
  GstCudaMemoryPrivate *priv = mem->priv;
  gboolean ret = TRUE;
  CUDA_MEMCPY2D param = { 0, };
  CUstream stream = gst_cuda_stream_get_handle (priv->stream);

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

  ret = gst_cuda_result (CuMemcpy2DAsync (&param, stream));
  /* Sync only if we use default stream.
   * Otherwise (in case of non-default stream case) sync is caller's
   * responsibility */
  if (!priv->stream) {
    CuStreamSynchronize (stream);
    GST_MINI_OBJECT_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
  } else {
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
  }
  gst_cuda_context_pop (nullptr);

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
  CUstream stream = gst_cuda_stream_get_handle (priv->stream);

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
      gst_cuda_context_pop (nullptr);
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

  ret = gst_cuda_result (CuMemcpy2DAsync (&param, stream));
  /* For CPU access, sync immediately */
  CuStreamSynchronize (stream);
  gst_cuda_context_pop (nullptr);
  GST_MINI_OBJECT_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);

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
  gpointer ret = nullptr;

  std::lock_guard < std::mutex > lk (priv->lock);

  priv->saw_io = TRUE;

  if ((flags & GST_MAP_CUDA) == GST_MAP_CUDA) {
    if (!gst_cuda_memory_upload (self, cmem))
      return nullptr;

    GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);
      /* Assume that memory needs sync if we are using non-default stream */
      if (priv->stream)
        GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
    }

    return (gpointer) priv->data;
  }

  /* First CPU access, must be downloaded */
  if (!priv->staging)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

  if (!gst_cuda_memory_download (self, cmem))
    return nullptr;

  ret = priv->staging;

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

  GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return ret;
}

static void
cuda_mem_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);
  GstCudaMemoryPrivate *priv = cmem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if ((info->flags & GST_MAP_CUDA) == GST_MAP_CUDA) {
    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_DOWNLOAD);
    return;
  }

  if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_UPLOAD);

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
  GstMemory *copy = nullptr;
  gboolean ret;
  GstCudaStream *stream = src_mem->priv->stream;
  CUstream stream_handle = gst_cuda_stream_get_handle (stream);

  /* non-zero offset or different size is not supported */
  if (offset != 0 || (size != -1 && (gsize) size != mem->size)) {
    GST_DEBUG_OBJECT (self, "Different size/offset, try fallback copy");
    return self->priv->fallback_copy (mem, offset, size);
  }

  if (GST_IS_CUDA_POOL_ALLOCATOR (self)) {
    gst_cuda_pool_allocator_acquire_memory (GST_CUDA_POOL_ALLOCATOR (self),
        &copy);
  }

  if (!copy) {
    copy = gst_cuda_allocator_alloc_internal (self, context, stream,
        &src_mem->info, src_mem->priv->width_in_bytes, src_mem->priv->height);
  }

  if (!copy) {
    GST_ERROR_OBJECT (self, "Failed to allocate memory for copying");
    return nullptr;
  }

  if (!gst_memory_map (mem, &src_info,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (self, "Failed to map src memory");
    gst_memory_unref (copy);
    return nullptr;
  }

  if (!gst_memory_map (copy, &dst_info,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (self, "Failed to map dst memory");
    gst_memory_unmap (mem, &src_info);
    gst_memory_unref (copy);
    return nullptr;
  }

  if (!gst_cuda_context_push (context)) {
    GST_ERROR_OBJECT (self, "Failed to push cuda context");
    gst_memory_unmap (mem, &src_info);
    gst_memory_unmap (copy, &dst_info);

    return nullptr;
  }

  param.srcMemoryType = CU_MEMORYTYPE_DEVICE;
  param.srcDevice = (CUdeviceptr) src_info.data;
  param.srcPitch = src_mem->priv->pitch;

  param.dstMemoryType = CU_MEMORYTYPE_DEVICE;
  param.dstDevice = (CUdeviceptr) dst_info.data;
  param.dstPitch = src_mem->priv->pitch;
  param.WidthInBytes = src_mem->priv->width_in_bytes;
  param.Height = src_mem->priv->height;

  ret = gst_cuda_result (CuMemcpy2DAsync (&param, stream_handle));
  CuStreamSynchronize (stream_handle);
  gst_cuda_context_pop (nullptr);

  gst_memory_unmap (mem, &src_info);
  gst_memory_unmap (copy, &dst_info);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to copy memory");
    gst_memory_unref (copy);
    return nullptr;
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
  GST_CUDA_CALL_ONCE_BEGIN {
    _gst_cuda_allocator =
        (GstAllocator *) g_object_new (GST_TYPE_CUDA_ALLOCATOR, nullptr);
    gst_object_ref_sink (_gst_cuda_allocator);
    gst_object_ref (_gst_cuda_allocator);

    gst_allocator_register (GST_CUDA_MEMORY_TYPE_NAME, _gst_cuda_allocator);
  } GST_CUDA_CALL_ONCE_END;
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
  return mem != nullptr && mem->allocator != nullptr &&
      GST_IS_CUDA_ALLOCATOR (mem->allocator);
}

/**
 * gst_cuda_memory_get_stream:
 * @mem: A #GstCudaMemory
 *
 * Gets CUDA stream object associated with @mem
 *
 * Returns: (transfer none) (nullable): a #GstCudaStream or %NULL if default
 * CUDA stream is in use
 *
 * Since: 1.24
 */
GstCudaStream *
gst_cuda_memory_get_stream (GstCudaMemory * mem)
{
  g_return_val_if_fail (gst_is_cuda_memory ((GstMemory *) mem), nullptr);

  return mem->priv->stream;
}

/**
 * gst_cuda_memory_sync:
 * @mem: A #GstCudaMemory
 *
 * Performs synchronization if needed
 *
 * Since: 1.24
 */
void
gst_cuda_memory_sync (GstCudaMemory * mem)
{
  GstCudaMemoryPrivate *priv;

  g_return_if_fail (gst_is_cuda_memory ((GstMemory *) mem));

  priv = mem->priv;
  if (!priv->stream)
    return;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (GST_MEMORY_FLAG_IS_SET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC)) {
    GST_MEMORY_FLAG_UNSET (mem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
    if (gst_cuda_context_push (mem->context)) {
      CuStreamSynchronize (gst_cuda_stream_get_handle (priv->stream));
      gst_cuda_context_pop (nullptr);
    }
  }
}

typedef struct _TextureFormat
{
  GstVideoFormat format;
  CUarray_format array_format[GST_VIDEO_MAX_COMPONENTS];
  guint channels[GST_VIDEO_MAX_COMPONENTS];
} TextureFormat;

#define CU_AD_FORMAT_NONE ((CUarray_format) 0)
#define MAKE_FORMAT_YUV_PLANAR(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_ ##cf, \
      CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_NONE },  {1, 1, 1, 0} }
#define MAKE_FORMAT_YUV_SEMI_PLANAR(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_ ##cf, \
      CU_AD_FORMAT_NONE, CU_AD_FORMAT_NONE }, {1, 2, 0, 0} }
#define MAKE_FORMAT_RGB(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_NONE, \
      CU_AD_FORMAT_NONE, CU_AD_FORMAT_NONE }, {4, 0, 0, 0} }
#define MAKE_FORMAT_RGBP(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_ ##cf, \
      CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_NONE }, {1, 1, 1, 0} }
#define MAKE_FORMAT_RGBAP(f,cf) \
  { GST_VIDEO_FORMAT_ ##f,  { CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_ ##cf, \
      CU_AD_FORMAT_ ##cf, CU_AD_FORMAT_ ##cf }, {1, 1, 1, 1} }

static const TextureFormat format_map[] = {
  MAKE_FORMAT_YUV_PLANAR (I420, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (YV12, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV12, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (NV21, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P010_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_SEMI_PLANAR (P016_LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I420_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (Y444, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (Y444_16LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGB (RGBA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (BGRA, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (RGBx, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (BGRx, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (ARGB, UNSIGNED_INT8),
  MAKE_FORMAT_RGB (ARGB64, UNSIGNED_INT16),
  MAKE_FORMAT_RGB (ABGR, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (Y42B, UNSIGNED_INT8),
  MAKE_FORMAT_YUV_PLANAR (I422_10LE, UNSIGNED_INT16),
  MAKE_FORMAT_YUV_PLANAR (I422_12LE, UNSIGNED_INT16),
  MAKE_FORMAT_RGBP (RGBP, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (BGRP, UNSIGNED_INT8),
  MAKE_FORMAT_RGBP (GBR, UNSIGNED_INT8),
  MAKE_FORMAT_RGBAP (GBRA, UNSIGNED_INT8),
};

/**
 * gst_cuda_memory_get_texture:
 * @mem: A #GstCudaMemory
 * @plane: the plane index
 * @filter_mode: filter mode
 * @texture: (out) (transfer none): a pointer to CUtexObject object
 *
 * Creates CUtexObject with given parameters
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.24
 */
gboolean
gst_cuda_memory_get_texture (GstCudaMemory * mem, guint plane,
    CUfilter_mode filter_mode, CUtexObject * texture)
{
  GstAllocator *alloc;
  GstCudaMemoryPrivate *priv;
  CUtexObject tex = 0;
  CUdeviceptr src_ptr;
  CUDA_RESOURCE_DESC resource_desc;
  CUDA_TEXTURE_DESC texture_desc;
  const TextureFormat *format = nullptr;
  CUresult ret;

  g_return_val_if_fail (gst_is_cuda_memory ((GstMemory *) mem), FALSE);
  g_return_val_if_fail (plane < GST_VIDEO_INFO_N_PLANES (&mem->info), FALSE);
  g_return_val_if_fail (filter_mode == CU_TR_FILTER_MODE_POINT ||
      filter_mode == CU_TR_FILTER_MODE_LINEAR, FALSE);

  alloc = mem->mem.allocator;
  priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->texture[plane][filter_mode]) {
    *texture = priv->texture[plane][filter_mode];
    return TRUE;
  }

  src_ptr = (CUdeviceptr) (((guint8 *) priv->data) + mem->info.offset[plane]);
  if (priv->texture_align > 0 && (src_ptr % priv->texture_align) != 0) {
    GST_INFO_OBJECT (alloc, "Plane %d data is not aligned", plane);
    return FALSE;
  }

  for (guint i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == GST_VIDEO_INFO_FORMAT (&mem->info)) {
      format = &format_map[i];
      break;
    }
  }

  if (!format) {
    GST_WARNING_OBJECT (alloc, "Not supported format %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&mem->info)));
    return FALSE;
  }

  memset (&resource_desc, 0, sizeof (CUDA_RESOURCE_DESC));
  memset (&texture_desc, 0, sizeof (CUDA_TEXTURE_DESC));

  resource_desc.resType = CU_RESOURCE_TYPE_PITCH2D;
  resource_desc.res.pitch2D.format = format->array_format[plane];
  resource_desc.res.pitch2D.numChannels = format->channels[plane];
  resource_desc.res.pitch2D.width =
      GST_VIDEO_INFO_COMP_WIDTH (&mem->info, plane);
  resource_desc.res.pitch2D.height =
      GST_VIDEO_INFO_COMP_HEIGHT (&mem->info, plane);
  resource_desc.res.pitch2D.pitchInBytes =
      GST_VIDEO_INFO_PLANE_STRIDE (&mem->info, plane);
  resource_desc.res.pitch2D.devPtr = src_ptr;

  texture_desc.filterMode = filter_mode;
  /* Will read texture value as a normalized [0, 1] float value
   * with [0, 1) coordinates */
  /* CU_TRSF_NORMALIZED_COORDINATES */
  texture_desc.flags = 0x2;
  /* CU_TR_ADDRESS_MODE_CLAMP */
  texture_desc.addressMode[0] = (CUaddress_mode) 1;
  texture_desc.addressMode[1] = (CUaddress_mode) 1;
  texture_desc.addressMode[2] = (CUaddress_mode) 1;

  if (!gst_cuda_context_push (mem->context))
    return FALSE;

  ret = CuTexObjectCreate (&tex, &resource_desc, &texture_desc, nullptr);
  gst_cuda_context_pop (nullptr);

  if (!gst_cuda_result (ret)) {
    GST_ERROR_OBJECT (alloc, "Could not create texture");
    return FALSE;
  }

  /* Cache this texture to reuse later */
  priv->texture[plane][filter_mode] = tex;

  *texture = tex;

  return TRUE;
}

/**
 * gst_cuda_memory_get_user_data:
 * @mem: A #GstCudaMemory
 *
 * Gets user data pointer stored via gst_cuda_allocator_alloc_wrapped()
 *
 * Returns: (transfer none) (nullable): the user data pointer
 *
 * Since: 1.24
 */
gpointer
gst_cuda_memory_get_user_data (GstCudaMemory * mem)
{
  g_return_val_if_fail (gst_is_cuda_memory ((GstMemory *) mem), nullptr);

  return mem->priv->user_data;
}

/**
 * gst_cuda_memory_set_token_data:
 * @mem: a #GstCudaMemory
 * @token: an user token
 * @data: an user data
 * @notify: function to invoke with @data as argument, when @data needs to be
 *          freed
 *
 * Sets an opaque user data on a #GstCudaMemory
 *
 * Since: 1.24
 */
void
gst_cuda_memory_set_token_data (GstCudaMemory * mem, gint64 token,
    gpointer data, GDestroyNotify notify)
{
  GstCudaMemoryPrivate *priv;

  g_return_if_fail (gst_is_cuda_memory (GST_MEMORY_CAST (mem)));

  priv = mem->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  auto old_token = priv->token_map.find (token);
  if (old_token != priv->token_map.end ())
    priv->token_map.erase (old_token);

  if (data) {
    priv->token_map[token] =
        std::unique_ptr < GstCudaMemoryTokenData >
        (new GstCudaMemoryTokenData (data, notify));
  }
}

/**
 * gst_cuda_memory_get_token_data:
 * @mem: a #GstCudaMemory
 * @token: an user token
 *
 * Gets back user data pointer stored via gst_cuda_memory_set_token_data()
 *
 * Returns: (transfer none) (nullable): user data pointer or %NULL
 *
 * Since: 1.24
 */
gpointer
gst_cuda_memory_get_token_data (GstCudaMemory * mem, gint64 token)
{
  GstCudaMemoryPrivate *priv;
  gpointer ret = nullptr;

  g_return_val_if_fail (gst_is_cuda_memory (GST_MEMORY_CAST (mem)), nullptr);

  priv = mem->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  auto old_token = priv->token_map.find (token);
  if (old_token != priv->token_map.end ())
    ret = old_token->second->user_data;

  return ret;
}

/**
 * gst_cuda_allocator_alloc:
 * @allocator: (transfer none) (allow-none): a #GstCudaAllocator
 * @context: (transfer none): a #GstCudaContext
 * @stream: (transfer none) (allow-none): a #GstCudaStream
 * @info: a #GstVideoInfo
 *
 * Returns: (transfer full) (nullable): a newly allocated #GstCudaMemory
 *
 * Since: 1.22
 */
GstMemory *
gst_cuda_allocator_alloc (GstCudaAllocator * allocator,
    GstCudaContext * context, GstCudaStream * stream, const GstVideoInfo * info)
{
  guint alloc_height;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), nullptr);
  g_return_val_if_fail (!stream || GST_IS_CUDA_STREAM (stream), nullptr);
  g_return_val_if_fail (info != nullptr, nullptr);

  if (stream && stream->context != context) {
    GST_ERROR_OBJECT (context,
        "stream object is holding different CUDA context");
    return nullptr;
  }

  if (!allocator)
    allocator = (GstCudaAllocator *) _gst_cuda_allocator;

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
      alloc_height *= 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
      alloc_height += alloc_height / 2;
      break;
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
      alloc_height *= 3;
      break;
    case GST_VIDEO_FORMAT_GBRA:
      alloc_height *= 4;
      break;
    default:
      break;
  }

  return gst_cuda_allocator_alloc_internal (allocator, context, stream,
      info, info->stride[0], alloc_height);
}

/**
 * gst_cuda_allocator_set_active:
 * @allocator: a #GstCudaAllocator
 * @active: the new active state
 *
 * Controls the active state of @allocator. Default #GstCudaAllocator is
 * stateless and therefore active state is ignored, but subclass implementation
 * (e.g., #GstCudaPoolAllocator) will require explicit active state control
 * for its internal resource management.
 *
 * This method is conceptually identical to gst_buffer_pool_set_active method.
 *
 * Returns: %TRUE if active state of @allocator was successfully updated.
 *
 * Since: 1.24
 */
gboolean
gst_cuda_allocator_set_active (GstCudaAllocator * allocator, gboolean active)
{
  GstCudaAllocatorClass *klass;

  g_return_val_if_fail (GST_IS_CUDA_ALLOCATOR (allocator), FALSE);

  klass = GST_CUDA_ALLOCATOR_GET_CLASS (allocator);
  if (klass->set_active)
    return klass->set_active (allocator, active);

  return TRUE;
}

/**
 * gst_cuda_allocator_alloc_wrapped:
 * @allocator: (transfer none) (allow-none): a #GstCudaAllocator
 * @context: (transfer none): a #GstCudaContext
 * @stream: (transfer none) (allow-none): a #GstCudaStream
 * @info: a #GstVideoInfo
 * @dev_ptr: a CUdeviceptr CUDA device memory
 * @user_data: (allow-none): user data
 * @notify: (allow-none) (scope async) (closure user_data):
 *   Called with @user_data when the memory is freed
 *
 * Allocates a new memory that wraps the given CUDA device memory.
 *
 * @info must represent actual memory layout, in other words, offset, stride
 * and size fields of @info should be matched with memory layout of @dev_ptr
 *
 * By default, wrapped @dev_ptr will be freed at the time when #GstMemory
 * is freed if @notify is %NULL. Otherwise, if caller sets @notify,
 * freeing @dev_ptr is callers responsibility and default #GstCudaAllocator
 * will not free it.
 *
 * Returns: (transfer full): a new #GstMemory
 *
 * Since: 1.24
 */
GstMemory *
gst_cuda_allocator_alloc_wrapped (GstCudaAllocator * allocator,
    GstCudaContext * context, GstCudaStream * stream, const GstVideoInfo * info,
    CUdeviceptr dev_ptr, gpointer user_data, GDestroyNotify notify)
{
  GstCudaMemory *mem;
  GstCudaMemoryPrivate *priv;

  if (!allocator)
    allocator = (GstCudaAllocator *) _gst_cuda_allocator;

  g_return_val_if_fail (GST_IS_CUDA_ALLOCATOR (allocator), nullptr);
  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), nullptr);
  g_return_val_if_fail (!stream || GST_IS_CUDA_STREAM (stream), nullptr);
  g_return_val_if_fail (info, nullptr);
  g_return_val_if_fail (dev_ptr, nullptr);

  mem = g_new0 (GstCudaMemory, 1);
  mem->priv = priv = new GstCudaMemoryPrivate ();

  priv->data = dev_ptr;
  priv->pitch = info->stride[0];
  priv->width_in_bytes = GST_VIDEO_INFO_COMP_WIDTH (info, 0) *
      GST_VIDEO_INFO_COMP_PSTRIDE (info, 0);
  priv->height = info->size / priv->pitch;
  if (stream)
    priv->stream = gst_cuda_stream_ref (stream);
  priv->user_data = user_data;
  priv->notify = notify;

  mem->context = (GstCudaContext *) gst_object_ref (context);
  mem->info = *info;

  gst_memory_init (GST_MEMORY_CAST (mem), (GstMemoryFlags) 0,
      GST_ALLOCATOR_CAST (allocator), nullptr, info->size, 0, 0, info->size);

  return GST_MEMORY_CAST (mem);
}

void
gst_cuda_memory_set_from_fixed_pool (GstMemory * mem)
{
  GstCudaMemory *cmem;

  if (!gst_is_cuda_memory (mem))
    return;

  cmem = GST_CUDA_MEMORY_CAST (mem);
  cmem->priv->from_fixed_pool = TRUE;
}

gboolean
gst_cuda_memory_is_from_fixed_pool (GstMemory * mem)
{
  GstCudaMemory *cmem;

  if (!gst_is_cuda_memory (mem))
    return FALSE;

  cmem = GST_CUDA_MEMORY_CAST (mem);

  return cmem->priv->from_fixed_pool;
}

#define GST_CUDA_POOL_ALLOCATOR_IS_FLUSHING(alloc)  (g_atomic_int_get (&alloc->priv->flushing))

struct _GstCudaPoolAllocatorPrivate
{
  GstAtomicQueue *queue;
  GstPoll *poll;

  GRecMutex lock;
  gboolean started;
  gboolean active;

  guint outstanding;
  guint cur_mems;
  gboolean flushing;
};

static void gst_cuda_pool_allocator_finalize (GObject * object);

static gboolean
gst_cuda_pool_allocator_set_active (GstCudaAllocator * allocator,
    gboolean active);

static gboolean gst_cuda_pool_allocator_start (GstCudaPoolAllocator * self);
static gboolean gst_cuda_pool_allocator_stop (GstCudaPoolAllocator * self);
static gboolean gst_cuda_memory_release (GstMiniObject * mini_object);

#define gst_cuda_pool_allocator_parent_class pool_alloc_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstCudaPoolAllocator,
    gst_cuda_pool_allocator, GST_TYPE_CUDA_ALLOCATOR);

static void
gst_cuda_pool_allocator_class_init (GstCudaPoolAllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstCudaAllocatorClass *cuda_alloc_class = GST_CUDA_ALLOCATOR_CLASS (klass);

  gobject_class->finalize = gst_cuda_pool_allocator_finalize;

  cuda_alloc_class->set_active = gst_cuda_pool_allocator_set_active;
}

static void
gst_cuda_pool_allocator_init (GstCudaPoolAllocator * allocator)
{
  GstCudaPoolAllocatorPrivate *priv;

  priv = allocator->priv = (GstCudaPoolAllocatorPrivate *)
      gst_cuda_pool_allocator_get_instance_private (allocator);

  g_rec_mutex_init (&priv->lock);

  priv->poll = gst_poll_new_timer ();
  priv->queue = gst_atomic_queue_new (16);
  priv->flushing = 1;
  priv->active = FALSE;
  priv->started = FALSE;

  /* 1 control write for flushing - the flush token */
  gst_poll_write_control (priv->poll);
  /* 1 control write for marking that we are not waiting for poll - the wait token */
  gst_poll_write_control (priv->poll);
}

static void
gst_cuda_pool_allocator_finalize (GObject * object)
{
  GstCudaPoolAllocator *self = GST_CUDA_POOL_ALLOCATOR (object);
  GstCudaPoolAllocatorPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Finalize");

  gst_cuda_pool_allocator_stop (self);
  gst_atomic_queue_unref (priv->queue);
  gst_poll_free (priv->poll);
  g_rec_mutex_clear (&priv->lock);

  gst_clear_cuda_stream (&self->stream);
  gst_clear_object (&self->context);

  G_OBJECT_CLASS (pool_alloc_parent_class)->finalize (object);
}

static gboolean
gst_cuda_pool_allocator_start (GstCudaPoolAllocator * self)
{
  GstCudaPoolAllocatorPrivate *priv = self->priv;

  priv->started = TRUE;

  return TRUE;
}

static void
gst_cuda_pool_allocator_do_set_flushing (GstCudaPoolAllocator * self,
    gboolean flushing)
{
  GstCudaPoolAllocatorPrivate *priv = self->priv;

  if (GST_CUDA_POOL_ALLOCATOR_IS_FLUSHING (self) == flushing)
    return;

  if (flushing) {
    g_atomic_int_set (&priv->flushing, 1);
    /* Write the flush token to wake up any waiters */
    gst_poll_write_control (priv->poll);
  } else {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* This should not really happen unless flushing and unflushing
         * happens on different threads. Let's wait a bit to get back flush
         * token from the thread that was setting it to flushing */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }

    g_atomic_int_set (&priv->flushing, 0);
  }
}

static gboolean
gst_cuda_pool_allocator_set_active (GstCudaAllocator * allocator,
    gboolean active)
{
  GstCudaPoolAllocator *self = GST_CUDA_POOL_ALLOCATOR (allocator);
  GstCudaPoolAllocatorPrivate *priv = self->priv;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (self, "active %d", active);

  g_rec_mutex_lock (&priv->lock);
  /* just return if we are already in the right state */
  if (priv->active == active)
    goto done;

  if (active) {
    gst_cuda_pool_allocator_start (self);

    /* flush_stop may release memory objects, setting to active to avoid running
     * do_stop while activating the pool */
    priv->active = TRUE;

    gst_cuda_pool_allocator_do_set_flushing (self, FALSE);
  } else {
    gint outstanding;

    /* set to flushing first */
    gst_cuda_pool_allocator_do_set_flushing (self, TRUE);

    /* when all memory objects are in the pool, free them. Else they will be
     * freed when they are released */
    outstanding = g_atomic_int_get (&priv->outstanding);
    GST_LOG_OBJECT (self, "outstanding memories %d, (in queue %d)",
        outstanding, gst_atomic_queue_length (priv->queue));
    if (outstanding == 0) {
      if (!gst_cuda_pool_allocator_stop (self)) {
        GST_ERROR_OBJECT (self, "stop failed");
        ret = FALSE;
        goto done;
      }
    }

    priv->active = FALSE;
  }

done:
  g_rec_mutex_unlock (&priv->lock);

  return ret;
}

static void
gst_cuda_pool_allocator_free_memory (GstCudaPoolAllocator * self,
    GstMemory * mem)
{
  GstCudaPoolAllocatorPrivate *priv = self->priv;

  g_atomic_int_add (&priv->cur_mems, -1);
  GST_LOG_OBJECT (self, "freeing memory %p (%u left)", mem, priv->cur_mems);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  gst_memory_unref (mem);
}

static gboolean
gst_cuda_pool_allocator_clear_queue (GstCudaPoolAllocator * self)
{
  GstCudaPoolAllocatorPrivate *priv = self->priv;
  GstMemory *memory;

  GST_LOG_OBJECT (self, "Clearing queue");

  if (self->stream) {
    /* Wait for outstanding operations */
    gst_cuda_context_push (self->context);
    CuStreamSynchronize (gst_cuda_stream_get_handle (self->stream));
    gst_cuda_context_pop (nullptr);
  }

  while ((memory = (GstMemory *) gst_atomic_queue_pop (priv->queue))) {
    while (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* We put the memory into the queue but did not finish writing control
         * yet, let's wait a bit and retry */
        g_thread_yield ();
        continue;
      } else {
        /* Critical error but GstPoll already complained */
        break;
      }
    }

    /* Already synchronized above */
    GST_MEMORY_FLAG_UNSET (memory, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
    gst_cuda_pool_allocator_free_memory (self, memory);
  }

  GST_LOG_OBJECT (self, "Clear done");

  return priv->cur_mems == 0;
}

/* must be called with the lock */
static gboolean
gst_cuda_pool_allocator_stop (GstCudaPoolAllocator * self)
{
  GstCudaPoolAllocatorPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->started) {
    if (!gst_cuda_pool_allocator_clear_queue (self))
      return FALSE;

    priv->started = FALSE;
  }

  return TRUE;
}

static inline void
dec_outstanding (GstCudaPoolAllocator * self)
{
  if (g_atomic_int_dec_and_test (&self->priv->outstanding)) {
    /* all memory objects are returned to the pool, see if we need to free them */
    if (GST_CUDA_POOL_ALLOCATOR_IS_FLUSHING (self)) {
      /* take the lock so that set_active is not run concurrently */
      g_rec_mutex_lock (&self->priv->lock);
      /* now that we have the lock, check if we have been de-activated with
       * outstanding buffers */
      if (!self->priv->active)
        gst_cuda_pool_allocator_stop (self);
      g_rec_mutex_unlock (&self->priv->lock);
    }
  }
}

static void
gst_cuda_pool_allocator_release_memory (GstCudaPoolAllocator * self,
    GstMemory * mem)
{
  GST_LOG_OBJECT (self, "Released memory %p", mem);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  mem->allocator = (GstAllocator *) gst_object_ref (_gst_cuda_allocator);

  /* keep it around in our queue */
  gst_atomic_queue_push (self->priv->queue, mem);
  gst_poll_write_control (self->priv->poll);
  dec_outstanding (self);

  gst_object_unref (self);
}

static gboolean
gst_cuda_memory_release (GstMiniObject * object)
{
  GstMemory *mem = GST_MEMORY_CAST (object);
  GstCudaPoolAllocator *alloc;

  g_assert (mem->allocator);

  if (!GST_IS_CUDA_POOL_ALLOCATOR (mem->allocator)) {
    GST_LOG_OBJECT (mem->allocator, "Not our memory, free");
    return TRUE;
  }

  alloc = GST_CUDA_POOL_ALLOCATOR (mem->allocator);
  /* if flushing, free this memory */
  if (GST_CUDA_POOL_ALLOCATOR_IS_FLUSHING (alloc)) {
    GST_LOG_OBJECT (alloc, "allocator is flushing, free %p", mem);
    return TRUE;
  }

  /* return the memory to the allocator */
  gst_memory_ref (mem);
  gst_cuda_pool_allocator_release_memory (alloc, mem);

  return FALSE;
}

/* must be called with the lock */
static GstFlowReturn
gst_cuda_pool_allocator_alloc (GstCudaPoolAllocator * self, GstMemory ** mem)
{
  GstCudaPoolAllocatorPrivate *priv = self->priv;
  GstMemory *new_mem;

  /* increment the allocation counter */
  g_atomic_int_add (&priv->cur_mems, 1);
  new_mem = gst_cuda_allocator_alloc ((GstCudaAllocator *) _gst_cuda_allocator,
      self->context, self->stream, &self->info);
  if (!new_mem) {
    GST_ERROR_OBJECT (self, "Failed to allocate new memory");
    g_atomic_int_add (&priv->cur_mems, -1);
    return GST_FLOW_ERROR;
  }

  *mem = new_mem;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_cuda_pool_allocator_acquire_memory_internal (GstCudaPoolAllocator * self,
    GstMemory ** memory)
{
  GstFlowReturn result;
  GstCudaPoolAllocatorPrivate *priv = self->priv;

  while (TRUE) {
    if (G_UNLIKELY (GST_CUDA_POOL_ALLOCATOR_IS_FLUSHING (self)))
      goto flushing;

    /* try to get a memory from the queue */
    *memory = (GstMemory *) gst_atomic_queue_pop (priv->queue);
    if (G_LIKELY (*memory)) {
      while (!gst_poll_read_control (priv->poll)) {
        if (errno == EWOULDBLOCK) {
          /* We put the memory into the queue but did not finish writing control
           * yet, let's wait a bit and retry */
          g_thread_yield ();
          continue;
        } else {
          /* Critical error but GstPoll already complained */
          break;
        }
      }
      result = GST_FLOW_OK;
      GST_LOG_OBJECT (self, "acquired memory %p", *memory);
      break;
    }

    /* no memory, try to allocate some more */
    GST_LOG_OBJECT (self, "no memory, trying to allocate");
    result = gst_cuda_pool_allocator_alloc (self, memory);
    if (G_LIKELY (result == GST_FLOW_OK))
      /* we have a memory, return it */
      break;

    if (G_UNLIKELY (result != GST_FLOW_EOS))
      /* something went wrong, return error */
      break;

    /* now we release the control socket, we wait for a memory release or
     * flushing */
    if (!gst_poll_read_control (priv->poll)) {
      if (errno == EWOULDBLOCK) {
        /* This means that we have two threads trying to allocate memory
         * already, and the other one already got the wait token. This
         * means that we only have to wait for the poll now and not write the
         * token afterwards: we will be woken up once the other thread is
         * woken up and that one will write the wait token it removed */
        GST_LOG_OBJECT (self, "waiting for free memory or flushing");
        gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
      } else {
        /* This is a critical error, GstPoll already gave a warning */
        result = GST_FLOW_ERROR;
        break;
      }
    } else {
      /* We're the first thread waiting, we got the wait token and have to
       * write it again later
       * OR
       * We're a second thread and just consumed the flush token and block all
       * other threads, in which case we must not wait and give it back
       * immediately */
      if (!GST_CUDA_POOL_ALLOCATOR_IS_FLUSHING (self)) {
        GST_LOG_OBJECT (self, "waiting for free memory or flushing");
        gst_poll_wait (priv->poll, GST_CLOCK_TIME_NONE);
      }
      gst_poll_write_control (priv->poll);
    }
  }

  return result;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (self, "we are flushing");
    return GST_FLOW_FLUSHING;
  }
}

/**
 * gst_cuda_pool_allocator_new:
 * @context: a #GstCudaContext
 * @stream: (allow-none): a #GstCudaStream
 * @info: a #GstVideoInfo
 *
 * Creates a new #GstCudaPoolAllocator instance.
 *
 * Returns: (transfer full): a new #GstCudaPoolAllocator instance
 *
 * Since: 1.24
 */
GstCudaPoolAllocator *
gst_cuda_pool_allocator_new (GstCudaContext * context, GstCudaStream * stream,
    const GstVideoInfo * info)
{
  GstCudaPoolAllocator *self;

  g_return_val_if_fail (GST_IS_CUDA_CONTEXT (context), nullptr);
  g_return_val_if_fail (!stream || GST_IS_CUDA_STREAM (stream), nullptr);

  self = (GstCudaPoolAllocator *)
      g_object_new (GST_TYPE_CUDA_POOL_ALLOCATOR, nullptr);
  gst_object_ref_sink (self);

  self->context = (GstCudaContext *) gst_object_ref (context);
  if (stream)
    self->stream = gst_cuda_stream_ref (stream);
  self->info = *info;

  return self;
}

/**
 * gst_cuda_pool_allocator_acquire_memory:
 * @allocator: a #GstCudaPoolAllocator
 * @memory: (out): a #GstMemory
 *
 * Acquires a #GstMemory from @allocator. @memory should point to a memory
 * location that can hold a pointer to the new #GstMemory.
 *
 * Returns: a #GstFlowReturn such as %GST_FLOW_FLUSHING when the allocator is
 * inactive.
 *
 * Since: 1.24
 */
GstFlowReturn
gst_cuda_pool_allocator_acquire_memory (GstCudaPoolAllocator * allocator,
    GstMemory ** memory)
{
  GstFlowReturn result;
  GstCudaPoolAllocatorPrivate *priv;

  g_return_val_if_fail (GST_IS_CUDA_POOL_ALLOCATOR (allocator), GST_FLOW_ERROR);
  g_return_val_if_fail (memory, GST_FLOW_ERROR);

  priv = allocator->priv;

  g_atomic_int_inc (&priv->outstanding);
  result = gst_cuda_pool_allocator_acquire_memory_internal (allocator, memory);

  if (result == GST_FLOW_OK) {
    GstMemory *mem = *memory;
    /* Replace default allocator with ours */
    gst_object_unref (mem->allocator);
    mem->allocator = (GstAllocator *) gst_object_ref (allocator);
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_cuda_memory_release;
    allocator->priv->outstanding++;
  } else {
    dec_outstanding (allocator);
  }

  return result;
}

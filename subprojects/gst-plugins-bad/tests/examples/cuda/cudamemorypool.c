/*
 * GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/cuda/gstcuda.h>
#include <gst/video/video.h>

typedef struct
{
  GstCudaMemoryPool *mem_pool;
} NeedPoolCbData;

static GstCudaMemoryPool *
on_need_pool (GstCudaAllocator * allocator, GstCudaContext * context,
    NeedPoolCbData * data)
{
  g_assert (data->mem_pool);

  gst_println ("Need pool callback");

  return gst_cuda_memory_pool_ref (data->mem_pool);
}

gint
main (gint argc, gchar ** argv)
{
  GstCudaContext *context;
  GstCudaStream *stream;
  GstBufferPool *pool;
  GstStructure *config;
  gboolean stream_ordered_alloc = FALSE;
  CUmemoryPool handle;
  CUresult ret;
  guint64 threshold;
  guint64 reserved = 0;
  guint64 current = 0;
  GstBuffer *buffer;
  GstCaps *caps;
  GstVideoInfo info;
  GstMapInfo map_info;
  guint i;
  NeedPoolCbData data;

  gst_init (NULL, NULL);

  if (!gst_cuda_load_library ()) {
    gst_println ("Couldn't load cuda library");
    return 0;
  }

  /* Install need-pool callback to provide application's CUDA memory pool.
   * This callback will be called when GstCudaBufferPool is configured */
  gst_cuda_register_allocator_need_pool_callback (
      (GstCudaMemoryAllocatorNeedPoolCallback) on_need_pool, &data, NULL);

  context = gst_cuda_context_new (0);
  if (!context) {
    gst_println ("Couldn't create cuda context");
    return 0;
  }

  g_object_get (context, "stream-ordered-alloc", &stream_ordered_alloc, NULL);
  if (!stream_ordered_alloc) {
    gst_println ("Stream ordered allocation is not supported");
    gst_object_unref (context);
    return 0;
  }

  stream = gst_cuda_stream_new (context);
  if (!stream) {
    gst_printerrln ("Couldn't create cuda stream");
    return 0;
  }

  /* Since default prop is enough in this example case, pass NULL prop */
  data.mem_pool = gst_cuda_memory_pool_new (context, NULL);
  if (!data.mem_pool) {
    gst_printerrln ("Couldn't create memory pool");
    return 0;
  }

  handle = gst_cuda_memory_pool_get_handle (data.mem_pool);

  gst_cuda_context_push (context);
  /* Configure pool attributes. In this example, release threshold will be
   * increased (default is zero) so that allocated memory can be retained */
  threshold = 1024 * 1024 * 20;

  ret = CuMemPoolSetAttribute (handle, CU_MEMPOOL_ATTR_RELEASE_THRESHOLD,
      (void *) &threshold);
  if (!gst_cuda_result (ret)) {
    gst_printerrln ("Couldn't increase release threshold");
    return 0;
  }

  ret = CuMemPoolGetAttribute (handle, CU_MEMPOOL_ATTR_RESERVED_MEM_CURRENT,
      (void *) &reserved);
  if (!gst_cuda_result (ret)) {
    gst_printerrln ("Couldn't get reserved size");
    return 0;
  }

  ret = CuMemPoolGetAttribute (handle, CU_MEMPOOL_ATTR_USED_MEM_CURRENT,
      (void *) &current);
  if (!gst_cuda_result (ret)) {
    gst_printerrln ("Couldn't get current size");
    return 0;
  }
  gst_println ("Initial pool configuration, release threshold: %"
      G_GUINT64_FORMAT ", reserved: %" G_GUINT64_FORMAT ", current: %"
      G_GUINT64_FORMAT, threshold, reserved, current);
  gst_cuda_context_pop (NULL);

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_RGBA, 640, 480);
  caps = gst_video_info_to_caps (&info);

  for (i = 0; i < 2; i++) {
    /* Creates cuda buffer pool */
    pool = gst_cuda_buffer_pool_new (context);
    if (!pool) {
      gst_printerrln ("Couldn't create buffer pool");
      return 0;
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, info.size, 0, 0);

    /* Sets CUDA specific buffer pool options. For stream ordered allocation,
     * CUDA stream object must be configured in the config */
    gst_buffer_pool_config_set_cuda_stream (config, stream);
    /* NOTE: stream ordered allocation is enabled by default */
    gst_buffer_pool_config_set_cuda_stream_ordered_alloc (config, TRUE);
    gst_buffer_pool_set_config (pool, config);

    gst_buffer_pool_set_active (pool, TRUE);

    gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);

    gst_cuda_context_push (context);
    ret = CuMemPoolGetAttribute (handle, CU_MEMPOOL_ATTR_RESERVED_MEM_CURRENT,
        (void *) &reserved);
    if (!gst_cuda_result (ret)) {
      gst_printerrln ("Couldn't get reserved size");
      return 0;
    }

    ret = CuMemPoolGetAttribute (handle, CU_MEMPOOL_ATTR_USED_MEM_CURRENT,
        (void *) &current);
    if (!gst_cuda_result (ret)) {
      gst_printerrln ("Couldn't get current size");
      return 0;
    }

    gst_buffer_map (buffer, &map_info, GST_MAP_READ | GST_MAP_CUDA);

    gst_println ("[%d] After allocation, address %p, reserved: %"
        G_GUINT64_FORMAT ", current: %" G_GUINT64_FORMAT, i,
        map_info.data, reserved, current);
    gst_buffer_unmap (buffer, &map_info);
    gst_cuda_context_pop (NULL);

    /* Release pool and check pool status */
    gst_buffer_unref (buffer);
    gst_buffer_pool_set_active (pool, FALSE);
    gst_object_unref (pool);

    gst_cuda_context_push (context);
    ret = CuMemPoolGetAttribute (handle, CU_MEMPOOL_ATTR_RESERVED_MEM_CURRENT,
        (void *) &reserved);
    if (!gst_cuda_result (ret)) {
      gst_printerrln ("Couldn't get reserved size");
      return 0;
    }

    ret = CuMemPoolGetAttribute (handle, CU_MEMPOOL_ATTR_USED_MEM_CURRENT,
        (void *) &current);
    if (!gst_cuda_result (ret)) {
      gst_printerrln ("Couldn't get current size");
      return 0;
    }

    gst_println ("[%d] After buffer pool release, reserved: %" G_GUINT64_FORMAT
        ", current: %" G_GUINT64_FORMAT, i, reserved, current);
    gst_cuda_context_pop (NULL);
  }

  gst_caps_unref (caps);
  gst_cuda_memory_pool_unref (data.mem_pool);
  gst_cuda_stream_unref (stream);
  gst_object_unref (context);

  gst_deinit ();

  return 0;
}

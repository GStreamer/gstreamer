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

#include "gstmsdkbufferpool.h"
#include "gstmsdksystemmemory.h"
#include "gstmsdkvideomemory.h"

GST_DEBUG_CATEGORY_STATIC (gst_debug_msdkbufferpool);
#define GST_CAT_DEFAULT gst_debug_msdkbufferpool

#define GST_MSDK_BUFFER_POOL_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MSDK_BUFFER_POOL, \
      GstMsdkBufferPoolPrivate))

#define gst_msdk_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMsdkBufferPool, gst_msdk_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL,
    GST_DEBUG_CATEGORY_INIT (gst_debug_msdkbufferpool, "msdkbufferpool", 0,
        "MSDK Buffer Pool"));

struct _GstMsdkBufferPoolPrivate
{
  GstMsdkContext *context;
  GstAllocator *allocator;
  mfxFrameAllocResponse *alloc_response;
  gboolean use_video_memory;
  gboolean add_videometa;
};

static const gchar **
gst_msdk_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT,
    GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY,
    NULL
  };

  return options;
}

static gboolean
gst_msdk_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstMsdkBufferPool *msdk_pool = GST_MSDK_BUFFER_POOL_CAST (pool);
  GstMsdkBufferPoolPrivate *priv = msdk_pool->priv;
  GstCaps *caps = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo video_info;
  guint size, min_buffers, max_buffers;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto error_invalid_config;

  if (!caps)
    goto error_no_caps;

  if (!gst_video_info_from_caps (&video_info, caps))
    goto error_invalid_caps;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, NULL))
    goto error_invalid_allocator;

  if (allocator
      && (g_strcmp0 (allocator->mem_type, GST_MSDK_SYSTEM_MEMORY_NAME) != 0
          && g_strcmp0 (allocator->mem_type,
              GST_MSDK_VIDEO_MEMORY_NAME) != 0)) {
    GST_INFO_OBJECT (pool,
        "This is not MSDK allocator. So this will be ignored");
    gst_object_unref (allocator);
    allocator = NULL;
  }

  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (priv->add_videometa && gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    GstVideoAlignment alignment;

    gst_msdk_set_video_alignment (&video_info, &alignment);
    gst_video_info_align (&video_info, &alignment);
    gst_buffer_pool_config_set_video_alignment (config, &alignment);
  }

  priv->use_video_memory = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_MSDK_USE_VIDEO_MEMORY);

  if (priv->use_video_memory && (!priv->context || !priv->alloc_response)) {
    GST_ERROR_OBJECT (pool,
        "No MSDK context or Allocation response for using video memory");
    goto error_invalid_config;
  }

  /* create a new allocator if needed */
  if (!allocator) {
    GstAllocationParams params = { 0, 31, 0, 0, };

    if (priv->use_video_memory)
      allocator =
          gst_msdk_video_allocator_new (priv->context, &video_info,
          priv->alloc_response);
    else
      allocator = gst_msdk_system_allocator_new (&video_info);

    if (!allocator)
      goto error_no_allocator;

    GST_INFO_OBJECT (pool, "created new allocator %" GST_PTR_FORMAT, allocator);

    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    gst_object_unref (allocator);
  }

  if (priv->allocator)
    gst_object_unref (priv->allocator);
  priv->allocator = gst_object_ref (allocator);

  return GST_BUFFER_POOL_CLASS
      (gst_msdk_buffer_pool_parent_class)->set_config (pool, config);

error_invalid_config:
  {
    GST_ERROR_OBJECT (pool, "invalid config");
    return FALSE;
  }
error_no_caps:
  {
    GST_ERROR_OBJECT (pool, "no caps in config");
    return FALSE;
  }
error_invalid_caps:
  {
    GST_ERROR_OBJECT (pool, "invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
error_invalid_allocator:
  {
    GST_ERROR_OBJECT (pool, "no allocator in config");
    return FALSE;
  }
error_no_allocator:
  {
    GST_ERROR_OBJECT (pool, "no allocator defined");
    return FALSE;
  }
}

static GstFlowReturn
gst_msdk_buffer_pool_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** out_buffer_ptr, GstBufferPoolAcquireParams * params)
{
  GstMsdkBufferPool *msdk_pool = GST_MSDK_BUFFER_POOL_CAST (pool);
  GstMsdkBufferPoolPrivate *priv = msdk_pool->priv;
  GstMemory *mem;
  GstBuffer *buf;

  buf = gst_buffer_new ();

  if (priv->use_video_memory)
    mem = gst_msdk_video_memory_new (priv->allocator);
  else
    mem = gst_msdk_system_memory_new (priv->allocator);

  if (!mem)
    goto no_memory;

  gst_buffer_append_memory (buf, mem);

  if (priv->add_videometa) {
    GstVideoMeta *vmeta;
    GstVideoInfo *info;

    if (priv->use_video_memory)
      info = &GST_MSDK_VIDEO_ALLOCATOR_CAST (priv->allocator)->image_info;
    else
      info = &GST_MSDK_SYSTEM_ALLOCATOR_CAST (priv->allocator)->image_info;

    vmeta = gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
        GST_VIDEO_INFO_N_PLANES (info), info->offset, info->stride);

    if (priv->use_video_memory) {
      vmeta->map = gst_video_meta_map_msdk_memory;
      vmeta->unmap = gst_video_meta_unmap_msdk_memory;
    }
  }

  *out_buffer_ptr = buf;
  return GST_FLOW_OK;

no_memory:
  {
    GST_ERROR_OBJECT (pool, "failed to create new MSDK memory");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_msdk_buffer_pool_acquire_buffer (GstBufferPool * pool,
    GstBuffer ** out_buffer_ptr, GstBufferPoolAcquireParams * params)
{
  GstMsdkBufferPool *msdk_pool = GST_MSDK_BUFFER_POOL_CAST (pool);
  GstMsdkBufferPoolPrivate *priv = msdk_pool->priv;
  GstBuffer *buf = NULL;
  GstFlowReturn ret;
  mfxFrameSurface1 *surface;

  ret =
      GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (pool, &buf, params);

  /* When using video memory, mfx surface is still locked even though
   * it's finished by SyncOperation. There's no way to get notified when it gets unlocked.
   * So we need to confirm if it's unlocked every time a gst buffer is acquired.
   * If it's still locked, we can replace it with new unlocked/unused surface.
   */
  if (ret != GST_FLOW_OK || !priv->use_video_memory) {
    if (buf)
      *out_buffer_ptr = buf;
    return ret;
  }

  surface = gst_msdk_get_surface_from_buffer (buf);
  if (!surface || surface->Data.Locked > 0) {
    if (!gst_msdk_video_memory_get_surface_available (GST_MSDK_VIDEO_MEMORY_CAST
            (gst_buffer_peek_memory (buf, 0)))) {
      GST_WARNING_OBJECT (pool, "failed to get new surface available");
      return GST_FLOW_ERROR;
    }
  }

  *out_buffer_ptr = buf;
  return GST_FLOW_OK;
}

static void
gst_msdk_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buf)
{
  mfxFrameSurface1 *surface;
  GstMsdkBufferPool *msdk_pool = GST_MSDK_BUFFER_POOL_CAST (pool);
  GstMsdkBufferPoolPrivate *priv = msdk_pool->priv;

  if (!priv->use_video_memory)
    goto done;

  surface = gst_msdk_get_surface_from_buffer (buf);
  if (!surface)
    goto done;

  gst_msdk_video_memory_release_surface (GST_MSDK_VIDEO_MEMORY_CAST
      (gst_buffer_peek_memory (buf, 0)));

done:
  return GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (pool, buf);
}

static void
gst_msdk_buffer_pool_finalize (GObject * object)
{
  GstMsdkBufferPool *pool = GST_MSDK_BUFFER_POOL_CAST (object);
  GstMsdkBufferPoolPrivate *priv = pool->priv;

  if (priv->allocator)
    gst_object_unref (priv->allocator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_msdk_buffer_pool_init (GstMsdkBufferPool * pool)
{
  pool->priv = GST_MSDK_BUFFER_POOL_GET_PRIVATE (pool);
}

static void
gst_msdk_buffer_pool_class_init (GstMsdkBufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstMsdkBufferPoolPrivate));

  object_class->finalize = gst_msdk_buffer_pool_finalize;

  pool_class->get_options = gst_msdk_buffer_pool_get_options;
  pool_class->set_config = gst_msdk_buffer_pool_set_config;
  pool_class->alloc_buffer = gst_msdk_buffer_pool_alloc_buffer;
  pool_class->acquire_buffer = gst_msdk_buffer_pool_acquire_buffer;
  pool_class->release_buffer = gst_msdk_buffer_pool_release_buffer;
}

GstBufferPool *
gst_msdk_buffer_pool_new (GstMsdkContext * context,
    mfxFrameAllocResponse * alloc_resp)
{
  GstMsdkBufferPool *pool = g_object_new (GST_TYPE_MSDK_BUFFER_POOL, NULL);

  /* Doesn't need to count reference of the context */
  pool->priv->context = context;
  pool->priv->alloc_response = alloc_resp;

  return GST_BUFFER_POOL_CAST (pool);
}

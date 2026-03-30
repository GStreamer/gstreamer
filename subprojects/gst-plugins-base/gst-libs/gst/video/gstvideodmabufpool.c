/* GStreamer video dmabuf pool
 *
 * Copyright (C) 2025 Collabora Ltd.
 * Author: Robert Mader <robert.mader@collabora.com>
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

/**
 * SECTION:gstvideodmabufpool
 * @title: GstVideoDmabufPool
 * @short_description: Pool for virtual memory backed dmabufs
 * @see_also: #GstUdmabufAllocator
 *
 * Using #GstUdmabufAllocator, setting defaults and implementing implicit sync.
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideodmabufpool.h"
#include "gstvideodmabufsynchandler.h"

#include <gst/allocators/gstudmabufallocator.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* This alignment is needed on many AMD GPUs and is known to work well across
 * many vendors/GPUs, see e.g. GstGLDMABufBufferPool. */
#define UDMABUF_ALIGNMENT_MASK (256 - 1)

struct _GstVideoDmabufPool
{
  GstVideoBufferPool parent;
  GstVideoDmabufSyncHandler *sync_handler;
};

GST_DEBUG_CATEGORY_STATIC (gst_video_dmabuf_pool_debug);
#define GST_CAT_DEFAULT gst_video_dmabuf_pool_debug

G_DEFINE_TYPE_WITH_CODE (GstVideoDmabufPool, gst_video_dmabuf_pool,
    GST_TYPE_VIDEO_BUFFER_POOL,
    GST_DEBUG_CATEGORY_INIT (gst_video_dmabuf_pool_debug, "video-dmabuf-pool",
        0, "video dmabuf pool");
    );

#ifdef HAVE_LINUX_DMA_BUF_H

static gboolean
gst_video_dmabuf_pool_start (GstBufferPool * pool)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);

  if (!gst_video_dmabuf_sync_handler_start (self->sync_handler))
    return FALSE;

  return
      GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->start (pool);
}

static gboolean
gst_video_dmabuf_pool_stop (GstBufferPool * pool)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);

  if (!gst_video_dmabuf_sync_handler_stop (self->sync_handler))
    return FALSE;

  return
      GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->stop (pool);
}

static void
gst_video_dmabuf_pool_final_release_buffer (gpointer user_data, gpointer buffer)
{
  GstBufferPool *pool = GST_BUFFER_POOL_CAST (user_data);
  GstBuffer *buf = GST_BUFFER_CAST (buffer);

  GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->release_buffer
      (pool, buf);
}

static void
gst_video_dmabuf_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);
  GstMemory *mems[GST_VIDEO_MAX_PLANES];
  guint n_mem;

  n_mem = gst_buffer_n_memory (buffer);
  for (gint i = 0; i < n_mem; i++)
    mems[i] = gst_buffer_peek_memory (buffer, i);

  gst_video_dmabuf_sync_handler_release_buffer (self->sync_handler,
      (gpointer) buffer, mems, n_mem);
}

static gboolean
gst_video_dmabuf_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);
  GstAllocator *allocator;
  GstAllocationParams params;
  GstVideoAlignment video_align = { 0 };
  gboolean config_updated = FALSE;
  gboolean alignment_updated = FALSE;
  gboolean res;

  gst_buffer_pool_config_get_allocator (config, &allocator, &params);
  if (!GST_IS_DMABUF_ALLOCATOR (allocator) ||
      GST_OBJECT_FLAG_IS_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC)) {
    GST_DEBUG_OBJECT (self,
        "Allocator not a dmabuf allocator or having the CUSTOM_ALLOC flag set, trying to update to udmabuf");

    allocator = gst_udmabuf_allocator_get ();
    if (allocator) {
      params.align |= UDMABUF_ALIGNMENT_MASK;
      gst_buffer_pool_config_set_allocator (config, allocator, &params);
      gst_object_unref (allocator);
      config_updated = TRUE;
    } else {
      GST_ERROR_OBJECT (self, "udmabuf allocator not available");
      return FALSE;
    }
  } else if (params.align < UDMABUF_ALIGNMENT_MASK) {
    GST_DEBUG_OBJECT (self, "updating allocator params");
    params.align |= UDMABUF_ALIGNMENT_MASK;
    gst_buffer_pool_config_set_allocator (config, allocator, &params);
    config_updated = TRUE;
  }

  if (!gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_META)) {
    GST_DEBUG_OBJECT (self, "missing video meta option");
    return FALSE;
  }
  if (!gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    GST_DEBUG_OBJECT (self, "missing video alignment option");
    return FALSE;
  }

  gst_buffer_pool_config_get_video_alignment (config, &video_align);
  for (int i = 0; i != GST_VIDEO_MAX_PLANES; ++i) {
    if (video_align.stride_align[i] < UDMABUF_ALIGNMENT_MASK) {
      video_align.stride_align[i] |= UDMABUF_ALIGNMENT_MASK;
      alignment_updated = TRUE;
    }
  }
  if (alignment_updated) {
    GST_DEBUG_OBJECT (self, "updating video alignment");
    gst_buffer_pool_config_set_video_alignment (config, &video_align);
    config_updated = TRUE;
  }

  res =
      GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->set_config
      (pool, config);

  if (config_updated)
    return FALSE;

  return res;
}

static void
gst_video_dmabuf_pool_finalize (GObject * object)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (object);

  gst_object_unref (self->sync_handler);
}
#endif /* HAVE_LINUX_DMA_BUF_H */

static void
gst_video_dmabuf_pool_init (GstVideoDmabufPool * self)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  self->sync_handler =
      gst_video_dmabuf_sync_handler_new
      (gst_video_dmabuf_pool_final_release_buffer, (gpointer) self);
#endif
}

static void
gst_video_dmabuf_pool_class_init (GstVideoDmabufPoolClass * klass)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->finalize = gst_video_dmabuf_pool_finalize;

  pool_class->start = gst_video_dmabuf_pool_start;
  pool_class->stop = gst_video_dmabuf_pool_stop;
  pool_class->release_buffer = gst_video_dmabuf_pool_release_buffer;
  pool_class->set_config = gst_video_dmabuf_pool_set_config;
#endif
}

/**
 * gst_video_dmabuf_pool_new:
 *
 * Create a new #GstVideoDmabufPool instance.
 *
 * Returns: (transfer full) (nullable): a #GstVideoDmabufPool or %NULL
 *     if dmabufs are not supported.
 *
 * Since: 1.28
 */
GstBufferPool *
gst_video_dmabuf_pool_new (void)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  return g_object_new (GST_TYPE_VIDEO_DMABUF_POOL, NULL);
#else
  return NULL;
#endif
}

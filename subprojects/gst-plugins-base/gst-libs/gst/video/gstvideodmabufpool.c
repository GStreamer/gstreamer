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
  GMainContext *context;
  GMainLoop *loop;
  GThread *thread;
};

GST_DEBUG_CATEGORY_STATIC (gst_video_dmabuf_pool_debug);

G_DEFINE_TYPE_WITH_CODE (GstVideoDmabufPool, gst_video_dmabuf_pool,
    GST_TYPE_VIDEO_BUFFER_POOL,
    GST_DEBUG_CATEGORY_INIT (gst_video_dmabuf_pool_debug, "video-dmabuf-pool",
        0, "video dmabuf pool");
    );

#ifdef HAVE_LINUX_DMA_BUF_H
#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
typedef struct _DmaBufSource
{
  GSource base;

  GstVideoDmabufPool *pool;
  GstBuffer *buffer;

  gint mem_fds[GST_VIDEO_MAX_PLANES];
  gpointer fd_tags[GST_VIDEO_MAX_PLANES];
} DmaBufSource;

static gboolean
dma_buf_fd_readable (gint fd)
{
  GPollFD poll_fd;

  poll_fd.fd = fd;
  poll_fd.events = G_IO_IN;
  poll_fd.revents = 0;

  if (!g_poll (&poll_fd, 1, 0))
    return FALSE;

  return (poll_fd.revents & (G_IO_IN | G_IO_NVAL)) != 0;
}

static int
get_sync_file (gint fd)
{
  struct dma_buf_export_sync_file sync_file_in_out = {
    .flags = DMA_BUF_SYNC_WRITE,
    .fd = -1
  };
  gint ret;

  do {
    ret = ioctl (fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &sync_file_in_out);
  } while (ret == -1 && errno == EINTR);

  if (ret == 0)
    return sync_file_in_out.fd;

  return -1;
}

static gboolean
dma_buf_source_dispatch (GSource * base,
    GSourceFunc callback, gpointer user_data)
{
  DmaBufSource *source = (DmaBufSource *) base;
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (source->pool);
  gboolean ready;

  GST_DEBUG_OBJECT (self, "Dispatch source for buffer %p", source->buffer);

  ready = TRUE;

  for (gint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (!source->fd_tags[i])
      continue;

    if (!dma_buf_fd_readable (source->mem_fds[i])) {
      GST_DEBUG_OBJECT (self, "Buffer %p not ready, sync file: %d",
          source->buffer, source->mem_fds[i]);
      ready = FALSE;
      continue;
    }

    close (source->mem_fds[i]);
    g_source_remove_unix_fd (base, source->fd_tags[i]);
    source->fd_tags[i] = NULL;
  }

  if (!ready)
    return G_SOURCE_CONTINUE;

  GST_DEBUG_OBJECT (self, "Releasing buffer %p from source, pool %p",
      source->buffer, self);
  GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->release_buffer
      (GST_BUFFER_POOL_CAST (self), source->buffer);
  g_source_unref (base);

  return G_SOURCE_REMOVE;
}

static void
dma_buf_source_finalize (GSource * base)
{
  DmaBufSource *source = (DmaBufSource *) base;
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (source->pool);
  gboolean need_buffer_release = FALSE;

  for (gint i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (!source->fd_tags[i])
      continue;

    close (source->mem_fds[i]);
    g_source_remove_unix_fd (base, source->fd_tags[i]);
    source->fd_tags[i] = NULL;
    need_buffer_release = TRUE;
  }

  if (need_buffer_release) {
    GST_DEBUG_OBJECT (self, "Releasing buffer %p from source, pool %p",
        source->buffer, self);
    GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->release_buffer
        (GST_BUFFER_POOL_CAST (self), source->buffer);
  }
}

static GSourceFuncs dma_buf_source_funcs = {
  .dispatch = dma_buf_source_dispatch,
  .finalize = dma_buf_source_finalize,
};

static gpointer
dmabuf_source_thread (gpointer data)
{
  GstVideoDmabufPool *self = data;
  GMainContext *context = NULL;
  GMainLoop *loop = NULL;

  GST_OBJECT_LOCK (self);
  if (self->context)
    context = g_main_context_ref (self->context);
  if (self->loop)
    loop = g_main_loop_ref (self->loop);

  if (context == NULL || loop == NULL) {
    g_clear_pointer (&loop, g_main_loop_unref);
    g_clear_pointer (&context, g_main_context_unref);
    GST_OBJECT_UNLOCK (self);
    return NULL;
  }
  GST_OBJECT_UNLOCK (self);

  g_main_context_push_thread_default (context);

  GST_DEBUG_OBJECT (self, "Running main loop");
  g_main_loop_run (loop);
  g_main_loop_unref (loop);
  g_main_context_unref (context);

  gst_object_unref (self);

  return NULL;
}

static gboolean
gst_video_dmabuf_pool_start (GstBufferPool * pool)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);

  GST_OBJECT_LOCK (self);
  GST_DEBUG_OBJECT (self, "Starting main loop");
  g_assert (self->context == NULL);

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);

  self->thread =
      g_thread_new ("video-dmabuf-pool-source-loop", dmabuf_source_thread,
      g_object_ref (self));

  GST_OBJECT_UNLOCK (self);
  return
      GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->start (pool);
}

static gboolean
gst_video_dmabuf_pool_stop (GstBufferPool * pool)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);
  GMainContext *context;
  GMainLoop *loop;
  GSource *idle_stop_source;

  GST_OBJECT_LOCK (self);
  GST_DEBUG_OBJECT (self, "Stopping main loop");
  context = self->context;
  loop = self->loop;
  self->context = NULL;
  self->loop = NULL;
  GST_OBJECT_UNLOCK (self);

  if (!context || !loop) {
    g_clear_pointer (&loop, g_main_loop_unref);
    g_clear_pointer (&context, g_main_context_unref);
    goto out;
  }

  idle_stop_source = g_idle_source_new ();
  g_source_set_callback (idle_stop_source, (GSourceFunc) g_main_loop_quit, loop,
      NULL);
  g_source_attach (idle_stop_source, context);
  g_source_unref (idle_stop_source);

  g_thread_join (self->thread);
  self->thread = NULL;

  g_main_loop_unref (loop);
  g_main_context_unref (context);

out:
  return
      GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->stop (pool);
}

static void
gst_video_dmabuf_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstVideoDmabufPool *self = GST_VIDEO_DMABUF_POOL (pool);
  DmaBufSource *source = NULL;
  guint n_mem;

  GST_DEBUG_OBJECT (self, "Buffer: %p", buffer);

  n_mem = gst_buffer_n_memory (buffer);
  for (gint i = 0; i < n_mem; i++) {
    GstMemory *mem;
    gint mem_fd, sync_file;

    mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_dmabuf_memory (mem))
      continue;

    mem_fd = gst_dmabuf_memory_get_fd (mem);
    sync_file = get_sync_file (mem_fd);
    if (sync_file == -1) {
      GST_ERROR_OBJECT (self, "Exporting sync file failed");
      continue;
    }

    if (dma_buf_fd_readable (sync_file)) {
      GST_DEBUG_OBJECT (self, "Sync file readable");
      close (sync_file);
      continue;
    }

    if (!source) {
      GST_DEBUG_OBJECT (self, "Creating source for buffer %p, pool %p", buffer,
          self);
      source =
          (DmaBufSource *) g_source_new (&dma_buf_source_funcs,
          sizeof (*source));
      source->pool = self;
      source->buffer = buffer;
    }

    GST_DEBUG_OBJECT (self, "Adding sync file to source");
    source->mem_fds[i] = sync_file;
    source->fd_tags[i] =
        g_source_add_unix_fd (&source->base, sync_file, G_IO_IN);
  }

  if (source) {
    g_assert (self->context);
    g_source_attach ((GSource *) source, self->context);
    return;
  }

  GST_BUFFER_POOL_CLASS (gst_video_dmabuf_pool_parent_class)->release_buffer
      (pool, buffer);
}
#endif /* DMA_BUF_IOCTL_EXPORT_SYNC_FILE */

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
#endif /* HAVE_LINUX_DMA_BUF_H */

static void
gst_video_dmabuf_pool_init (GstVideoDmabufPool * self)
{
}

static void
gst_video_dmabuf_pool_class_init (GstVideoDmabufPoolClass * klass)
{
#ifdef HAVE_LINUX_DMA_BUF_H
  GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS (klass);

#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
  pool_class->start = gst_video_dmabuf_pool_start;
  pool_class->stop = gst_video_dmabuf_pool_stop;
  pool_class->release_buffer = gst_video_dmabuf_pool_release_buffer;
#endif /* DMA_BUF_IOCTL_EXPORT_SYNC_FILE */
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

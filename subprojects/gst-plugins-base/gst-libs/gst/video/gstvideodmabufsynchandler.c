/* GStreamer video dmabuf sync_handler
 *
 * Copyright (C) 2025 Collabora Ltd.
 *   Author: Robert Mader <robert.mader@collabora.com>
 * Copyright (C) 2026 Pengutronix, Michael Olbrich <m.olbrich@pengutronix.de>
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
 * SECTION:gstvideodmabufsync_handler
 * @title: GstVideoDmabufSyncHandler
 * @short_description: Handle implict syncs for dmabufs
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideodmabufsynchandler.h"

#include <gst/allocators/gstudmabufallocator.h>

#ifdef HAVE_LINUX_DMA_BUF_H
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

struct _GstVideoDmabufSyncHandler
{
  GstObject parent;

  GMainContext *context;
  GMainLoop *loop;
  GThread *thread;

  gpointer user_data;
  GstVideoDmabufSyncHandlerReleaseBuffer release_buffer;
};

#define GST_CAT_DEFAULT gst_video_dmabuf_sync_handler_debug
GST_DEBUG_CATEGORY_STATIC (gst_video_dmabuf_sync_handler_debug);

G_DEFINE_TYPE_WITH_CODE (GstVideoDmabufSyncHandler,
    gst_video_dmabuf_sync_handler, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_video_dmabuf_sync_handler_debug,
        "video-dmabuf-sync_handler", 0, "video dmabuf sync_handler");
    );

#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
typedef struct _DmaBufSource
{
  GSource base;

  GstVideoDmabufSyncHandler *sync_handler;
  gpointer buffer;

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
  GstVideoDmabufSyncHandler *self =
      GST_VIDEO_DMABUF_SYNC_HANDLER (source->sync_handler);
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

  GST_DEBUG_OBJECT (self, "Releasing buffer %p from source, sync_handler %p",
      source->buffer, self);
  self->release_buffer (self->user_data, source->buffer);
  g_source_unref (base);

  return G_SOURCE_REMOVE;
}

static void
dma_buf_source_finalize (GSource * base)
{
  DmaBufSource *source = (DmaBufSource *) base;
  GstVideoDmabufSyncHandler *self =
      GST_VIDEO_DMABUF_SYNC_HANDLER (source->sync_handler);
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
    GST_DEBUG_OBJECT (self, "Releasing buffer %p from source, sync_handler %p",
        source->buffer, self);
    self->release_buffer (self->user_data, source->buffer);
  }
}

static GSourceFuncs dma_buf_source_funcs = {
  .dispatch = dma_buf_source_dispatch,
  .finalize = dma_buf_source_finalize,
};

static gpointer
dmabuf_source_thread (gpointer data)
{
  GstVideoDmabufSyncHandler *self = data;
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

  return NULL;
}
#endif /* DMA_BUF_IOCTL_EXPORT_SYNC_FILE */

gboolean
gst_video_dmabuf_sync_handler_start (GstVideoDmabufSyncHandler * sync_handler)
{
#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
  GST_OBJECT_LOCK (sync_handler);
  if (!sync_handler->release_buffer) {
    GST_ERROR_OBJECT (sync_handler, "Missing buffer release handler");
    GST_OBJECT_UNLOCK (sync_handler);
    return FALSE;
  }
  GST_DEBUG_OBJECT (sync_handler, "Starting main loop");
  g_assert (sync_handler->context == NULL);

  sync_handler->context = g_main_context_new ();
  sync_handler->loop = g_main_loop_new (sync_handler->context, FALSE);

  sync_handler->thread =
      g_thread_new ("video-dmabuf-sync_handler-source-loop",
      dmabuf_source_thread, sync_handler);

  GST_OBJECT_UNLOCK (sync_handler);
#endif /* DMA_BUF_IOCTL_EXPORT_SYNC_FILE */
  return TRUE;
}

gboolean
gst_video_dmabuf_sync_handler_stop (GstVideoDmabufSyncHandler * sync_handler)
{
#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
  GMainContext *context;
  GMainLoop *loop;
  GSource *idle_stop_source;

  GST_OBJECT_LOCK (sync_handler);
  GST_DEBUG_OBJECT (sync_handler, "Stopping main loop");
  context = sync_handler->context;
  loop = sync_handler->loop;
  sync_handler->context = NULL;
  sync_handler->loop = NULL;
  GST_OBJECT_UNLOCK (sync_handler);

  if (!context || !loop) {
    g_clear_pointer (&loop, g_main_loop_unref);
    g_clear_pointer (&context, g_main_context_unref);
    return TRUE;
  }

  idle_stop_source = g_idle_source_new ();
  g_source_set_callback (idle_stop_source, (GSourceFunc) g_main_loop_quit, loop,
      NULL);
  g_source_attach (idle_stop_source, context);
  g_source_unref (idle_stop_source);

  g_thread_join (sync_handler->thread);
  sync_handler->thread = NULL;

  g_main_loop_unref (loop);
  g_main_context_unref (context);
#endif /* DMA_BUF_IOCTL_EXPORT_SYNC_FILE */

  return TRUE;
}

void
gst_video_dmabuf_sync_handler_release_buffer (GstVideoDmabufSyncHandler *
    sync_handler, gpointer buffer, GstMemory ** memories, guint n_memories)
{
#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
  DmaBufSource *source = NULL;

  GST_DEBUG_OBJECT (sync_handler, "Buffer: %p", buffer);

  for (gint i = 0; i < n_memories; i++) {
    gint mem_fd, sync_file;

    if (!gst_is_dmabuf_memory (memories[i]))
      continue;

    mem_fd = gst_dmabuf_memory_get_fd (memories[i]);
    sync_file = get_sync_file (mem_fd);
    if (sync_file == -1) {
      GST_ERROR_OBJECT (sync_handler, "Exporting sync file failed");
      continue;
    }

    if (dma_buf_fd_readable (sync_file)) {
      GST_DEBUG_OBJECT (sync_handler, "Sync file readable");
      close (sync_file);
      continue;
    }

    if (!source) {
      GST_DEBUG_OBJECT (sync_handler,
          "Creating source for buffer %p, sync_handler %p", buffer,
          sync_handler);
      source =
          (DmaBufSource *) g_source_new (&dma_buf_source_funcs,
          sizeof (*source));
      source->sync_handler = sync_handler;
      source->buffer = buffer;
    }

    GST_DEBUG_OBJECT (sync_handler, "Adding sync file to source");
    source->mem_fds[i] = sync_file;
    source->fd_tags[i] =
        g_source_add_unix_fd (&source->base, sync_file, G_IO_IN);
  }

  if (source) {
    g_assert (sync_handler->context);
    g_source_attach ((GSource *) source, sync_handler->context);
    return;
  }
#endif /* DMA_BUF_IOCTL_EXPORT_SYNC_FILE */

  sync_handler->release_buffer (sync_handler->user_data, buffer);
}

static void
gst_video_dmabuf_sync_handler_init (GstVideoDmabufSyncHandler * self)
{
}

static void
gst_video_dmabuf_sync_handler_class_init (GstVideoDmabufSyncHandlerClass *
    klass)
{
}

/**
 * gst_video_dmabuf_sync_handler_new:
 *
 * Create a new #GstVideoDmabufSyncHandler instance.
 *
 * Returns: (transfer full) (nullable): a #GstVideoDmabufSyncHandler or %NULL
 *     if dmabufs are not supported.
 *
 * Since: 1.30
 */
GstVideoDmabufSyncHandler *
gst_video_dmabuf_sync_handler_new (GstVideoDmabufSyncHandlerReleaseBuffer
    release_buffer, gpointer user_data)
{
#ifdef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
  GstVideoDmabufSyncHandler *handler =
      g_object_new (GST_TYPE_VIDEO_DMABUF_SYNC_HANDLER, NULL);
  handler->user_data = user_data;
  handler->release_buffer = release_buffer;
  return handler;
#else
  return NULL;
#endif
}

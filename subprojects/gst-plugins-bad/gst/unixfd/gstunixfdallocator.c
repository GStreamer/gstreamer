/* GStreamer unix file-descriptor source/sink
 *
 * Copyright (C) 2025 Netflix Inc.
 *  Author: Xavier Claessens <xclaessens@netflix.com>
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

#include "gstunixfdallocator.h"

struct _GstUnixFdAllocator
{
  GstShmAllocator parent;

  GMutex lock;
  GList *pool;
  gboolean flush;
};

G_DEFINE_TYPE (GstUnixFdAllocator, gst_unix_fd_allocator,
    GST_TYPE_SHM_ALLOCATOR);

static gboolean
gst_unix_fd_allocator_mem_dispose (GstMiniObject * obj)
{
  GstMemory *mem = GST_MEMORY_CAST (obj);
  GstUnixFdAllocator *self = GST_UNIX_FD_ALLOCATOR (mem->allocator);

  g_mutex_lock (&self->lock);

  if (self->flush) {
    g_mutex_unlock (&self->lock);
    return TRUE;
  }

  gsize offset, maxsize;
  gst_memory_get_sizes (mem, &offset, &maxsize);
  gst_memory_resize (mem, -offset, maxsize);

  self->pool = g_list_prepend (self->pool, gst_memory_ref (mem));

  g_mutex_unlock (&self->lock);

  return FALSE;
}

static GstMemory *
gst_unix_fd_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstUnixFdAllocator *self = GST_UNIX_FD_ALLOCATOR (allocator);
  gsize smallest_size = G_MAXSIZE;
  GList *smallest_link = NULL;

  /* Check if we have a memory big enough in our pool. */
  g_mutex_lock (&self->lock);
  for (GList * l = self->pool; l != NULL; l = l->next) {
    GstMemory *mem = l->data;
    gsize maxsize;

    gst_memory_get_sizes (mem, NULL, &maxsize);
    if (maxsize >= size) {
      self->pool = g_list_delete_link (self->pool, l);
      g_mutex_unlock (&self->lock);
      return mem;
    }
    if (maxsize < smallest_size) {
      smallest_size = maxsize;
      smallest_link = l;
    }
  }
  /* All our memories are too small. Delete the smallest one to converge to a
   * size that will avoid re-allocations in the future. */
  if (smallest_link != NULL) {
    GstMemory *mem = smallest_link->data;
    self->pool = g_list_delete_link (self->pool, smallest_link);
    GST_MINI_OBJECT_CAST (mem)->dispose = NULL;
    gst_memory_unref (mem);
  }
  g_mutex_unlock (&self->lock);

  /* Allocate a new memory */
  GstMemory *mem =
      GST_ALLOCATOR_CLASS (gst_unix_fd_allocator_parent_class)->alloc
      (allocator, size, params);
  if (mem != NULL)
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_unix_fd_allocator_mem_dispose;

  return mem;
}

static void
gst_unix_fd_allocator_finalize (GObject * object)
{
  GstUnixFdAllocator *self = GST_UNIX_FD_ALLOCATOR (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (gst_unix_fd_allocator_parent_class)->finalize (object);
}

static void
gst_unix_fd_allocator_class_init (GstUnixFdAllocatorClass * klass)
{
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  object_class->finalize = gst_unix_fd_allocator_finalize;

  alloc_class->alloc = GST_DEBUG_FUNCPTR (gst_unix_fd_allocator_alloc);
}

static void
gst_unix_fd_allocator_init (GstUnixFdAllocator * self)
{
  g_mutex_init (&self->lock);
}

GstUnixFdAllocator *
gst_unix_fd_allocator_new (void)
{
  return g_object_new (GST_TYPE_UNIX_FD_ALLOCATOR, NULL);
}

void
gst_unix_fd_allocator_flush (GstUnixFdAllocator * self)
{
  g_return_if_fail (GST_IS_UNIX_FD_ALLOCATOR (self));

  g_mutex_lock (&self->lock);
  GList *pool = self->pool;
  self->pool = NULL;
  self->flush = TRUE;
  g_mutex_unlock (&self->lock);

  g_list_free_full (pool, (GDestroyNotify) gst_memory_unref);
}

/* GStreamer fd backed memory
 * Copyright (C) 2013 Linaro SA
 * Author: Benjamin Gaignard <benjamin.gaignard@linaro.org> for Linaro.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for mordetails.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfdmemory.h"

#ifdef HAVE_MMAP
#include <sys/mman.h>
#include <unistd.h>
#endif

static void
gst_fd_mem_free (GstAllocator * allocator, GstMemory * gmem)
{
#ifdef HAVE_MMAP
  GstFdMemory *mem = (GstFdMemory *) gmem;

  if (mem->data) {
    if (!(mem->flags & GST_FD_MEMORY_FLAG_KEEP_MAPPED))
      g_warning (G_STRLOC ":%s: Freeing memory %p still mapped", G_STRFUNC,
          mem);

    munmap ((void *) mem->data, gmem->maxsize);
  }
  if (mem->fd >= 0 && gmem->parent == NULL)
    close (mem->fd);
  g_mutex_clear (&mem->lock);
  g_slice_free (GstFdMemory, mem);
  GST_DEBUG ("%p: freed", mem);
#endif
}

static gpointer
gst_fd_mem_map (GstMemory * gmem, gsize maxsize, GstMapFlags flags)
{
#ifdef HAVE_MMAP
  GstFdMemory *mem = (GstFdMemory *) gmem;
  gint prot;
  gpointer ret = NULL;

  if (gmem->parent)
    return gst_fd_mem_map (gmem->parent, maxsize, flags);

  prot = flags & GST_MAP_READ ? PROT_READ : 0;
  prot |= flags & GST_MAP_WRITE ? PROT_WRITE : 0;

  g_mutex_lock (&mem->lock);
  /* do not mmap twice the buffer */
  if (mem->data) {
    /* only return address if mapping flags are a subset
     * of the previous flags */
    if ((mem->mmapping_flags & prot) == prot) {
      ret = mem->data;
      mem->mmap_count++;
    }

    goto out;
  }

  if (mem->fd != -1) {
    gint flags;

    flags =
        (mem->flags & GST_FD_MEMORY_FLAG_MAP_PRIVATE) ? MAP_PRIVATE :
        MAP_SHARED;

    mem->data = mmap (0, gmem->maxsize, prot, flags, mem->fd, 0);
    if (mem->data == MAP_FAILED) {
      mem->data = NULL;
      GST_ERROR ("%p: fd %d: mmap failed: %s", mem, mem->fd,
          g_strerror (errno));
      goto out;
    }
  }

  GST_DEBUG ("%p: fd %d: mapped %p", mem, mem->fd, mem->data);

  if (mem->data) {
    mem->mmapping_flags = prot;
    mem->mmap_count++;
    ret = mem->data;
  }

out:
  g_mutex_unlock (&mem->lock);
  return ret;
#else /* !HAVE_MMAP */
  return FALSE;
#endif
}

static void
gst_fd_mem_unmap (GstMemory * gmem)
{
#ifdef HAVE_MMAP
  GstFdMemory *mem = (GstFdMemory *) gmem;

  if (gmem->parent)
    return gst_fd_mem_unmap (gmem->parent);

  if (mem->flags & GST_FD_MEMORY_FLAG_KEEP_MAPPED)
    return;

  g_mutex_lock (&mem->lock);
  if (mem->data && !(--mem->mmap_count)) {
    munmap ((void *) mem->data, gmem->maxsize);
    mem->data = NULL;
    mem->mmapping_flags = 0;
    GST_DEBUG ("%p: fd %d unmapped", mem, mem->fd);
  }
  g_mutex_unlock (&mem->lock);
#endif
}

static GstMemory *
gst_fd_mem_share (GstMemory * gmem, gssize offset, gssize size)
{
#ifdef HAVE_MMAP
  GstFdMemory *mem = (GstFdMemory *) gmem;
  GstFdMemory *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = gmem->maxsize - offset;

  sub = g_slice_new0 (GstFdMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  sub->fd = mem->fd;
  g_mutex_init (&sub->lock);

  return GST_MEMORY_CAST (sub);
#else /* !HAVE_MMAP */
  return NULL;
#endif
}

/**
 * gst_fd_memory_new:
 * @allocator: allocator to be used for this memory
 * @fd: file descriptor
 * @size: memory size
 * @flags: extra #GstFdMemoryFlags
 *
 * Return a %GstMemory that wraps a file descriptor.
 *
 * Returns: (transfer full): a GstMemory based on @allocator.
 * When the buffer is released, @fd is closed.
 * The memory is only mmapped on gst_buffer_mmap() request.
 *
 * Since: 1.2
 */
GstMemory *
__gst_fd_memory_new (GstAllocator * allocator, gint fd, gsize size,
    GstFdMemoryFlags flags)
{
#ifdef HAVE_MMAP
  GstFdMemory *mem;

  mem = g_slice_new0 (GstFdMemory);
  gst_memory_init (GST_MEMORY_CAST (mem), 0, allocator, NULL, size, 0, 0, size);

  mem->flags = flags;
  mem->fd = fd;
  g_mutex_init (&mem->lock);

  GST_DEBUG ("%p: fd: %d size %" G_GSIZE_FORMAT, mem, mem->fd,
      mem->mem.maxsize);

  return (GstMemory *) mem;
#else /* !HAVE_MMAP */
  return NULL;
#endif
}

/**
 * gst_fd_memory_class_init_allocator:
 * @allocator: a #GstAllocatorClass
 *
 * Sets up the methods to alloc and free fd backed memory created
 * with @gst_fd_memory_new by @allocator.
 */
void
__gst_fd_memory_class_init_allocator (GstAllocatorClass * allocator)
{
  allocator->alloc = NULL;
  allocator->free = gst_fd_mem_free;
}

/**
 * gst_fd_memory_init_allocator:
 * @allocator: a #GstAllocator
 * @type: the memory type
 *
 * Sets up the methods to map and unmap and share fd backed memory
 * created with @allocator.
 */
void
__gst_fd_memory_init_allocator (GstAllocator * allocator, const gchar * type)
{
  allocator->mem_type = type;
  allocator->mem_map = gst_fd_mem_map;
  allocator->mem_unmap = gst_fd_mem_unmap;
  allocator->mem_share = gst_fd_mem_share;
}

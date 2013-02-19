/* GStreamer dmabuf allocator
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

#include "gstdmabuf.h"

/**
 * SECTION:gstdmabuf
 * @short_description: Memory wrapper for Linux dmabuf memory
 * @see_also: #GstMemory
 *
 * Since: 1.2
 */

#ifdef HAVE_MMAP
#include <sys/mman.h>
#include <unistd.h>

/*
 * GstDmaBufMemory
 * @fd: the file descriptor associated this memory
 * @data: mmapped address
 * @mmapping_flags: mmapping flags
 * @mmap_count: mmapping counter
 * @lock: a mutex to make mmapping thread safe
 */
typedef struct
{
  GstMemory mem;

  gint fd;
  gpointer data;
  gint mmapping_flags;
  gint mmap_count;
  GMutex lock;
} GstDmaBufMemory;

#define ALLOCATOR_NAME "dmabuf"

GST_DEBUG_CATEGORY_STATIC (dmabuf_debug);
#define GST_CAT_DEFAULT dmabuf_debug

static GstMemory *
gst_dmabuf_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning ("Use dmabuf_mem_alloc() to allocate from this allocator");

  return NULL;
}

static void
gst_dmabuf_free (GstAllocator * allocator, GstMemory * mem)
{
  GstDmaBufMemory *dbmem = (GstDmaBufMemory *) mem;

  if (dbmem->data)
    g_warning ("Freeing memory still mapped");

  close (dbmem->fd);
  g_mutex_clear (&dbmem->lock);
  g_slice_free (GstDmaBufMemory, dbmem);
  GST_DEBUG ("%p: freed", dbmem);
}

static gpointer
gst_dmabuf_mem_map (GstDmaBufMemory * mem, gsize maxsize, GstMapFlags flags)
{
  gint prot;
  gpointer ret = NULL;

  g_mutex_lock (&mem->lock);

  prot = flags & GST_MAP_READ ? PROT_READ : 0;
  prot |= flags & GST_MAP_WRITE ? PROT_WRITE : 0;

  /* do not mmap twice the buffer */
  if (mem->data) {
    /* only return address if mapping flags are a subset
     * of the previous flags */
    if (mem->mmapping_flags & prot)
      ret = mem->data;

    goto out;
  }

  if (mem->fd != -1)
    mem->data = mmap (0, maxsize, prot, MAP_SHARED, mem->fd, 0);

  GST_DEBUG ("%p: fd %d: mapped %p", mem, mem->fd, mem->data);

  if (mem->data) {
    mem->mmapping_flags = prot;
    mem->mem.size = maxsize;
    mem->mmap_count++;
    ret = mem->data;
  }

out:
  g_mutex_unlock (&mem->lock);
  return ret;
}

static gboolean
gst_dmabuf_mem_unmap (GstDmaBufMemory * mem)
{
  g_mutex_lock (&mem->lock);

  if (mem->data && !(--mem->mmap_count)) {
    munmap ((void *) mem->data, mem->mem.size);
    mem->data = NULL;
    mem->mem.size = 0;
    mem->mmapping_flags = 0;
    GST_DEBUG ("%p: fd %d unmapped", mem, mem->fd);
  }
  g_mutex_unlock (&mem->lock);
  return TRUE;
}

static GstDmaBufMemory *
gst_dmabuf_mem_share (GstDmaBufMemory * mem, gssize offset, gsize size)
{
  GstDmaBufMemory *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  sub = g_slice_new (GstDmaBufMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  return sub;
}

static GstDmaBufMemory *
gst_dmabuf_mem_copy (GstDmaBufMemory * mem, gssize offset, gsize size)
{
  gint newfd = dup (mem->fd);

  if (newfd == -1) {
    GST_WARNING ("Can't duplicate dmabuf file descriptor");
    return NULL;
  }

  GST_DEBUG ("%p: copy %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);
  return (GstDmaBufMemory *) gst_dmabuf_allocator_alloc (mem->mem.allocator,
      newfd, size);
}

typedef struct
{
  GstAllocator parent;
} dmabuf_mem_Allocator;

typedef struct
{
  GstAllocatorClass parent_class;
} dmabuf_mem_AllocatorClass;

GType dmabuf_mem_allocator_get_type (void);
G_DEFINE_TYPE (dmabuf_mem_Allocator, dmabuf_mem_allocator, GST_TYPE_ALLOCATOR);

#define GST_TYPE_DMABUF_ALLOCATOR   (dmabuf_mem_allocator_get_type())
#define GST_IS_DMABUF_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DMABUF_ALLOCATOR))

static void
dmabuf_mem_allocator_class_init (dmabuf_mem_AllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_dmabuf_alloc;
  allocator_class->free = gst_dmabuf_free;
}

static void
dmabuf_mem_allocator_init (dmabuf_mem_Allocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = ALLOCATOR_NAME;
  alloc->mem_map = (GstMemoryMapFunction) gst_dmabuf_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) gst_dmabuf_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) gst_dmabuf_mem_share;
  alloc->mem_copy = (GstMemoryCopyFunction) gst_dmabuf_mem_copy;
}

static void
gst_dmabuf_mem_init (void)
{
  GstAllocator *allocator =
      g_object_new (dmabuf_mem_allocator_get_type (), NULL);
  gst_allocator_register (ALLOCATOR_NAME, allocator);

  GST_DEBUG_CATEGORY_INIT (dmabuf_debug, "dmabuf", 0, "dmabuf memory");
}

/**
 * gst_dmabuf_allocator_obtain:
 *
 * Return a dmabuf allocator.
 *
 * Returns: (transfer full): a dmabuf allocator, or NULL if the allocator
 *    isn't available. Use gst_object_unref() to release the allocator after
 *    usage
 *
 * Since: 1.2
 */
GstAllocator *
gst_dmabuf_allocator_obtain (void)
{
  static GOnce dmabuf_allocator_once = G_ONCE_INIT;
  GstAllocator *allocator;

  g_once (&dmabuf_allocator_once, (GThreadFunc) gst_dmabuf_mem_init, NULL);

  allocator = gst_allocator_find (ALLOCATOR_NAME);
  if (!allocator)
    GST_WARNING ("No allocator named %s found", ALLOCATOR_NAME);
  return allocator;
}

/**
 * gst_dmabuf_allocator_alloc:
 * @allocator: (allow-none): allocator to be used for this memory
 * @fd: dmabuf file descriptor
 * @size: memory size
 *
 * Return a %GstMemory that wraps a dmabuf file descriptor.
 *
 * Returns: (transfer full): a GstMemory based on @allocator.
 * When the buffer will be released dmabuf allocator will close the @fd.
 * The memory is only mmapped on gst_buffer_mmap() request.
 *
 * Since: 1.2
 */
GstMemory *
gst_dmabuf_allocator_alloc (GstAllocator * allocator, gint fd, gsize size)
{
  GstDmaBufMemory *mem;

  if (!allocator) {
    allocator = gst_dmabuf_allocator_obtain ();
  }

  if (!GST_IS_DMABUF_ALLOCATOR (allocator)) {
    GST_WARNING ("it isn't the correct allocator for dmabuf");
    return NULL;
  }

  GST_DEBUG ("alloc from allocator %p", allocator);

  mem = g_slice_new (GstDmaBufMemory);

  gst_memory_init (GST_MEMORY_CAST (mem), 0, allocator, NULL, size, 0, 0, 0);

  mem->fd = fd;
  mem->data = NULL;
  mem->mmapping_flags = 0;
  mem->mmap_count = 0;
  g_mutex_init (&mem->lock);

  GST_DEBUG ("%p: fd: %d size %d", mem, mem->fd, mem->mem.maxsize);

  return (GstMemory *) mem;
}

/**
 * gst_dmabuf_memory_get_fd:
 * @mem: the memory to get the file descriptor
 *
 * Return the file descriptor associated with @mem.
 *
 * Returns: the file descriptor associated with the memory, or -1
 *
 * Since: 1.2
 */
gint
gst_dmabuf_memory_get_fd (GstMemory * mem)
{
  GstDmaBufMemory *dbmem = (GstDmaBufMemory *) mem;

  g_return_val_if_fail (gst_is_dmabuf_memory (mem), -1);

  return dbmem->fd;
}

/**
 * gst_is_dmabuf_memory:
 * @mem: the memory to be check
 *
 * Check if @mem is dmabuf memory.
 *
 * Returns: %TRUE if @mem is dmabuf memory, otherwise %FALSE
 *
 * Since: 1.2
 */
gboolean
gst_is_dmabuf_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return g_strcmp0 (mem->allocator->mem_type, ALLOCATOR_NAME) == 0;
}

#else /* !HAVE_MMAP */

GstAllocator *
gst_dmabuf_allocator_obtain (void)
{
  return NULL;
}

GstMemory *
gst_dmabuf_allocator_alloc (GstAllocator * allocator, gint fd, gsize size)
{
  return NULL;
}

gint
gst_dmabuf_memory_get_fd (GstMemory * mem)
{
  return -1;
}

gboolean
gst_is_dmabuf_memory (GstMemory * mem)
{
  return FALSE;
}

#endif /* HAVE_MMAP */

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfdmemory.h"
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
#endif

GST_DEBUG_CATEGORY_STATIC (dmabuf_debug);
#define GST_CAT_DEFAULT dmabuf_debug

typedef struct
{
  GstFdAllocator parent;
} GstDmaBufAllocator;

typedef struct
{
  GstFdAllocatorClass parent_class;
} GstDmaBufAllocatorClass;

GType dmabuf_mem_allocator_get_type (void);
G_DEFINE_TYPE (GstDmaBufAllocator, dmabuf_mem_allocator, GST_TYPE_FD_ALLOCATOR);

#define GST_TYPE_DMABUF_ALLOCATOR   (dmabuf_mem_allocator_get_type())
#define GST_IS_DMABUF_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DMABUF_ALLOCATOR))

static void
dmabuf_mem_allocator_class_init (GstDmaBufAllocatorClass * klass)
{
}

static void
dmabuf_mem_allocator_init (GstDmaBufAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_ALLOCATOR_DMABUF;
}

/**
 * gst_dmabuf_allocator_new:
 *
 * Return a new dmabuf allocator.
 *
 * Returns: (transfer full): a new dmabuf allocator, or NULL if the allocator
 *    isn't available. Use gst_object_unref() to release the allocator after
 *    usage
 *
 * Since: 1.2
 */
GstAllocator *
gst_dmabuf_allocator_new (void)
{
  GST_DEBUG_CATEGORY_INIT (dmabuf_debug, "dmabuf", 0, "dmabuf memory");

  return g_object_new (GST_TYPE_DMABUF_ALLOCATOR, NULL);
}

/**
 * gst_dmabuf_allocator_alloc:
 * @allocator: allocator to be used for this memory
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
  g_return_val_if_fail (GST_IS_DMABUF_ALLOCATOR (allocator), NULL);

  return gst_fd_allocator_alloc (allocator, fd, size, GST_FD_MEMORY_FLAG_NONE);
}

/**
 * gst_dmabuf_memory_get_fd:
 * @mem: the memory to get the file descriptor
 *
 * Return the file descriptor associated with @mem.
 *
 * Returns: the file descriptor associated with the memory, or -1.  The file
 *     descriptor is still owned by the GstMemory.  Use dup to take a copy
 *     if you intend to use it beyond the lifetime of this GstMemory.
 *
 * Since: 1.2
 */
gint
gst_dmabuf_memory_get_fd (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_dmabuf_memory (mem), -1);

  return gst_fd_memory_get_fd (mem);
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
  return gst_memory_is_type (mem, GST_ALLOCATOR_DMABUF);
}

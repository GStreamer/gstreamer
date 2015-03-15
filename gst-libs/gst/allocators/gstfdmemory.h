/* GStreamer fd memory
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
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_FD_MEMORY_H__
#define __GST_FD_MEMORY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/*
 * GstFdfMemory
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
} GstFdMemory;

void        __gst_fd_memory_class_init_allocator  (GstAllocatorClass * allocator);
void        __gst_fd_memory_init_allocator        (GstAllocator * allocator, const gchar *type);

GstMemory * __gst_fd_memory_new                   (GstAllocator * allocator, gint fd, gsize size);


G_END_DECLS
#endif /* __GST_FD_MEMORY_H__ */

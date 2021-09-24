/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.be>
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

#include "my-vidmem.h"

static GstAllocator *_my_allocator;

typedef struct
{
  GstMemory mem;

  guint format;
  guint width;
  guint height;
  gpointer data;

} MyVidmem;


static GstMemory *
_my_alloc (GstAllocator * allocator, gsize size, GstAllocationParams * params)
{
  g_warning ("Use my_vidmem_alloc() to allocate from this allocator");

  return NULL;
}

static void
_my_free (GstAllocator * allocator, GstMemory * mem)
{
  MyVidmem *vmem = (MyVidmem *) mem;

  g_free (vmem->data);
  g_slice_free (MyVidmem, vmem);
  GST_DEBUG ("%p: freed", vmem);
}

static gpointer
_my_vidmem_map (MyVidmem * mem, gsize maxsize, GstMapFlags flags)
{
  gpointer res;

  while (TRUE) {
    if ((res = g_atomic_pointer_get (&mem->data)) != NULL)
      break;

    res = g_malloc (maxsize);

    if (g_atomic_pointer_compare_and_exchange (&mem->data, NULL, res))
      break;

    g_free (res);
  }

  GST_DEBUG ("%p: mapped %p", mem, res);

  return res;
}

static gboolean
_my_vidmem_unmap (MyVidmem * mem)
{
  GST_DEBUG ("%p: unmapped", mem);
  return TRUE;
}

static MyVidmem *
_my_vidmem_share (MyVidmem * mem, gssize offset, gsize size)
{
  MyVidmem *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  sub = g_slice_new (MyVidmem);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  /* install pointer */
  sub->data = _my_vidmem_map (mem, mem->mem.maxsize, GST_MAP_READ);

  return sub;
}

typedef struct
{
  GstAllocator parent;
} MyVidmemAllocator;

typedef struct
{
  GstAllocatorClass parent_class;
} MyVidmemAllocatorClass;

GType my_vidmem_allocator_get_type (void);
G_DEFINE_TYPE (MyVidmemAllocator, my_vidmem_allocator, GST_TYPE_ALLOCATOR);

static void
my_vidmem_allocator_class_init (MyVidmemAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _my_alloc;
  allocator_class->free = _my_free;
}

static void
my_vidmem_allocator_init (MyVidmemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "MyVidmem";
  alloc->mem_map = (GstMemoryMapFunction) _my_vidmem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _my_vidmem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) _my_vidmem_share;
}

void
my_vidmem_init (void)
{
  _my_allocator = g_object_new (my_vidmem_allocator_get_type (), NULL);

  gst_allocator_register ("MyVidmem", gst_object_ref (_my_allocator));
}

GstMemory *
my_vidmem_alloc (guint format, guint width, guint height)
{
  MyVidmem *mem;
  gsize maxsize;

  GST_DEBUG ("alloc frame format %u %ux%u", format, width, height);

  maxsize = (GST_ROUND_UP_4 (width) * height);

  mem = g_slice_new (MyVidmem);

  gst_memory_init (GST_MEMORY_CAST (mem), 0, _my_allocator, NULL,
      maxsize, 31, 0, maxsize);

  mem->format = format;
  mem->width = width;
  mem->height = height;
  mem->data = NULL;

  return (GstMemory *) mem;
}

gboolean
my_is_vidmem (GstMemory * mem)
{
  return mem->allocator == _my_allocator;
}

void
my_vidmem_get_format (GstMemory * mem, guint * format, guint * width,
    guint * height)
{
  MyVidmem *vmem = (MyVidmem *) mem;

  *format = vmem->format;
  *width = vmem->width;
  *height = vmem->height;
}

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

#include "my-memory.h"

typedef struct
{
  GstMemory mem;

  gpointer data;

} MyMemory;


static GstMemory *
_my_alloc (GstAllocator * allocator, gsize size, GstAllocationParams * params)
{
  MyMemory *mem;
  gsize maxsize = size + params->prefix + params->padding;

  GST_DEBUG ("alloc from allocator %p", allocator);

  mem = g_slice_new (MyMemory);

  gst_memory_init (GST_MEMORY_CAST (mem), params->flags, allocator, NULL,
      maxsize, params->align, params->prefix, size);

  mem->data = NULL;

  return (GstMemory *) mem;
}

static void
_my_free (GstAllocator * allocator, GstMemory * mem)
{
  MyMemory *mmem = (MyMemory *) mem;

  g_free (mmem->data);
  g_slice_free (MyMemory, mmem);
  GST_DEBUG ("%p: freed", mmem);
}

static gpointer
_my_mem_map (MyMemory * mem, gsize maxsize, GstMapFlags flags)
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
_my_mem_unmap (MyMemory * mem)
{
  GST_DEBUG ("%p: unmapped", mem);
  return TRUE;
}

static MyMemory *
_my_mem_share (MyMemory * mem, gssize offset, gsize size)
{
  MyMemory *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT " %" G_GSIZE_FORMAT, mem, offset,
      size);

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  sub = g_slice_new (MyMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, mem->mem.offset + offset, size);

  /* install pointer */
  sub->data = _my_mem_map (mem, mem->mem.maxsize, GST_MAP_READ);

  return sub;
}

typedef struct
{
  GstAllocator parent;
} MyMemoryAllocator;

typedef struct
{
  GstAllocatorClass parent_class;
} MyMemoryAllocatorClass;

GType my_memory_allocator_get_type (void);
G_DEFINE_TYPE (MyMemoryAllocator, my_memory_allocator, GST_TYPE_ALLOCATOR);

static void
my_memory_allocator_class_init (MyMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _my_alloc;
  allocator_class->free = _my_free;
}

static void
my_memory_allocator_init (MyMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "MyMemory";
  alloc->mem_map = (GstMemoryMapFunction) _my_mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _my_mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) _my_mem_share;
}

void
my_memory_init (void)
{
  GstAllocator *allocator;

  allocator = g_object_new (my_memory_allocator_get_type (), NULL);

  gst_allocator_register ("MyMemory", allocator);
}

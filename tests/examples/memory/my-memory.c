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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "my-memory.h"

static GstAllocator *_my_allocator;

typedef struct
{
  GstMemory mem;

  gpointer data;

} MyMemory;


static GstMemory *
_my_alloc_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params, gpointer user_data)
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

static void
_my_mem_free (MyMemory * mem)
{
  g_free (mem->data);
  g_slice_free (MyMemory, mem);
  GST_DEBUG ("%p: freed", mem);
}

static MyMemory *
_my_mem_share (MyMemory * mem, gssize offset, gsize size)
{
  MyMemory *sub;
  GstMemory *parent;

  GST_DEBUG ("%p: share %" G_GSSIZE_FORMAT, G_GSIZE_FORMAT, mem, offset, size);

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

static void
free_allocator (GstMiniObject * obj)
{
  g_slice_free (GstAllocator, (GstAllocator *) obj);
}

void
my_memory_init (void)
{
  static const GstMemoryInfo info = {
    "MyMemory",
    (GstAllocatorAllocFunction) _my_alloc_alloc,
    (GstMemoryMapFunction) _my_mem_map,
    (GstMemoryUnmapFunction) _my_mem_unmap,
    (GstMemoryFreeFunction) _my_mem_free,
    (GstMemoryCopyFunction) NULL,
    (GstMemoryShareFunction) _my_mem_share,
    (GstMemoryIsSpanFunction) NULL,
  };

  _my_allocator = g_slice_new (GstAllocator);
  gst_allocator_init (_my_allocator, 0, &info, free_allocator);

  gst_allocator_register ("MyMemory", gst_allocator_ref (_my_allocator));
}

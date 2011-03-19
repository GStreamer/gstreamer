/* GStreamer
 * Copyright (C) 2011 Wim Taymans <wim.taymans@gmail.be>
 *
 * gstmemory.c: memory block handling
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

#include "config.h"

#include "gst_private.h"

#include "gstmemory.h"

struct _GstMemoryImpl
{
  GQuark name;

  GstMemoryInfo info;
};

typedef struct
{
  GstMemory mem;
  gsize slice_size;
  guint8 *data;
  GFreeFunc free_func;
  gsize maxsize;
  gsize offset;
  gsize size;
} GstMemoryDefault;

static const GstMemoryImpl *_default_mem_impl;
static const GstMemoryImpl *_default_sub_impl;

static void
_default_mem_init (GstMemoryDefault * mem, GstMemory * parent,
    gsize slice_size, gpointer data, GFreeFunc free_func,
    gsize maxsize, gsize offset, gsize size)
{
  mem->mem.refcount = 1;
  mem->mem.impl = data ? _default_mem_impl : _default_sub_impl;
  mem->mem.parent = parent ? gst_memory_ref (parent) : NULL;
  mem->slice_size = slice_size;
  mem->data = data;
  mem->free_func = free_func;
  mem->maxsize = maxsize;
  mem->offset = offset;
  mem->size = size;
}

static GstMemoryDefault *
_default_mem_new (GstMemory * parent, gpointer data,
    GFreeFunc free_func, gsize maxsize, gsize offset, gsize size)
{
  GstMemoryDefault *mem;

  mem = g_slice_new (GstMemoryDefault);
  _default_mem_init (mem, parent, sizeof (GstMemoryDefault),
      data, free_func, maxsize, offset, size);

  return mem;
}

static gsize
_default_mem_get_sizes (GstMemoryDefault * mem, gsize * maxsize)
{
  if (maxsize)
    *maxsize = mem->maxsize;

  return mem->size;
}

static void
_default_mem_trim (GstMemoryDefault * mem, gsize offset, gsize size)
{
  g_return_if_fail (size + mem->offset + offset > mem->maxsize);

  mem->offset += offset;
  mem->size = size;
}

static gpointer
_default_mem_map (GstMemoryDefault * mem, gsize * size, gsize * maxsize,
    GstMapFlags flags)
{
  if (size)
    *size = mem->size;
  if (maxsize)
    *maxsize = mem->maxsize;

  return mem->data + mem->offset;
}

static gpointer
_default_sub_map (GstMemoryDefault * mem, gsize * size, gsize * maxsize,
    GstMapFlags flags)
{
  guint8 *data;

  data = gst_memory_map (mem->mem.parent, size, maxsize, flags);

  if (size)
    *size = mem->size;
  if (maxsize)
    *maxsize -= mem->offset;

  return data + mem->offset;
}

static gboolean
_default_mem_unmap (GstMemoryDefault * mem, gpointer data, gsize size)
{
  mem->size = size;
  return TRUE;
}

static gboolean
_default_sub_unmap (GstMemoryDefault * mem, gpointer data, gsize size)
{
  gboolean res;
  guint8 *ptr = data;

  mem->size = size;

  res =
      gst_memory_unmap (mem->mem.parent, ptr - mem->offset, size + mem->offset);

  return res;
}

static void
_default_mem_free (GstMemoryDefault * mem)
{
  if (mem->mem.parent)
    gst_memory_unref (mem->mem.parent);

  if (mem->free_func)
    mem->free_func (mem->data);

  g_slice_free1 (mem->slice_size, mem);
}

static GstMemoryDefault *
_default_mem_copy (GstMemoryDefault * mem)
{
  GstMemoryDefault *copy;
  gpointer data;

  data = g_memdup (mem->data, mem->maxsize);
  copy = _default_mem_new (NULL, data, g_free, mem->maxsize,
      mem->offset, mem->size);

  return copy;
}

static void
_default_mem_extract (GstMemoryDefault * mem, gsize offset, gpointer dest,
    gsize size)
{
  g_return_if_fail (size + mem->offset + offset > mem->maxsize);

  memcpy (dest, mem->data + mem->offset + offset, size);
}

static GstMemoryDefault *
_default_mem_sub (GstMemoryDefault * mem, gsize offset, gsize size)
{
  GstMemoryDefault *sub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  sub = _default_mem_new (parent, mem->data, NULL, mem->maxsize,
      mem->offset + offset, size);

  return sub;
}

static gboolean
_default_mem_is_span (GstMemoryDefault * mem1, GstMemoryDefault * mem2)
{
  /* need to have the same implementation */
  if (mem1->mem.impl != mem2->mem.impl)
    return FALSE;

  /* need to have the same parent */
  if (mem1->mem.parent == NULL || mem1->mem.parent != mem2->mem.parent)
    return FALSE;

  /* and memory is contiguous */
  if (mem1->data + mem1->offset + mem1->size != mem2->data + mem2->offset)
    return FALSE;

  return TRUE;
}

static GstMemoryDefault *
_default_mem_span (GstMemoryDefault * mem1, gsize offset,
    GstMemoryDefault * mem2, gsize size)
{
  GstMemoryDefault *span;

  if (_default_mem_is_span (mem1, mem2)) {
    GstMemoryDefault *parent = (GstMemoryDefault *) mem1->mem.parent;

    span =
        _default_mem_sub (parent, mem1->offset - parent->offset + offset, size);
  } else {
    guint8 *data;
    gsize len1;

    data = g_malloc (size);
    len1 = mem1->size - offset;

    memcpy (data, mem1->data + mem1->offset + offset, len1);
    memcpy (data + len1, mem2->data + mem2->offset, size - len1);

    span = _default_mem_new (NULL, data, g_free, size, 0, size);
  }
  return span;
}

static void
_fallback_extract (GstMemory * mem, gsize offset, gpointer dest, gsize size)
{
  guint8 *data;
  gsize msize;

  data = gst_memory_map (mem, &msize, NULL, GST_MAP_READ);
  memcpy (dest, data + offset, size);
  gst_memory_unmap (mem, data, msize);
}

static GstMemory *
_fallback_copy (GstMemory * mem)
{
  GstMemoryDefault *copy;
  gpointer data, cdata;
  gsize size;

  data = gst_memory_map (mem, &size, NULL, GST_MAP_READ);
  cdata = g_memdup (data, size);
  gst_memory_unmap (mem, data, size);

  copy = _default_mem_new (NULL, cdata, g_free, size, 0, size);

  return (GstMemory *) copy;
}

static GstMemory *
_fallback_sub (GstMemory * mem, gsize offset, gsize size)
{
  GstMemoryDefault *sub;
  GstMemory *parent;

  /* find the real parent */
  parent = mem->parent ? mem->parent : mem;

  sub = _default_mem_new (parent, NULL, NULL, size, offset, size);

  return (GstMemory *) sub;
}

static gboolean
_fallback_is_span (GstMemory * mem1, GstMemory * mem2)
{
  return FALSE;
}

static GstMemory *
_fallback_span (GstMemory * mem1, gsize offset, GstMemory * mem2, gsize size)
{
  GstMemoryDefault *span;
  guint8 *data, *dest;
  gsize ssize, len1;

  dest = g_malloc (size);

  data = gst_memory_map (mem1, &ssize, NULL, GST_MAP_READ);
  len1 = ssize - offset;
  memcpy (dest, data + offset, len1);
  gst_memory_unmap (mem1, data, size);

  data = gst_memory_map (mem2, &ssize, NULL, GST_MAP_READ);
  memcpy (dest + len1, data, ssize - len1);
  gst_memory_unmap (mem2, data, size);

  span = _default_mem_new (NULL, dest, g_free, size, 0, size);

  return (GstMemory *) span;
}

const GstMemoryImpl *
gst_memory_register (const gchar * name, const GstMemoryInfo * info)
{
  GstMemoryImpl *impl;

#define INSTALL_FALLBACK(_t) \
  if (impl->info._t == NULL) impl->info._t = _fallback_ ##_t;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->get_sizes != NULL, NULL);
  g_return_val_if_fail (info->trim != NULL, NULL);
  g_return_val_if_fail (info->map != NULL, NULL);
  g_return_val_if_fail (info->unmap != NULL, NULL);
  g_return_val_if_fail (info->free != NULL, NULL);

  impl = g_slice_new (GstMemoryImpl);
  impl->name = g_quark_from_string (name);
  impl->info = *info;
  INSTALL_FALLBACK (copy);
  INSTALL_FALLBACK (extract);
  INSTALL_FALLBACK (sub);
  INSTALL_FALLBACK (is_span);
  INSTALL_FALLBACK (span);

  GST_DEBUG ("register \"%s\" of size %" G_GSIZE_FORMAT, name);

#if 0
  g_static_rw_lock_writer_lock (&lock);
  g_hash_table_insert (memoryimpl, (gpointer) name, (gpointer) impl);
  g_static_rw_lock_writer_unlock (&lock);
#endif
#undef INSTALL_FALLBACK

  return impl;
}

void
_gst_memory_init (void)
{
  static const GstMemoryInfo _mem_info = {
    (GstMemoryGetSizesFunction) _default_mem_get_sizes,
    (GstMemoryTrimFunction) _default_mem_trim,
    (GstMemoryMapFunction) _default_mem_map,
    (GstMemoryUnmapFunction) _default_mem_unmap,
    (GstMemoryFreeFunction) _default_mem_free,
    (GstMemoryCopyFunction) _default_mem_copy,
    (GstMemoryExtractFunction) _default_mem_extract,
    (GstMemorySubFunction) _default_mem_sub,
    (GstMemoryIsSpanFunction) _default_mem_is_span,
    (GstMemorySpanFunction) _default_mem_span
  };
  static const GstMemoryInfo _sub_info = {
    (GstMemoryGetSizesFunction) _default_mem_get_sizes,
    (GstMemoryTrimFunction) _default_mem_trim,
    (GstMemoryMapFunction) _default_sub_map,
    (GstMemoryUnmapFunction) _default_sub_unmap,
    (GstMemoryFreeFunction) _default_mem_free,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  };

  _default_mem_impl = gst_memory_register ("GstMemoryDefault", &_mem_info);
  _default_sub_impl = gst_memory_register ("GstMemorySubbuffer", &_sub_info);
}

GstMemory *
gst_memory_ref (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, NULL);

  g_atomic_int_inc (&mem->refcount);

  return mem;
}

void
gst_memory_unref (GstMemory * mem)
{
  g_return_if_fail (mem != NULL);

  if (g_atomic_int_dec_and_test (&mem->refcount))
    mem->impl->info.free (mem);
}

gsize
gst_memory_get_sizes (GstMemory * mem, gsize * maxsize)
{
  g_return_val_if_fail (mem != NULL, 0);

  return mem->impl->info.get_sizes (mem, maxsize);
}

gpointer
gst_memory_map (GstMemory * mem, gsize * size, gsize * maxsize,
    GstMapFlags flags)
{
  g_return_val_if_fail (mem != NULL, NULL);

  return mem->impl->info.map (mem, size, maxsize, flags);
}

gboolean
gst_memory_unmap (GstMemory * mem, gpointer data, gsize size)
{
  g_return_val_if_fail (mem != NULL, FALSE);

  return mem->impl->info.unmap (mem, data, size);
}

GstMemory *
gst_memory_copy (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, NULL);

  return mem->impl->info.copy (mem);
}

void
gst_memory_extract (GstMemory * mem, gsize offset, gpointer dest, gsize size)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (dest != NULL);

  return mem->impl->info.extract (mem, offset, dest, size);
}

void
gst_memory_trim (GstMemory * mem, gsize offset, gsize size)
{
  g_return_if_fail (mem != NULL);

  mem->impl->info.trim (mem, offset, size);
}

GstMemory *
gst_memory_sub (GstMemory * mem, gsize offset, gsize size)
{
  g_return_val_if_fail (mem != NULL, NULL);

  return mem->impl->info.sub (mem, offset, size);
}

gboolean
gst_memory_is_span (GstMemory * mem1, GstMemory * mem2)
{
  g_return_val_if_fail (mem1 != NULL, FALSE);
  g_return_val_if_fail (mem2 != NULL, FALSE);

  return mem1->impl->info.is_span (mem1, mem2);
}

GstMemory *
gst_memory_span (GstMemory * mem1, gsize offset, GstMemory * mem2, gsize size)
{
  g_return_val_if_fail (mem1 != NULL, NULL);
  g_return_val_if_fail (mem2 != NULL, NULL);

  return mem1->impl->info.span (mem1, offset, mem2, size);
}

GstMemory *
gst_memory_new_wrapped (gpointer data, GFreeFunc free_func,
    gsize maxsize, gsize offset, gsize size)
{
  GstMemoryDefault *mem;

  mem = _default_mem_new (NULL, data, free_func, maxsize, offset, size);

  return (GstMemory *) mem;
}

GstMemory *
gst_memory_new_alloc (gsize maxsize, gsize align)
{
  GstMemoryDefault *mem;
  guint8 *data;
  gsize offset, size;

  /* alloc header and data in one block */
  size = sizeof (GstMemoryDefault) + maxsize + align;

  mem = g_slice_alloc (size);
  if (mem == NULL)
    return NULL;

  data = (guint8 *) mem + sizeof (GstMemoryDefault);

  if ((offset = ((guintptr) data & align)))
    offset = align - offset;

  _default_mem_init (mem, NULL, size, data, NULL, maxsize + align, offset,
      maxsize);

  return (GstMemory *) mem;
}

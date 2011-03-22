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
  mem->mem.impl = data ? _default_mem_impl : _default_sub_impl;
  mem->mem.flags = 0;
  mem->mem.refcount = 1;
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
  gsize slice_size;

  slice_size = sizeof (GstMemoryDefault);

  mem = g_slice_alloc (slice_size);
  _default_mem_init (mem, parent, slice_size,
      data, free_func, maxsize, offset, size);

  return mem;
}

static GstMemoryDefault *
_default_mem_new_block (gsize maxsize, gsize align, gsize offset, gsize size)
{
  GstMemoryDefault *mem;
  gsize aoffset, slice_size;
  guint8 *data;

  /* alloc header and data in one block */
  slice_size = sizeof (GstMemoryDefault) + maxsize + align;

  mem = g_slice_alloc (slice_size);
  if (mem == NULL)
    return NULL;

  data = (guint8 *) mem + sizeof (GstMemoryDefault);

  if ((aoffset = ((guintptr) data & align)))
    aoffset = align - aoffset;

  _default_mem_init (mem, NULL, slice_size, data, NULL, maxsize + align,
      aoffset + offset, size);

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
_default_mem_copy (GstMemoryDefault * mem, gsize offset, gsize size)
{
  GstMemoryDefault *copy;

  copy = _default_mem_new_block (mem->maxsize, 0, mem->offset + offset, size);
  memcpy (copy->data, mem->data, mem->maxsize);

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
_default_mem_is_span (GstMemoryDefault * mem1, GstMemoryDefault * mem2,
    gsize * offset)
{
  if (offset)
    *offset = mem1->offset;

  /* and memory is contiguous */
  return mem1->data + mem1->offset + mem1->size == mem2->data + mem2->offset;
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
_fallback_copy (GstMemory * mem, gsize offset, gsize size)
{
  GstMemoryDefault *copy;
  guint8 *data;
  gsize msize;

  data = gst_memory_map (mem, &msize, NULL, GST_MAP_READ);
  copy = _default_mem_new_block (size, 0, 0, size);
  memcpy (copy->data, data + offset, size);
  gst_memory_unmap (mem, data, msize);

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
_fallback_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
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
    (GstMemoryIsSpanFunction) _default_mem_is_span
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
  g_return_if_fail (mem->impl != NULL);

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
gst_memory_copy (GstMemory * mem, gsize offset, gsize size)
{
  g_return_val_if_fail (mem != NULL, NULL);

  return mem->impl->info.copy (mem, offset, size);
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
gst_memory_is_span (GstMemory ** mem1, gsize len1, GstMemory ** mem2,
    gsize len2, GstMemory ** parent, gsize * offset)
{
  GstMemory *m1, *m2, **arr;
  gsize len, i;
  guint count;
  gboolean have_offset = FALSE;

  g_return_val_if_fail (mem1 != NULL, FALSE);
  g_return_val_if_fail (mem2 != NULL, FALSE);

  arr = mem1;
  len = len1;
  m1 = m2 = NULL;

  for (count = 0; count < 2; count++) {
    gsize offs;

    for (i = 0; i < len; i++) {
      if (m2)
        m1 = m2;
      m2 = arr[i];

      if (m1 && m2) {
        /* need to have the same implementation */
        if (m1->impl != m2->impl)
          return FALSE;

        /* need to have the same parent */
        if (m1->parent == NULL || m1->parent != m2->parent)
          return FALSE;

        /* and memory is contiguous */
        if (!m1->impl->info.is_span (m1, m2, &offs))
          return FALSE;

        if (!have_offset) {
          *offset = offs;
          have_offset = TRUE;
        }
      }
    }
    arr = mem2;
    len = len2;
  }
  if (!have_offset)
    return FALSE;

  return TRUE;
}

GstMemory *
gst_memory_span (GstMemory ** mem1, gsize len1, gsize offset, GstMemory ** mem2,
    gsize len2, gsize size)
{
  GstMemory *span, **mem, *parent;
  guint8 *data, *dest;
  gsize count, ssize, tocopy, len, poffset, i;

  g_return_val_if_fail (mem1 != NULL, NULL);
  g_return_val_if_fail (mem2 != NULL, NULL);

  if (gst_memory_is_span (mem1, len1, mem2, len2, &parent, &poffset)) {
    span = gst_memory_sub (parent, offset + poffset, size);
  } else {
    GstMemoryDefault *tspan;

    tspan = _default_mem_new_block (size, 0, 0, size);
    dest = tspan->data;

    mem = mem1;
    len = len1;

    for (count = 0; count < 2; count++) {
      for (i = 0; i < len && size > 0; i++) {
        data = gst_memory_map (mem[i], &ssize, NULL, GST_MAP_READ);
        tocopy = MIN (ssize, size);
        if (tocopy > offset) {
          memcpy (dest, data + offset, tocopy - offset);
          size -= tocopy;
          dest += tocopy;
          offset = 0;
        } else {
          offset -= tocopy;
        }
        gst_memory_unmap (mem[i], data, ssize);
      }
      mem = mem2;
      len = len2;
    }
    span = (GstMemory *) tspan;
  }
  return span;
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

  mem = _default_mem_new_block (maxsize, align, 0, 0);

  return (GstMemory *) mem;
}

GstMemory *
gst_memory_new_copy (gsize maxsize, gsize align, gpointer data,
    gsize offset, gsize size)
{
  GstMemoryDefault *mem;

  mem = _default_mem_new_block (maxsize, align, offset, size);
  memcpy (mem->data, data, maxsize);

  return (GstMemory *) mem;
}

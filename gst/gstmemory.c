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

  guint8 *data;
  GFreeFunc free_func;
  gpointer free_data;
  gsize maxsize;
  gsize offset;
  gsize size;
} GstMemoryDefault;

static const GstMemoryImpl *_default_memory_impl;

static gsize
_default_get_sizes (GstMemory * mem, gsize * maxsize)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;

  if (maxsize)
    *maxsize = def->maxsize;

  return def->size;
}

static gpointer
_default_map (GstMemory * mem, gsize * size, gsize * maxsize, GstMapFlags flags)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;

  if (size)
    *size = def->size;
  if (maxsize)
    *maxsize = def->maxsize;

  return def->data + def->offset;
}

static gboolean
_default_unmap (GstMemory * mem, gpointer data, gsize size)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;

  def->size = size;

  return TRUE;
}

static void
_default_free (GstMemory * mem)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;

  if (def->free_func)
    def->free_func (def->free_data);
}

static GstMemory *
_default_copy (GstMemory * mem)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;
  GstMemoryDefault *copy;

  copy = g_slice_new (GstMemoryDefault);
  copy->mem.impl = _default_memory_impl;
  copy->data = g_memdup (def->data, def->maxsize);
  copy->free_data = copy->data;
  copy->free_func = g_free;
  copy->maxsize = def->maxsize;
  copy->offset = def->offset;
  copy->size = def->size;

  return (GstMemory *) copy;
}

static void
_default_copy_into (GstMemory * mem, gsize offset, gpointer dest, gsize size)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;

  g_return_if_fail (size + def->offset + offset > def->maxsize);

  memcpy (dest, def->data + def->offset + offset, size);
}

static void
_default_trim (GstMemory * mem, gsize offset, gsize size)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;

  g_return_if_fail (size + def->offset + offset > def->maxsize);

  def->offset += offset;
  def->size = size;
}

static GstMemory *
_default_sub (GstMemory * mem, gsize offset, gsize size)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;
  GstMemoryDefault *sub;

  sub = g_slice_new (GstMemoryDefault);
  sub->mem.impl = _default_memory_impl;
  sub->data = def->data;
  sub->free_data = gst_memory_ref (mem);
  sub->free_func = (GFreeFunc) gst_memory_unref;
  sub->maxsize = def->maxsize;
  sub->offset = def->offset + offset;
  sub->size = size;

  return (GstMemory *) sub;
}

static void
_fallback_copy_into (GstMemory * mem, gsize offset, gpointer dest, gsize size)
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
  GstMemory *copy;
  gpointer data, cdata;
  gsize size;

  data = gst_memory_map (mem, &size, NULL, GST_MAP_READ);
  cdata = g_memdup (data, size);
  gst_memory_unmap (mem, data, size);

  copy = gst_memory_new_wrapped (cdata, g_free, size, 0, size);

  return copy;
}

static GstMemory *
_fallback_sub (GstMemory * mem, gsize offset, gsize size)
{
  GstMemoryDefault *def = (GstMemoryDefault *) mem;
  GstMemoryDefault *sub;

  sub = g_slice_new (GstMemoryDefault);
  sub->mem.impl = _default_memory_impl;
  sub->data = def->data;
  sub->free_data = sub->data;
  sub->free_func = NULL;
  sub->maxsize = def->maxsize;
  sub->offset = def->offset + offset;
  sub->size = size;

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
  INSTALL_FALLBACK (copy_into);
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
  static const GstMemoryInfo info = {
    _default_get_sizes,
    _default_trim,
    _default_map,
    _default_unmap,
    _default_free,
    _default_copy,
    _default_copy_into,
    _default_sub
  };
  _default_memory_impl = gst_memory_register ("GstMemoryDefault", &info);
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
gst_memory_copy_into (GstMemory * mem, gsize offset, gpointer dest, gsize size)
{
  g_return_if_fail (mem != NULL);
  g_return_if_fail (dest != NULL);

  return mem->impl->info.copy_into (mem, offset, dest, size);
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

GstMemory *
gst_memory_new_wrapped (gpointer data, GFreeFunc free_func,
    gsize maxsize, gsize offset, gsize size)
{
  GstMemoryDefault *mem;

  mem = g_slice_new (GstMemoryDefault);
  mem->mem.impl = _default_memory_impl;
  mem->data = data;
  mem->free_data = data;
  mem->free_func = free_func;
  mem->maxsize = maxsize;
  mem->offset = offset;
  mem->size = size;

  return (GstMemory *) mem;
}

GstMemory *
gst_memory_new_alloc (gsize maxsize, gsize align)
{
  GstMemory *mem;
  gpointer data;
  gsize offset;

  data = g_try_malloc (maxsize + align);
  if (data == NULL)
    return NULL;

  if ((offset = ((guintptr) data & align)))
    offset = align - offset;

  mem = gst_memory_new_wrapped (data, g_free, maxsize + align, offset, maxsize);

  return mem;
}

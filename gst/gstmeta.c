/* GStreamer
 * Copyright (C) 2011 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstmeta.c: metadata operations
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

/**
 * SECTION:gstmeta
 * @short_description: Buffer metadata
 *
 * Last reviewed on December 17th, 2009 (0.10.26)
 */
#include "gst_private.h"

#include "gstbuffer.h"
#include "gstmeta.h"
#include "gstinfo.h"
#include "gstutils.h"

static GHashTable *metainfo = NULL;
static GStaticRWLock lock = G_STATIC_RW_LOCK_INIT;

void
_gst_meta_init (void)
{
  metainfo = g_hash_table_new (g_str_hash, g_str_equal);
}

/**
 * gst_meta_register_info:
 * @info: a #GstMetaInfo
 *
 * Register a #GstMetaInfo. The same @info can be retrieved later with
 * gst_meta_get_info() by using @impl as the key.
 *
 * Returns: a #GstMetaInfo that can be used to access metadata.
 */

const GstMetaInfo *
gst_meta_register (const gchar * api, const gchar * impl, gsize size,
    GstMetaInitFunction init_func, GstMetaFreeFunction free_func,
    GstMetaCopyFunction copy_func, GstMetaSubFunction sub_func,
    GstMetaSerializeFunction serialize_func,
    GstMetaDeserializeFunction deserialize_func)
{
  GstMetaInfo *info;

  g_return_val_if_fail (api != NULL, NULL);
  g_return_val_if_fail (impl != NULL, NULL);
  g_return_val_if_fail (size != 0, NULL);

  info = g_slice_new (GstMetaInfo);
  info->api = g_quark_from_string (api);
  info->impl = g_quark_from_string (impl);
  info->size = size;
  info->init_func = init_func;
  info->free_func = free_func;
  info->copy_func = copy_func;
  info->sub_func = sub_func;
  info->serialize_func = serialize_func;
  info->deserialize_func = deserialize_func;

  GST_DEBUG ("register \"%s\" implementing \"%s\" of size %" G_GSIZE_FORMAT,
      api, impl, size);

  g_static_rw_lock_writer_lock (&lock);
  g_hash_table_insert (metainfo, (gpointer) impl, (gpointer) info);
  g_static_rw_lock_writer_unlock (&lock);

  return info;
}

/**
 * gst_meta_get_info:
 * @name: the name
 *
 * Lookup a previously registered meta info structure by its @implementor name.
 *
 * Returns: a #GstMetaInfo with @name or #NULL when no such metainfo
 * exists.
 */
const GstMetaInfo *
gst_meta_get_info (const gchar * impl)
{
  GstMetaInfo *info;

  g_return_val_if_fail (impl != NULL, NULL);

  g_static_rw_lock_reader_lock (&lock);
  info = g_hash_table_lookup (metainfo, impl);
  g_static_rw_lock_reader_unlock (&lock);

  return info;
}

typedef struct
{
  guint8 *data;
  GFreeFunc free_func;
  gsize size;
  gsize offset;
} GstMetaMemoryParams;

typedef struct
{
  GstMetaMemory memory;
  GstMetaMemoryParams params;
} GstMetaMemoryImpl;

static gpointer
meta_memory_mmap (GstMetaMemoryImpl * meta, gsize offset, gsize * size,
    GstMetaMapFlags flags)
{
  *size = meta->params.size - offset;
  return meta->params.data + offset;
}

static gboolean
meta_memory_munmap (GstMetaMemoryImpl * meta, gpointer data, gsize size)
{
  return TRUE;
}

static gboolean
meta_memory_init (GstMetaMemoryImpl * meta, GstMetaMemoryParams * params,
    GstBuffer * buffer)
{
  meta->memory.mmap_func = (GstMetaMapFunc) meta_memory_mmap;
  meta->memory.munmap_func = (GstMetaUnmapFunc) meta_memory_munmap;
  meta->params = *params;
  return TRUE;
}

static void
meta_memory_free (GstMetaMemoryImpl * meta, GstBuffer * buffer)
{
  if (meta->params.free_func)
    meta->params.free_func (meta->params.data);
}

static void
meta_memory_copy (GstBuffer * copy, GstMetaMemoryImpl * meta,
    const GstBuffer * buffer)
{
  gst_buffer_add_meta_memory (copy,
      g_memdup (meta->params.data, meta->params.size),
      g_free, meta->params.size, meta->params.offset);
}

static void
meta_memory_sub (GstBuffer * subbuf, GstMetaMemoryImpl * meta,
    GstBuffer * buffer, guint offset, guint size)
{
  gst_buffer_add_meta_memory (subbuf,
      meta->params.data, NULL, size, meta->params.offset + offset);
}

const GstMetaInfo *
gst_meta_memory_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (meta_info == NULL) {
    meta_info = gst_meta_register ("GstMetaMemory", "GstMetaMemoryImpl",
        sizeof (GstMetaMemoryImpl),
        (GstMetaInitFunction) meta_memory_init,
        (GstMetaFreeFunction) meta_memory_free,
        (GstMetaCopyFunction) meta_memory_copy,
        (GstMetaSubFunction) meta_memory_sub,
        (GstMetaSerializeFunction) NULL, (GstMetaDeserializeFunction) NULL);
  }
  return meta_info;
}

GstMetaMemory *
gst_buffer_add_meta_memory (GstBuffer * buffer, gpointer data,
    GFreeFunc free_func, gsize size, gsize offset)
{
  GstMeta *meta;
  GstMetaMemoryParams params;

  params.data = data;
  params.free_func = free_func;
  params.size = size;
  params.offset = offset;

  meta = gst_buffer_add_meta (buffer, GST_META_MEMORY_INFO, &params);

  return (GstMetaMemory *) meta;
}

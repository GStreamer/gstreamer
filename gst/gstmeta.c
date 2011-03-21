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
    GstMetaTransformFunction transform_func,
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
  info->transform_func = transform_func;
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

/* Memory metadata */
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
meta_memory_mmap (GstMetaMemory * meta, gsize offset, gsize * size,
    GstMetaMapFlags flags)
{
  GstMetaMemoryImpl *impl = (GstMetaMemoryImpl *) meta;

  *size = impl->params.size - offset;
  return impl->params.data + offset;
}

static gboolean
meta_memory_munmap (GstMetaMemory * meta, gpointer data, gsize size)
{
  return TRUE;
}

static gboolean
meta_memory_init (GstMetaMemoryImpl * meta, GstMetaMemoryParams * params,
    GstBuffer * buffer)
{
  GST_DEBUG ("init %p", buffer);
  meta->memory.mmap_func = meta_memory_mmap;
  meta->memory.munmap_func = meta_memory_munmap;
  meta->params = *params;

  /* FIXME, backwards compatibility */
  //GST_BUFFER_DATA (buffer) = params->data + params->offset;
  //GST_BUFFER_SIZE (buffer) = params->size;

  return TRUE;
}

static void
meta_memory_free (GstMetaMemoryImpl * meta, GstBuffer * buffer)
{
  GST_DEBUG ("free buffer %p", buffer);
  if (meta->params.free_func)
    meta->params.free_func (meta->params.data);
}

static void
meta_memory_transform (GstBuffer * transbuf, GstMetaMemoryImpl * meta,
    GstBuffer * buffer, GstMetaTransformData * data)
{
  switch (data->type) {
    case GST_META_TRANSFORM_COPY:
    {
      GST_DEBUG ("copy %p to %p", buffer, transbuf);
      gst_buffer_add_meta_memory (transbuf,
          g_memdup (meta->params.data, meta->params.size),
          g_free, meta->params.size, meta->params.offset);
      break;
    }
    case GST_META_TRANSFORM_TRIM:
    {
      GstMetaTransformSubbuffer *subdata = (GstMetaTransformSubbuffer *) data;

      GST_DEBUG ("trim %p to %p", buffer, transbuf);
      gst_buffer_add_meta_memory (transbuf,
          meta->params.data, NULL, subdata->size,
          meta->params.offset + subdata->offset);
      break;
    }
    case GST_META_TRANSFORM_MAKE_WRITABLE:
    {
      GST_DEBUG ("make writable %p to %p", buffer, transbuf);
      gst_buffer_add_meta_memory (transbuf,
          meta->params.data, NULL, meta->params.size, meta->params.offset);
      break;
    }
    default:
      /* don't copy by default */
      break;
  }
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
        (GstMetaTransformFunction) meta_memory_transform,
        (GstMetaSerializeFunction) NULL, (GstMetaDeserializeFunction) NULL);
  }
  return meta_info;
}

GstMetaMemory *
gst_buffer_add_meta_memory (GstBuffer * buffer, gpointer data,
    GFreeFunc free_func, gsize size, gsize offset)
{
  GstMeta *meta;
  GstMetaMemoryParams params = { data, free_func, size, offset };

  meta = gst_buffer_add_meta (buffer, GST_META_MEMORY_INFO, &params);

  return (GstMetaMemory *) meta;
}

/* Timing metadata */
static void
meta_timing_transform (GstBuffer * transbuf, GstMetaTiming * meta,
    GstBuffer * buffer, GstMetaTransformData * data)
{
  GstMetaTiming *timing;
  guint offset;
  guint size;

  if (data->type == GST_META_TRANSFORM_TRIM) {
    GstMetaTransformSubbuffer *subdata = (GstMetaTransformSubbuffer *) data;
    offset = subdata->offset;
    size = subdata->size;
  } else {
    offset = 0;
    size = gst_buffer_get_size (buffer);
  }

  GST_DEBUG ("trans called from buffer %p to %p, meta %p, %u-%u", buffer,
      transbuf, meta, offset, size);

  timing = gst_buffer_add_meta_timing (transbuf);
  if (offset == 0) {
    /* same offset, copy timestamps */
    timing->pts = meta->pts;
    timing->dts = meta->dts;
    if (size == gst_buffer_get_size (buffer)) {
      /* same size, copy duration */
      timing->duration = meta->duration;
    } else {
      /* else clear */
      timing->duration = GST_CLOCK_TIME_NONE;
    }
  } else {
    timing->pts = -1;
    timing->dts = -1;
    timing->duration = -1;
  }
  timing->clock_rate = meta->clock_rate;
}

const GstMetaInfo *
gst_meta_timing_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (meta_info == NULL) {
    meta_info = gst_meta_register ("GstMetaTiming", "GstMetaTiming",
        sizeof (GstMetaTiming),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL,
        (GstMetaTransformFunction) meta_timing_transform,
        (GstMetaSerializeFunction) NULL, (GstMetaDeserializeFunction) NULL);
  }
  return meta_info;
}

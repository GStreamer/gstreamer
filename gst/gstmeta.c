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
_priv_gst_meta_initialize (void)
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
    GstMetaCopyFunction copy_func, GstMetaTransformFunction transform_func)
{
  GstMetaInfo *info;

  g_return_val_if_fail (api != NULL, NULL);
  g_return_val_if_fail (impl != NULL, NULL);
  g_return_val_if_fail (size != 0, NULL);

  info = g_slice_new (GstMetaInfo);
  info->api = g_quark_from_string (api);
  info->type = g_pointer_type_register_static (impl);
  info->size = size;
  info->init_func = init_func;
  info->free_func = free_func;
  info->copy_func = copy_func;
  info->transform_func = transform_func;

  GST_DEBUG ("register \"%s\" implementing \"%s\" of size %" G_GSIZE_FORMAT,
      api, impl, size);

  g_static_rw_lock_writer_lock (&lock);
  g_hash_table_insert (metainfo, (gpointer) impl, (gpointer) info);
  g_static_rw_lock_writer_unlock (&lock);

  return info;
}

/**
 * gst_meta_get_info:
 * @impl: the name
 *
 * Lookup a previously registered meta info structure by its implementor name
 * @impl.
 *
 * Returns: a #GstMetaInfo with @impl or #NULL when no such metainfo
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

/* Timing metadata */
static void
meta_timing_copy (GstBuffer * copybuf, GstMetaTiming * meta,
    GstBuffer * buffer, gsize offset, gsize size)
{
  GstMetaTiming *timing;

  GST_DEBUG ("trans called from buffer %p to %p, meta %p,"
      "offset %" G_GSIZE_FORMAT ", size %" G_GSIZE_FORMAT, buffer,
      copybuf, meta, offset, size);

  timing = gst_buffer_add_meta_timing (copybuf);
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
        (GstMetaCopyFunction) meta_timing_copy,
        (GstMetaTransformFunction) NULL);
  }
  return meta_info;
}

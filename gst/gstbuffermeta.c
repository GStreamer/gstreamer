/* GStreamer
 * Copyright (C) 2009 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstbuffermeta.c: Buffer metadata operations
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
 * SECTION:gstbuffermeta
 * @short_description: Buffer metadata
 *
 * Last reviewed on December 17th, 2009 (0.10.26)
 */
#include "gst_private.h"

#include "gstbuffermeta.h"
#include "gstbuffer.h"
#include "gstinfo.h"
#include "gstutils.h"

static GHashTable *metainfo = NULL;
static GStaticRWLock lock = G_STATIC_RW_LOCK_INIT;

void
_gst_buffer_meta_init (void)
{
  metainfo = g_hash_table_new (g_str_hash, g_str_equal);
}

/**
 * gst_buffer_meta_register_info:
 * @info: a #GstBufferMetaInfo
 *
 * Register a #GstBufferMetaInfo. The same @info can be retrieved later with
 * gst_buffer_meta_get_info() by using the name as the key. 
 *
 * Returns: a #GstBufferMetaInfo that can be used to access buffer metadata.
 */
const GstBufferMetaInfo *
gst_buffer_meta_register_info (const GstBufferMetaInfo * info)
{
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (info->name != NULL, NULL);
  g_return_val_if_fail (info->size != 0, NULL);

  GST_DEBUG ("register \"%s\" of size %" G_GSIZE_FORMAT, info->name,
      info->size);

  g_static_rw_lock_writer_lock (&lock);
  g_hash_table_insert (metainfo, (gpointer) info->name, (gpointer) info);
  g_static_rw_lock_writer_unlock (&lock);

  return info;
}

/**
 * gst_buffer_meta_get_info:
 * @name: the name
 *
 * Lookup a previously registered meta info structure by its @name.
 *
 * Returns: a #GstBufferMetaInfo with @name or #NULL when no such metainfo
 * exists.
 */
const GstBufferMetaInfo *
gst_buffer_meta_get_info (const gchar * name)
{
  GstBufferMetaInfo *info;

  g_return_val_if_fail (name != NULL, NULL);

  g_static_rw_lock_reader_lock (&lock);
  info = g_hash_table_lookup (metainfo, name);
  g_static_rw_lock_reader_unlock (&lock);

  return info;
}

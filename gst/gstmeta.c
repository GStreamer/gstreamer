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
static GRWLock lock;

GQuark _gst_meta_transform_copy;

void
_priv_gst_meta_initialize (void)
{
  g_rw_lock_init (&lock);
  metainfo = g_hash_table_new (g_str_hash, g_str_equal);

  _gst_meta_transform_copy = g_quark_from_static_string ("copy");
}

/**
 * gst_meta_register:
 * @api: the name of the #GstMeta API
 * @impl: the name of the #GstMeta implementation
 * @size: the size of the #GstMeta structure
 * @init_func: a #GstMetaInitFunction
 * @free_func: a #GstMetaFreeFunction
 * @transform_func: a #GstMetaTransformFunction
 *
 * Register a new #GstMeta implementation.
 *
 * The same @info can be retrieved later with gst_meta_get_info() by using
 * @impl as the key.
 *
 * Returns: (transfer none): a #GstMetaInfo that can be used to access metadata.
 */

const GstMetaInfo *
gst_meta_register (const gchar * api, const gchar * impl, gsize size,
    GstMetaInitFunction init_func, GstMetaFreeFunction free_func,
    GstMetaTransformFunction transform_func)
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
  info->transform_func = transform_func;

  GST_DEBUG ("register \"%s\" implementing \"%s\" of size %" G_GSIZE_FORMAT,
      api, impl, size);

  g_rw_lock_writer_lock (&lock);
  g_hash_table_insert (metainfo, (gpointer) impl, (gpointer) info);
  g_rw_lock_writer_unlock (&lock);

  return info;
}

/**
 * gst_meta_get_info:
 * @impl: the name
 *
 * Lookup a previously registered meta info structure by its implementation name
 * @impl.
 *
 * Returns: (transfer none): a #GstMetaInfo with @impl, or #NULL when no such
 * metainfo exists.
 */
const GstMetaInfo *
gst_meta_get_info (const gchar * impl)
{
  GstMetaInfo *info;

  g_return_val_if_fail (impl != NULL, NULL);

  g_rw_lock_reader_lock (&lock);
  info = g_hash_table_lookup (metainfo, impl);
  g_rw_lock_reader_unlock (&lock);

  return info;
}

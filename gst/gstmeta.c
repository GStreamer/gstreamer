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
 * The #GstMeta structure should be included as the first member of a #GstBuffer
 * metadata structure. The structure defines the API of the metadata and should
 * be accessible to all elements using the metadata.
 *
 * A metadata API is registered with gst_meta_api_type_register() which takes a
 * name for the metadata API and some tags associated with the metadata.
 * With gst_meta_api_type_has_tag() one can check if a certain metadata API
 * contains a given tag.
 *
 * Multiple implementations of a metadata API can be registered.
 * To implement a metadata API, gst_meta_register() should be used. This
 * function takes all parameters needed to create, free and transform metadata
 * along with the size of the metadata. The function returns a #GstMetaInfo
 * structure that contains the information for the implementation of the API.
 *
 * A specific implementation can be retrieved by name with gst_meta_get_info().
 *
 * See #GstBuffer for how the metadata can be added, retrieved and removed from
 * buffers.
 *
 * Last reviewed on 2012-03-28 (0.11.3)
 */
#include "gst_private.h"

#include "gstbuffer.h"
#include "gstmeta.h"
#include "gstinfo.h"
#include "gstutils.h"

static GHashTable *metainfo = NULL;
static GRWLock lock;

GQuark _gst_meta_transform_copy;
GQuark _gst_meta_tag_memory;

void
_priv_gst_meta_initialize (void)
{
  g_rw_lock_init (&lock);
  metainfo = g_hash_table_new (g_str_hash, g_str_equal);

  _gst_meta_transform_copy = g_quark_from_static_string ("gst-copy");
  _gst_meta_tag_memory = g_quark_from_static_string ("memory");
}

/**
 * gst_meta_api_type_register:
 * @api: an API to register
 * @tags: tags for @api
 *
 * Register and return a GType for the @api and associate it with
 * @tags.
 *
 * Returns: a unique GType for @api.
 */
GType
gst_meta_api_type_register (const gchar * api, const gchar ** tags)
{
  GType type;

  g_return_val_if_fail (api != NULL, 0);
  g_return_val_if_fail (tags != NULL, 0);

  GST_CAT_DEBUG (GST_CAT_META, "register API \"%s\"", api);
  type = g_pointer_type_register_static (api);

  if (type != 0) {
    gint i;

    for (i = 0; tags[i]; i++) {
      GST_CAT_DEBUG (GST_CAT_META, "  adding tag \"%s\"", tags[i]);
      g_type_set_qdata (type, g_quark_from_string (tags[i]),
          GINT_TO_POINTER (TRUE));
    }
  }
  return type;
}

/**
 * gst_meta_api_type_has_tag:
 * @api: an API
 * @tag: the tag to check
 *
 * Check if @api was registered with @tag.
 *
 * Returns: %TRUE if @api was registered with @tag.
 */
gboolean
gst_meta_api_type_has_tag (GType api, GQuark tag)
{
  g_return_val_if_fail (api != 0, FALSE);
  g_return_val_if_fail (tag != 0, FALSE);

  return g_type_get_qdata (api, tag) != NULL;
}

/**
 * gst_meta_register:
 * @api: the type of the #GstMeta API
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
gst_meta_register (GType api, const gchar * impl, gsize size,
    GstMetaInitFunction init_func, GstMetaFreeFunction free_func,
    GstMetaTransformFunction transform_func)
{
  GstMetaInfo *info;

  g_return_val_if_fail (api != 0, NULL);
  g_return_val_if_fail (impl != NULL, NULL);
  g_return_val_if_fail (size != 0, NULL);

  info = g_slice_new (GstMetaInfo);
  info->api = api;
  info->type = g_pointer_type_register_static (impl);
  info->size = size;
  info->init_func = init_func;
  info->free_func = free_func;
  info->transform_func = transform_func;

  GST_CAT_DEBUG (GST_CAT_META,
      "register \"%s\" implementing \"%s\" of size %" G_GSIZE_FORMAT, impl,
      g_type_name (api), size);

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

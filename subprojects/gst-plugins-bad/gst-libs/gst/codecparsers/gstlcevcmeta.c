/* GStreamer LCEVC meta
 *  Copyright (C) <2024> V-Nova International Limited
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstlcevcmeta.h"

/**
 * SECTION:gstlcevcmeta
 * @title: GstMeta for LCEVC
 * @short_description: LCEVC related GstMeta
 *
 */

#define GST_CAT_DEFAULT lcevcmeta_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static gboolean
gst_lcevc_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstLcevcMeta *emeta = (GstLcevcMeta *) meta;

  emeta->id = 0;
  emeta->enhancement_data = NULL;

  return TRUE;
}

static void
gst_lcevc_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstLcevcMeta *emeta = (GstLcevcMeta *) meta;

  g_clear_pointer (&emeta->enhancement_data, gst_buffer_unref);
}

static gboolean
gst_lcevc_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstLcevcMeta *dmeta, *smeta;

  smeta = (GstLcevcMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy = data;

    if (!copy->region) {
      /* only copy if the complete data is copied as well */
      dmeta = (GstLcevcMeta *) gst_buffer_add_meta (dest,
          GST_LCEVC_META_INFO, NULL);

      if (!dmeta)
        return FALSE;

      GST_TRACE ("copying lcevc metadata");
      dmeta->id = smeta->id;

      g_clear_pointer (&dmeta->enhancement_data, gst_buffer_unref);
      dmeta->enhancement_data = gst_buffer_copy (smeta->enhancement_data);
    }
  } else {
    /* return FALSE, if transform type is not supported */
    return FALSE;
  }
  return TRUE;
}


/**
 * gst_lcevc_meta_api_get_type:
 *
 * Gets the #GType of the LCEVC meta API.
 *
 * Returns: the #GType of the LCEVC meta API.
 *
 * Since: 1.26
 */
GType
gst_lcevc_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { "video", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstLcevcMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

/**
 * gst_lcevc_meta_get_info:
 *
 * Gets the #GstMetaInfo of the LCEVC meta.
 *
 * Returns: (transfer none) : the #GstLcevcMeta of the LCEVC meta.
 *
 * Since: 1.26
 */
const GstMetaInfo *
gst_lcevc_meta_get_info (void)
{
  static const GstMetaInfo *lcevc_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & lcevc_meta_info)) {
    GST_DEBUG_CATEGORY_INIT (lcevcmeta_debug, "lcevcmeta", 0, "LCEVC Metadata");

    const GstMetaInfo *meta = gst_meta_register (GST_LCEVC_META_API_TYPE,
        "GstLcevcMeta",
        sizeof (GstLcevcMeta),
        (GstMetaInitFunction) gst_lcevc_meta_init,
        (GstMetaFreeFunction) gst_lcevc_meta_free,
        gst_lcevc_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & lcevc_meta_info,
        (GstMetaInfo *) meta);
  }
  return lcevc_meta_info;
}

/**
 * gst_buffer_get_lcevc_meta:
 * @buffer: a #GstBuffer
 *
 * Find the #GstLcevcMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstLcevcMeta metadata items when dealing with
 * multiview buffers.
 *
 * Returns: (transfer none) (nullable): the #GstLcevcMeta with lowest id (usually 0) or %NULL when there
 * is no such metadata on @buffer.
 *
 * Since: 1.26
 */
GstLcevcMeta *
gst_buffer_get_lcevc_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstLcevcMeta *out = NULL;
  GstMeta *meta;
  const GstMetaInfo *info = GST_LCEVC_META_INFO;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      GstLcevcMeta *vmeta = (GstLcevcMeta *) meta;
      if (vmeta->id == 0)
        return vmeta;           /* Early out for id 0 */
      if (out == NULL || vmeta->id < out->id)
        out = vmeta;
    }
  }
  return out;
}

/**
 * gst_buffer_get_lcevc_meta_id:
 * @buffer: a #GstBuffer
 * @id: a metadata id
 *
 * Find the #GstLcevcMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstLcevcMeta metadata items when dealing with
 * multiview buffers.
 *
 * Returns: (transfer none) (nullable): the #GstLcevcMeta with @id or %NULL when there is no such metadata
 * on @buffer.
 *
 * Since: 1.26
 */
GstLcevcMeta *
gst_buffer_get_lcevc_meta_id (GstBuffer * buffer, gint id)
{
  gpointer state = NULL;
  GstMeta *meta;
  const GstMetaInfo *info = GST_LCEVC_META_INFO;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      GstLcevcMeta *vmeta = (GstLcevcMeta *) meta;
      if (vmeta->id == id)
        return vmeta;
    }
  }
  return NULL;
}

/**
 * gst_buffer_add_lcevc_meta:
 * @buffer: a #GstBuffer
 * @enhancement_data: (transfer none): the parsed LCEVC enhancement data
 *
 * Attaches GstLcevcMeta metadata to @buffer.
 *
 * Returns: (transfer none): the #GstLcevcMeta on @buffer.
 *
 * Since: 1.26
 */
GstLcevcMeta *
gst_buffer_add_lcevc_meta (GstBuffer * buffer, GstBuffer * enhancement_data)
{
  GstLcevcMeta *meta;

  meta = (GstLcevcMeta *) gst_buffer_add_meta (buffer, GST_LCEVC_META_INFO,
      NULL);
  if (!meta)
    return NULL;

  meta->id = 0;
  g_clear_pointer (&meta->enhancement_data, gst_buffer_unref);
  meta->enhancement_data = gst_buffer_ref (enhancement_data);

  return meta;
}

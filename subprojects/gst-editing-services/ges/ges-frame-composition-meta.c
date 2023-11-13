/* GStreamer
 * Copyright (C) 2013 Mathieu Duponchelle <mduponchelle1@gmail.com>
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

/**
 * SECTION: gesframecompositionmeta
 * @title: GESFrameCompositionMeta interface
 * @short_description: A Meta providing positioning information for a given
 * video frame
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-frame-composition-meta.h"
#include "gstframepositioner.h"

static gboolean ges_frame_composition_meta_init (GstMeta * meta,
    gpointer params, GstBuffer * buffer);
static gboolean ges_frame_composition_meta_transform (GstBuffer * dest,
    GstMeta * meta, GstBuffer * buffer, GQuark type, gpointer data);

GType
ges_frame_composition_meta_api_get_type (void)
{
  static GType type;
  static const gchar *tags[] = { "video", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstFrameCompositionApi", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static const GstMetaInfo *
ges_frame_composition_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (ges_frame_composition_meta_api_get_type (),
        "GESFrameCompositionMeta",
        sizeof (GESFrameCompositionMeta), ges_frame_composition_meta_init, NULL,
        ges_frame_composition_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & meta_info, (GstMetaInfo *) meta);
  }
  return meta_info;
}

static gboolean
ges_frame_composition_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  int default_operator_value = 0;
  GESFrameCompositionMeta *smeta;

  smeta = (GESFrameCompositionMeta *) meta;

  gst_compositor_operator_get_type_and_default_value (&default_operator_value);

  smeta->alpha = 0.0;
  smeta->posx = smeta->posy = smeta->height = smeta->width = 0;
  smeta->zorder = 0;
  smeta->operator = default_operator_value;

  return TRUE;
}

static gboolean
ges_frame_composition_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GESFrameCompositionMeta *dmeta, *smeta;

  smeta = (GESFrameCompositionMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    /* only copy if the complete data is copied as well */
    dmeta =
        (GESFrameCompositionMeta *) gst_buffer_add_meta (dest,
        ges_frame_composition_meta_get_info (), NULL);
    dmeta->alpha = smeta->alpha;
    dmeta->posx = smeta->posx;
    dmeta->posy = smeta->posy;
    dmeta->width = smeta->width;
    dmeta->height = smeta->height;
    dmeta->zorder = smeta->zorder;
    dmeta->operator = smeta->operator;
  }

  return TRUE;
}

/**
 * ges_buffer_add_frame_composition_meta:
 * @buffer: #GstBuffer to which protection metadata should be added.
 *
 * Attaches positioning metadata to a #GstBuffer.
 *
 * Returns: (transfer none): a pointer to the added #GESFrameCompositionMeta.
 *
 * Since: 1.24
 */
GESFrameCompositionMeta *
ges_buffer_add_frame_composition_meta (GstBuffer * buffer)
{
  GESFrameCompositionMeta *meta;

  meta =
      (GESFrameCompositionMeta *) gst_buffer_add_meta (buffer,
      ges_frame_composition_meta_get_info (), NULL);
  return meta;
}

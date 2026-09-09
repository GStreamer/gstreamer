/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

/**
 * SECTION:gstanalyticstextmtd
 * @title: GstAnalyticsTextMtd
 * @short_description: Analytics metadata for text
 * @symbols:
 * - GstAnalyticsTextMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta, #GstAnalyticsODMtd
 *
 * This type of metadata stores UTF-8 text and its confidence level.
 * It can be used for image/video-to-text and audio-to-text inference.
 *
 * #GstAnalyticsTextMtd only stores text-related information. Additional
 * information such as a bounding box can be represented by other metadata
 * types such as #GstAnalyticsODMtd and associated with the text metadata
 * using #GstAnalyticsRelationMeta.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstanalyticstextmtd.h"
#include <string.h>

typedef struct
{
  gsize len;
  gfloat confidence;
  gchar text[];
} GstAnalyticsTextMtdData;

static const GstAnalyticsMtdImpl text_impl = {
  "text",
  NULL,
  NULL
};

/**
 * gst_analytics_text_mtd_get_mtd_type:
 *
 * Returns: the #GstAnalyticsMtdType of #GstAnalyticsTextMtd
 *
 * Since: 1.30
 */
GstAnalyticsMtdType
gst_analytics_text_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & text_impl;
}

/**
 * gst_analytics_text_mtd_get_text:
 * @mtd: a #GstAnalyticsTextMtd
 *
 * Gets the text stored in @mtd.
 *
 * Returns: (transfer none): the UTF-8 text stored in @mtd
 *
 * Since: 1.30
 */
const gchar *
gst_analytics_text_mtd_get_text (const GstAnalyticsTextMtd * mtd)
{
  GstAnalyticsTextMtdData *data;

  g_return_val_if_fail (mtd, NULL);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, NULL);

  return data->text;
}

/**
 * gst_analytics_text_mtd_get_length:
 * @mtd: a #GstAnalyticsTextMtd
 *
 * Gets the length in bytes of the UTF-8 text stored in @mtd, excluding the
 * terminating nul byte.
 *
 * Returns: the length of the text in bytes
 *
 * Since: 1.30
 */
gsize
gst_analytics_text_mtd_get_length (const GstAnalyticsTextMtd * mtd)
{
  GstAnalyticsTextMtdData *data;

  g_return_val_if_fail (mtd, 0);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, 0);

  return data->len;
}

/**
 * gst_analytics_text_mtd_get_confidence:
 * @mtd: a #GstAnalyticsTextMtd
 *
 * Gets the confidence level of the text stored in @mtd.
 *
 * Returns: the confidence level
 *
 * Since: 1.30
 */
gfloat
gst_analytics_text_mtd_get_confidence (const GstAnalyticsTextMtd * mtd)
{
  GstAnalyticsTextMtdData *data;

  g_return_val_if_fail (mtd, 0);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, 0);

  return data->confidence;
}

/**
 * gst_analytics_relation_meta_add_text_mtd:
 * @meta: a #GstAnalyticsRelationMeta
 * @text: UTF-8 text
 * @confidence: confidence level of @text
 * @text_mtd: (out): location for the newly added #GstAnalyticsTextMtd
 *
 * Adds text metadata to @meta. If location information is needed, it can be
 * represented by separate metadata such as #GstAnalyticsODMtd and associated
 * with @text_mtd using @meta.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_text_mtd (GstAnalyticsRelationMeta * meta,
    const gchar * text, gfloat confidence, GstAnalyticsTextMtd * text_mtd)
{
  GstAnalyticsTextMtdData *data;
  gsize text_size;
  gsize size;

  g_return_val_if_fail (meta, FALSE);
  g_return_val_if_fail (text, FALSE);
  g_return_val_if_fail (text_mtd, FALSE);

  text_size = strlen (text) + 1;
  size = sizeof (GstAnalyticsTextMtdData) + text_size;

  data = gst_analytics_relation_meta_add_mtd (meta, &text_impl, size, text_mtd);
  if (!data)
    return FALSE;

  data->confidence = confidence;
  data->len = text_size - 1;
  memcpy (data->text, text, text_size);

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_text_mtd:
 * @meta: a #GstAnalyticsRelationMeta
 * @an_meta_id: the identifier of the metadata
 * @rlt: (out caller-allocates): location for the #GstAnalyticsTextMtd
 *
 * Gets the #GstAnalyticsTextMtd identified by @an_meta_id from @meta.
 *
 * Returns: %TRUE if the metadata was found, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_get_text_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsTextMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_text_mtd_get_mtd_type (), (GstAnalyticsMtd *) rlt);
}

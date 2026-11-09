/* GStreamer
 * Copyright (C) 2026 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticspropertiesmtd.c
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
 * SECTION:gstanalyticspropertiesmtd
 * @title: GstAnalyticsPropertiesMtd
 * @short_description: Analytics metadata for key/value properties
 * @symbols:
 * - GstAnalyticsPropertiesMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta, #GstAnalyticsODMtd
 *
 * This type of metadata stores a set of key/value properties. Keys are strings
 * and values are #GValue. It is intended to annotate another analytics
 * metadata, like a #GstAnalyticsODMtd, with arbitrary attributes (color,
 * speed, license plate, ...).
 *
 * The annotated metadata is associated with the #GstAnalyticsPropertiesMtd
 * using #GstAnalyticsRelationMeta. The recommended relation is
 * %GST_ANALYTICS_REL_TYPE_RELATE_TO from the annotated metadata to the
 * #GstAnalyticsPropertiesMtd:
 *
 * |[<!-- language="C" -->
 * GstAnalyticsODMtd od_mtd;
 * GstAnalyticsPropertiesMtd props_mtd;
 *
 * gst_analytics_relation_meta_add_od_mtd (rmeta, type, x, y, w, h, conf,
 *     &od_mtd);
 * gst_analytics_relation_meta_add_properties_mtd (rmeta, &props_mtd);
 *
 * gst_analytics_properties_mtd_set_string (&props_mtd, "color", "red");
 * gst_analytics_properties_mtd_set (&props_mtd,
 *     "license-plate", G_TYPE_STRING, "ABC-123",
 *     "speed", G_TYPE_DOUBLE, 12.5,
 *     NULL);
 *
 * gst_analytics_relation_meta_set_relation (rmeta,
 *     GST_ANALYTICS_REL_TYPE_RELATE_TO, od_mtd.id, props_mtd.id);
 * ]|
 *
 * Downstream, gst_analytics_relation_meta_iterate() filtered on
 * gst_analytics_od_mtd_get_mtd_type() visits each object, and
 * gst_analytics_relation_meta_get_direct_related() filtered on
 * gst_analytics_properties_mtd_get_mtd_type() returns the properties
 * related to it:
 *
 * |[<!-- language="C" -->
 * GstAnalyticsODMtd od_mtd;
 * GstAnalyticsPropertiesMtd props_mtd;
 * gpointer od_state = NULL;
 *
 * while (gst_analytics_relation_meta_iterate (rmeta, &od_state,
 *         gst_analytics_od_mtd_get_mtd_type (), &od_mtd)) {
 *   gpointer props_state = NULL;
 *
 *   if (gst_analytics_relation_meta_get_direct_related (rmeta, od_mtd.id,
 *           GST_ANALYTICS_REL_TYPE_RELATE_TO,
 *           gst_analytics_properties_mtd_get_mtd_type (), &props_state,
 *           &props_mtd)) {
 *     const gchar *plate =
 *         gst_analytics_properties_mtd_get_string (&props_mtd,
 *         "license-plate");
 *     ...
 *   }
 * }
 * ]|
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstanalyticspropertiesmtd.h"

typedef struct
{
  GstStructure *props;
} GstAnalyticsPropertiesMtdData;

static gboolean
gst_analytics_properties_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data);

static void gst_analytics_properties_mtd_clear (GstBuffer * buffer,
    GstAnalyticsMtd * mtd);

static const GstAnalyticsMtdImpl properties_impl = {
  "properties",
  gst_analytics_properties_mtd_transform,
  gst_analytics_properties_mtd_clear
};

static gboolean
gst_analytics_properties_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsPropertiesMtdData *propsdata =
      gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
      transmtd->id);

  /* Properties don't depend on geometry, so any transform type is accepted. */
  propsdata->props = gst_structure_copy (propsdata->props);

  return TRUE;
}

static void
gst_analytics_properties_mtd_clear (GstBuffer * buffer, GstAnalyticsMtd * mtd)
{
  GstAnalyticsPropertiesMtdData *propsdata;

  propsdata = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_if_fail (propsdata != NULL);
  gst_clear_structure (&propsdata->props);
}

/**
 * gst_analytics_properties_mtd_get_mtd_type:
 *
 * Returns: the #GstAnalyticsMtdType of #GstAnalyticsPropertiesMtd
 *
 * Since: 1.30
 */
GstAnalyticsMtdType
gst_analytics_properties_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & properties_impl;
}

/**
 * gst_analytics_properties_mtd_set_value:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @key: the name of the property
 * @value: the value of the property
 *
 * Sets the property @key to a copy of @value. If @key already exists its
 * value is replaced.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_properties_mtd_set_value (GstAnalyticsPropertiesMtd * mtd,
    const gchar * key, const GValue * value)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (key, FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  gst_structure_set_value (data->props, key, value);

  return TRUE;
}

static gboolean
gst_analytics_properties_mtd_set_valist (GstAnalyticsPropertiesMtd * mtd,
    const gchar * first_key, va_list varargs)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (first_key, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  gst_structure_set_valist (data->props, first_key, varargs);

  return TRUE;
}

/**
 * gst_analytics_properties_mtd_set:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @first_key: the name of the first property to set
 * @...: #GType and value of the first property, followed by more
 *   key/#GType/value triplets, terminated by %NULL
 *
 * Sets several properties at once, using the same variable arguments
 * convention as gst_structure_set(). Existing properties are replaced.
 *
 * Each value must have exactly the C type of its #GType, as no conversion
 * is done: pass `1.0` and not `1` for %G_TYPE_DOUBLE, and cast to #gint64
 * or #guint64 for 64-bit integer types.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_properties_mtd_set (GstAnalyticsPropertiesMtd * mtd,
    const gchar * first_key, ...)
{
  va_list varargs;
  gboolean ret;

  va_start (varargs, first_key);
  ret = gst_analytics_properties_mtd_set_valist (mtd, first_key, varargs);
  va_end (varargs);

  return ret;
}

/**
 * gst_analytics_properties_mtd_set_string:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @key: the name of the property
 * @value: the string value of the property
 *
 * Sets the property @key to a copy of the string @value. If @key already
 * exists its value is replaced.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_properties_mtd_set_string (GstAnalyticsPropertiesMtd * mtd,
    const gchar * key, const gchar * value)
{
  return gst_analytics_properties_mtd_set (mtd, key, G_TYPE_STRING, value,
      NULL);
}

/**
 * gst_analytics_properties_mtd_get_string:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @key: the name of the property
 *
 * Gets the string value of the property @key.
 *
 * Returns: (transfer none) (nullable): the string value of the property, or
 *   %NULL if @key doesn't exist or doesn't hold a string
 *
 * Since: 1.30
 */
const gchar *
gst_analytics_properties_mtd_get_string (const GstAnalyticsPropertiesMtd * mtd,
    const gchar * key)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, NULL);
  g_return_val_if_fail (key, NULL);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, NULL);

  return gst_structure_get_string (data->props, key);
}

/**
 * gst_analytics_properties_mtd_get_value:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @key: the name of the property
 *
 * Gets the value of the property @key.
 *
 * Returns: (transfer none) (nullable): the value of the property, or %NULL if
 *   @key doesn't exist
 *
 * Since: 1.30
 */
const GValue *
gst_analytics_properties_mtd_get_value (const GstAnalyticsPropertiesMtd * mtd,
    const gchar * key)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, NULL);
  g_return_val_if_fail (key, NULL);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, NULL);

  return gst_structure_get_value (data->props, key);
}

/**
 * gst_analytics_properties_mtd_has_key:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @key: the name of the property
 *
 * Returns: %TRUE if @mtd has a property named @key, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_properties_mtd_has_key (const GstAnalyticsPropertiesMtd * mtd,
    const gchar * key)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (key, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  return gst_structure_has_field (data->props, key);
}

/**
 * gst_analytics_properties_mtd_remove_key:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @key: the name of the property
 *
 * Removes the property @key from @mtd.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_properties_mtd_remove_key (GstAnalyticsPropertiesMtd * mtd,
    const gchar * key)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (key, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  gst_structure_remove_field (data->props, key);

  return TRUE;
}

/**
 * gst_analytics_properties_mtd_get_length:
 * @mtd: a #GstAnalyticsPropertiesMtd
 *
 * Returns: the number of properties stored in @mtd
 *
 * Since: 1.30
 */
guint
gst_analytics_properties_mtd_get_length (const GstAnalyticsPropertiesMtd * mtd)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, 0);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, 0);

  return gst_structure_n_fields (data->props);
}

/**
 * gst_analytics_properties_mtd_get_nth_key:
 * @mtd: a #GstAnalyticsPropertiesMtd
 * @index: the index of the property
 *
 * Gets the name of the property at @index, to iterate over all properties
 * along with gst_analytics_properties_mtd_get_length().
 *
 * Returns: (transfer none) (nullable): the name of the property, or %NULL if
 *   @index is out of range
 *
 * Since: 1.30
 */
const gchar *
gst_analytics_properties_mtd_get_nth_key (const GstAnalyticsPropertiesMtd *
    mtd, guint index)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, NULL);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, NULL);

  if (index >= gst_structure_n_fields (data->props))
    return NULL;

  return gst_structure_nth_field_name (data->props, index);
}

/**
 * gst_analytics_properties_mtd_get_structure:
 * @mtd: a #GstAnalyticsPropertiesMtd
 *
 * Gets the #GstStructure holding all properties of @mtd. Each field of the
 * structure is a property.
 *
 * Returns: (transfer none): the properties of @mtd
 *
 * Since: 1.30
 */
const GstStructure *
gst_analytics_properties_mtd_get_structure (const GstAnalyticsPropertiesMtd *
    mtd)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (mtd, NULL);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, NULL);

  return data->props;
}

/**
 * gst_analytics_relation_meta_add_properties_mtd:
 * @meta: a #GstAnalyticsRelationMeta
 * @properties_mtd: (out): location for the newly added
 *   #GstAnalyticsPropertiesMtd
 *
 * Adds an empty #GstAnalyticsPropertiesMtd to @meta. Properties can be added
 * with gst_analytics_properties_mtd_set_value() and associated with an Mtd
 * it describes using gst_analytics_relation_meta_set_relation().
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_properties_mtd (GstAnalyticsRelationMeta *
    meta, GstAnalyticsPropertiesMtd * properties_mtd)
{
  GstAnalyticsPropertiesMtdData *data;

  g_return_val_if_fail (meta, FALSE);
  g_return_val_if_fail (properties_mtd, FALSE);

  data = gst_analytics_relation_meta_add_mtd (meta, &properties_impl,
      sizeof (GstAnalyticsPropertiesMtdData), properties_mtd);
  if (!data)
    return FALSE;

  data->props = gst_structure_new_empty ("properties");

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_properties_mtd:
 * @meta: a #GstAnalyticsRelationMeta
 * @an_meta_id: the identifier of the metadata
 * @rlt: (out caller-allocates): location for the #GstAnalyticsPropertiesMtd
 *
 * Gets the #GstAnalyticsPropertiesMtd identified by @an_meta_id from @meta.
 *
 * Returns: %TRUE if the metadata was found, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_get_properties_mtd (GstAnalyticsRelationMeta *
    meta, guint an_meta_id, GstAnalyticsPropertiesMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_properties_mtd_get_mtd_type (), (GstAnalyticsMtd *) rlt);
}

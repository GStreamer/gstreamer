/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 *
 * gstanalyticsobjectdetectionmtd.c
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

#include "gstanalyticsobjectdetectionmtd.h"

#define GST_RELATABLE_MTD_OD_TYPE_NAME "object-detection"

static char type[] = GST_RELATABLE_MTD_OD_TYPE_NAME;

typedef struct _GstAnalyticsODMtdData GstAnalyticsODMtdData;

/**
 * GstAnalyticsODMtdData:
 * @parent: parent #GstAnalyticsMtd
 * @object_type: Type of object
 * @x: x component of upper-left corner
 * @y: y component of upper-left corner
 * @w: bounding box width
 * @h: bounding box height
 * @location_confidence_lvl: Confidence on object location
 *
 * Store information on results of object detection
 *
 * Since: 1.24
 */
struct _GstAnalyticsODMtdData
{
  GstAnalyticsRelatableMtdData parent;
  GQuark object_type;
  gint x;
  gint y;
  gint w;
  gint h;
  gfloat location_confidence_lvl;
};


/**
 * gst_analytics_od_mtd_get_type_quark:
 * Get a quark that represent object-detection metadata type
 *
 * Returns: Quark of #GstAnalyticsMtd type
 *
 * Since: 1.24
 */
GstAnalyticsMtdType
gst_analytics_od_mtd_get_type_quark (void)
{
  return g_quark_from_static_string (type);
}

/**
 * gst_analytics_od_mtd_get_type_name:
 * Get a text representing object-detection metadata type.
 *
 * Returns: #GstAnalyticsMtd type name.
 *
 * Since: 1.24
 */
const gchar *
gst_analytics_od_mtd_get_type_name (void)
{
  return GST_RELATABLE_MTD_OD_TYPE_NAME;
}

static GstAnalyticsODMtdData *
gst_analytics_od_mtd_get_data (GstAnalyticsODMtd * instance)
{
  GstAnalyticsRelatableMtdData *rlt_data =
      gst_analytics_relation_meta_get_mtd_data (instance->meta,
      instance->id);
  g_return_val_if_fail (rlt_data, NULL);
  g_return_val_if_fail (rlt_data->analysis_type ==
      gst_analytics_od_mtd_get_type_quark (), NULL);

  return (GstAnalyticsODMtdData *) rlt_data;
}

/**
 * gst_analytics_od_mtd_get_location:
 * @instance: instance
 * @x: (out): x component of upper-left corner of the object location
 * @y: (out): y component of upper-left corner of the object location
 * @w: (out): bounding box width of the object location
 * @h: (out): bounding box height of the object location
 * @loc_conf_lvl: (out)(optional): Confidence on object location

 *
 * Retrieve location and location confidence level.
 *
 * Returns: TRUE on success, otherwise FALSE.
 *
 * Since: 1.24
 */
gboolean
gst_analytics_od_mtd_get_location (GstAnalyticsODMtd * instance,
    gint * x, gint * y, gint * w, gint * h, gfloat * loc_conf_lvl)
{
  g_return_val_if_fail (instance && x && y && w && h, FALSE);
  GstAnalyticsODMtdData *data;
  data = gst_analytics_od_mtd_get_data (instance);
  g_return_val_if_fail (data != NULL, FALSE);

  if (data) {
    *x = data->x;
    *y = data->y;
    *w = data->w;
    *h = data->h;

    if (loc_conf_lvl) {
      *loc_conf_lvl = data->location_confidence_lvl;
    }
  }

  return TRUE;
}

/**
 * gst_analytics_od_mtd_get_type:
 * @handle: Instance handle
 * Quark of the class of object associated with this location.
 * Returns: Quark different from on success and 0 on failure.
 *
 * Since: 1.24
 */
GQuark
gst_analytics_od_mtd_get_type (GstAnalyticsODMtd * handle)
{
  GstAnalyticsODMtdData *data;
  g_return_val_if_fail (handle != NULL, 0);
  data = gst_analytics_od_mtd_get_data (handle);
  g_return_val_if_fail (data != NULL, 0);
  return data->object_type;
}

/**
 * gst_analytics_relation_meta_add_od_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add classification instance
 * @type: Quark of the object type
 * @x: x component of bounding box upper-left corner
 * @y: y component of bounding box upper-left corner
 * @w: bounding box width
 * @h: bounding box height
 * @loc_conf_lvl: confidence level on the object location
 * @od_mtd: (out)(nullable): Handle updated with newly added object detection
 *    meta. Add an object-detetion metadata to @instance.
 *
 * Returns: Added successfully
 *
 * Since: 1.24
 */
gboolean
gst_analytics_relation_meta_add_od_mtd (GstAnalyticsRelationMeta *
    instance, GQuark type, gint x, gint y, gint w, gint h,
    gfloat loc_conf_lvl, GstAnalyticsODMtd * od_mtd)
{
  g_return_val_if_fail (instance != NULL, FALSE);
  GstAnalyticsMtdType mtd_type = gst_analytics_od_mtd_get_type_quark ();
  gsize size = sizeof (GstAnalyticsODMtdData);
  GstAnalyticsODMtdData *od_mtd_data = (GstAnalyticsODMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, mtd_type, size, od_mtd);
  if (od_mtd_data) {
    od_mtd_data->x = x;
    od_mtd_data->y = y;
    od_mtd_data->w = w;
    od_mtd_data->h = h;
    od_mtd_data->location_confidence_lvl = loc_conf_lvl;
    od_mtd_data->object_type = type;
  }
  return od_mtd_data != NULL;
}

/**
 * gst_analytics_relation_meta_get_od_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsOdMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with relatable
 *    meta
 *
 * Fill @rlt if a analytics-meta with id == @an_meta_id exist in @meta instance,
 * otherwise this method return FALSE and @rlt is invalid.
 *
 * Returns: TRUE if successful.
 *
 * Since 1.24
 */
gboolean
gst_analytics_relation_meta_get_od_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsODMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_od_mtd_get_type_quark (), (GstAnalyticsODMtd *) rlt);
}

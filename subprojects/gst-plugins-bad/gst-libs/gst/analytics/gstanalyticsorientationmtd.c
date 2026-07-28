/* GStreamer
 * Copyright (C) 2026 Collabora
 *  @author: Olivier Crête <olivier.crete@collabora.com>
 *
 * gstanalyticsorientationmtd.c
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

#include <gst/video/video.h>
#include <math.h>
#include "gstanalyticsorientationmtd.h"

GST_DEBUG_CATEGORY_EXTERN (gst_analytics_relation_meta_debug);
#define GST_CAT_DEFAULT gst_analytics_relation_meta_debug

/**
 * SECTION:gstanalyticsorientationmtd
 * @title: GstAnalyticsOrientationMtd
 * @short_description: Analytics metadata for orientation information
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This metadata item represents the orientation in space of an object
 *
 * Since: 1.30
 */

typedef struct _GstAnalyticsOrientationMtdData GstAnalyticsOrientationMtdData;

/*
 * GstAnalyticsOrientationMtdData:
 * @yaw: Yaw angle in degrees, with 0 facing forward and increasing to the right
 * @pitch: Pitch angle in degrees, with 0 facing forward and increasing upwards
 * @confidence: Confidence score
 *
 * Store Orientation data
 *
 * Since: 1.30
 */
struct _GstAnalyticsOrientationMtdData
{
  gfloat yaw;
  gfloat pitch;
  gfloat confidence;
};

#define DEG_TO_RAD(deg) ((deg) * (G_PI / 180.0))
#define RAD_TO_DEG(rad) ((rad) * (180.0 / G_PI))

static gboolean
gst_analytics_orientation_mtd_meta_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  if (GST_VIDEO_META_TRANSFORM_IS_MATRIX (type)) {
    GstVideoMetaTransformMatrix *trans = data;
    GstAnalyticsOrientationMtdData *orientation_data =
        gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
        transmtd->id);
    gdouble yaw_rad = DEG_TO_RAD (orientation_data->yaw);
    gdouble pitch_rad = DEG_TO_RAD (orientation_data->pitch);

    /* Map (yaw, pitch) to normalized 2D image space coordinates [x, y, 1]
     * yaw = 0, pitch = 0 maps directly to (0, 0, 1).
     *
     * The matrix operates in a coordinate system with (0, 0) in the
     * top-left corner and +y pointing down, so an upward pitch
     * corresponds to a negative y component.
     */
    gdouble x = tan (yaw_rad);
    gdouble y = -tan (pitch_rad);

    /* Apply the 3x3 matrix multiplication: [x', y', w']^T = M * [x, y, 1]^T */
    gdouble x_out = trans->matrix[0][0] * x + trans->matrix[0][1] * y
        + trans->matrix[0][2];
    gdouble y_out = trans->matrix[1][0] * x + trans->matrix[1][1] * y
        + trans->matrix[1][2];
    gdouble w_out = trans->matrix[2][0] * x + trans->matrix[2][1] * y
        + trans->matrix[2][2];

    /* Convert transformed homogeneous coordinates back to angles (degrees) */
    orientation_data->yaw = RAD_TO_DEG (atan2 (x_out, w_out));
    orientation_data->pitch = RAD_TO_DEG (-atan2 (y_out, w_out));
  }

  return TRUE;
}

static const GstAnalyticsMtdImpl orientation_impl = {
  "orientation",
  gst_analytics_orientation_mtd_meta_transform,
  NULL
};

/**
 * gst_analytics_orientation_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsMtd type.
 *
 * Returns: opaque id of #GstAnalyticsMtd type
 *
 * Since: 1.30
 */
GstAnalyticsMtdType
gst_analytics_orientation_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & orientation_impl;
}

/**
 * gst_analytics_relation_meta_add_orientation_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add orientation
 * @yaw: Yaw angle in degrees, with 0 facing forward and increasing to the right
 * @pitch: Pitch angle in degrees, with 0 facing forward and increasing upwards
 * @confidence: Confidence score
 * @orientation_mtd: (out) (not nullable): Handle updated to newly added orientation
 *
 * Add orientation metadata to @instance. The (0,0) represents looking straight
 * through the image (or screen).
 *
 * Returns: TRUE if added successfully, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_orientation_mtd (GstAnalyticsRelationMeta *
    instance, gfloat yaw, gfloat pitch, gfloat confidence,
    GstAnalyticsOrientationMtd * orientation_mtd)
{
  g_return_val_if_fail (instance, FALSE);
  g_return_val_if_fail (orientation_mtd, FALSE);

  GstAnalyticsOrientationMtdData *orientation_data =
      (GstAnalyticsOrientationMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, &orientation_impl,
      sizeof (GstAnalyticsOrientationMtdData), orientation_mtd);

  if (orientation_data) {
    orientation_data->yaw = yaw;
    orientation_data->pitch = pitch;
    orientation_data->confidence = confidence;
    return TRUE;
  }

  return FALSE;
}

/**
 * gst_analytics_orientation_mtd_get_angles:
 * @handle: handle
 * @yaw: (out): Yaw angle in degrees, with 0 facing forward and increasing to the right
 * @pitch: (out): Pitch angle in degrees, with 0 facing forward and increasing upwards
 *
 * Get orientation angles
 * See #gst_analytics_relation_meta_add_orientation_mtd for more details
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_orientation_mtd_get_angles (const GstAnalyticsOrientationMtd *
    handle, gfloat * yaw, gfloat * pitch)
{
  GstAnalyticsOrientationMtdData *orientation_data;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (yaw, FALSE);
  g_return_val_if_fail (pitch, FALSE);

  orientation_data = gst_analytics_relation_meta_get_mtd_data (handle->meta,
      handle->id);
  g_return_val_if_fail (orientation_data != NULL, FALSE);

  *yaw = orientation_data->yaw;
  *pitch = orientation_data->pitch;

  return TRUE;
}

/**
 * gst_analytics_orientation_mtd_get_confidence:
 * @handle: handle
 * @confidence: (out): Confidence score
 *
 * Get orientation confidence.
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_orientation_mtd_get_confidence (const GstAnalyticsOrientationMtd *
    handle, gfloat * confidence)
{
  GstAnalyticsOrientationMtdData *orientation_data;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (confidence, FALSE);

  orientation_data = gst_analytics_relation_meta_get_mtd_data (handle->meta,
      handle->id);
  g_return_val_if_fail (orientation_data != NULL, FALSE);

  *confidence = orientation_data->confidence;

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_orientation_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsOrientationMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with orientation mtd
 *
 * Retrieve orientation metadata instance from @meta.
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_get_orientation_mtd (GstAnalyticsRelationMeta *
    meta, guint an_meta_id, GstAnalyticsOrientationMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_orientation_mtd_get_mtd_type (),
      (GstAnalyticsOrientationMtd *) rlt);
}

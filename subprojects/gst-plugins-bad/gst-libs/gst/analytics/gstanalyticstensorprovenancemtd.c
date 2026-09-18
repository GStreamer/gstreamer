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
 * SECTION:gstanalyticstensorprovenancemtd
 * @title: GstAnalyticsTensorProvenanceMtd
 * @short_description: Analytics metadata for tensor provenance
 * @symbols:
 * - GstAnalyticsTensorProvenanceMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta, #GstTensorListMeta
 *
 * #GstAnalyticsTensorProvenanceMtd stores provenance associated with a tensor
 * without keeping the tensor data itself alive. It stores the group and
 * tensor IDs of the associated tensor.
 *
 * Tensor provenance metadata can be related to other analytics metadata. For
 * example, a preprocessing element can create a tensor from an object detection
 * and keep the relation between them:
 *
 * |[
 *   GstAnalyticsRelationMeta
 *     |
 *     +-- GstAnalyticsODMtd
 *     |        ^
 *     |        |
 *     |     relation
 *     |        |
 *     +-- GstAnalyticsTensorProvenanceMtd
 *              |
 *              +-- correlation-id = 10
 * ]|
 *
 * The correlation ID can be stored as provenance in #GstTensorListMeta.
 * This allows derived tensor data to be associated with its provenance metadata
 * without keeping the original tensor alive:
 *
 * |[
 *   GstTensorListMeta
 *     |
 *     +-- item
 *           |
 *           +-- tensor "image"
 *           |
 *           +-- provenance
 *                 +-- "image" -> [ 10 ]
 *                                      |
 *                                      +--> GstAnalyticsTensorProvenanceMtd
 *                                             |
 *                                             +-- relation --> GstAnalyticsODMtd
 * ]|
 *
 * The correlation ID is preserved when the provenance metadata is copied or
 * transformed. Multiple provenance records can have the same correlation ID
 * after copy or transform.
 *
 * Tensor provenance metadata can also store a spatial transform from tensor
 * coordinates to video buffer coordinates. A tensor decoder can use this
 * transform to map spatial inference results back to the video buffer.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstanalyticstensorprovenancemtd.h"
#include <gst/video/video.h>
#include <string.h>

static gint64 correlation_seq = 1;      /* ATOMIC */

/* TODO: use GLib's once https://gitlab.gnome.org/GNOME/glib/issues/1076 lands */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
static inline gint64
gst_atomic_int64_inc (gint64 * atomic)
{
  return atomic_fetch_add ((_Atomic gint64 *) atomic, 1);
}
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
static inline gint64
gst_atomic_int64_inc (gint64 * atomic)
{
  return __sync_fetch_and_add (atomic, 1);
}
#elif defined (G_OS_WIN32)
#include <windows.h>
static inline gint64
gst_atomic_int64_inc (gint64 * atomic)
{
  return InterlockedExchangeAdd64 (atomic, 1);
}
#else
G_LOCK_DEFINE_STATIC (correlation_seq);
static inline gint64
gst_atomic_int64_inc (gint64 * atomic)
{
  gint64 ret;

  G_LOCK (correlation_seq);
  ret = (*atomic)++;
  G_UNLOCK (correlation_seq);

  return ret;
}
#endif

typedef struct
{
  GQuark group_id;
  GQuark tensor_id;
  gint64 correlation_id;
  gboolean has_spatial_transform;
  /* Spatial transformation matrix */
  gfloat tr_matrix[3][3];
  GstClockTime timestamp;
  GstClockTime duration;
} GstAnalyticsTensorProvenanceMtdData;

static void
matrix_multiply (gfloat dst[3][3], const gfloat a[3][3], const gfloat b[3][3])
{
  gfloat ret[3][3] = { 0, };

  for (guint i = 0; i < 3; i++) {
    for (guint j = 0; j < 3; j++) {
      for (guint k = 0; k < 3; k++)
        ret[i][j] += a[i][k] * b[k][j];
    }
  }

  memcpy (dst, ret, sizeof (ret));
}

static void
matrix_from_video_transform (gfloat dst[3][3],
    const GstVideoMetaTransformMatrix * trans)
{
  const gfloat in_translate[3][3] = {
    {1.0f, 0.0f, -(gfloat) trans->in_rectangle.x},
    {0.0f, 1.0f, -(gfloat) trans->in_rectangle.y},
    {0.0f, 0.0f, 1.0f},
  };
  const gfloat out_translate[3][3] = {
    {1.0f, 0.0f, (gfloat) trans->out_rectangle.x},
    {0.0f, 1.0f, (gfloat) trans->out_rectangle.y},
    {0.0f, 0.0f, 1.0f},
  };
  gfloat tmp[3][3];

  matrix_multiply (tmp, trans->matrix, in_translate);
  matrix_multiply (dst, out_translate, tmp);
}

static gboolean
gst_analytics_tensor_provenance_mtd_meta_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsTensorProvenanceMtdData *provenance_data =
      gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
      transmtd->id);

  if (GST_META_TRANSFORM_IS_COPY (type))
    return TRUE;

  if (!provenance_data->has_spatial_transform)
    return TRUE;

  if (!GST_VIDEO_META_TRANSFORM_IS_MATRIX (type) &&
      !GST_VIDEO_META_TRANSFORM_IS_SCALE (type)) {
    return TRUE;
  }

  if (GST_VIDEO_META_TRANSFORM_IS_MATRIX (type)) {
    const GstVideoMetaTransformMatrix *trans = data;
    gfloat matrix[3][3];

    matrix_from_video_transform (matrix, trans);

    /* [tensor -> new video] = [old video -> new video] * [tensor -> old video] */
    matrix_multiply (provenance_data->tr_matrix, matrix,
        provenance_data->tr_matrix);
  } else {
    const GstVideoMetaTransform *trans = data;
    gint ow, oh, nw, nh;
    gfloat matrix[3][3] = {
      {0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
    };

    ow = GST_VIDEO_INFO_WIDTH (trans->in_info);
    oh = GST_VIDEO_INFO_HEIGHT (trans->in_info);
    nw = GST_VIDEO_INFO_WIDTH (trans->out_info);
    nh = GST_VIDEO_INFO_HEIGHT (trans->out_info);

    matrix[0][0] = (gfloat) nw / ow;
    matrix[1][1] = (gfloat) nh / oh;

    /* [tensor -> new video] = [old video -> new video] * [tensor -> old video] */
    matrix_multiply (provenance_data->tr_matrix, matrix,
        provenance_data->tr_matrix);
  }

  return TRUE;
}

static const GstAnalyticsMtdImpl tensor_provenance_impl = {
  "tensor-provenance",
  gst_analytics_tensor_provenance_mtd_meta_transform,
  NULL
};

/**
 * gst_analytics_tensor_provenance_mtd_get_mtd_type:
 *
 * Returns: the #GstAnalyticsMtdType of #GstAnalyticsTensorProvenanceMtd
 *
 * Since: 1.30
 */
GstAnalyticsMtdType
gst_analytics_tensor_provenance_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & tensor_provenance_impl;
}

/**
 * gst_analytics_relation_meta_add_tensor_provenance_mtd:
 * @meta: a #GstAnalyticsRelationMeta
 * @group_id: group ID of the tensor
 * @tensor: a #GstTensor
 * @tensor_provenance_mtd: (out caller-allocates): location for the newly added
 *   #GstAnalyticsTensorProvenanceMtd
 *
 * Adds tensor provenance metadata for @tensor to @meta. The tensor is used only
 * for its identity and is not referenced or stored by the metadata.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_tensor_provenance_mtd (GstAnalyticsRelationMeta
    * meta, GQuark group_id, const GstTensor * tensor,
    GstAnalyticsTensorProvenanceMtd * tensor_provenance_mtd)
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (meta, FALSE);
  g_return_val_if_fail (tensor, FALSE);
  g_return_val_if_fail (tensor_provenance_mtd, FALSE);

  data = gst_analytics_relation_meta_add_mtd (meta, &tensor_provenance_impl,
      sizeof (GstAnalyticsTensorProvenanceMtdData), tensor_provenance_mtd);
  if (!data)
    return FALSE;

  data->group_id = group_id;
  data->tensor_id = tensor->id;
  data->correlation_id = gst_atomic_int64_inc (&correlation_seq);
  data->has_spatial_transform = FALSE;
  memset (data->tr_matrix, 0, sizeof (data->tr_matrix));
  data->tr_matrix[0][0] = 1.0f;
  data->tr_matrix[1][1] = 1.0f;
  data->tr_matrix[2][2] = 1.0f;
  data->timestamp = GST_CLOCK_TIME_NONE;
  data->duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_tensor_provenance_mtd:
 * @meta: a #GstAnalyticsRelationMeta
 * @an_meta_id: the identifier of the metadata
 * @rlt: (out caller-allocates): location for the #GstAnalyticsTensorProvenanceMtd
 *
 * Gets the #GstAnalyticsTensorProvenanceMtd identified by @an_meta_id from @meta.
 *
 * Returns: %TRUE if the metadata was found, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_get_tensor_provenance_mtd (GstAnalyticsRelationMeta
    * meta, guint an_meta_id, GstAnalyticsTensorProvenanceMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_tensor_provenance_mtd_get_mtd_type (),
      (GstAnalyticsMtd *) rlt);
}

/**
 * gst_analytics_tensor_provenance_mtd_get_group_id:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 *
 * Gets the tensor group identifier.
 *
 * Returns: the tensor group identifier
 *
 * Since: 1.30
 */
GQuark
gst_analytics_tensor_provenance_mtd_get_group_id (const
    GstAnalyticsTensorProvenanceMtd * mtd)
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, 0);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, 0);

  return data->group_id;
}

/**
 * gst_analytics_tensor_provenance_mtd_get_tensor_id:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 *
 * Gets the tensor identifier.
 *
 * Returns: the tensor identifier
 *
 * Since: 1.30
 */
GQuark
gst_analytics_tensor_provenance_mtd_get_tensor_id (const
    GstAnalyticsTensorProvenanceMtd * mtd)
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, 0);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, 0);

  return data->tensor_id;
}

/**
 * gst_analytics_tensor_provenance_mtd_get_correlation_id:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 *
 * Gets the correlation ID of @mtd. The ID can be used to associate derived
 * tensor data with this provenance metadata and is preserved when the metadata
 * is copied.
 *
 * Returns: the correlation ID
 *
 * Since: 1.30
 */
gint64
gst_analytics_tensor_provenance_mtd_get_correlation_id (const
    GstAnalyticsTensorProvenanceMtd * mtd)
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, 0);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, 0);

  return data->correlation_id;
}

/**
 * gst_analytics_tensor_provenance_mtd_has_spatial_transform:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 *
 * Checks whether @mtd contains spatial transformation information.
 *
 * Returns: %TRUE if spatial transformation information is available,
 *   otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_tensor_provenance_mtd_has_spatial_transform (const
    GstAnalyticsTensorProvenanceMtd * mtd)
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  return data->has_spatial_transform;
}

/**
 * gst_analytics_tensor_provenance_mtd_set_spatial_transform:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 * @transform: (array fixed-size=9): a 3x3 transformation matrix in row-major order
 *
 * Sets a spatial transform from tensor coordinates to video buffer
 * coordinates.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
    gst_analytics_tensor_provenance_mtd_set_spatial_transform
    (GstAnalyticsTensorProvenanceMtd * mtd, const gfloat transform[9])
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (transform, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  memcpy (data->tr_matrix, transform, sizeof (data->tr_matrix));
  data->has_spatial_transform = TRUE;

  return TRUE;
}

/**
 * gst_analytics_tensor_provenance_mtd_set_spatial_transform_from_rectangles:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 * @tensor_rect: rectangle in tensor coordinates
 * @video_rect: rectangle in video buffer coordinates
 *
 * Sets a spatial transform that maps @tensor_rect to @video_rect.
 *
 * This is a convenience function for axis-aligned scale and translation.
 * For other transformations, use
 * gst_analytics_tensor_provenance_mtd_set_spatial_transform().
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
    gst_analytics_tensor_provenance_mtd_set_spatial_transform_from_rectangles
    (GstAnalyticsTensorProvenanceMtd * mtd,
    const GstVideoRectangle * tensor_rect, const GstVideoRectangle * video_rect)
{
  GstVideoMetaTransformMatrix trans = { 0, };
  gfloat matrix[3][3];

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (tensor_rect, FALSE);
  g_return_val_if_fail (video_rect, FALSE);
  if (tensor_rect->w <= 0 || tensor_rect->h <= 0 ||
      video_rect->w <= 0 || video_rect->h <= 0) {
    return FALSE;
  }

  trans.in_rectangle = *tensor_rect;
  trans.out_rectangle = *video_rect;
  trans.matrix[0][0] = (gfloat) video_rect->w / tensor_rect->w;
  trans.matrix[1][1] = (gfloat) video_rect->h / tensor_rect->h;
  trans.matrix[2][2] = 1.0f;
  matrix_from_video_transform (matrix, &trans);

  return gst_analytics_tensor_provenance_mtd_set_spatial_transform (mtd,
      (const gfloat *) matrix);
}

/**
 * gst_analytics_tensor_provenance_mtd_get_spatial_transform:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 * @transform: (out caller-allocates) (array fixed-size=9): location
 *   for the 3x3 spatial transformation matrix in row-major order
 *
 * Gets the spatial transformation matrix. The matrix transforms coordinates
 * from the tensor coordinate space to the coordinate space of the video buffer
 * carrying the #GstAnalyticsRelationMeta.
 *
 * Returns: %TRUE if spatial transformation information is available,
 *   otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_tensor_provenance_mtd_get_spatial_transform (const
    GstAnalyticsTensorProvenanceMtd * mtd, gfloat transform[9])
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (transform, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  if (!data->has_spatial_transform)
    return FALSE;

  memcpy (transform, data->tr_matrix, sizeof (data->tr_matrix));

  return TRUE;
}

/**
 * gst_analytics_tensor_provenance_mtd_transform_point:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 * @x: (inout): x coordinate
 * @y: (inout): y coordinate
 *
 * Transforms (@x, @y) from tensor coordinates to video buffer coordinates.
 *
 * Returns: %TRUE on success, otherwise %FALSE
 *
 * Since: 1.30
 */
gboolean
gst_analytics_tensor_provenance_mtd_transform_point (const
    GstAnalyticsTensorProvenanceMtd * mtd, gfloat * x, gfloat * y)
{
  GstAnalyticsTensorProvenanceMtdData *data;
  gfloat x_in, y_in;
  gfloat x_temp, y_temp;
  gfloat w_prime;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (x, FALSE);
  g_return_val_if_fail (y, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  if (!data->has_spatial_transform)
    return FALSE;

  /* Perform the same operation as gst_video_meta_transform_matrix_point() */
  x_in = *x;
  y_in = *y;
  w_prime = data->tr_matrix[2][0] * x_in +
      data->tr_matrix[2][1] * y_in + data->tr_matrix[2][2];

  if (w_prime == 0.0f)
    return FALSE;

  if (w_prime == 1.0f) {
    x_temp = data->tr_matrix[0][0] * x_in +
        data->tr_matrix[0][1] * y_in + data->tr_matrix[0][2];
    y_temp = data->tr_matrix[1][0] * x_in +
        data->tr_matrix[1][1] * y_in + data->tr_matrix[1][2];
  } else {
    x_temp = (data->tr_matrix[0][0] * x_in +
        data->tr_matrix[0][1] * y_in + data->tr_matrix[0][2]) / w_prime;
    y_temp = (data->tr_matrix[1][0] * x_in +
        data->tr_matrix[1][1] * y_in + data->tr_matrix[1][2]) / w_prime;
  }

  *x = x_temp;
  *y = y_temp;

  return TRUE;
}

/**
 * gst_analytics_tensor_provenance_mtd_transform_rectangle:
 * @mtd: a #GstAnalyticsTensorProvenanceMtd
 * @x: (inout): x coordinate of the rectangle
 * @y: (inout): y coordinate of the rectangle
 * @width: (inout): width of the rectangle
 * @height: (inout): height of the rectangle
 *
 * Transforms an axis-aligned rectangle from tensor coordinates to video
 * buffer coordinates.
 *
 * Returns: %TRUE on success. %FALSE if the transform is not available or
 *   does not preserve an axis-aligned rectangle.
 *
 * Since: 1.30
 */
gboolean
gst_analytics_tensor_provenance_mtd_transform_rectangle (const
    GstAnalyticsTensorProvenanceMtd * mtd, gfloat * x, gfloat * y,
    gfloat * width, gfloat * height)
{
  GstAnalyticsTensorProvenanceMtdData *data;
  gboolean ret = TRUE;
  gfloat x1, y1;
  gfloat x2, y2;

  g_return_val_if_fail (mtd, FALSE);
  g_return_val_if_fail (x, FALSE);
  g_return_val_if_fail (y, FALSE);
  g_return_val_if_fail (width, FALSE);
  g_return_val_if_fail (height, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  if (!data->has_spatial_transform)
    return FALSE;

  /* Perform the same operation as gst_video_meta_transform_matrix_rectangle().
   * If the transformation is not affine, can't do it on a rectangle */
  if (data->tr_matrix[2][0] != 0 || data->tr_matrix[2][1] != 0 ||
      data->tr_matrix[2][2] != 1) {
    return FALSE;
  }

  /* If there is shearing, it won't preserve the rectangle either */
  if ((data->tr_matrix[0][0] != 0 || data->tr_matrix[1][1] != 0) &&
      (data->tr_matrix[0][1] != 0 || data->tr_matrix[1][0] != 0)) {
    return FALSE;
  }

  x1 = *x;
  y1 = *y;
  x2 = *x + *width;
  y2 = *y + *height;

  ret = gst_analytics_tensor_provenance_mtd_transform_point (mtd, &x1, &y1) &&
      gst_analytics_tensor_provenance_mtd_transform_point (mtd, &x2, &y2);

  *x = MIN (x1, x2);
  *y = MIN (y1, y2);

  *width = MAX (x1, x2) - *x;
  *height = MAX (y1, y2) - *y;

  return ret;
}

gboolean
    gst_analytics_tensor_provenance_mtd_set_timestamp
    (GstAnalyticsTensorProvenanceMtd * mtd, GstClockTime timestamp,
    GstClockTime duration) {
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  data->timestamp = timestamp;
  data->duration = duration;

  return TRUE;
}

gboolean
gst_analytics_tensor_provenance_mtd_get_timestamp (const
    GstAnalyticsTensorProvenanceMtd * mtd, GstClockTime * timestamp,
    GstClockTime * duration)
{
  GstAnalyticsTensorProvenanceMtdData *data;

  g_return_val_if_fail (mtd, FALSE);

  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_return_val_if_fail (data, FALSE);

  if (timestamp)
    *timestamp = data->timestamp;

  if (duration)
    *duration = data->duration;

  return TRUE;
}

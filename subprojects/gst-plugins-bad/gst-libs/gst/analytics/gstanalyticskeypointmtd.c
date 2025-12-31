/* GStreamer
 * Copyright (C) 2024 Intel Corporation
 *  @author: Tomasz Janczak <tomasz.janczak@intel.com>
 * Copyright (C) 2026 Collabora
 *  @author: Daniel Morin <daniel.morin@collabora.com>

 *
 * gstanalyticskeypointmtd.c
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
#include "gstanalyticskeypointmtd.h"

GST_DEBUG_CATEGORY_EXTERN (gst_analytics_relation_meta_debug);
#define GST_CAT_DEFAULT gst_analytics_relation_meta_debug

/**
 * SECTION:gstanalyticskeypointmtd
 * @title: GstAnalyticsKeypointMtd
 * @short_description: Analytics metadata for individual keypoints and keypoint groups
 * @symbols:
 * - GstAnalyticsKeypointMtd
 * - GstAnalyticsKeypointDimensions
 * - GstAnalyticsKeypointVisibility
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta, #GstAnalyticsGroupMtd
 *
 * This metadata type represents individual keypoints with 2D or 3D positions.
 * Keypoints can be grouped using #GstAnalyticsGroupMtd with semantic tags
 * (e.g., "hand-21-kp", "pose-17-kp", "face-68-kp").
 *
 * Skeleton links between keypoints are represented as relations using
 * GST_ANALYTICS_REL_TYPE_RELATE_TO, allowing standard relation queries
 * to traverse the skeleton structure.
 *
 * ## Creating Keypoint Groups
 *
 * The most common usage is to create a group of keypoints with skeleton structure:
 *
 * |[<!-- language="C" -->
 * GstAnalyticsGroupMtd hand_group;
 * gint positions[] = {100, 200, 150, 250, ...}; // 21 x,y pairs for hand
 * gfloat confidences[] = {0.9f, 0.85f, 0.95f, ...};
 * gint skeleton_pairs[] = {0, 1, 1, 2, 2, 3, ...}; // 20 links (40 values)
 *
 * gst_analytics_relation_meta_add_keypoints_group(rmeta,
 *     "hand-21-kp", GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
 *     42, positions, 21, confidences, NULL, 40, skeleton_pairs, &hand_group);
 * ]|
 *
 * ## Iterating Through Keypoints
 *
 * Iterate keypoints in a group with type filtering:
 *
 * |[<!-- language="C" -->
 * GstAnalyticsMtd member;
 * gpointer state = NULL;
 * while (gst_analytics_group_mtd_iterate(&hand_group, &state,
 *         gst_analytics_keypoint_mtd_get_mtd_type(), &member)) {
 *     gint x, y, z;
 *     gfloat conf;
 *     GstAnalyticsKeypointDimensions dim;
 *     gst_analytics_keypoint_mtd_get_position((GstAnalyticsKeypointMtd*)&member,
 *         &x, &y, &z, &dim);
 *     gst_analytics_keypoint_mtd_get_confidence((GstAnalyticsKeypointMtd*)&member,
 *         &conf);
 *     g_print("Keypoint at (%d, %d) confidence: %.2f\n", x, y, conf);
 * }
 * ]|
 *
 * ## Querying Skeleton Links
 *
 * Verify skeleton connections between keypoints:
 *
 * |[<!-- language="C" -->
 * // Get first two keypoints
 * GstAnalyticsMtd wrist_kp, thumb_kp;
 * gst_analytics_group_mtd_get_member(&hand_group, 0, &wrist_kp);
 * gst_analytics_group_mtd_get_member(&hand_group, 1, &thumb_kp);
 *
 * // Verify skeleton link exists
 * gboolean has_link = gst_analytics_relation_meta_exist(rmeta,
 *     wrist_kp.id, thumb_kp.id, 1,
 *     GST_ANALYTICS_REL_TYPE_RELATE_TO, NULL);
 * if (has_link) {
 *     g_print("Skeleton link exists between wrist and thumb\n");
 * }
 * ]|
 *
 * ## Relating Keypoints to Other Metadata
 *
 * Since keypoints are #GstAnalyticsMtd, they can participate in relations:
 *
 * |[<!-- language="C" -->
 * // Get first keypoint
 * GstAnalyticsMtd keypoint;
 * gst_analytics_group_mtd_get_member(&hand_group, 0, &keypoint);
 *
 * // Relate to object detection
 * gst_analytics_relation_meta_set_relation(rmeta,
 *     GST_ANALYTICS_REL_TYPE_CONTAINED_BY, keypoint.id, od_mtd.id);
 * ]|
 *
 * Since: 1.30
 */

typedef struct _GstAnalyticsKeypointMtdData GstAnalyticsKeypointMtdData;

/**
 * GstAnalyticsKeypointMtdData:
 * @dimension: 2D or 3D keypoint
 * @x: X coordinate
 * @y: Y coordinate
 * @z: Z coordinate (0 for 2D keypoints)
 * @confidence: Confidence score
 * @visibility_flags: Combination of #GstAnalyticsKeypointVisibility flags
 *
 * Store keypoint data.
 *
 * Since: 1.30
 */
struct _GstAnalyticsKeypointMtdData
{
  GstAnalyticsKeypointDimensions dimension;
  gint x;
  gint y;
  gint z;
  gfloat confidence;
  guint8 visibility_flags;
};

#define _GET_KEYPOINT_STRIDE(dim) \
    (dim == GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D ? 2 : 3)

static void gst_analytics_keypoint_mtd_clear (GstBuffer * buffer,
    GstAnalyticsMtd * mtd);

static gboolean
gst_analytics_keypoint_mtd_meta_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  /* Handle coordinate transformation when buffer goes through video transforms */
  if (GST_VIDEO_META_TRANSFORM_IS_MATRIX (type)) {
    GstVideoMetaTransformMatrix *trans = data;
    GstAnalyticsKeypointMtdData *kpdata =
        gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
        transmtd->id);

    /* TODO: Add support for 3d point transformation, meanwhile project
     * z component on XY plane. */
    if (kpdata->z != 0) {
      GST_WARNING ("Only 2d keypoint transformation is supported, z component "
          "projected on XY plane");
      kpdata->z = 0;
      kpdata->dimension = GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D;
    }

    if (!gst_video_meta_transform_matrix_point (trans, &kpdata->x, &kpdata->y))
      return FALSE;

  } else if (GST_VIDEO_META_TRANSFORM_IS_SCALE (type)) {
    /* Handle scaling transforms (e.g., videoconvert with different resolution) */
    GstVideoMetaTransform *trans = data;
    GstAnalyticsKeypointMtdData *kpdata;
    gint ow, oh, nw, nh;

    ow = GST_VIDEO_INFO_WIDTH (trans->in_info);
    nw = GST_VIDEO_INFO_WIDTH (trans->out_info);
    oh = GST_VIDEO_INFO_HEIGHT (trans->in_info);
    nh = GST_VIDEO_INFO_HEIGHT (trans->out_info);

    kpdata = gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
        transmtd->id);

    GST_DEBUG ("Keypoint scale transform: in=(%ux%u) out=(%ux%u)",
        ow, oh, nw, nh);

    kpdata->x *= nw;
    kpdata->x /= ow;

    kpdata->y *= nh;
    kpdata->y /= oh;

    GST_DEBUG ("Keypoint scaled: (%d,%d) in frame (%u,%u) -> (%u,%u)",
        kpdata->x, kpdata->y, ow, oh, nw, nh);
  }

  return TRUE;
}

static const GstAnalyticsMtdImpl keypoint_impl = {
  "keypoint-mtd",
  gst_analytics_keypoint_mtd_meta_transform,
  gst_analytics_keypoint_mtd_clear
};

/**
 * gst_analytics_keypoint_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsMtd type.
 *
 * Returns: opaque id of #GstAnalyticsMtd type
 *
 * Since: 1.30
 */
GstAnalyticsMtdType
gst_analytics_keypoint_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & keypoint_impl;
}

/**
 * gst_analytics_relation_meta_add_keypoint_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add keypoint
 * @dimension: 2D or 3D keypoint dimension
 * @x: X coordinate, increases toward the right
 * @y: Y coordinate, increases toward the bottom
 * @z: Z coordinate, increases along the axis defined by a right-hand coordinate
 *   system with respect to X and Y, that is out of the XY plane. z=0 is on
 *   the XY plane. The unit of Z is the average of the X and Y units:
 *   (scale_x + scale_y) / 2.
 * @visibility_flags: Visibility flags, a combination of #GstAnalyticsKeypointVisibility values
 * @confidence: Confidence score
 * @keypoint_mtd: (out) (not nullable): Handle updated to newly added keypoint
 *
 * Add individual keypoint metadata to @instance. The point represents a location
 * in Euclidean space.
 *
 * Returns: TRUE if added successfully, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_keypoint_mtd (GstAnalyticsRelationMeta *
    instance, GstAnalyticsKeypointDimensions dimension, gint x, gint y, gint z,
    guint8 visibility_flags, gfloat confidence,
    GstAnalyticsKeypointMtd * keypoint_mtd)
{
  g_return_val_if_fail (instance, FALSE);
  g_return_val_if_fail (keypoint_mtd, FALSE);

  GstAnalyticsKeypointMtdData *keypoint_data = (GstAnalyticsKeypointMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, &keypoint_impl,
      sizeof (GstAnalyticsKeypointMtdData), keypoint_mtd);

  if (keypoint_data) {
    keypoint_data->dimension = dimension;
    keypoint_data->x = x;
    keypoint_data->y = y;
    keypoint_data->z = z;
    keypoint_data->visibility_flags = visibility_flags;
    keypoint_data->confidence = confidence;
    return TRUE;
  }

  return FALSE;
}

/**
 * gst_analytics_keypoint_mtd_get_position:
 * @handle: handle
 * @x: (out): X coordinate
 * @y: (out): Y coordinate
 * @z: (out) (nullable): Z coordinate, or %NULL to ignore. Always 0 for 2D keypoints.
 * @dimension: (out): Keypoint dimension (2D or 3D)
 *
 * Get keypoint position and dimension.
 * See #gst_analytics_relation_meta_add_keypoint_mtd for more details
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_keypoint_mtd_get_position (const GstAnalyticsKeypointMtd * handle,
    gint * x, gint * y, gint * z, GstAnalyticsKeypointDimensions * dimension)
{
  GstAnalyticsKeypointMtdData *keypoint_data;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (x, FALSE);
  g_return_val_if_fail (y, FALSE);
  g_return_val_if_fail (dimension, FALSE);

  keypoint_data = gst_analytics_relation_meta_get_mtd_data (handle->meta,
      handle->id);
  g_return_val_if_fail (keypoint_data != NULL, FALSE);

  *x = keypoint_data->x;
  *y = keypoint_data->y;

  if (z != NULL)
    *z = keypoint_data->z;
  else if (keypoint_data->z != 0)
    GST_DEBUG ("keypoint has non-zero z component but z was not read");

  *dimension = keypoint_data->dimension;

  return TRUE;
}

/**
 * gst_analytics_keypoint_mtd_get_confidence:
 * @handle: handle
 * @confidence: (out): Confidence score
 *
 * Get keypoint confidence.
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_keypoint_mtd_get_confidence (const GstAnalyticsKeypointMtd *
    handle, gfloat * confidence)
{
  GstAnalyticsKeypointMtdData *keypoint_data;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (confidence, FALSE);

  keypoint_data = gst_analytics_relation_meta_get_mtd_data (handle->meta,
      handle->id);
  g_return_val_if_fail (keypoint_data != NULL, FALSE);

  *confidence = keypoint_data->confidence;

  return TRUE;
}

/**
 * gst_analytics_relation_meta_get_keypoint_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsKeypointMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with keypoint mtd
 *
 * Retrieve keypoint metadata instance from @meta.
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_get_keypoint_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsKeypointMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_keypoint_mtd_get_mtd_type (),
      (GstAnalyticsKeypointMtd *) rlt);
}

/**
 * gst_analytics_relation_meta_add_keypoints_group:
 * @instance: Instance of #GstAnalyticsRelationMeta
 * @semantic_tag: Semantic tag for the group (e.g., "hand-21-kp", "pose-17-kp")
 * @dimension: 2D or 3D keypoints
 * @positions_len: length of @positions array.
 * @positions: (array length=positions_len): Array of positions [x0, y0, [z0], x1, y1, [z1], ...]
 * @keypoint_count: Number of keypoints
 * @confidences: (array length=keypoint_count) (nullable): Array of confidence scores [c0, c1, ...] or NULL
 * @visibilities: (array length=keypoint_count) (nullable): Array of #GstAnalyticsKeypointVisibility flags [v0, v1, ...] or NULL
 * @skeleton_pairs_len: Length of @skeleton_pairs array (must be even, representing pairs of keypoint indices)
 * @skeleton_pairs: (array length=skeleton_pairs_len) (nullable): Array of keypoint index pairs [kp1_idx, kp2_idx, kp1_idx, kp2_idx, ...] or NULL
 * @group_mtd: (out) (not nullable): Handle to newly created group
 *
 * Creates a group of keypoints with optional skeleton structure.
 * Individual keypoints are created as #GstAnalyticsMtd.
 * Skeleton links are represented as RELATE_TO relations between keypoints.
 *
 * The @skeleton_pairs array contains pairs of keypoint indices that are connected.
 * For example, [0, 1, 1, 2, 2, 3] creates links: 0<->1, 1<->2, 2<->3.
 *
 * See #gst_analytics_relation_meta_add_keypoint_mtd for more details
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_relation_meta_add_keypoints_group (GstAnalyticsRelationMeta *
    instance, const gchar * semantic_tag,
    GstAnalyticsKeypointDimensions dimension, gsize positions_len,
    const gint * positions, gsize keypoint_count, const gfloat * confidences,
    const guint8 * visibilities, gsize skeleton_pairs_len,
    const gint * skeleton_pairs, GstAnalyticsGroupMtd * group_mtd)
{
  gboolean ret;
  gsize i, pos_stride;
  guint *keypoint_ids;
  GstAnalyticsKeypointMtd kp_mtd;

  g_return_val_if_fail (instance, FALSE);
  g_return_val_if_fail (semantic_tag, FALSE);
  g_return_val_if_fail (positions, FALSE);
  g_return_val_if_fail (group_mtd, FALSE);
  g_return_val_if_fail ((positions_len % _GET_KEYPOINT_STRIDE (dimension)) == 0,
      FALSE);

  /* confidences is optional and if it's not provided, introspection will
   * set keypoint_count to 0. To overcome this issue will calculate
   * keypoint_count based on dimension and positions_len. */
  if (keypoint_count == 0)
    keypoint_count = positions_len / _GET_KEYPOINT_STRIDE (dimension);

  /* Create group with space for all keypoints */
  ret = gst_analytics_relation_meta_add_group_mtd_with_size (instance,
      keypoint_count, group_mtd);
  if (!ret) {
    return FALSE;
  }

  /* Set semantic tag */
  gst_analytics_group_mtd_set_semantic_tag (group_mtd, semantic_tag);

  /* Allocate array to store keypoint IDs for skeleton processing */
  keypoint_ids = g_malloc_n (keypoint_count, sizeof (guint));

  /* Position stride depends on dimension */
  pos_stride = _GET_KEYPOINT_STRIDE (dimension);

  /* Create individual keypoints and add to group */
  for (i = 0; i < keypoint_count; i++) {
    gint x, y, z = 0;
    gfloat confidence;

    /* Extract position from array */
    x = positions[i * pos_stride];
    y = positions[i * pos_stride + 1];
    if (dimension == GST_ANALYTICS_KEYPOINT_DIMENSIONS_3D) {
      z = positions[i * pos_stride + 2];
    }

    /* Get confidence and visibility or use defaults */
    confidence = confidences ? confidences[i] : 1.0f;
    guint8 visibility = visibilities ? visibilities[i]
        : GST_ANALYTICS_KEYPOINT_VISIBILITY_UNKNOWN;

    /* Add keypoint metadata which may trigger buffer reallocation */
    ret = gst_analytics_relation_meta_add_keypoint_mtd (instance, dimension,
        x, y, z, visibility, confidence, &kp_mtd);
    if (!ret) {
      GST_WARNING ("Failed to add keypoint %zu", i);
      g_free (keypoint_ids);
      return FALSE;
    }

    /* Store ID for skeleton processing */
    keypoint_ids[i] = kp_mtd.id;

    /* Update group meta pointer in case buffer was reallocated */
    group_mtd->meta = kp_mtd.meta;

    /* Add keypoint to group */
    ret = gst_analytics_group_mtd_add_member (group_mtd, kp_mtd.id);
    if (!ret) {
      GST_WARNING ("Failed to add keypoint %zu to group", i);
      g_free (keypoint_ids);
      return FALSE;
    }
  }

  /* Create skeleton links as relations */
  if (skeleton_pairs && skeleton_pairs_len > 0) {
    gsize skeleton_count;

    /* Validate array length is even (pairs of indices) */
    if (skeleton_pairs_len % 2 != 0) {
      GST_WARNING ("skeleton_pairs_len must be even (got %zu)",
          skeleton_pairs_len);
      g_free (keypoint_ids);
      return FALSE;
    }

    skeleton_count = skeleton_pairs_len / 2;

    for (i = 0; i < skeleton_count; i++) {
      guint kp1_idx = skeleton_pairs[i * 2];
      guint kp2_idx = skeleton_pairs[i * 2 + 1];

      /* Validate indices */
      if (kp1_idx >= keypoint_count || kp2_idx >= keypoint_count) {
        GST_WARNING
            ("Invalid skeleton link indices: %u, %u (keypoint_count: %zu)",
            kp1_idx, kp2_idx, keypoint_count);
        g_free (keypoint_ids);
        return FALSE;
      }

      /* Create relation between keypoints */
      ret = gst_analytics_relation_meta_set_relation (instance,
          GST_ANALYTICS_REL_TYPE_RELATE_TO,
          keypoint_ids[kp1_idx], keypoint_ids[kp2_idx]);
      if (!ret) {
        GST_WARNING ("Failed to create skeleton link between %u and %u",
            kp1_idx, kp2_idx);
        g_free (keypoint_ids);
        return FALSE;
      }
    }
  }

  g_free (keypoint_ids);
  return TRUE;
}

/**
 * gst_analytics_keypoint_mtd_get_visibility_flags:
 * @handle: handle
 * @visibility_flags: (out): Visibility flags, a combination of #GstAnalyticsKeypointVisibility values
 *
 * Get keypoint visibility flags.
 *
 * Returns: TRUE if successful, FALSE otherwise
 *
 * Since: 1.30
 */
gboolean
gst_analytics_keypoint_mtd_get_visibility_flags (const GstAnalyticsKeypointMtd *
    handle, guint8 * visibility_flags)
{
  GstAnalyticsKeypointMtdData *keypoint_data;

  g_return_val_if_fail (handle, FALSE);
  g_return_val_if_fail (visibility_flags, FALSE);

  keypoint_data = gst_analytics_relation_meta_get_mtd_data (handle->meta,
      handle->id);
  g_return_val_if_fail (keypoint_data != NULL, FALSE);

  *visibility_flags = keypoint_data->visibility_flags;

  return TRUE;
}

static void
gst_analytics_keypoint_mtd_clear (GstBuffer * buffer, GstAnalyticsMtd * mtd)
{
  (void) buffer;
  (void) mtd;
  /* No cleanup needed for keypoint metadata */
}

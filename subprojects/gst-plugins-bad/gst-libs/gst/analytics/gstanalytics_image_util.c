/* GStreamer
 * Copyright (C) 2025 Collabora Ltd
 *  @author: Daniel Morin <daniel.morin@dmohub.org>
 *
 * gstanalytics_image_util.c
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

#include "gstanalytics_image_util.h"

/* Evaluate if there's an intersection between segement s1 and s2 */
static guint
linear_intersection_uint (guint s1_min, guint s1_max, guint s2_min,
    guint s2_max)
{
  guint tmp;
  if (s1_max > s2_min && s2_max > s1_min) {
    if (s1_min > s2_min) {
      tmp = (s2_max > s1_max) ? s1_max : s2_max;
      return tmp - s1_min;
    } else {
      tmp = (s1_max > s2_max) ? s2_max : s1_max;
      return tmp - s2_min;
    }
  }
  return 0.0f;
}

static gfloat
linear_intersection_float (gfloat s1_min, gfloat s1_max, gfloat s2_min,
    gfloat s2_max)
{
  gfloat tmp;
  if (s1_max > s2_min && s2_max > s1_min) {
    if (s1_min > s2_min) {
      tmp = (s2_max > s1_max) ? s1_max : s2_max;
      return tmp - s1_min;
    } else {
      tmp = (s1_max > s2_max) ? s2_max : s1_max;
      return tmp - s2_min;
    }
  }
  return 0.0f;
}


static gboolean
_clips_and_adj_dim_int (gint * xy, gint * wh)
{
  g_return_val_if_fail (xy != NULL, FALSE);
  g_return_val_if_fail (wh != NULL, FALSE);

  if (*xy < 0) {
    if ((*xy + *wh) < 0) {
      /* Bouding box completly outside the visible area */
      return FALSE;
    }

    /* reduce width by the portion that is negative */
    *wh += *xy;
    *xy = 0;
  }
  return TRUE;
}

static gboolean
_clips_and_adj_dim_float (gfloat * xy, gfloat * wh)
{
  g_return_val_if_fail (xy != NULL, FALSE);
  g_return_val_if_fail (wh != NULL, FALSE);

  if (*xy < 0.0f) {
    if ((*xy + *wh) < 0.0f) {
      /* Bouding box completly outside the visible area */
      return FALSE;
    }

    /* reduce width by the portion that is negative */
    *wh += *xy;
    *xy = 0.0;
  }
  return TRUE;
}

/**
 * gst_analytics_image_util_iou_int:
 * @bb1_x: Bounding box 1, X coordinate
 * @bb1_y: Bounding box 1, Y coordinate
 * @bb1_w: Bounding box 1, width
 * @bb1_h: Bounding box 1, height
 * @bb2_x: Bounding box 2, X coordinate
 * @bb2_y: Bounding box 2, Y coordinate
 * @bb2_w: Bounding box 2, width
 * @bb2_h: Bounding box 2, height
 *
 * Calculate the intersection over the union (IoU) of the two areas defined by
 * the bounding box 1 and bounding box 2. IoU is a measure of how much two
 * regions overlap.
 *
 * Return: IoU of bb1 and bb2.
 *
 * Since: 1.28
 */
gfloat
gst_analytics_image_util_iou_int (gint bb1_x, gint bb1_y, gint bb1_w,
    gint bb1_h, gint bb2_x, gint bb2_y, gint bb2_w, gint bb2_h)
{
  if (_clips_and_adj_dim_int (&bb1_x, &bb1_w) == FALSE) {
    return 0.0f;
  }

  if (_clips_and_adj_dim_int (&bb1_y, &bb1_h) == FALSE) {
    return 0.0f;
  }

  if (_clips_and_adj_dim_int (&bb2_x, &bb2_w) == FALSE) {
    return 0.0f;
  }

  if (_clips_and_adj_dim_int (&bb2_y, &bb2_h) == FALSE) {
    return 0.0f;
  }

  /* Rational: linear intersection is much faster to calculate then
   * 2d intersection. We project the two bounding boxes considered for
   * intersection on one axis and verify if the segments the create intersect.
   * If they don't, the bounding boxes can't intersect in 2d and we don't
   * need to verify if they intersect on the other dimension. If they
   * intersect on the first dimension we verify if they intersec on the other
   * dimension. Again if the don't intersect the bounding boxes can't intersect
   * on in a 2D space. If they intersected on both axis we calculate the IoU.*/
  const guint x_intersection =
      linear_intersection_uint (bb1_x, bb1_x + bb1_w, bb2_x, bb2_x + bb2_w);
  if (x_intersection > 0) {
    const guint y_intersection = linear_intersection_uint (bb1_y, bb1_y + bb1_h,
        bb2_y, bb2_y + bb2_h);
    if (y_intersection > 0) {
      const guint bb1_area = bb1_w * bb1_h;
      const guint bb2_area = bb2_w * bb2_h;
      const guint intersect_area = x_intersection * y_intersection;
      const guint union_area = bb1_area + bb2_area - intersect_area;
      return union_area == 0 ? 0.0f : ((gfloat) intersect_area) / union_area;
    }
  }

  return 0.0f;
}

/**
 * gst_analytics_image_util_iou_float:
 * @bb1_x: Bounding box 1, X coordinate
 * @bb1_y: Bounding box 1, Y coordinate
 * @bb1_w: Bounding box 1, width
 * @bb1_h: Bounding box 1, height
 * @bb2_x: Bounding box 2, X coordinate
 * @bb2_y: Bounding box 2, Y coordinate
 * @bb2_w: Bounding box 2, width
 * @bb2_h: Bounding box 2, height
 *
 * Calculate the intersection over the union (IoU) of the two areas defined by
 * the bounding box 1 and bounding box 2. IoU is a measure of how much two
 * regions overlap.
 *
 * Return: IoU of bb1 and bb2.
 *
 * Since: 1.28
 */
gfloat
gst_analytics_image_util_iou_float (gfloat bb1_x, gfloat bb1_y, gfloat bb1_w,
    gfloat bb1_h, gfloat bb2_x, gfloat bb2_y, gfloat bb2_w, gfloat bb2_h)
{
  if (_clips_and_adj_dim_float (&bb1_x, &bb1_w) == FALSE) {
    return 0.0f;
  }

  if (_clips_and_adj_dim_float (&bb1_y, &bb1_h) == FALSE) {
    return 0.0f;
  }

  if (_clips_and_adj_dim_float (&bb2_x, &bb2_w) == FALSE) {
    return 0.0f;
  }

  if (_clips_and_adj_dim_float (&bb2_y, &bb2_h) == FALSE) {
    return 0.0f;
  }

  /* Rational: linear intersection is much faster to calculate then
   * 2d intersection. We project the two bounding boxes considered for
   * intersection on one axis and verify if the segments the create intersect.
   * If they don't, the bounding boxes can't intersect in 2d and we don't
   * need to verify if they intersect on the other dimension. If they
   * intersect on the first dimension we verify if they intersec on the other
   * dimension. Again if the don't intersect the bounding boxes can't intersect
   * on in a 2D space. If they intersected on both axis we calculate the IoU.*/
  const gfloat x_intersection =
      linear_intersection_float (bb1_x, bb1_x + bb1_w, bb2_x, bb2_x + bb2_w);
  if (x_intersection > 0) {
    const float y_intersection =
        linear_intersection_float (bb1_y, bb1_y + bb1_h,
        bb2_y, bb2_y + bb2_h);
    if (y_intersection > 0) {
      const gfloat bb1_area = bb1_w * bb1_h;
      const gfloat bb2_area = bb2_w * bb2_h;
      const gfloat intersect_area = x_intersection * y_intersection;
      const gfloat union_area = bb1_area + bb2_area - intersect_area;
      return union_area == 0.0f ? 0.0f : intersect_area / union_area;
    }
  }

  return 0.0f;
}

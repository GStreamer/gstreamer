/* GStreamer
 * Copyright (C) 2023 He Junyan <junyan.he@intel.com>
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
#include <config.h>
#endif

#include "gsth266picture.h"
#include "gstcodecpicture-private.h"

GST_DEBUG_CATEGORY_EXTERN (gst_h266_decoder_debug);
#define GST_CAT_DEFAULT gst_h266_decoder_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstH266Picture, gst_h266_picture);

/**
 * gst_h266_picture_new:
 *
 * Create new #GstH266Picture
 *
 * Returns: a new #GstH266Picture
 *
 * Since: 1.26
 */
GstH266Picture *
gst_h266_picture_new (void)
{
  GstH266Picture *pic;

  pic = g_new0 (GstH266Picture, 1);

  pic->ff_info.field_pic_flag = 0;
  pic->ff_info.display_fields_from_frame_flag = 0;
  /* 0: interlaced, 1: progressive, 2: unspecified, 3: reserved, can be
   * interpreted as 2 */
  pic->ff_info.source_scan_type = 2;
  pic->ff_info.duplicate_flag = 0;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (pic), 0,
      GST_TYPE_H266_PICTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_codec_picture_free);

  return pic;
}

/**
 * GstH266Dpb:
 *
 * The #GstH266Dpb represents the dpb for decoding.
 *
 * Since: 1.26
 */
struct _GstH266Dpb
{
  GArray *pic_list;
  gint max_num_pics;
  gint num_output_needed;
};

/**
 * gst_h266_dpb_new: (skip)
 *
 * Create new #GstH266Dpb
 *
 * Returns: a new #GstH266Dpb
 *
 * Since: 1.26
 */
GstH266Dpb *
gst_h266_dpb_new (void)
{
  GstH266Dpb *dpb;

  dpb = g_new0 (GstH266Dpb, 1);

  dpb->pic_list = g_array_sized_new (FALSE, TRUE, sizeof (GstH266Picture *),
      GST_H266_MAX_DPB_SIZE);
  g_array_set_clear_func (dpb->pic_list,
      (GDestroyNotify) gst_clear_h266_picture);

  return dpb;
}

/**
 * gst_h266_dpb_set_max_num_pics:
 * @dpb: a #GstH266Dpb
 * @max_num_pics: the maximum number of picture
 *
 * Set the number of maximum allowed pictures to store
 *
 * Since: 1.26
 */
void
gst_h266_dpb_set_max_num_pics (GstH266Dpb * dpb, gint max_num_pics)
{
  g_return_if_fail (dpb != NULL);

  dpb->max_num_pics = max_num_pics;
}

/**
 * gst_h266_dpb_get_max_num_pics:
 * @dpb: a #GstH266Dpb
 *
 * Returns: the number of maximum pictures
 *
 * Since: 1.26
 */
gint
gst_h266_dpb_get_max_num_pics (GstH266Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, 0);

  return dpb->max_num_pics;
}

/**
 * gst_h266_dpb_free:
 * @dpb: a #GstH266Dpb to free
 *
 * Free the @dpb
 *
 * Since: 1.26
 */
void
gst_h266_dpb_free (GstH266Dpb * dpb)
{
  g_return_if_fail (dpb != NULL);

  gst_h266_dpb_clear (dpb);
  g_array_unref (dpb->pic_list);
  g_free (dpb);
}

/**
 * gst_h266_dpb_clear:
 * @dpb: a #GstH266Dpb
 *
 * Clear all stored #GstH266Picture
 *
 * Since: 1.26
 */
void
gst_h266_dpb_clear (GstH266Dpb * dpb)
{
  g_return_if_fail (dpb != NULL);

  g_array_set_size (dpb->pic_list, 0);
  dpb->num_output_needed = 0;
}

/**
 * gst_h266_dpb_add:
 * @dpb: a #GstH266Dpb
 * @picture: (transfer full): a #GstH266Picture
 *
 * Store the @picture and perform increase pic_latency_cnt as defined in
 * "C.5.2.3 Additional bumping" process
 *
 * Since: 1.26
 */
void
gst_h266_dpb_add (GstH266Dpb * dpb, GstH266Picture * picture)
{
  g_return_if_fail (dpb != NULL);
  g_return_if_fail (GST_IS_H266_PICTURE (picture));

  if (picture->output_flag) {
    gint i;

    for (i = 0; i < dpb->pic_list->len; i++) {
      GstH266Picture *other =
          g_array_index (dpb->pic_list, GstH266Picture *, i);

      if (other->needed_for_output)
        other->pic_latency_cnt++;
    }

    dpb->num_output_needed++;
    picture->needed_for_output = TRUE;
  } else {
    picture->needed_for_output = FALSE;
  }

  /* C.3.4 */
  picture->ref = TRUE;
  picture->long_term = FALSE;

  g_array_append_val (dpb->pic_list, picture);
}

/**
 * gst_h266_dpb_delete_unused:
 * @dpb: a #GstH266Dpb
 *
 * Delete unneeded pictures from dpb as defined in "C.5.2.2 Output and
 * removal of pictures from the DPB".
 *
 * Since: 1.26
 */
void
gst_h266_dpb_delete_unused (GstH266Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    if (!picture->needed_for_output && !picture->ref) {
      GST_TRACE ("remove picture %p (poc %d) from dpb",
          picture, picture->pic_order_cnt);
      g_array_remove_index (dpb->pic_list, i);
      i--;
    }
  }
}

/**
 * gst_h266_dpb_num_ref_pictures:
 * @dpb: a #GstH266Dpb
 *
 * Returns: The number of referenced pictures in dpb.
 *
 * Since: 1.26
 */
gint
gst_h266_dpb_num_ref_pictures (GstH266Dpb * dpb)
{
  gint i;
  gint ret = 0;

  g_return_val_if_fail (dpb != NULL, -1);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    if (picture->ref)
      ret++;
  }

  return ret;
}

static gboolean
gst_h266_dpb_check_latency_count (GstH266Dpb * dpb, guint32 max_latency)
{
  gint i;

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    if (!picture->needed_for_output)
      continue;

    if (picture->pic_latency_cnt >= max_latency)
      return TRUE;
  }

  return FALSE;
}

/**
 * gst_h266_dpb_needs_bump:
 * @dpb: a #GstH266Dpb
 * @max_num_reorder_pics: dpb_max_num_reorder_pics[HighestTid]
 * @max_latency_increase: MaxLatencyPictures[HighestTid]
 * @max_dec_pic_buffering: dpb_max_dec_pic_buffering_minus1[HighestTid] + 1
 *   or zero if this shouldn't be used for bumping decision.
 *
 * Returns: %TRUE if bumping is required
 *
 * Since: 1.26
 */
gboolean
gst_h266_dpb_needs_bump (GstH266Dpb * dpb, guint max_num_reorder_pics,
    guint max_latency_increase, guint max_dec_pic_buffering)
{
  g_return_val_if_fail (dpb != NULL, FALSE);
  g_assert (dpb->num_output_needed >= 0);

  /* If DPB is full and there is no empty space to store current picture,
   * need bumping.
   * NOTE: current picture was added already by our decoding flow, so we
   * need to do bumping until dpb->pic_list->len == dpb->max_num_pic
   */
  if (dpb->pic_list->len > dpb->max_num_pics) {
    GST_TRACE ("No empty frame buffer, need bumping");
    return TRUE;
  }

  /* C.5.2.3 */
  if (dpb->num_output_needed > max_num_reorder_pics) {
    GST_TRACE ("num_output_needed (%d) > max_num_reorder_pics (%d)",
        dpb->num_output_needed, max_num_reorder_pics);
    return TRUE;
  }

  if (dpb->num_output_needed && max_latency_increase &&
      gst_h266_dpb_check_latency_count (dpb, max_latency_increase)) {
    GST_TRACE ("has late picture, max_latency_increase: %d",
        max_latency_increase);
    return TRUE;
  }

  /* C.5.2.2 */
  if (max_dec_pic_buffering && dpb->pic_list->len >= max_dec_pic_buffering) {
    GST_TRACE ("dpb size (%d) >= max_dec_pic_buffering (%d)",
        dpb->pic_list->len, max_dec_pic_buffering);
    return TRUE;
  }

  return FALSE;
}

static gint
gst_h266_dpb_get_lowest_output_needed_picture (GstH266Dpb * dpb,
    GstH266Picture ** picture)
{
  gint i;
  GstH266Picture *lowest = NULL;
  gint index = -1;

  *picture = NULL;

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    if (!picture->needed_for_output)
      continue;

    if (!lowest) {
      lowest = picture;
      index = i;
      continue;
    }

    if (picture->pic_order_cnt < lowest->pic_order_cnt) {
      lowest = picture;
      index = i;
    }
  }

  if (lowest)
    *picture = gst_h266_picture_ref (lowest);

  return index;
}

/**
 * gst_h266_dpb_bump:
 * @dpb: a #GstH266Dpb
 * @drain: whether draining or not
 *
 * Perform bumping process as defined in C.5.2.4 "Bumping" process.
 * If @drain is %TRUE, @dpb will remove a #GstH266Picture from internal array
 * so that returned #GstH266Picture could hold the last reference of it.
 *
 * Returns: (nullable) (transfer full): a #GstH266Picture which is needed to be
 * outputted
 *
 * Since: 1.26
 */
GstH266Picture *
gst_h266_dpb_bump (GstH266Dpb * dpb, gboolean drain)
{
  GstH266Picture *picture;
  gint index;

  g_return_val_if_fail (dpb != NULL, NULL);

  /* C.5.2.4 "Bumping" process */
  index = gst_h266_dpb_get_lowest_output_needed_picture (dpb, &picture);

  if (!picture || index < 0)
    return NULL;

  picture->needed_for_output = FALSE;

  dpb->num_output_needed--;
  g_assert (dpb->num_output_needed >= 0);

  if (!picture->ref || drain)
    g_array_remove_index_fast (dpb->pic_list, index);

  return picture;
}

/**
 * gst_h266_dpb_mark_all_non_ref:
 * @dpb: a #GstH266Dpb
 *
 * Mark all pictures are not referenced
 *
 * Since: 1.26
 */
void
gst_h266_dpb_mark_all_non_ref (GstH266Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    picture->ref = FALSE;
  }
}

/**
 * gst_h266_dpb_mark_all_non_output:
 * @dpb: a #GstH266Dpb
 *
 * Mark all pictures are no needed for output
 *
 * Since: 1.26
 */
void
gst_h266_dpb_mark_all_non_output (GstH266Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    picture->needed_for_output = FALSE;
  }

  dpb->num_output_needed = 0;
}

/**
 * gst_h266_dpb_get_size:
 * @dpb: a #GstH266Dpb
 *
 * Return: the length of stored dpb array
 *
 * Since: 1.26
 */
gint
gst_h266_dpb_get_size (GstH266Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, -1);

  return dpb->pic_list->len;
}

/**
 * gst_h266_dpb_get_picture_by_poc_lsb:
 * @dpb: a #GstH266Dpb
 * @poc_lsb: a picture order count lsb
 *
 * Find a picture which has matching poc_lsb
 *
 * Returns: (nullable) (transfer full): a #GstH266Picture
 *
 * Since: 1.26
 */
GstH266Picture *
gst_h266_dpb_get_picture_by_poc_lsb (GstH266Dpb * dpb, gint poc_lsb)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    if (picture->pic_order_cnt_lsb == poc_lsb)
      return gst_h266_picture_ref (picture);
  }

  GST_DEBUG ("No reference picture for poc lsb %d", poc_lsb);

  return NULL;
}

/**
 * gst_h266_dpb_get_picture_by_poc:
 * @dpb: a #GstH266Dpb
 * @poc: a picture order count
 *
 * Find a picture which has matching poc
 *
 * Returns: (nullable) (transfer full): a #GstH266Picture
 *
 * Since: 1.26
 */
GstH266Picture *
gst_h266_dpb_get_picture_by_poc (GstH266Dpb * dpb, gint poc)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH266Picture *picture =
        g_array_index (dpb->pic_list, GstH266Picture *, i);

    if (picture->pic_order_cnt == poc)
      return gst_h266_picture_ref (picture);
  }

  GST_DEBUG ("No picture for poc %d", poc);

  return NULL;
}

/**
 * gst_h266_dpb_get_pictures_all:
 * @dpb: a #GstH266Dpb
 *
 * Return: (element-type GstH266Picture) (transfer full): a #GArray of
 *   #GstH266Picture stored in @dpb
 *
 * Since: 1.26
 */
GArray *
gst_h266_dpb_get_pictures_all (GstH266Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, NULL);

  return g_array_ref (dpb->pic_list);
}

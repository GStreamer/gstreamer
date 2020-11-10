/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "gsth264picture.h"
#include <stdlib.h>

GST_DEBUG_CATEGORY_EXTERN (gst_h264_decoder_debug);
#define GST_CAT_DEFAULT gst_h264_decoder_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstH264Picture, gst_h264_picture);

static void
_gst_h264_picture_free (GstH264Picture * picture)
{
  if (picture->notify)
    picture->notify (picture->user_data);

  g_free (picture);
}

/**
 * gst_h264_picture_new:
 *
 * Create new #GstH264Picture
 *
 * Returns: a new #GstH264Picture
 */
GstH264Picture *
gst_h264_picture_new (void)
{
  GstH264Picture *pic;

  pic = g_new0 (GstH264Picture, 1);

  pic->top_field_order_cnt = G_MAXINT32;
  pic->bottom_field_order_cnt = G_MAXINT32;
  pic->field = GST_H264_PICTURE_FIELD_FRAME;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (pic), 0,
      GST_TYPE_H264_PICTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) _gst_h264_picture_free);

  return pic;
}

/**
 * gst_h264_picture_set_user_data:
 * @picture: a #GstH264Picture
 * @user_data: private data
 * @notify: (closure user_data): a #GDestroyNotify
 *
 * Sets @user_data on the picture and the #GDestroyNotify that will be called when
 * the picture is freed.
 *
 * If a @user_data was previously set, then the previous set @notify will be called
 * before the @user_data is replaced.
 */
void
gst_h264_picture_set_user_data (GstH264Picture * picture, gpointer user_data,
    GDestroyNotify notify)
{
  g_return_if_fail (GST_IS_H264_PICTURE (picture));

  if (picture->notify)
    picture->notify (picture->user_data);

  picture->user_data = user_data;
  picture->notify = notify;
}

/**
 * gst_h264_picture_get_user_data:
 * @picture: a #GstH264Picture
 *
 * Gets private data set on the picture via
 * gst_h264_picture_set_user_data() previously.
 *
 * Returns: (transfer none): The previously set user_data
 */
gpointer
gst_h264_picture_get_user_data (GstH264Picture * picture)
{
  return picture->user_data;
}

struct _GstH264Dpb
{
  GArray *pic_list;
  gint max_num_pics;
  gint num_output_needed;
  gint32 last_output_poc;
};

static void
gst_h264_dpb_init (GstH264Dpb * dpb)
{
  dpb->num_output_needed = 0;
  dpb->last_output_poc = G_MININT32;
}

/**
 * gst_h264_dpb_new: (skip)
 *
 * Create new #GstH264Dpb
 *
 * Returns: a new #GstH264Dpb
 */
GstH264Dpb *
gst_h264_dpb_new (void)
{
  GstH264Dpb *dpb;

  dpb = g_new0 (GstH264Dpb, 1);
  gst_h264_dpb_init (dpb);

  dpb->pic_list =
      g_array_sized_new (FALSE, TRUE, sizeof (GstH264Picture *),
      GST_H264_DPB_MAX_SIZE);
  g_array_set_clear_func (dpb->pic_list,
      (GDestroyNotify) gst_h264_picture_clear);

  return dpb;
}

/**
 * gst_h264_dpb_set_max_num_pics:
 * @dpb: a #GstH264Dpb
 * @max_num_pics: the maximum number of picture
 *
 * Set the number of maximum allowed pictures to store
 */
void
gst_h264_dpb_set_max_num_pics (GstH264Dpb * dpb, gint max_num_pics)
{
  g_return_if_fail (dpb != NULL);

  dpb->max_num_pics = max_num_pics;
}

/**
 * gst_h264_dpb_get_max_num_pics:
 * @dpb: a #GstH264Dpb
 *
 * Returns: the number of maximum pictures
 */
gint
gst_h264_dpb_get_max_num_pics (GstH264Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, 0);

  return dpb->max_num_pics;
}

/**
 * gst_h264_dpb_free:
 * @dpb: a #GstH264Dpb to free
 *
 * Free the @dpb
 */
void
gst_h264_dpb_free (GstH264Dpb * dpb)
{
  g_return_if_fail (dpb != NULL);

  gst_h264_dpb_clear (dpb);
  g_array_unref (dpb->pic_list);
  g_free (dpb);
}

/**
 * gst_h264_dpb_clear:
 * @dpb: a #GstH264Dpb
 *
 * Clear all stored #GstH264Picture
 */
void
gst_h264_dpb_clear (GstH264Dpb * dpb)
{
  g_return_if_fail (dpb != NULL);

  g_array_set_size (dpb->pic_list, 0);
  gst_h264_dpb_init (dpb);
}

/**
 * gst_h264_dpb_add:
 * @dpb: a #GstH264Dpb
 * @picture: (transfer full): a #GstH264Picture
 *
 * Store the @picture
 */
void
gst_h264_dpb_add (GstH264Dpb * dpb, GstH264Picture * picture)
{
  g_return_if_fail (dpb != NULL);
  g_return_if_fail (GST_IS_H264_PICTURE (picture));

  /* C.4.2 Decoding of gaps in frame_num and storage of "non-existing" pictures
   *
   * The "non-existing" frame is stored in an empty frame buffer and is marked
   * as "not needed for output", and the DPB fullness is incremented by one */
  if (!picture->nonexisting) {
    picture->needed_for_output = TRUE;
    dpb->num_output_needed++;
  } else {
    picture->needed_for_output = FALSE;
  }

  g_array_append_val (dpb->pic_list, picture);
}

/**
 * gst_h264_dpb_delete_unused:
 * @dpb: a #GstH264Dpb
 *
 * Delete already outputted and not referenced all pictures from dpb
 */
void
gst_h264_dpb_delete_unused (GstH264Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    /* NOTE: don't use g_array_remove_index_fast here since the last picture
     * need to be referenced for bumping decision */
    if (!picture->needed_for_output && !picture->ref) {
      GST_TRACE ("remove picture %p (frame num %d) from dpb",
          picture, picture->frame_num);
      g_array_remove_index (dpb->pic_list, i);
      i--;
    }
  }
}

/**
 * gst_h264_dpb_num_ref_pictures:
 * @dpb: a #GstH264Dpb
 *
 * Returns: The number of referenced pictures
 */
gint
gst_h264_dpb_num_ref_pictures (GstH264Dpb * dpb)
{
  gint i;
  gint ret = 0;

  g_return_val_if_fail (dpb != NULL, -1);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref)
      ret++;
  }

  return ret;
}

/**
 * gst_h264_dpb_mark_all_non_ref:
 * @dpb: a #GstH264Dpb
 *
 * Mark all pictures are not referenced
 */
void
gst_h264_dpb_mark_all_non_ref (GstH264Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    picture->ref = FALSE;
  }
}

/**
 * gst_h264_dpb_get_short_ref_by_pic_num:
 * @dpb: a #GstH264Dpb
 * @pic_num: a picture number
 *
 * Find a short term reference picture which has matching picture number
 *
 * Returns: (nullable) (transfer none): a #GstH264Picture
 */
GstH264Picture *
gst_h264_dpb_get_short_ref_by_pic_num (GstH264Dpb * dpb, gint pic_num)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref && !picture->long_term && picture->pic_num == pic_num)
      return picture;
  }

  GST_WARNING ("No short term reference picture for %d", pic_num);

  return NULL;
}

/**
 * gst_h264_dpb_get_long_ref_by_long_term_pic_num:
 * @dpb: a #GstH264Dpb
 * @long_term_pic_num: a long term picture number
 *
 * Find a long term reference picture which has matching long term picture number
 *
 * Returns: (nullable) (transfer none): a #GstH264Picture
 *
 * Since: 1.20
 */
GstH264Picture *
gst_h264_dpb_get_long_ref_by_long_term_pic_num (GstH264Dpb * dpb,
    gint long_term_pic_num)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref && picture->long_term &&
        picture->long_term_pic_num == long_term_pic_num)
      return picture;
  }

  GST_WARNING ("No long term reference picture for %d", long_term_pic_num);

  return NULL;
}

/**
 * gst_h264_dpb_get_lowest_frame_num_short_ref:
 * @dpb: a #GstH264Dpb
 *
 * Find a short term reference picture which has the lowest frame_num_wrap
 *
 * Returns: (transfer full): a #GstH264Picture
 */
GstH264Picture *
gst_h264_dpb_get_lowest_frame_num_short_ref (GstH264Dpb * dpb)
{
  gint i;
  GstH264Picture *ret = NULL;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref && !picture->long_term &&
        (!ret || picture->frame_num_wrap < ret->frame_num_wrap))
      ret = picture;
  }

  if (ret)
    gst_h264_picture_ref (ret);

  return ret;
}

/**
 * gst_h264_dpb_get_pictures_short_term_ref:
 * @dpb: a #GstH264Dpb
 * @out: (out) (element-type GstH264Picture) (transfer full): an array
 *   of #GstH264Picture pointers
 *
 * Retrieve all short-term reference pictures from @dpb. The picture will be
 * appended to the array.
 */
void
gst_h264_dpb_get_pictures_short_term_ref (GstH264Dpb * dpb, GArray * out)
{
  gint i;

  g_return_if_fail (dpb != NULL);
  g_return_if_fail (out != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref && !picture->long_term) {
      gst_h264_picture_ref (picture);
      g_array_append_val (out, picture);
    }
  }
}

/**
 * gst_h264_dpb_get_pictures_long_term_ref:
 * @dpb: a #GstH264Dpb
 * @out: (out) (element-type GstH264Picture) (transfer full): an arrat
 *   of #GstH264Picture pointer
 *
 * Retrieve all long-term reference pictures from @dpb. The picture will be
 * appended to the array.
 */
void
gst_h264_dpb_get_pictures_long_term_ref (GstH264Dpb * dpb, GArray * out)
{
  gint i;

  g_return_if_fail (dpb != NULL);
  g_return_if_fail (out != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref && picture->long_term) {
      gst_h264_picture_ref (picture);
      g_array_append_val (out, picture);
    }
  }
}

/**
 * gst_h264_dpb_get_pictures_all:
 * @dpb: a #GstH264Dpb
 *
 * Return: (element-type GstH264Picture) (transfer full): a #GArray of
 *   #GstH264Picture stored in @dpb
 */
GArray *
gst_h264_dpb_get_pictures_all (GstH264Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, NULL);

  return g_array_ref (dpb->pic_list);
}

/**
 * gst_h264_dpb_get_size:
 * @dpb: a #GstH264Dpb
 *
 * Return: the length of stored dpb array
 */
gint
gst_h264_dpb_get_size (GstH264Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, -1);

  return dpb->pic_list->len;
}

/**
 * gst_h264_dpb_get_picture:
 * @dpb: a #GstH264Dpb
 * @system_frame_number The system frame number
 *
 * Returns: (transfer full): the picture identified with the specified
 * @system_frame_number, or %NULL if DPB does not contain a #GstH264Picture
 * corresponding to the @system_frame_number
 *
 * Since: 1.18
 */
GstH264Picture *
gst_h264_dpb_get_picture (GstH264Dpb * dpb, guint32 system_frame_number)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->system_frame_number == system_frame_number) {
      gst_h264_picture_ref (picture);
      return picture;
    }
  }

  return NULL;
}

static gboolean
gst_h264_dpb_has_empty_frame_buffer (GstH264Dpb * dpb)
{
  if (dpb->pic_list->len <= dpb->max_num_pics)
    return TRUE;

  return FALSE;
}

static gint
gst_h264_dpb_get_lowest_output_needed_picture (GstH264Dpb * dpb,
    GstH264Picture ** picture)
{
  gint i;
  GstH264Picture *lowest = NULL;
  gint index = -1;

  *picture = NULL;

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

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
    *picture = gst_h264_picture_ref (lowest);

  return index;
}

/**
 * gst_h264_dpb_needs_bump:
 * @dpb: a #GstH264Dpb
 * @max_num_reorder_frames: allowed max_num_reorder_frames as specified by sps
 * @low_latency: %TRUE if low-latency bumping is required
 *
 * Returns: %TRUE if bumping is required
 *
 * Since: 1.20
 */
gboolean
gst_h264_dpb_needs_bump (GstH264Dpb * dpb, guint32 max_num_reorder_frames,
    gboolean low_latency)
{
  GstH264Picture *current_picture;

  g_return_val_if_fail (dpb != NULL, FALSE);
  g_assert (dpb->num_output_needed >= 0);

  /* Empty so nothing to bump */
  if (dpb->pic_list->len == 0 || dpb->num_output_needed == 0)
    return FALSE;

  /* FIXME: Need to revisit for intelaced decoding */

  /* Case 1)
   * C.4.2 Decoding of gaps in frame_num and storage of "non-existing" pictures
   * C.4.5.1 Storage and marking of a reference decoded picture into the DPB
   * C.4.5.2 Storage and marking of a non-reference decoded picture into the DPB
   *
   * In summary, if DPB is full and there is no empty space to store current
   * picture, need bumping.
   * NOTE: current picture was added already by our decoding flow, So we need to
   * do bumping until dpb->pic_list->len == dpb->max_num_pic
   */
  if (!gst_h264_dpb_has_empty_frame_buffer (dpb)) {
    GST_TRACE ("No empty frame buffer, need bumping");
    return TRUE;
  }

  if (dpb->num_output_needed > max_num_reorder_frames) {
    GST_TRACE
        ("not outputted frames (%d) > max_num_reorder_frames (%d), need bumping",
        dpb->num_output_needed, max_num_reorder_frames);

    return TRUE;
  }

  current_picture =
      g_array_index (dpb->pic_list, GstH264Picture *, dpb->pic_list->len - 1);

  if (current_picture->needed_for_output && current_picture->idr &&
      !current_picture->dec_ref_pic_marking.no_output_of_prior_pics_flag) {
    GST_TRACE ("IDR with no_output_of_prior_pics_flag == 0, need bumping");
    return TRUE;
  }

  if (current_picture->needed_for_output && current_picture->mem_mgmt_5) {
    GST_TRACE ("Memory management type 5, need bumping");
    return TRUE;
  }

  /* HACK: Not all streams have PicOrderCnt increment by 2, but in practice this
   * condition can be used */
  if (low_latency && dpb->last_output_poc != G_MININT32) {
    GstH264Picture *picture = NULL;
    gint32 lowest_poc = G_MININT32;

    gst_h264_dpb_get_lowest_output_needed_picture (dpb, &picture);
    if (picture) {
      lowest_poc = picture->pic_order_cnt;
      gst_h264_picture_unref (picture);
    }

    if (lowest_poc != G_MININT32 && lowest_poc > dpb->last_output_poc
        && abs (lowest_poc - dpb->last_output_poc) <= 2) {
      GST_TRACE ("bumping for low-latency, lowest-poc: %d, last-output-poc: %d",
          lowest_poc, dpb->last_output_poc);
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * gst_h264_dpb_bump:
 * @dpb: a #GstH265Dpb
 * @drain: whether draining or not
 *
 * Perform bumping process as defined in C.4.5.3 "Bumping" process.
 * If @drain is %TRUE, @dpb will remove a #GstH264Picture from internal array
 * so that returned #GstH264Picture could hold the last reference of it
 *
 * Returns: (nullable) (transfer full): a #GstH264Picture which is needed to be
 * outputted
 *
 * Since: 1.20
 */
GstH264Picture *
gst_h264_dpb_bump (GstH264Dpb * dpb, gboolean drain)
{
  GstH264Picture *picture;
  gint index;

  g_return_val_if_fail (dpb != NULL, NULL);

  index = gst_h264_dpb_get_lowest_output_needed_picture (dpb, &picture);

  if (!picture || index < 0)
    return NULL;

  picture->needed_for_output = FALSE;

  dpb->num_output_needed--;
  g_assert (dpb->num_output_needed >= 0);

  /* NOTE: don't use g_array_remove_index_fast here since the last picture
   * need to be referenced for bumping decision */
  if (!picture->ref || drain)
    g_array_remove_index (dpb->pic_list, index);

  dpb->last_output_poc = picture->pic_order_cnt;

  return picture;
}

static gint
get_picNumX (GstH264Picture * picture, GstH264RefPicMarking * ref_pic_marking)
{
  /* FIXME: support interlaced */
  return picture->pic_num -
      (ref_pic_marking->difference_of_pic_nums_minus1 + 1);
}

/**
 * gst_h264_dpb_perform_memory_management_control_operation:
 * @dpb: a #GstH265Dpb
 * @ref_pic_marking: a #GstH264RefPicMarking
 * @picture: a #GstH264Picture
 *
 * Perform "8.2.5.4 Adaptive memory control decoded reference picture marking process"
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.20
 */
gboolean
gst_h264_dpb_perform_memory_management_control_operation (GstH264Dpb * dpb,
    GstH264RefPicMarking * ref_pic_marking, GstH264Picture * picture)
{
  guint8 type;
  gint pic_num_x;
  gint max_long_term_frame_idx;
  GstH264Picture *other;
  gint i;

  g_return_val_if_fail (dpb != NULL, FALSE);
  g_return_val_if_fail (ref_pic_marking != NULL, FALSE);
  g_return_val_if_fail (picture != NULL, FALSE);

  type = ref_pic_marking->memory_management_control_operation;

  switch (type) {
    case 0:
      /* Normal end of operations' specification */
      break;
    case 1:
      /* 8.2.5.4.1 Mark a short term reference picture as unused so it can be
       * removed if outputted */
      pic_num_x = get_picNumX (picture, ref_pic_marking);
      other = gst_h264_dpb_get_short_ref_by_pic_num (dpb, pic_num_x);
      if (other) {
        other->ref = FALSE;
        GST_TRACE ("MMCO-1: unmark short-term ref picture %p, (poc %d)",
            other, other->pic_order_cnt);
      } else {
        GST_WARNING ("Invalid picNumX %d for operation type 1", pic_num_x);
        return FALSE;
      }
      break;
    case 2:
      /* 8.2.5.4.2 Mark a long term reference picture as unused so it can be
       * removed if outputted */
      other = gst_h264_dpb_get_long_ref_by_long_term_pic_num (dpb,
          ref_pic_marking->long_term_pic_num);
      if (other) {
        other->ref = FALSE;
        GST_TRACE ("MMCO-2: unmark long-term ref picture %p, (poc %d)",
            other, other->pic_order_cnt);
      } else {
        GST_WARNING ("Invalid LongTermPicNum %d for operation type 2",
            ref_pic_marking->long_term_pic_num);
        return FALSE;
      }
      break;
    case 3:
      /* 8.2.5.4.3 Mark a short term reference picture as long term reference */

      /* If we have long-term ref picture for LongTermFrameIdx,
       * mark the picture as non-reference */
      for (i = 0; i < dpb->pic_list->len; i++) {
        other = g_array_index (dpb->pic_list, GstH264Picture *, i);

        if (other->ref && other->long_term && other->long_term_frame_idx ==
            ref_pic_marking->long_term_frame_idx) {
          other->ref = FALSE;
          other->long_term = FALSE;
          GST_TRACE ("MMCO-3: unmark old long-term ref pic %p (poc %d)",
              other, other->pic_order_cnt);
          break;
        }
      }

      pic_num_x = get_picNumX (picture, ref_pic_marking);
      other = gst_h264_dpb_get_short_ref_by_pic_num (dpb, pic_num_x);
      if (other) {
        other->long_term = TRUE;
        other->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
        GST_TRACE ("MMCO-3: mark long-term ref pic %p, index %d, (poc %d)",
            other, other->long_term_frame_idx, other->pic_order_cnt);
      } else {
        GST_WARNING ("Invalid picNumX %d for operation type 3", pic_num_x);
        return FALSE;
      }
      break;
    case 4:
      /* 8.2.5.4.4  All pictures for which LongTermFrameIdx is greater than
       * max_long_term_frame_idx_plus1 − 1 and that are marked as
       * "used for long-term reference" are marked as "unused for reference */
      max_long_term_frame_idx =
          ref_pic_marking->max_long_term_frame_idx_plus1 - 1;

      GST_TRACE ("MMCO-4: max_long_term_frame_idx %d", max_long_term_frame_idx);

      for (i = 0; i < dpb->pic_list->len; i++) {
        other = g_array_index (dpb->pic_list, GstH264Picture *, i);

        if (other->ref && other->long_term &&
            other->long_term_frame_idx > max_long_term_frame_idx) {
          other->ref = FALSE;
          other->long_term = FALSE;
          GST_TRACE ("MMCO-4: unmark long-term ref pic %p, index %d, (poc %d)",
              other, other->long_term_frame_idx, other->pic_order_cnt);
        }
      }
      break;
    case 5:
      /* 8.2.5.4.5 Unmark all reference pictures */
      for (i = 0; i < dpb->pic_list->len; i++) {
        other = g_array_index (dpb->pic_list, GstH264Picture *, i);
        other->ref = FALSE;
        other->long_term = FALSE;
      }
      picture->mem_mgmt_5 = TRUE;
      picture->frame_num = 0;
      break;
    case 6:
      /* 8.2.5.4.6 Replace long term reference pictures with current picture.
       * First unmark if any existing with this long_term_frame_idx */

      /* If we have long-term ref picture for LongTermFrameIdx,
       * mark the picture as non-reference */
      for (i = 0; i < dpb->pic_list->len; i++) {
        other = g_array_index (dpb->pic_list, GstH264Picture *, i);

        if (other->ref && other->long_term && other->long_term_frame_idx ==
            ref_pic_marking->long_term_frame_idx) {
          GST_TRACE ("MMCO-6: unmark old long-term ref pic %p (poc %d)",
              other, other->pic_order_cnt);
          other->ref = FALSE;
          other->long_term = FALSE;
          break;
        }
      }

      picture->ref = TRUE;
      picture->long_term = TRUE;
      picture->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

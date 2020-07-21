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

  pic->pts = GST_CLOCK_TIME_NONE;
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
};

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

    if (picture->outputted && !picture->ref) {
      GST_TRACE ("remove picture %p (frame num %d) from dpb",
          picture, picture->frame_num);
      g_array_remove_index_fast (dpb->pic_list, i);
      i--;
    }
  }
}

/**
 * gst_h264_dpb_delete_outputed:
 * @dpb: a #GstH264Dpb
 *
 * Delete already outputted picture, even if they are referenced.
 *
 * Since: 1.18
 */
void
gst_h264_dpb_delete_outputed (GstH264Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->outputted) {
      GST_TRACE ("remove picture %p (frame num %d) from dpb",
          picture, picture->frame_num);
      g_array_remove_index_fast (dpb->pic_list, i);
      i--;
    }
  }
}

/**
 * gst_h264_dpb_delete_by_poc:
 * @dpb: a #GstH264Dpb
 * @poc: a poc of #GstH264Picture to remove
 *
 * Delete a #GstH264Dpb by @poc
 */
void
gst_h264_dpb_delete_by_poc (GstH264Dpb * dpb, gint poc)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->pic_order_cnt == poc) {
      GST_TRACE ("remove picture %p for poc %d (frame num %d) from dpb",
          picture, poc, picture->frame_num);

      g_array_remove_index_fast (dpb->pic_list, i);
      return;
    }
  }

  GST_WARNING ("Couldn't find picture with poc %d", poc);
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
 * gst_h264_dpb_get_long_ref_by_pic_num:
 * @dpb: a #GstH264Dpb
 * @pic_num: a picture number
 *
 * Find a long term reference picture which has matching picture number
 *
 * Returns: (nullable) (transfer none): a #GstH264Picture
 */
GstH264Picture *
gst_h264_dpb_get_long_ref_by_pic_num (GstH264Dpb * dpb, gint pic_num)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (picture->ref && picture->long_term && picture->pic_num == pic_num)
      return picture;
  }

  GST_WARNING ("No long term reference picture for %d", pic_num);

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
 * gst_h264_dpb_get_pictures_not_outputted:
 * @dpb: a #GstH264Dpb
 * @out: (out) (element-type GstH264Picture) (transfer full): an array
 *   of #GstH264Picture pointer
 *
 * Retrieve all not-outputted pictures from @dpb
 */
void
gst_h264_dpb_get_pictures_not_outputted (GstH264Dpb * dpb, GArray * out)
{
  gint i;

  g_return_if_fail (dpb != NULL);
  g_return_if_fail (out != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH264Picture *picture =
        g_array_index (dpb->pic_list, GstH264Picture *, i);

    if (!picture->outputted) {
      gst_h264_picture_ref (picture);
      g_array_append_val (out, picture);
    }
  }
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
 * gst_h264_dpb_is_full:
 * @dpb: a #GstH264Dpb
 *
 * Return: %TRUE if @dpb is full
 */
gboolean
gst_h264_dpb_is_full (GstH264Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, -1);

  return dpb->pic_list->len >= dpb->max_num_pics;
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

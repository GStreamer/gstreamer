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

#include "gsth265picture.h"

GST_DEBUG_CATEGORY_EXTERN (gst_h265_decoder_debug);
#define GST_CAT_DEFAULT gst_h265_decoder_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstH265Picture, gst_h265_picture);

static void
_gst_h265_picture_free (GstH265Picture * picture)
{
  if (picture->notify)
    picture->notify (picture->user_data);

  g_free (picture);
}

/**
 * gst_h265_picture_new:
 *
 * Create new #GstH265Picture
 *
 * Returns: a new #GstH265Picture
 */
GstH265Picture *
gst_h265_picture_new (void)
{
  GstH265Picture *pic;

  pic = g_new0 (GstH265Picture, 1);

  pic->pts = GST_CLOCK_TIME_NONE;
  pic->field = GST_H265_PICTURE_FIELD_FRAME;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (pic), 0,
      GST_TYPE_H265_PICTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) _gst_h265_picture_free);

  return pic;
}

/**
 * gst_h265_picture_set_user_data:
 * @picture: a #GstH265Picture
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
gst_h265_picture_set_user_data (GstH265Picture * picture, gpointer user_data,
    GDestroyNotify notify)
{
  g_return_if_fail (GST_IS_H265_PICTURE (picture));

  if (picture->notify)
    picture->notify (picture->user_data);

  picture->user_data = user_data;
  picture->notify = notify;
}

/**
 * gst_h265_picture_get_user_data:
 * @picture: a #GstH265Picture
 *
 * Gets private data set on the picture via
 * gst_h265_picture_set_user_data() previously.
 *
 * Returns: (transfer none): The previously set user_data
 */
gpointer
gst_h265_picture_get_user_data (GstH265Picture * picture)
{
  return picture->user_data;
}

struct _GstH265Dpb
{
  GArray *pic_list;
  gint max_num_pics;
};

/**
 * gst_h265_dpb_new: (skip)
 *
 * Create new #GstH265Dpb
 *
 * Returns: a new #GstH265Dpb
 */
GstH265Dpb *
gst_h265_dpb_new (void)
{
  GstH265Dpb *dpb;

  dpb = g_new0 (GstH265Dpb, 1);

  dpb->pic_list =
      g_array_sized_new (FALSE, TRUE, sizeof (GstH265Picture *),
      GST_H265_DPB_MAX_SIZE);
  g_array_set_clear_func (dpb->pic_list,
      (GDestroyNotify) gst_h265_picture_clear);

  return dpb;
}

/**
 * gst_h265_dpb_set_max_num_pics:
 * @dpb: a #GstH265Dpb
 * @max_num_pics: the maximum number of picture
 *
 * Set the number of maximum allowed pictures to store
 */
void
gst_h265_dpb_set_max_num_pics (GstH265Dpb * dpb, gint max_num_pics)
{
  g_return_if_fail (dpb != NULL);

  dpb->max_num_pics = max_num_pics;
}

/**
 * gst_h265_dpb_get_max_num_pics:
 * @dpb: a #GstH265Dpb
 *
 * Returns: the number of maximum pictures
 */
gint
gst_h265_dpb_get_max_num_pics (GstH265Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, 0);

  return dpb->max_num_pics;
}

/**
 * gst_h265_dpb_free:
 * @dpb: a #GstH265Dpb to free
 *
 * Free the @dpb
 */
void
gst_h265_dpb_free (GstH265Dpb * dpb)
{
  g_return_if_fail (dpb != NULL);

  gst_h265_dpb_clear (dpb);
  g_array_unref (dpb->pic_list);
  g_free (dpb);
}

/**
 * gst_h265_dpb_clear:
 * @dpb: a #GstH265Dpb
 *
 * Clear all stored #GstH265Picture
 */
void
gst_h265_dpb_clear (GstH265Dpb * dpb)
{
  g_return_if_fail (dpb != NULL);

  g_array_set_size (dpb->pic_list, 0);
}

/**
 * gst_h265_dpb_add:
 * @dpb: a #GstH265Dpb
 * @picture: (transfer full): a #GstH265Picture
 *
 * Store the @picture
 */
void
gst_h265_dpb_add (GstH265Dpb * dpb, GstH265Picture * picture)
{
  g_return_if_fail (dpb != NULL);
  g_return_if_fail (GST_IS_H265_PICTURE (picture));

  g_array_append_val (dpb->pic_list, picture);
}

/**
 * gst_h265_dpb_delete_unused:
 * @dpb: a #GstH265Dpb
 *
 * Delete already outputted and not referenced all pictures from dpb
 */
void
gst_h265_dpb_delete_unused (GstH265Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->outputted && !picture->ref) {
      GST_TRACE ("remove picture %p (poc %d) from dpb",
          picture, picture->pic_order_cnt);
      g_array_remove_index (dpb->pic_list, i);
      i--;
    }
  }
}

/**
 * gst_h265_dpb_delete_by_poc:
 * @dpb: a #GstH265Dpb
 * @poc: a poc of #GstH265Picture to remove
 *
 * Delete a #GstH265Dpb by @poc
 */
void
gst_h265_dpb_delete_by_poc (GstH265Dpb * dpb, gint poc)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->pic_order_cnt == poc) {
      g_array_remove_index (dpb->pic_list, i);
      return;
    }
  }

  GST_WARNING ("Couldn't find picture with poc %d", poc);
}

/**
 * gst_h265_dpb_num_ref_pictures:
 * @dpb: a #GstH265Dpb
 *
 * Returns: The number of referenced pictures
 */
gint
gst_h265_dpb_num_ref_pictures (GstH265Dpb * dpb)
{
  gint i;
  gint ret = 0;

  g_return_val_if_fail (dpb != NULL, -1);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->ref)
      ret++;
  }

  return ret;
}

/**
 * gst_h265_dpb_mark_all_non_ref:
 * @dpb: a #GstH265Dpb
 *
 * Mark all pictures are not referenced
 */
void
gst_h265_dpb_mark_all_non_ref (GstH265Dpb * dpb)
{
  gint i;

  g_return_if_fail (dpb != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    picture->ref = FALSE;
  }
}

/**
 * gst_h265_dpb_get_ref_by_poc:
 * @dpb: a #GstH265Dpb
 * @poc: a picture order count
 *
 * Find a short or long term reference picture which has matching poc
 *
 * Returns: (nullable) (transfer full): a #GstH265Picture
 */
GstH265Picture *
gst_h265_dpb_get_ref_by_poc (GstH265Dpb * dpb, gint poc)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->ref && picture->pic_order_cnt == poc)
      return gst_h265_picture_ref (picture);
  }

  GST_DEBUG ("No short term reference picture for %d", poc);

  return NULL;
}

/**
 * gst_h265_dpb_get_ref_by_poc_lsb:
 * @dpb: a #GstH265Dpb
 * @poc_lsb: a picture order count lsb
 *
 * Find a short or long term reference picture which has matching poc_lsb
 *
 * Returns: (nullable) (transfer full): a #GstH265Picture
 */
GstH265Picture *
gst_h265_dpb_get_ref_by_poc_lsb (GstH265Dpb * dpb, gint poc_lsb)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->ref && picture->pic_order_cnt_lsb == poc_lsb)
      return gst_h265_picture_ref (picture);
  }

  GST_DEBUG ("No short term reference picture for %d", poc_lsb);

  return NULL;
}

/**
 * gst_h265_dpb_get_short_ref_by_poc:
 * @dpb: a #GstH265Dpb
 * @poc: a picture order count
 *
 * Find a short term reference picture which has matching poc
 *
 * Returns: (nullable) (transfer full): a #GstH265Picture
 */
GstH265Picture *
gst_h265_dpb_get_short_ref_by_poc (GstH265Dpb * dpb, gint poc)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->ref && !picture->long_term && picture->pic_order_cnt == poc)
      return gst_h265_picture_ref (picture);
  }

  GST_DEBUG ("No short term reference picture for %d", poc);

  return NULL;
}

/**
 * gst_h265_dpb_get_long_ref_by_poc:
 * @dpb: a #GstH265Dpb
 * @poc: a picture order count
 *
 * Find a long term reference picture which has matching poc
 *
 * Returns: (nullable) (transfer full): a #GstH265Picture
 */
GstH265Picture *
gst_h265_dpb_get_long_ref_by_poc (GstH265Dpb * dpb, gint poc)
{
  gint i;

  g_return_val_if_fail (dpb != NULL, NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (picture->ref && picture->long_term && picture->pic_order_cnt == poc)
      return gst_h265_picture_ref (picture);
  }

  GST_DEBUG ("No long term reference picture for %d", poc);

  return NULL;
}

/**
 * gst_h265_dpb_get_pictures_not_outputted:
 * @dpb: a #GstH265Dpb
 * @out: (out) (element-type GstH265Picture) (transfer full): a list
 *   of #GstH265Dpb
 *
 * Retrieve all not-outputted pictures from @dpb
 */
void
gst_h265_dpb_get_pictures_not_outputted (GstH265Dpb * dpb, GList ** out)
{
  gint i;

  g_return_if_fail (dpb != NULL);
  g_return_if_fail (out != NULL);

  for (i = 0; i < dpb->pic_list->len; i++) {
    GstH265Picture *picture =
        g_array_index (dpb->pic_list, GstH265Picture *, i);

    if (!picture->outputted)
      *out = g_list_append (*out, gst_h265_picture_ref (picture));
  }
}

/**
 * gst_h265_dpb_get_pictures_all:
 * @dpb: a #GstH265Dpb
 *
 * Return: (element-type GstH265Picture) (transfer full): a #GArray of
 *   #GstH265Picture stored in @dpb
 */
GArray *
gst_h265_dpb_get_pictures_all (GstH265Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, NULL);

  return g_array_ref (dpb->pic_list);
}

/**
 * gst_h265_dpb_get_size:
 * @dpb: a #GstH265Dpb
 *
 * Return: the length of stored dpb array
 */
gint
gst_h265_dpb_get_size (GstH265Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, -1);

  return dpb->pic_list->len;
}

/**
 * gst_h265_dpb_is_full:
 * @dpb: a #GstH265Dpb
 *
 * Return: %TRUE if @dpb is full
 */
gboolean
gst_h265_dpb_is_full (GstH265Dpb * dpb)
{
  g_return_val_if_fail (dpb != NULL, -1);

  return dpb->pic_list->len >= dpb->max_num_pics;
}

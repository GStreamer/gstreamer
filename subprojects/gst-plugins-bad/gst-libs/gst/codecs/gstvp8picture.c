/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include "gstvp8picture.h"

GST_DEBUG_CATEGORY_EXTERN (gst_vp8_decoder_debug);
#define GST_CAT_DEFAULT gst_vp8_decoder_debug

GST_DEFINE_MINI_OBJECT_TYPE (GstVp8Picture, gst_vp8_picture);

static void
_gst_vp8_picture_free (GstVp8Picture * picture)
{
  GST_TRACE ("Free picture %p", picture);

  if (picture->notify)
    picture->notify (picture->user_data);

  if (picture->discont_state)
    gst_video_codec_state_unref (picture->discont_state);

  g_free (picture);
}

/**
 * gst_vp8_picture_new:
 *
 * Create new #GstVp8Picture
 *
 * Returns: a new #GstVp8Picture
 */
GstVp8Picture *
gst_vp8_picture_new (void)
{
  GstVp8Picture *pic;

  pic = g_new0 (GstVp8Picture, 1);
  pic->pts = GST_CLOCK_TIME_NONE;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (pic), 0,
      GST_TYPE_VP8_PICTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) _gst_vp8_picture_free);

  GST_TRACE ("New picture %p", pic);

  return pic;
}

/**
 * gst_vp8_picture_set_user_data:
 * @picture: a #GstVp8Picture
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
gst_vp8_picture_set_user_data (GstVp8Picture * picture, gpointer user_data,
    GDestroyNotify notify)
{
  g_return_if_fail (GST_IS_VP8_PICTURE (picture));

  if (picture->notify)
    picture->notify (picture->user_data);

  picture->user_data = user_data;
  picture->notify = notify;
}

/**
 * gst_vp8_picture_get_user_data:
 * @picture: a #GstVp8Picture
 *
 * Gets private data set on the picture via
 * gst_vp8_picture_set_user_data() previously.
 *
 * Returns: (transfer none): The previously set user_data
 */
gpointer
gst_vp8_picture_get_user_data (GstVp8Picture * picture)
{
  return picture->user_data;
}

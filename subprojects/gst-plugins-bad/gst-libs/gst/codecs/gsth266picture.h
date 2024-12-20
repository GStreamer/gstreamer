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

#pragma once

#ifndef GST_USE_UNSTABLE_API
#warning "The CODECs library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/codecs/codecs-prelude.h>
#include <gst/codecs/gstcodecpicture.h>
#include <gst/codecparsers/gsth266parser.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_H266_PICTURE     (gst_h266_picture_get_type())
#define GST_IS_H266_PICTURE(obj)  (GST_IS_MINI_OBJECT_TYPE(obj, GST_TYPE_H266_PICTURE))
#define GST_H266_PICTURE(obj)     ((GstH266Picture *)obj)

typedef struct _GstH266Slice GstH266Slice;
typedef struct _GstH266Picture GstH266Picture;
typedef struct _GstH266Dpb GstH266Dpb;

/**
 * GstH266Slice:
 *
 * The #GstH266Slice represents a slice for decoding.
 *
 * Since: 1.26
 */
struct _GstH266Slice
{
  GstH266SliceHdr header;

  /* parsed nal unit (doesn't take ownership of raw data) */
  GstH266NalUnit nalu;

  /*< private > */
  gboolean no_output_before_recovery_flag;
  gboolean no_output_of_prior_pics_flag;
  /* The first slice of the picture. */
  gboolean first_slice;
  gboolean clear_dpb;
};

/**
 * GstH266Picture:
 *
 * The #GstH266Picture represents a picture for decoding.
 *
 * Since: 1.26
 */
struct _GstH266Picture
{
  /*< private > */
  GstCodecPicture parent;

  GstH266SliceType type;

  gint pic_order_cnt;
  gint pic_order_cnt_msb;
  gint pic_order_cnt_lsb;

  guint pic_latency_cnt;

  gboolean output_flag;
  gboolean NoOutputOfPriorPicsFlag;
  gboolean NoOutputBeforeRecoveryFlag;

  /* PPS resolution may be smaller than SPS resolution. */
  gint pps_width, pps_height;
  gboolean pps_conformance_window_flag;
  gint pps_crop_rect_width, pps_crop_rect_height;
  gint pps_crop_rect_x, pps_crop_rect_y;

  /* Never be a ref in RPL, except the first time adding to DPB. */
  gboolean non_ref;
  gboolean ref;
  gboolean long_term;
  gboolean inter_layer_ref;
  gboolean needed_for_output;

  GstH266FrameFieldInfo ff_info;

  GstVideoBufferFlags buffer_flags;
};

GST_CODECS_API
GType gst_h266_picture_get_type (void);

GST_CODECS_API
GstH266Picture *gst_h266_picture_new (void);

static inline GstH266Picture *
gst_h266_picture_ref (GstH266Picture * picture)
{
  return (GstH266Picture *)
      gst_mini_object_ref (GST_MINI_OBJECT_CAST (picture));
}

static inline void
gst_h266_picture_unref (GstH266Picture * picture)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (picture));
}

static inline gboolean
gst_h266_picture_replace (GstH266Picture ** old_picture,
    GstH266Picture * new_picture)
{
  return gst_mini_object_replace ((GstMiniObject **) old_picture,
      (GstMiniObject *) new_picture);
}

static inline void
gst_clear_h266_picture (GstH266Picture ** picture)
{
  if (picture && *picture) {
    gst_h266_picture_unref (*picture);
    *picture = NULL;
  }
}

static inline void
gst_h266_picture_set_user_data (GstH266Picture * picture, gpointer user_data,
    GDestroyNotify notify)
{
  gst_codec_picture_set_user_data (GST_CODEC_PICTURE (picture),
      user_data, notify);
}

static inline gpointer
gst_h266_picture_get_user_data (GstH266Picture * picture)
{
  return gst_codec_picture_get_user_data (GST_CODEC_PICTURE (picture));
}

static inline void
gst_h266_picture_set_discont_state (GstH266Picture * picture,
    GstVideoCodecState * discont_state)
{
  gst_codec_picture_set_discont_state (GST_CODEC_PICTURE (picture),
      discont_state);
}

GST_CODECS_API
GstH266Dpb *gst_h266_dpb_new (void);

GST_CODECS_API
void gst_h266_dpb_free (GstH266Dpb * dpb);

GST_CODECS_API
void gst_h266_dpb_clear (GstH266Dpb * dpb);

GST_CODECS_API
void gst_h266_dpb_set_max_num_pics (GstH266Dpb * dpb,
    gint max_num_pics);

GST_CODECS_API
gint gst_h266_dpb_get_max_num_pics (GstH266Dpb * dpb);

GST_CODECS_API
void gst_h266_dpb_add (GstH266Dpb * dpb, GstH266Picture * picture);

GST_CODECS_API
void gst_h266_dpb_delete_unused (GstH266Dpb * dpb);

GST_CODECS_API
gint gst_h266_dpb_num_ref_pictures (GstH266Dpb * dpb);

GST_CODECS_API
gint gst_h266_dpb_get_size (GstH266Dpb * dpb);

GST_CODECS_API
gboolean gst_h266_dpb_needs_bump (GstH266Dpb * dpb, guint max_num_reorder_pics,
    guint max_latency_increase, guint max_dec_pic_buffering);

GST_CODECS_API
GstH266Picture *gst_h266_dpb_bump (GstH266Dpb * dpb, gboolean drain);

GST_CODECS_API
void gst_h266_dpb_mark_all_non_ref (GstH266Dpb * dpb);

GST_CODECS_API
void gst_h266_dpb_mark_all_non_output (GstH266Dpb * dpb);

GST_CODECS_API
GstH266Picture *gst_h266_dpb_get_picture_by_poc_lsb (GstH266Dpb * dpb,
    gint poc_lsb);

GST_CODECS_API
GstH266Picture *gst_h266_dpb_get_picture_by_poc (GstH266Dpb * dpb, gint poc);

GST_CODECS_API
GArray *gst_h266_dpb_get_pictures_all (GstH266Dpb * dpb);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstH266Picture, gst_h266_picture_unref)

G_END_DECLS

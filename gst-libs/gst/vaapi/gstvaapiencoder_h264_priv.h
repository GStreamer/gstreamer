/*
 *  gstvaapiencoder_h264_priv.h - H.264 encoder (private definitions)
 *
 *  Copyright (C) 2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_ENCODER_H264_PRIV_H
#define GST_VAAPI_ENCODER_H264_PRIV_H

#include "gstvaapiencoder_priv.h"
#include "gstvaapiutils_h264.h"

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_H264_CAST(encoder) \
    ((GstVaapiEncoderH264 *)(encoder))

#define GST_VAAPI_ENCODER_H264_MAX_IDR_PERIOD       512

struct _GstVaapiEncoderH264
{
  GstVaapiEncoder parent_instance;

  GstVaapiProfile profile;
  GstVaapiLevelH264 level;
  guint8 profile_idc;
  guint8 level_idc;
  guint32 idr_period;
  guint32 init_qp;
  guint32 min_qp;
  guint32 num_slices;
  guint32 num_bframes;
  guint32 mb_width;
  guint32 mb_height;
  gboolean use_cabac;
  gboolean use_dct8x8;

  /* re-ordering */
  GQueue reorder_frame_list;
  guint reorder_state;
  guint frame_index;
  guint cur_frame_num;
  guint cur_present_index;
  GstClockTime cts_offset;

  /* reference list */
  GQueue ref_list;
  guint max_ref_frames;
  /* max reflist count */
  guint max_reflist0_count;
  guint max_reflist1_count;

  /* frame, poc */
  guint32 max_frame_num;
  guint32 log2_max_frame_num;
  guint32 max_pic_order_cnt;
  guint32 log2_max_pic_order_cnt;
  guint32 idr_num;

  GstBuffer *sps_data;
  GstBuffer *pps_data;
};

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_H264_PRIV_H */

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

#include <glib.h>
#include <gst/base/gstbitwriter.h>
#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiencoder_priv.h>

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_H264(encoder) \
    ((GstVaapiEncoderH264 *)(encoder))
#define GST_VAAPI_ENCODER_H264_CAST(encoder) \
    ((GstVaapiEncoderH264 *)(encoder))

typedef enum
{
  GST_VAAPI_ENCODER_H264_LEVEL_10 = 10, /* QCIF format, < 380160 samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_11 = 11, /* CIF format,   < 768000 samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_12 = 12, /* CIF format,   < 1536000  samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_13 = 13, /* CIF format,   < 3041280  samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_20 = 20, /* CIF format,   < 3041280  samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_21 = 21, /* HHR format,  < 5068800  samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_22 = 22, /* SD/4CIF format,     < 5184000      samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_30 = 30, /* SD/4CIF format,     < 10368000    samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_31 = 31, /* 720pHD format,      < 27648000    samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_32 = 32, /* SXGA  format,         < 55296000    samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_40 = 40, /* 2Kx1K format,         < 62914560    samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_41 = 41, /* 2Kx1K format,         < 62914560    samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_42 = 42, /* 2Kx1K format,         < 125829120  samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_50 = 50, /* 3672x1536 format,  < 150994944  samples/sec */
  GST_VAAPI_ENCODER_H264_LEVEL_51 = 51, /* 4096x2304 format,  < 251658240  samples/sec */
} GstVaapiEncoderH264Level;

#define GST_VAAPI_ENCODER_H264_DEFAULT_PROFILE      GST_VAAPI_PROFILE_H264_BASELINE
#define GST_VAAPI_ENCODER_H264_DEFAULT_LEVEL        GST_VAAPI_ENCODER_H264_LEVEL_31
#define GST_VAAPI_ENCODER_H264_DEFAULT_INIT_QP      26
#define GST_VAAPI_ENCODER_H264_DEFAULT_MIN_QP       1
#define GST_VAAPI_ENCODER_H264_MAX_IDR_PERIOD       512

#define GST_VAAPI_ENCODER_H264_DEFAULT_SLICE_NUM    1

struct _GstVaapiEncoderH264
{
  GstVaapiEncoder parent;

  /* public */
  guint32 profile;
  guint32 level;
  guint32 idr_period;
  guint32 init_qp;              /*default 24 */
  guint32 min_qp;               /*default 1 */
  guint32 slice_num;
  guint32 b_frame_num;

  /* private */
  gboolean is_avc;              /* avc or bytestream */
  /* re-ordering */
  GQueue reorder_frame_list;
  guint reorder_state;
  guint frame_index;
  guint cur_frame_num;
  guint cur_present_index;
  GstClockTime cts_offset;

  /* reference list */
  GQueue ref_list;
  guint max_ref_num;
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

#endif /*GST_VAAPI_ENCODER_H264_PRIV_H */

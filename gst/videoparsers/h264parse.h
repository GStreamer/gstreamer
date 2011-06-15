/* GStreamer H.264 Parser
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2010> Collabora Multimedia
 * Copyright (C) <2010> Nokia Corporation
 *
 * Some bits C-c,C-v'ed and s/4/3 from h264parse:
 *           (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_H264_PARAMS_H__
#define __GST_H264_PARAMS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef enum
{
  NAL_UNKNOWN = 0,
  NAL_SLICE = 1,
  NAL_SLICE_DPA = 2,
  NAL_SLICE_DPB = 3,
  NAL_SLICE_DPC = 4,
  NAL_SLICE_IDR = 5,
  NAL_SEI = 6,
  NAL_SPS = 7,
  NAL_PPS = 8,
  NAL_AU_DELIMITER = 9,
  NAL_SEQ_END = 10,
  NAL_STREAM_END = 11,
  NAL_FILTER_DATA = 12
} GstH264ParamsNalUnitType;

/* SEI type */
typedef enum
{
  SEI_BUF_PERIOD = 0,
  SEI_PIC_TIMING = 1
      /* and more...  */
} GstH264ParamsSEIPayloadType;

/* SEI pic_struct type */
typedef enum
{
  SEI_PIC_STRUCT_FRAME = 0,
  SEI_PIC_STRUCT_TOP_FIELD = 1,
  SEI_PIC_STRUCT_BOTTOM_FIELD = 2,
  SEI_PIC_STRUCT_TOP_BOTTOM = 3,
  SEI_PIC_STRUCT_BOTTOM_TOP = 4,
  SEI_PIC_STRUCT_TOP_BOTTOM_TOP = 5,
  SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM = 6,
  SEI_PIC_STRUCT_FRAME_DOUBLING = 7,
  SEI_PIC_STRUCT_FRAME_TRIPLING = 8
} GstH264ParamsSEIPicStructType;

typedef struct _GstH264Params GstH264Params;
typedef struct _GstH264ParamsSPS GstH264ParamsSPS;
typedef struct _GstH264ParamsPPS GstH264ParamsPPS;

#define MAX_SPS_COUNT   32
#define MAX_PPS_COUNT   256

/* SPS: sequential parameter sets */
struct _GstH264ParamsSPS
{
  gboolean valid;

  /* raw values */
  guint8 profile_idc;
  guint8 level_idc;

  guint8 sps_id;

  guint8 pic_order_cnt_type;

  guint8 log2_max_frame_num_minus4;
  gboolean frame_mbs_only_flag;
  guint8 log2_max_pic_order_cnt_lsb_minus4;

  gboolean frame_cropping_flag;
  gboolean scp_flag;

  /* VUI parameters */
  gboolean vui_parameters_present_flag;

  gboolean timing_info_present_flag;
  guint32 num_units_in_tick;
  guint32 time_scale;
  gboolean fixed_frame_rate_flag;

  gboolean nal_hrd_parameters_present_flag;
  gboolean vcl_hrd_parameters_present_flag;

  /* hrd parameters */
  guint8 cpb_cnt_minus1;
  gint initial_cpb_removal_delay_length_minus1;
  gint cpb_removal_delay_length_minus1;
  gint dpb_output_delay_length_minus1;
  gboolean time_offset_length_minus1;

  gboolean pic_struct_present_flag;

  /* ... and probably more ... */

  /* derived values */
  gint width, height;
  gint fps_num, fps_den;
};

/* PPS: pic parameter sets */
struct _GstH264ParamsPPS
{
  gboolean valid;

  /* raw values */
  guint8 pps_id;
  guint8 sps_id;
};

struct _GstH264Params
{
  /* debug purposes */
  GstElement *el;

  /* SPS: sequential parameter set */
  GstH264ParamsSPS sps_buffers[MAX_SPS_COUNT];
  /* current SPS; most recent one in stream or referenced by PPS */
  GstH264ParamsSPS *sps;
  /* PPS: sequential parameter set */
  GstH264ParamsPPS pps_buffers[MAX_PPS_COUNT];
  /* current PPS; most recent one in stream */
  GstH264ParamsPPS *pps;

  /* extracted from slice header or otherwise relevant nal */
  guint8 first_mb_in_slice;
  guint8 slice_type;
  gboolean field_pic_flag;
  gboolean bottom_field_flag;

  /* SEI: supplemental enhancement messages */
#ifdef EXTRA_PARSE
  /* buffering period */
  guint32 initial_cpb_removal_delay[32];
#endif
  /* picture timing */
  guint32 sei_cpb_removal_delay;
  guint8 sei_pic_struct;
  /* And more... */

  /* cached timestamps */
  /* (trying to) track upstream dts and interpolate */
  GstClockTime dts;
  /* dts at start of last buffering period */
  GstClockTime ts_trn_nb;

  /* collected SPS and PPS NALUs */
  GstBuffer *sps_nals[MAX_SPS_COUNT];
  GstBuffer *pps_nals[MAX_PPS_COUNT];
};

gboolean  gst_h264_params_parse_nal (GstH264Params * params, guint8 * nal, gint size);
void      gst_h264_params_get_timestamp (GstH264Params * params,
                                  GstClockTime * out_ts, GstClockTime * out_dur,
                                  gboolean frame);
void      gst_h264_params_create (GstH264Params ** _params, GstElement * element);
void      gst_h264_params_free (GstH264Params * params);


G_END_DECLS
#endif

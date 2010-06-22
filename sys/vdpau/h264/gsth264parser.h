/* GStreamer
 *
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
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

#ifndef _GST_H264_PARSER_H_
#define _GST_H264_PARSER_H_

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
  GST_NAL_UNKNOWN = 0,
  GST_NAL_SLICE = 1,
  GST_NAL_SLICE_DPA = 2,
  GST_NAL_SLICE_DPB = 3,
  GST_NAL_SLICE_DPC = 4,
  GST_NAL_SLICE_IDR = 5,
  GST_NAL_SEI = 6,
  GST_NAL_SPS = 7,
  GST_NAL_PPS = 8,
  GST_NAL_AU_DELIMITER = 9,
  GST_NAL_SEQ_END = 10,
  GST_NAL_STREAM_END = 11,
  GST_NAL_FILTER_DATA = 12
} GstNalUnitType;

typedef enum
{
  GST_H264_P_SLICE,
  GST_H264_B_SLICE,
  GST_H264_I_SLICE,
  GST_H264_SP_SLICE,
  GST_H264_SI_SLICE,
  GST_H264_S_P_SLICE,
  GST_H264_S_B_SLICE,
  GST_H264_S_I_SLICE,
  GST_H264_S_SP_SLICE,
  GST_H264_S_SI_SLICE
} GstH264SliceType;

#define GST_H264_IS_P_SLICE(type)  ((type % 5) == GST_H264_P_SLICE)
#define GST_H264_IS_B_SLICE(type)  ((type % 5) == GST_H264_B_SLICE)
#define GST_H264_IS_I_SLICE(type)  ((type % 5) == GST_H264_I_SLICE)
#define GST_H264_IS_SP_SLICE(type) ((type % 5) == GST_H264_SP_SLICE)
#define GST_H264_IS_SI_SLICE(type) ((type % 5) == GST_H264_SI_SLICE)

typedef struct _GstNalUnit GstNalUnit;

typedef struct _GstH264HRDParameters GstH264HRDParameters;
typedef struct _GstH264VUIParameters GstH264VUIParameters;
typedef struct _GstH264Sequence GstH264Sequence;

typedef struct _GstH264Picture GstH264Picture;

typedef struct _GstH264DecRefPicMarking GstH264DecRefPicMarking;
typedef struct _GstH264RefPicMarking GstH264RefPicMarking;
typedef struct _GstH264PredWeightTable GstH264PredWeightTable;
typedef struct _GstH264Slice GstH264Slice;

typedef struct _GstH264ClockTimestamp GstH264ClockTimestamp;
typedef struct _GstH264PicTiming GstH264PicTiming;
typedef struct _GstH264BufferingPeriod GstH264BufferingPeriod;
typedef struct _GstH264SEIMessage GstH264SEIMessage;

struct _GstNalUnit
{
  guint16 ref_idc;
  guint16 type;

  /* calculated values */
  guint8 IdrPicFlag;
};

struct _GstH264HRDParameters
{
  guint8 cpb_cnt_minus1;
  guint8 bit_rate_scale;
  guint8 cpb_size_scale;

  guint32 bit_rate_value_minus1[32];
  guint32 cpb_size_value_minus1[32];
  guint8 cbr_flag[32];

  guint8 initial_cpb_removal_delay_length_minus1;
  guint8 cpb_removal_delay_length_minus1;
  guint8 dpb_output_delay_length_minus1;
  guint8 time_offset_length;
};

struct _GstH264VUIParameters
{
  guint8 aspect_ratio_idc;
  /* if aspect_ratio_idc == 255 */
  guint16 sar_width;
  guint16 sar_height;

  guint8 overscan_info_present_flag;
  /* if overscan_info_present_flag */
  guint8 overscan_appropriate_flag;

  guint8 video_format;
  guint8 video_full_range_flag;
  guint8 colour_description_present_flag;
  guint8 colour_primaries;
  guint8 transfer_characteristics;
  guint8 matrix_coefficients;

  guint8 chroma_sample_loc_type_top_field;
  guint8 chroma_sample_loc_type_bottom_field;

  guint8 timing_info_present_flag;
  /* if timing_info_present_flag */
  guint32 num_units_in_tick;
  guint32 time_scale;
  guint8 fixed_frame_rate_flag;

  guint8 nal_hrd_parameters_present_flag;
  /* if nal_hrd_parameters_present_flag */
  GstH264HRDParameters nal_hrd_parameters;

  guint8 vcl_hrd_parameters_present_flag;
  /* if nal_hrd_parameters_present_flag */
  GstH264HRDParameters vcl_hrd_parameters;

  guint8 low_delay_hrd_flag;
  guint8 pic_struct_present_flag;
};

struct _GstH264Sequence
{
  gint id;

  guint8 profile_idc;
  guint8 constraint_set0_flag;
  guint8 constraint_set1_flag;
  guint8 constraint_set2_flag;
  guint8 constraint_set3_flag;
  guint8 level_idc;

  guint8 chroma_format_idc;
  guint8 separate_colour_plane_flag;
  guint8 bit_depth_luma_minus8;
  guint8 bit_depth_chroma_minus8;
  guint8 qpprime_y_zero_transform_bypass_flag;

  guint8 scaling_matrix_present_flag;
  guint8 scaling_lists_4x4[6][16];
  guint8 scaling_lists_8x8[6][64];

  guint8 log2_max_frame_num_minus4;
  guint8 pic_order_cnt_type;

  /* if pic_order_cnt_type == 0 */
  guint8 log2_max_pic_order_cnt_lsb_minus4;

  /* else if pic_order_cnt_type == 1 */
  guint8 delta_pic_order_always_zero_flag;
  gint32 offset_for_non_ref_pic;
  gint32 offset_for_top_to_bottom_field;
  guint8 num_ref_frames_in_pic_order_cnt_cycle;
  gint32 offset_for_ref_frame[255];

  guint32 num_ref_frames;
  guint8 gaps_in_frame_num_value_allowed_flag;
  guint32 pic_width_in_mbs_minus1;
  guint32 pic_height_in_map_units_minus1;
  guint8 frame_mbs_only_flag;

  guint8 mb_adaptive_frame_field_flag;

  guint8 direct_8x8_inference_flag;

  guint32 frame_crop_left_offset;
  guint32 frame_crop_right_offset;
  guint32 frame_crop_top_offset;
  guint32 frame_crop_bottom_offset;

  guint8 vui_parameters_present_flag;
  /* if vui_parameters_present_flag */
  GstH264VUIParameters vui_parameters;
  
  /* calculated values */
  guint8 ChromaArrayType;
  guint32 MaxFrameNum;
};

struct _GstH264Picture
{
  gint id;

  GstH264Sequence *sequence;

  guint8 entropy_coding_mode_flag;
  guint8 pic_order_present_flag;

  guint32 num_slice_groups_minus1;

  /* if num_slice_groups_minus1 > 0 */
  guint8 slice_group_map_type;
  /* and if slice_group_map_type == 0 */
  guint32 run_length_minus1[8];
  /* or if slice_group_map_type == 2 */
  guint32 top_left[8];
  guint32 bottom_right[8];
  /* or if slice_group_map_type == (3, 4, 5) */
  guint8 slice_group_change_direction_flag;
  guint32 slice_group_change_rate_minus1;
  /* or if slice_group_map_type == 6 */
  guint32 pic_size_in_map_units_minus1;
  guint8 *slice_group_id;

  guint8 num_ref_idx_l0_active_minus1;
  guint8 num_ref_idx_l1_active_minus1;
  guint8 weighted_pred_flag;
  guint8 weighted_bipred_idc;
  gint8 pic_init_qp_minus26;
  gint8 pic_init_qs_minus26;
  gint8 chroma_qp_index_offset;
  guint8 deblocking_filter_control_present_flag;
  guint8 constrained_intra_pred_flag;
  guint8 redundant_pic_cnt_present_flag;

  guint8 transform_8x8_mode_flag;

  guint8 scaling_lists_4x4[6][16];
  guint8 scaling_lists_8x8[6][64];

  guint8 second_chroma_qp_index_offset;
};

struct _GstH264RefPicMarking
{
  guint8 memory_management_control_operation;

  guint32 difference_of_pic_nums_minus1;
  guint32 long_term_pic_num;
  guint32 long_term_frame_idx;
  guint32 max_long_term_frame_idx_plus1;
};

struct _GstH264DecRefPicMarking
{
  /* if slice->nal_unit.IdrPicFlag */
  guint8 no_output_of_prior_pics_flag;
  guint8 long_term_reference_flag;

  guint8 adaptive_ref_pic_marking_mode_flag;
  GstH264RefPicMarking ref_pic_marking[10]; 
  guint8 n_ref_pic_marking;
};

struct _GstH264PredWeightTable
{
  guint8 luma_log2_weight_denom;
  guint8 chroma_log2_weight_denom;

  guint8 luma_weight_l0[32];
  guint8 luma_offset_l0[32];

  /* if seq->ChromaArrayType != 0 */
  guint8 chroma_weight_l0[32][2];
  guint8 chroma_offset_l0[32][2];

  /* if slice->slice_type % 5 == 1 */
  guint8 luma_weight_l1[32];
  guint8 luma_offset_l1[32];
  /* and if seq->ChromaArrayType != 0 */
  guint8 chroma_weight_l1[32][2];
  guint8 chroma_offset_l1[32][2];
};

struct _GstH264Slice
{
  GstNalUnit nal_unit;
  
  guint32 first_mb_in_slice;
  guint32 type;
  
  GstH264Picture *picture;

  /* if seq->separate_colour_plane_flag */
  guint8 colour_plane_id;

  guint16 frame_num;

  guint8 field_pic_flag;
  guint8 bottom_field_flag;

  /* if nal_unit.type == 5 */
  guint16 idr_pic_id;

  /* if seq->pic_order_cnt_type == 0 */
  guint16 pic_order_cnt_lsb;
  gint32 delta_pic_order_cnt_bottom;

  gint32 delta_pic_order_cnt[2];
  guint8 redundant_pic_cnt;

  /* if slice_type == B_SLICE */
  guint8 direct_spatial_mv_pred_flag;

  guint8 num_ref_idx_l0_active_minus1;
  guint8 num_ref_idx_l1_active_minus1;

  GstH264PredWeightTable pred_weight_table;
  /* if nal_unit.ref_idc != 0 */
  GstH264DecRefPicMarking dec_ref_pic_marking;

  /* calculated values */
  guint32 MaxPicNum;
};

struct _GstH264ClockTimestamp
{
  guint8 ct_type;
  guint8 nuit_field_based_flag;
  guint8 counting_type;
  guint8 discontinuity_flag;
  guint8 cnt_dropped_flag;
  guint8 n_frames;

  guint8 seconds_flag;
  guint8 seconds_value;

  guint8 minutes_flag;
  guint8 minutes_value;

  guint8 hours_flag;
  guint8 hours_value;

  guint32 time_offset;
};

struct _GstH264PicTiming
{
  guint8 cpb_removal_delay;
  guint8 dpb_output_delay;

  guint8 pic_struct_present_flag;
  /* if pic_struct_present_flag */
  guint8 pic_struct;
  
  guint8 clock_timestamp_flag[3];
  GstH264ClockTimestamp clock_timestamp[3];
};

struct _GstH264BufferingPeriod
{
  GstH264Sequence *seq;
  
  /* seq->vui_parameters->nal_hrd_parameters_present_flag */
  guint8 nal_initial_cpb_removal_delay[32];
  guint8 nal_initial_cpb_removal_delay_offset[32];

  /* seq->vui_parameters->vcl_hrd_parameters_present_flag */
  guint8 vcl_initial_cpb_removal_delay[32];
  guint8 vcl_initial_cpb_removal_delay_offset[32];
};

struct _GstH264SEIMessage
{
  guint32 payloadType;

  union {
    GstH264BufferingPeriod buffering_period;
    GstH264PicTiming pic_timing;
  };
};

#define GST_TYPE_H264_PARSER             (gst_h264_parser_get_type ())
#define GST_H264_PARSER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_H264_PARSER, GstH264Parser))
#define GST_H264_PARSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_H264_PARSER, GstH264ParserClass))
#define GST_IS_H264_PARSER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_H264_PARSER))
#define GST_IS_H264_PARSER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_H264_PARSER))
#define GST_H264_PARSER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_H264_PARSER, GstH264ParserClass))

typedef struct _GstH264ParserClass GstH264ParserClass;
typedef struct _GstH264Parser GstH264Parser;

struct _GstH264ParserClass
{
  GObjectClass parent_class;
};

struct _GstH264Parser
{
  GObject parent_instance;

  GHashTable *sequences;
  GHashTable *pictures;
};
		
GType gst_h264_parser_get_type (void) G_GNUC_CONST;

GstH264Sequence *gst_h264_parser_parse_sequence (GstH264Parser * parser, guint8 * data, guint size);
GstH264Picture *gst_h264_parser_parse_picture (GstH264Parser * parser, guint8 * data, guint size);
gboolean gst_h264_parser_parse_slice_header (GstH264Parser * parser, GstH264Slice * slice, guint8 * data, guint size, GstNalUnit nal_unit);
gboolean gst_h264_parser_parse_sei_message (GstH264Parser * parser, GstH264Sequence *seq, GstH264SEIMessage * sei, guint8 * data, guint size);

G_END_DECLS

#endif /* _GST_H264_PARSER_H_ */

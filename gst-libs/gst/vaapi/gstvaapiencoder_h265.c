/*
 *  gstvaapiencoder_h265.c - H.265 encoder
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "sysdeps.h"
#include <math.h>
#include <va/va.h>
#include <va/va_enc_hevc.h>
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gsth265parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapiencoder_h265.h"
#include "gstvaapiutils_h265.h"
#include "gstvaapiutils_h265_priv.h"
#include "gstvaapiutils_h26x_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                          \
  (GST_VAAPI_RATECONTROL_MASK (CQP)) |                  \
  GST_VAAPI_RATECONTROL_MASK (CBR)

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS                          \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_SEQUENCE |              \
   VA_ENC_PACKED_HEADER_PICTURE  |              \
   VA_ENC_PACKED_HEADER_SLICE)

typedef struct
{
  GstVaapiSurfaceProxy *pic;
  guint poc;
} GstVaapiEncoderH265Ref;

typedef enum
{
  GST_VAAPI_ENC_H265_REORD_NONE = 0,
  GST_VAAPI_ENC_H265_REORD_DUMP_FRAMES = 1,
  GST_VAAPI_ENC_H265_REORD_WAIT_FRAMES = 2
} GstVaapiEncH265ReorderState;

typedef struct _GstVaapiH265RefPool
{
  GQueue ref_list;
  guint max_ref_frames;
  guint max_reflist0_count;
  guint max_reflist1_count;
} GstVaapiH265RefPool;

typedef struct _GstVaapiH265ReorderPool
{
  GQueue reorder_frame_list;
  guint reorder_state;
  guint frame_index;
  guint cur_present_index;
} GstVaapiH265ReorderPool;

/* ------------------------------------------------------------------------- */
/* --- H.265 Encoder                                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENCODER_H265_CAST(encoder) \
    ((GstVaapiEncoderH265 *)(encoder))

struct _GstVaapiEncoderH265
{
  GstVaapiEncoder parent_instance;

  GstVaapiProfile profile;
  GstVaapiTierH265 tier;
  GstVaapiLevelH265 level;
  guint8 profile_idc;
  guint8 max_profile_idc;
  guint8 hw_max_profile_idc;
  guint8 level_idc;
  guint32 idr_period;
  guint32 init_qp;
  guint32 min_qp;
  guint32 num_slices;
  guint32 num_bframes;
  guint32 ctu_width;            /* CTU == Coding Tree Unit */
  guint32 ctu_height;
  guint32 luma_width;
  guint32 luma_height;
  GstClockTime cts_offset;
  gboolean config_changed;

  /* maximum required size of the decoded picture buffer */
  guint32 max_dec_pic_buffering;
  /* maximum allowed number of pictures that can precede any picture in
   * the CVS in decoding order and follow that picture in output order */
  guint32 max_num_reorder_pics;

  /* frame, poc */
  guint32 max_pic_order_cnt;
  guint32 log2_max_pic_order_cnt;
  guint32 idr_num;

  GstBuffer *vps_data;
  GstBuffer *sps_data;
  GstBuffer *pps_data;

  guint bitrate_bits;           // bitrate (bits)
  guint cpb_length;             // length of CPB buffer (ms)
  guint cpb_length_bits;        // length of CPB buffer (bits)

  /* Crop rectangle */
  guint conformance_window_flag:1;
  guint32 conf_win_left_offset;
  guint32 conf_win_right_offset;
  guint32 conf_win_top_offset;
  guint32 conf_win_bottom_offset;

  GstVaapiH265RefPool ref_pool;
  GstVaapiH265ReorderPool reorder_pool;
  guint first_slice_segment_in_pic_flag:1;
  guint sps_temporal_mvp_enabled_flag:1;
  guint sample_adaptive_offset_enabled_flag:1;
};

static inline gboolean
_poc_greater_than (guint poc1, guint poc2, guint max_poc)
{
  return (((poc1 - poc2) & (max_poc - 1)) < max_poc / 2);
}

/* Get slice_type value for H.265 specification */
static guint8
h265_get_slice_type (GstVaapiPictureType type)
{
  switch (type) {
    case GST_VAAPI_PICTURE_TYPE_I:
      return GST_H265_I_SLICE;
    case GST_VAAPI_PICTURE_TYPE_P:
      return GST_H265_P_SLICE;
    case GST_VAAPI_PICTURE_TYPE_B:
      return GST_H265_B_SLICE;
    default:
      break;
  }
  return -1;
}

/* Get log2_max_pic_order_cnt value for H.265 specification */
static guint
h265_get_log2_max_pic_order_cnt (guint num)
{
  guint ret = 0;

  while (num) {
    ++ret;
    num >>= 1;
  }
  if (ret <= 4)
    ret = 4;
  else if (ret > 10)
    ret = 10;
  /* must be greater than 4 */
  return ret;
}

/* Write the NAL unit header */
static gboolean
bs_write_nal_header (GstBitWriter * bs, guint32 nal_unit_type)
{
  guint8 nuh_layer_id = 0;
  guint8 nuh_temporal_id_plus1 = 1;

  WRITE_UINT32 (bs, 0, 1);
  WRITE_UINT32 (bs, nal_unit_type, 6);
  WRITE_UINT32 (bs, nuh_layer_id, 6);
  WRITE_UINT32 (bs, nuh_temporal_id_plus1, 3);

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write NAL unit header");
    return FALSE;
  }
}

/* Write the NAL unit trailing bits */
static gboolean
bs_write_trailing_bits (GstBitWriter * bs)
{
  if (!gst_bit_writer_put_bits_uint32 (bs, 1, 1))
    goto bs_error;
  gst_bit_writer_align_bytes_unchecked (bs, 0);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write NAL unit trailing bits");
    return FALSE;
  }
}

/* Write profile_tier_level()  */
static gboolean
bs_write_profile_tier_level (GstBitWriter * bs,
    const VAEncSequenceParameterBufferHEVC * seq_param)
{
  guint i;
  /* general_profile_space */
  WRITE_UINT32 (bs, 0, 2);
  /* general_tier_flag */
  WRITE_UINT32 (bs, seq_param->general_tier_flag, 1);
  /* general_profile_idc */
  WRITE_UINT32 (bs, seq_param->general_profile_idc, 5);
  /* general_profile_compatibility_flag[32] */
  for (i = 0; i < 32; i++) {
    if (i == 1 || i == 2)
      WRITE_UINT32 (bs, 1, 1);
    else
      WRITE_UINT32 (bs, 0, 1);
  }
  /* general_progressive_source_flag */
  WRITE_UINT32 (bs, 1, 1);
  /* general_interlaced_source_flag */
  WRITE_UINT32 (bs, 0, 1);
  /* general_non_packed_constraint_flag */
  WRITE_UINT32 (bs, 0, 1);
  /* general_frame_only_constraint_flag */
  WRITE_UINT32 (bs, 1, 1);
  /* general_reserved_zero_44bits */
  for (i = 0; i < 44; i++)
    WRITE_UINT32 (bs, 0, 1);
  /* general_level_idc */
  WRITE_UINT32 (bs, seq_param->general_level_idc, 8);

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write Profile Tier Level");
    return FALSE;
  }
}

/* Write an VPS NAL unit */
static gboolean
bs_write_vps_data (GstBitWriter * bs, GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture,
    const VAEncSequenceParameterBufferHEVC * seq_param, GstVaapiProfile profile)
{
  guint32 video_parameter_set_id = 0;
  guint32 vps_max_layers_minus1 = 0;
  guint32 vps_max_sub_layers_minus1 = 0;
  guint32 vps_temporal_id_nesting_flag = 1;
  guint32 vps_sub_layer_ordering_info_present_flag = 0;
  guint32 vps_max_latency_increase_plus1 = 0;
  guint32 vps_max_layer_id = 0;
  guint32 vps_num_layer_sets_minus1 = 0;
  guint32 vps_timing_info_present_flag = 0;
  guint32 vps_extension_flag = 0;

  /* video_parameter_set_id */
  WRITE_UINT32 (bs, video_parameter_set_id, 4);
  /* vps_reserved_three_2bits */
  WRITE_UINT32 (bs, 3, 2);
  /* vps_max_layers_minus1 */
  WRITE_UINT32 (bs, vps_max_layers_minus1, 6);
  /* vps_max_sub_layers_minus1 */
  WRITE_UINT32 (bs, vps_max_sub_layers_minus1, 3);
  /* vps_temporal_id_nesting_flag */
  WRITE_UINT32 (bs, vps_temporal_id_nesting_flag, 1);
  /* vps_reserved_0xffff_16bits */
  WRITE_UINT32 (bs, 0xffff, 16);

  /* profile_tier_level */
  bs_write_profile_tier_level (bs, seq_param);

  /* vps_sub_layer_ordering_info_present_flag */
  WRITE_UINT32 (bs, vps_sub_layer_ordering_info_present_flag, 1);
  /* vps_max_dec_pic_buffering_minus1 */
  WRITE_UE (bs, encoder->max_dec_pic_buffering - 1);
  /* vps_max_num_reorder_pics */
  WRITE_UE (bs, encoder->max_num_reorder_pics);
  /* vps_max_latency_increase_plus1 */
  WRITE_UE (bs, vps_max_latency_increase_plus1);
  /* vps_max_layer_id */
  WRITE_UINT32 (bs, vps_max_layer_id, 6);
  /* vps_num_layer_sets_minus1 */
  WRITE_UE (bs, vps_num_layer_sets_minus1);
  /* vps_timing_info_present_flag */
  WRITE_UINT32 (bs, vps_timing_info_present_flag, 1);
  /* vps_extension_flag */
  WRITE_UINT32 (bs, vps_extension_flag, 1);

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write VPS NAL unit");
    return FALSE;
  }
}

static gboolean
bs_write_vps (GstBitWriter * bs, GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture,
    const VAEncSequenceParameterBufferHEVC * seq_param, GstVaapiProfile profile)
{
  if (!bs_write_vps_data (bs, encoder, picture, seq_param, profile))
    return FALSE;

  /* rbsp_trailing_bits */
  bs_write_trailing_bits (bs);

  return FALSE;
}

/* Write an SPS NAL unit */
static gboolean
bs_write_sps_data (GstBitWriter * bs, GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture,
    const VAEncSequenceParameterBufferHEVC * seq_param, GstVaapiProfile profile,
    const VAEncMiscParameterHRD * hrd_params)
{
  guint32 video_parameter_set_id = 0;
  guint32 max_sub_layers_minus1 = 0;
  guint32 temporal_id_nesting_flag = 1;
  guint32 seq_parameter_set_id = 0;
  guint32 sps_sub_layer_ordering_info_present_flag = 0;
  guint32 sps_max_latency_increase_plus1 = 0;
  guint32 num_short_term_ref_pic_sets = 0;
  guint32 long_term_ref_pics_present_flag = 0;
  guint32 sps_extension_flag = 0;
  guint32 nal_hrd_parameters_present_flag = 0;
  guint maxNumSubLayers = 1, i;

  /* video_parameter_set_id */
  WRITE_UINT32 (bs, video_parameter_set_id, 4);
  /* max_sub_layers_minus1 */
  WRITE_UINT32 (bs, max_sub_layers_minus1, 3);
  /* temporal_id_nesting_flag */
  WRITE_UINT32 (bs, temporal_id_nesting_flag, 1);

  /* profile_tier_level */
  bs_write_profile_tier_level (bs, seq_param);

  /* seq_parameter_set_id */
  WRITE_UE (bs, seq_parameter_set_id);
  /* chroma_format_idc  = 1, 4:2:0 */
  WRITE_UE (bs, seq_param->seq_fields.bits.chroma_format_idc);
  /* pic_width_in_luma_samples */
  WRITE_UE (bs, seq_param->pic_width_in_luma_samples);
  /* pic_height_in_luma_samples */
  WRITE_UE (bs, seq_param->pic_height_in_luma_samples);

  /* conformance_window_flag */
  WRITE_UINT32 (bs, encoder->conformance_window_flag, 1);
  if (encoder->conformance_window_flag) {
    WRITE_UE (bs, encoder->conf_win_left_offset);
    WRITE_UE (bs, encoder->conf_win_right_offset);
    WRITE_UE (bs, encoder->conf_win_top_offset);
    WRITE_UE (bs, encoder->conf_win_bottom_offset);
  }

  /* bit_depth_luma_minus8 */
  WRITE_UE (bs, seq_param->seq_fields.bits.bit_depth_luma_minus8);
  /* bit_depth_chroma_minus8 */
  WRITE_UE (bs, seq_param->seq_fields.bits.bit_depth_chroma_minus8);
  /* log2_max_pic_order_cnt_lsb_minus4  */
  WRITE_UE (bs, encoder->log2_max_pic_order_cnt - 4);

  /* sps_sub_layer_ordering_info_present_flag */
  WRITE_UINT32 (bs, sps_sub_layer_ordering_info_present_flag, 1);
  /* sps_max_dec_pic_buffering_minus1 */
  WRITE_UE (bs, encoder->max_dec_pic_buffering - 1);
  /* sps_max_num_reorder_pics */
  WRITE_UE (bs, encoder->max_num_reorder_pics);
  /* sps_max_latency_increase_plus1 */
  WRITE_UE (bs, sps_max_latency_increase_plus1);

  /* log2_min_luma_coding_block_size_minus3 */
  WRITE_UE (bs, seq_param->log2_min_luma_coding_block_size_minus3);
  /* log2_diff_max_min_luma_coding_block_size */
  WRITE_UE (bs, seq_param->log2_diff_max_min_luma_coding_block_size);
  /* log2_min_transform_block_size_minus2 */
  WRITE_UE (bs, seq_param->log2_min_transform_block_size_minus2);
  /* log2_diff_max_min_transform_block_size */
  WRITE_UE (bs, seq_param->log2_diff_max_min_transform_block_size);
  /* max_transform_hierarchy_depth_inter */
  WRITE_UE (bs, seq_param->max_transform_hierarchy_depth_inter);
  /*max_transform_hierarchy_depth_intra */
  WRITE_UE (bs, seq_param->max_transform_hierarchy_depth_intra);

  /* scaling_list_enabled_flag */
  WRITE_UINT32 (bs, seq_param->seq_fields.bits.scaling_list_enabled_flag, 1);
  /* amp_enabled_flag */
  WRITE_UINT32 (bs, seq_param->seq_fields.bits.amp_enabled_flag, 1);
  /* sample_adaptive_offset_enabled_flag */
  WRITE_UINT32 (bs,
      seq_param->seq_fields.bits.sample_adaptive_offset_enabled_flag, 1);
  /* pcm_enabled_flag */
  WRITE_UINT32 (bs, seq_param->seq_fields.bits.pcm_enabled_flag, 1);

  /* num_short_term_ref_pic_sets  */
  WRITE_UE (bs, num_short_term_ref_pic_sets);

  /* long_term_ref_pics_present_flag */
  WRITE_UINT32 (bs, long_term_ref_pics_present_flag, 1);

  /* sps_temporal_mvp_enabled_flag */
  WRITE_UINT32 (bs, seq_param->seq_fields.bits.sps_temporal_mvp_enabled_flag,
      1);
  /* strong_intra_smoothing_enabled_flag */
  WRITE_UINT32 (bs,
      seq_param->seq_fields.bits.strong_intra_smoothing_enabled_flag, 1);

  /* vui_parameters_present_flag */
  WRITE_UINT32 (bs, seq_param->vui_parameters_present_flag, 1);

  /*--------------- Write VUI Parameters--------------- */
  if (seq_param->vui_parameters_present_flag) {
    gboolean vui_hrd_parameters_present_flag;
    /* aspect_ratio_info_present_flag */
    WRITE_UINT32 (bs,
        seq_param->vui_fields.bits.aspect_ratio_info_present_flag, 1);
    if (seq_param->vui_fields.bits.aspect_ratio_info_present_flag) {
      WRITE_UINT32 (bs, seq_param->aspect_ratio_idc, 8);
      if (seq_param->aspect_ratio_idc == 0xFF) {
        WRITE_UINT32 (bs, seq_param->sar_width, 16);
        WRITE_UINT32 (bs, seq_param->sar_height, 16);
      }
    }
    /* overscan_info_present_flag */
    WRITE_UINT32 (bs, 0, 1);
    /* video_signal_type_present_flag */
    WRITE_UINT32 (bs, 0, 1);
    /* chroma_loc_info_present_flag */
    WRITE_UINT32 (bs, 0, 1);
    /* neutral_chroma_indication_flag */
    WRITE_UINT32 (bs, seq_param->vui_fields.bits.neutral_chroma_indication_flag,
        1);
    /* field_seq_flag */
    WRITE_UINT32 (bs, seq_param->vui_fields.bits.field_seq_flag, 1);
    /* frame_field_info_present_flag */
    WRITE_UINT32 (bs, 0, 1);
    /* default_display_window_flag */
    WRITE_UINT32 (bs, 0, 1);

    /* timing_info_present_flag */
    WRITE_UINT32 (bs, seq_param->vui_fields.bits.vui_timing_info_present_flag,
        1);
    if (seq_param->vui_fields.bits.vui_timing_info_present_flag) {
      /* vui_num_units_in_tick */
      WRITE_UINT32 (bs, seq_param->vui_num_units_in_tick, 32);
      /* vui_time_scale */
      WRITE_UINT32 (bs, seq_param->vui_time_scale, 32);
      /* vui_poc_proportional_to_timing_flag */
      WRITE_UINT32 (bs, 0, 1);

      /* vui_hrd_parameters_present_flag */
      vui_hrd_parameters_present_flag = seq_param->bits_per_second > 0;
      WRITE_UINT32 (bs, vui_hrd_parameters_present_flag, 1);

      if (vui_hrd_parameters_present_flag) {
        nal_hrd_parameters_present_flag = 1;
        /* nal_hrd_parameters_present_flag */
        WRITE_UINT32 (bs, nal_hrd_parameters_present_flag, 1);
        /* vcl_hrd_parameters_present_flag */
        WRITE_UINT32 (bs, 0, 1);

        if (nal_hrd_parameters_present_flag) {
          /* sub_pic_hrd_params_present_flag */
          WRITE_UINT32 (bs, 0, 1);
          /* bit_rate_scale */
          WRITE_UINT32 (bs, SX_BITRATE - 6, 4);
          /* cpb_size_scale */
          WRITE_UINT32 (bs, SX_CPB_SIZE - 4, 4);
          /* initial_cpb_removal_delay_length_minus1 */
          WRITE_UINT32 (bs, 23, 5);
          /* au_cpb_removal_delay_length_minus1 */
          WRITE_UINT32 (bs, 23, 5);
          /* dpb_output_delay_length_minus1 */
          WRITE_UINT32 (bs, 23, 5);

          for (i = 0; i < maxNumSubLayers; i++) {
            /* fixed_pic_rate_general_flag */
            WRITE_UINT32 (bs, 0, 1);
            /* fixed_pic_rate_within_cvs_flag */
            WRITE_UINT32 (bs, 0, 1);
            /* low_delay_hrd_flag */
            WRITE_UINT32 (bs, 1, 1);
            /* bit_rate_value_minus1 */
            WRITE_UE (bs, (seq_param->bits_per_second >> SX_BITRATE) - 1);
            /* cpb_size_value_minus1 */
            WRITE_UE (bs, (hrd_params->buffer_size >> SX_CPB_SIZE) - 1);
            /* cbr_flag */
            WRITE_UINT32 (bs, 1, 1);
          }
        }
      }
    }
    /* bitstream_restriction_flag */
    WRITE_UINT32 (bs, seq_param->vui_fields.bits.bitstream_restriction_flag, 1);
  }
  /* sps_extension_flag */
  WRITE_UINT32 (bs, sps_extension_flag, 1);

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write SPS NAL unit");
    return FALSE;
  }
}

static gboolean
bs_write_sps (GstBitWriter * bs, GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture,
    const VAEncSequenceParameterBufferHEVC * seq_param, GstVaapiProfile profile,
    const VAEncMiscParameterHRD * hrd_params)
{
  if (!bs_write_sps_data (bs, encoder, picture, seq_param, profile, hrd_params))
    return FALSE;

  /* rbsp_trailing_bits */
  bs_write_trailing_bits (bs);

  return FALSE;
}

/* Write a PPS NAL unit */
static gboolean
bs_write_pps (GstBitWriter * bs,
    const VAEncPictureParameterBufferHEVC * pic_param)
{
  guint32 pic_parameter_set_id = 0;
  guint32 seq_parameter_set_id = 0;
  guint32 output_flag_present_flag = 0;
  guint32 num_extra_slice_header_bits = 0;
  guint32 cabac_init_present_flag = 0;
  guint32 pps_slice_chroma_qp_offsets_present_flag = 0;
  guint32 deblocking_filter_control_present_flag = 0;
  guint32 lists_modification_present_flag = 0;
  guint32 slice_segment_header_extension_present_flag = 0;
  guint32 pps_extension_flag = 0;

  /* pic_parameter_set_id */
  WRITE_UE (bs, pic_parameter_set_id);
  /* seq_parameter_set_id */
  WRITE_UE (bs, seq_parameter_set_id);
  /* dependent_slice_segments_enabled_flag */
  WRITE_UINT32 (bs,
      pic_param->pic_fields.bits.dependent_slice_segments_enabled_flag, 1);
  /* output_flag_present_flag */
  WRITE_UINT32 (bs, output_flag_present_flag, 1);
  /* num_extra_slice_header_bits */
  WRITE_UINT32 (bs, num_extra_slice_header_bits, 3);
  /* sign_data_hiding_enabled_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.sign_data_hiding_enabled_flag,
      1);
  /* cabac_init_present_flag */
  WRITE_UINT32 (bs, cabac_init_present_flag, 1);
  /* num_ref_idx_l0_default_active_minus1 */
  WRITE_UE (bs, pic_param->num_ref_idx_l0_default_active_minus1);
  /* num_ref_idx_l1_default_active_minus1 */
  WRITE_UE (bs, pic_param->num_ref_idx_l1_default_active_minus1);
  /* pic_init_qp_minus26 */
  WRITE_SE (bs, pic_param->pic_init_qp - 26);
  /* constrained_intra_pred_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.constrained_intra_pred_flag, 1);
  /* transform_skip_enabled_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.transform_skip_enabled_flag, 1);
  /* cu_qp_delta_enabled_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.cu_qp_delta_enabled_flag, 1);
  /* diff_cu_qp_delta_depth */
  if (pic_param->pic_fields.bits.cu_qp_delta_enabled_flag)
    WRITE_UE (bs, pic_param->diff_cu_qp_delta_depth);

  /* pps_cb_qp_offset */
  WRITE_SE (bs, pic_param->pps_cb_qp_offset);
  /* pps_cr_qp_offset */
  WRITE_SE (bs, pic_param->pps_cr_qp_offset);
  /* pps_slice_chroma_qp_offsets_present_flag */
  WRITE_UINT32 (bs, pps_slice_chroma_qp_offsets_present_flag, 1);
  /* weighted_pred_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.weighted_pred_flag, 1);
  /* weighted_bipred_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.weighted_bipred_flag, 1);
  /* transquant_bypass_enabled_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.transquant_bypass_enabled_flag,
      1);
  /* tiles_enabled_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.tiles_enabled_flag, 1);
  /* entropy_coding_sync_enabled_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.entropy_coding_sync_enabled_flag,
      1);
  /* pps_loop_filter_across_slices_enabled_flag */
  WRITE_UINT32 (bs,
      pic_param->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag, 1);
  /* deblocking_filter_control_present_flag */
  WRITE_UINT32 (bs, deblocking_filter_control_present_flag, 1);
  /* pps_scaling_list_data_present_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.scaling_list_data_present_flag,
      1);
  /* lists_modification_present_flag */
  WRITE_UINT32 (bs, lists_modification_present_flag, 1);
  /* log2_parallel_merge_level_minus2 */
  WRITE_UE (bs, pic_param->log2_parallel_merge_level_minus2);
  /* slice_segment_header_extension_present_flag */
  WRITE_UINT32 (bs, slice_segment_header_extension_present_flag, 1);
  /* pps_extension_flag */
  WRITE_UINT32 (bs, pps_extension_flag, 1);

  /* rbsp_trailing_bits */
  bs_write_trailing_bits (bs);

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write PPS NAL unit");
    return FALSE;
  }
}

/* Write a Slice NAL unit */
static gboolean
bs_write_slice (GstBitWriter * bs,
    const VAEncSliceParameterBufferHEVC * slice_param,
    GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture,
    guint8 nal_unit_type)
{
  const VAEncPictureParameterBufferHEVC *const pic_param = picture->param;

  guint8 no_output_of_prior_pics_flag = 0;
  guint8 dependent_slice_segment_flag = 0;
  guint8 short_term_ref_pic_set_sps_flag = 0;
  guint8 num_ref_idx_active_override_flag = 0;
  guint8 slice_deblocking_filter_disabled_flag = 0;

  /* first_slice_segment_in_pic_flag */
  WRITE_UINT32 (bs, encoder->first_slice_segment_in_pic_flag, 1);

  /* Fixme: For all IRAP pics */
  /* no_output_of_prior_pics_flag */
  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture))
    WRITE_UINT32 (bs, no_output_of_prior_pics_flag, 1);

  /* slice_pic_parameter_set_id */
  WRITE_UE (bs, slice_param->slice_pic_parameter_set_id);

  /* slice_segment_address , bits_size = Ceil(Log2(PicSizeInCtbsY)) */
  if (!encoder->first_slice_segment_in_pic_flag) {
    guint pic_size_ctb = encoder->ctu_width * encoder->ctu_height;
    guint bits_size = (guint) ceil ((log2 (pic_size_ctb)));
    WRITE_UINT32 (bs, slice_param->slice_segment_address, bits_size);
  }

  if (!dependent_slice_segment_flag) {
    /* slice_type */
    WRITE_UE (bs, slice_param->slice_type);

    if (!pic_param->pic_fields.bits.idr_pic_flag) {
      /* slice_pic_order_cnt_lsb */
      WRITE_UINT32 (bs, picture->poc, encoder->log2_max_pic_order_cnt);
      /* short_term_ref_pic_set_sps_flag */
      WRITE_UINT32 (bs, short_term_ref_pic_set_sps_flag, 1);

    /*---------- Write short_term_ref_pic_set(0) ----------- */
      {
        guint num_positive_pics = 0, num_negative_pics = 0;
        guint delta_poc_s0_minus1 = 0, delta_poc_s1_minus1 = 0;
        guint used_by_curr_pic_s0_flag = 0, used_by_curr_pic_s1_flag = 0;

        if (picture->type == GST_VAAPI_PICTURE_TYPE_P) {
          num_negative_pics = 1;
          num_positive_pics = 0;
          delta_poc_s0_minus1 =
              picture->poc - slice_param->ref_pic_list0[0].pic_order_cnt - 1;
          used_by_curr_pic_s0_flag = 1;
          delta_poc_s1_minus1 = 0;
          used_by_curr_pic_s1_flag = 0;
        }
        if (picture->type == GST_VAAPI_PICTURE_TYPE_B) {
          num_negative_pics = 1;
          num_positive_pics = 1;
          delta_poc_s0_minus1 =
              picture->poc - slice_param->ref_pic_list0[0].pic_order_cnt - 1;
          used_by_curr_pic_s0_flag = 1;
          delta_poc_s1_minus1 =
              slice_param->ref_pic_list1[0].pic_order_cnt - picture->poc - 1;
          used_by_curr_pic_s1_flag = 1;
        }

        /* num_negative_pics */
        WRITE_UE (bs, num_negative_pics);
        /* num_positive_pics */
        WRITE_UE (bs, num_positive_pics);
        if (num_negative_pics) {
          /* delta_poc_s0_minus1 */
          WRITE_UE (bs, delta_poc_s0_minus1);
          /* used_by_curr_pic_s0_flag */
          WRITE_UINT32 (bs, used_by_curr_pic_s0_flag, 1);
        }
        if (num_positive_pics) {
          /* delta_poc_s1_minus1 */
          WRITE_UE (bs, delta_poc_s1_minus1);
          /* used_by_curr_pic_s1_flag */
          WRITE_UINT32 (bs, used_by_curr_pic_s1_flag, 1);
        }
      }

      /* slice_temporal_mvp_enabled_flag */
      if (encoder->sps_temporal_mvp_enabled_flag)
        WRITE_UINT32 (bs,
            slice_param->slice_fields.bits.slice_temporal_mvp_enabled_flag, 1);
    }

    if (encoder->sample_adaptive_offset_enabled_flag) {
      WRITE_UINT32 (bs, slice_param->slice_fields.bits.slice_sao_luma_flag, 1);
      WRITE_UINT32 (bs, slice_param->slice_fields.bits.slice_sao_chroma_flag,
          1);
    }

    if (slice_param->slice_type == GST_H265_P_SLICE ||
        slice_param->slice_type == GST_H265_B_SLICE) {
      /* num_ref_idx_active_override_flag */
      WRITE_UINT32 (bs, num_ref_idx_active_override_flag, 1);
      /* mvd_l1_zero_flag */
      if (slice_param->slice_type == GST_H265_B_SLICE)
        WRITE_UINT32 (bs, slice_param->slice_fields.bits.mvd_l1_zero_flag, 1);

      /* cabac_init_present_flag == FALSE */
      /* cabac_init_flag  = FALSE */

      /* collocated_from_l0_flag */
      if (slice_param->slice_fields.bits.slice_temporal_mvp_enabled_flag) {
        if (slice_param->slice_type == GST_H265_B_SLICE)
          WRITE_UINT32 (bs,
              slice_param->slice_fields.bits.collocated_from_l0_flag, 1);
      }
      /* five_minus_max_num_merge_cand */
      WRITE_UE (bs, 5 - slice_param->max_num_merge_cand);
    }

    /* slice_qp_delta */
    WRITE_SE (bs, slice_param->slice_qp_delta);
    if (pic_param->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag &&
        (slice_param->slice_fields.bits.slice_sao_luma_flag
            || slice_param->slice_fields.bits.slice_sao_chroma_flag
            || !slice_deblocking_filter_disabled_flag))
      WRITE_UINT32 (bs,
          slice_param->slice_fields.bits.
          slice_loop_filter_across_slices_enabled_flag, 1);

  }

  /* byte_alignment() */
  {
    /* alignment_bit_equal_to_one */
    WRITE_UINT32 (bs, 1, 1);
    while (GST_BIT_WRITER_BIT_SIZE (bs) % 8 != 0) {
      /* alignment_bit_equal_to_zero */
      WRITE_UINT32 (bs, 0, 1);
    }
  }

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write Slice NAL unit");
    return FALSE;
  }
}

static inline void
_check_vps_sps_pps_status (GstVaapiEncoderH265 * encoder,
    const guint8 * nal, guint32 size)
{
  guint8 nal_type;
  G_GNUC_UNUSED gsize ret;      /* FIXME */
  g_assert (size);

  if (encoder->vps_data && encoder->sps_data && encoder->pps_data)
    return;

  nal_type = (nal[0] & 0x7E) >> 1;
  switch (nal_type) {
    case GST_H265_NAL_VPS:
      encoder->vps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (encoder->vps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    case GST_H265_NAL_SPS:
      encoder->sps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (encoder->sps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    case GST_H265_NAL_PPS:
      encoder->pps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (encoder->pps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    default:
      break;
  }
}

/* Determines the largest supported profile by the underlying hardware */
static gboolean
ensure_hw_profile_limits (GstVaapiEncoderH265 * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GArray *profiles;
  guint i, profile_idc, max_profile_idc;

  if (encoder->hw_max_profile_idc)
    return TRUE;

  profiles = gst_vaapi_display_get_encode_profiles (display);
  if (!profiles)
    return FALSE;

  max_profile_idc = 0;
  for (i = 0; i < profiles->len; i++) {
    const GstVaapiProfile profile =
        g_array_index (profiles, GstVaapiProfile, i);
    profile_idc = gst_vaapi_utils_h265_get_profile_idc (profile);
    if (!profile_idc)
      continue;
    if (max_profile_idc < profile_idc)
      max_profile_idc = profile_idc;
  }
  g_array_unref (profiles);

  encoder->hw_max_profile_idc = max_profile_idc;
  return TRUE;
}

/* Derives the profile supported by the underlying hardware */
static gboolean
ensure_hw_profile (GstVaapiEncoderH265 * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  GstVaapiProfile profile, profiles[4];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = encoder->profile;
  switch (encoder->profile) {
    case GST_VAAPI_PROFILE_H265_MAIN_STILL_PICTURE:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H265_MAIN;
      // fall-through
    case GST_VAAPI_PROFILE_H265_MAIN:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H265_MAIN10;
      break;
    default:
      break;
  }

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  for (i = 0; i < num_profiles; i++) {
    if (gst_vaapi_display_has_encoder (display, profiles[i], entrypoint)) {
      profile = profiles[i];
      break;
    }
  }
  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    goto error_unsupported_profile;

  GST_VAAPI_ENCODER_CAST (encoder)->profile = profile;
  return TRUE;

  /* ERRORS */
error_unsupported_profile:
  {
    GST_ERROR ("unsupported HW profile (0x%08x)", encoder->profile);
    return FALSE;
  }
}

/* Check target decoder constraints */
static gboolean
ensure_profile_limits (GstVaapiEncoderH265 * encoder)
{

  if (!encoder->max_profile_idc
      || encoder->profile_idc <= encoder->max_profile_idc)
    return TRUE;

  GST_WARNING
      ("Needs to lower coding tools to meet target decoder constraints");
  GST_WARNING ("Only supporting Main profile, reset profile to Main");

  encoder->profile = GST_VAAPI_PROFILE_H265_MAIN;
  encoder->profile_idc =
      gst_vaapi_utils_h265_get_profile_idc (encoder->profile);

  return TRUE;
}

/* Derives the minimum profile from the active coding tools */
static gboolean
ensure_profile (GstVaapiEncoderH265 * encoder)
{
  GstVaapiProfile profile;
  const GstVideoFormat format =
      GST_VIDEO_INFO_FORMAT (GST_VAAPI_ENCODER_VIDEO_INFO (encoder));

  /* Always start from "Main" profile for maximum
     compatibility */
  profile = GST_VAAPI_PROFILE_H265_MAIN;

  if (format == GST_VIDEO_FORMAT_P010_10LE)
    profile = GST_VAAPI_PROFILE_H265_MAIN10;

  encoder->profile = profile;
  encoder->profile_idc = gst_vaapi_utils_h265_get_profile_idc (profile);
  return TRUE;
}

/* Derives the minimum tier from the active coding tools */
static gboolean
ensure_tier (GstVaapiEncoderH265 * encoder)
{

  encoder->tier = GST_VAAPI_TIER_H265_MAIN;
  /*Fixme: Derive proper tier based on upstream caps or limits, coding tools etc */

  return TRUE;
}

/* Derives the level from the currently set limits */
static gboolean
ensure_level (GstVaapiEncoderH265 * encoder)
{
  const GstVaapiH265LevelLimits *limits_table;
  guint i, num_limits, PicSizeInSamplesY;

  PicSizeInSamplesY = encoder->luma_width * encoder->luma_height;

  limits_table = gst_vaapi_utils_h265_get_level_limits_table (&num_limits);
  for (i = 0; i < num_limits; i++) {
    const GstVaapiH265LevelLimits *const limits = &limits_table[i];
    if (PicSizeInSamplesY <= limits->MaxLumaPs)
      break;
    /* Fixme: Add more constraint checking:tier (extracted from caps), cpb size,
     * bitrate, num_tile_columns and num_tile_rows */
  }
  if (i == num_limits)
    goto error_unsupported_level;

  encoder->level = limits_table[i].level;
  encoder->level_idc = limits_table[i].level_idc;
  return TRUE;

  /* ERRORS */
error_unsupported_level:
  {
    GST_ERROR ("failed to find a suitable level matching codec config");
    return FALSE;
  }
}

/* Enable "high-compression" tuning options */
static gboolean
ensure_tuning_high_compression (GstVaapiEncoderH265 * encoder)
{
  guint8 profile_idc;

  if (!ensure_hw_profile_limits (encoder))
    return FALSE;

  profile_idc = encoder->hw_max_profile_idc;
  if (encoder->max_profile_idc && encoder->max_profile_idc < profile_idc)
    profile_idc = encoder->max_profile_idc;

  /* Tuning options */
  if (!encoder->num_bframes)
    encoder->num_bframes = 3;

  return TRUE;
}

/* Ensure tuning options */
static gboolean
ensure_tuning (GstVaapiEncoderH265 * encoder)
{
  gboolean success;

  switch (GST_VAAPI_ENCODER_TUNE (encoder)) {
    case GST_VAAPI_ENCODER_TUNE_HIGH_COMPRESSION:
      success = ensure_tuning_high_compression (encoder);
      break;
    default:
      success = TRUE;
      break;
  }
  return success;
}

/* Handle new GOP starts */
static void
reset_gop_start (GstVaapiEncoderH265 * encoder)
{
  GstVaapiH265ReorderPool *const reorder_pool = &encoder->reorder_pool;

  reorder_pool->frame_index = 1;
  reorder_pool->cur_present_index = 0;
  ++encoder->idr_num;
}

/* Marks the supplied picture as a B-frame */
static void
set_b_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH265 * encoder)
{
  g_assert (pic && encoder);
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_B;
}

/* Marks the supplied picture as a P-frame */
static void
set_p_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH265 * encoder)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_P;
}

/* Marks the supplied picture as an I-frame */
static void
set_i_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH265 * encoder)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_I;

  g_assert (pic->frame);
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (pic->frame);
}

/* Marks the supplied picture as an IDR frame */
static void
set_idr_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH265 * encoder)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_I;
  pic->poc = 0;
  GST_VAAPI_ENC_PICTURE_FLAG_SET (pic, GST_VAAPI_ENC_PICTURE_FLAG_IDR);

  g_assert (pic->frame);
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (pic->frame);
}

/* Marks the supplied picture a a key-frame */
static void
set_key_frame (GstVaapiEncPicture * picture,
    GstVaapiEncoderH265 * encoder, gboolean is_idr)
{
  if (is_idr) {
    reset_gop_start (encoder);
    set_idr_frame (picture, encoder);
  } else
    set_i_frame (picture, encoder);
}

/* Fills in VA HRD parameters */
static void
fill_hrd_params (GstVaapiEncoderH265 * encoder, VAEncMiscParameterHRD * hrd)
{
  if (encoder->bitrate_bits > 0) {
    hrd->buffer_size = encoder->cpb_length_bits;
    hrd->initial_buffer_fullness = hrd->buffer_size / 2;
  } else {
    hrd->buffer_size = 0;
    hrd->initial_buffer_fullness = 0;
  }
}

/* Adds the supplied video parameter set header (VPS) to the list of packed
   headers to pass down as-is to the encoder */
static gboolean
add_packed_vps_header (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_vps;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_vps_param = { 0 };
  const VAEncSequenceParameterBufferHEVC *const seq_param = sequence->param;
  GstVaapiProfile profile = encoder->profile;

  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init (&bs, 128 * 8);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */
  bs_write_nal_header (&bs, GST_H265_NAL_VPS);

  bs_write_vps (&bs, encoder, picture, seq_param, profile);

  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_vps_param.type = VAEncPackedHeaderSequence;
  packed_vps_param.bit_length = data_bit_size;
  packed_vps_param.has_emulation_bytes = 0;

  packed_vps = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_vps_param, sizeof (packed_vps_param),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_vps);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_vps);
  gst_vaapi_codec_object_replace (&packed_vps, NULL);

  /* store vps data */
  _check_vps_sps_pps_status (encoder, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_clear (&bs, TRUE);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write VPS NAL unit");
    gst_bit_writer_clear (&bs, TRUE);
    return FALSE;
  }
}

/* Adds the supplied sequence header (SPS) to the list of packed
   headers to pass down as-is to the encoder */
static gboolean
add_packed_sequence_header (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_seq;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_seq_param = { 0 };
  const VAEncSequenceParameterBufferHEVC *const seq_param = sequence->param;
  GstVaapiProfile profile = encoder->profile;

  VAEncMiscParameterHRD hrd_params;
  guint32 data_bit_size;
  guint8 *data;

  fill_hrd_params (encoder, &hrd_params);

  gst_bit_writer_init (&bs, 128 * 8);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */
  bs_write_nal_header (&bs, GST_H265_NAL_SPS);

  bs_write_sps (&bs, encoder, picture, seq_param, profile, &hrd_params);

  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_seq_param.type = VAEncPackedHeaderSequence;
  packed_seq_param.bit_length = data_bit_size;
  packed_seq_param.has_emulation_bytes = 0;

  packed_seq = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_seq_param, sizeof (packed_seq_param),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_seq);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_seq);
  gst_vaapi_codec_object_replace (&packed_seq, NULL);

  /* store sps data */
  _check_vps_sps_pps_status (encoder, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_clear (&bs, TRUE);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write SPS NAL unit");
    gst_bit_writer_clear (&bs, TRUE);
    return FALSE;
  }
}

/* Adds the supplied picture header (PPS) to the list of packed
   headers to pass down as-is to the encoder */
static gboolean
add_packed_picture_header (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncPackedHeader *packed_pic;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_pic_param = { 0 };
  const VAEncPictureParameterBufferHEVC *const pic_param = picture->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init (&bs, 128 * 8);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */
  bs_write_nal_header (&bs, GST_H265_NAL_PPS);
  bs_write_pps (&bs, pic_param);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_pic_param.type = VAEncPackedHeaderPicture;
  packed_pic_param.bit_length = data_bit_size;
  packed_pic_param.has_emulation_bytes = 0;

  packed_pic = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_pic_param, sizeof (packed_pic_param),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_pic);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_pic);
  gst_vaapi_codec_object_replace (&packed_pic, NULL);

  /* store pps data */
  _check_vps_sps_pps_status (encoder, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_clear (&bs, TRUE);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write PPS NAL unit");
    gst_bit_writer_clear (&bs, TRUE);
    return FALSE;
  }
}

static gboolean
get_nal_unit_type (GstVaapiEncPicture * picture, guint8 * nal_unit_type)
{
  switch (picture->type) {
    case GST_VAAPI_PICTURE_TYPE_I:
      if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture))
        *nal_unit_type = GST_H265_NAL_SLICE_IDR_W_RADL;
      else
        *nal_unit_type = GST_H265_NAL_SLICE_TRAIL_R;
      break;
    case GST_VAAPI_PICTURE_TYPE_P:
      *nal_unit_type = GST_H265_NAL_SLICE_TRAIL_R;
      break;
    case GST_VAAPI_PICTURE_TYPE_B:
      *nal_unit_type = GST_H265_NAL_SLICE_TRAIL_N;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

/* Adds the supplied slice header to the list of packed
   headers to pass down as-is to the encoder */
static gboolean
add_packed_slice_header (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncSlice * slice)
{
  GstVaapiEncPackedHeader *packed_slice;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_slice_param = { 0 };
  const VAEncSliceParameterBufferHEVC *const slice_param = slice->param;
  guint32 data_bit_size;
  guint8 *data;
  guint8 nal_unit_type;

  gst_bit_writer_init (&bs, 128 * 8);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */

  if (!get_nal_unit_type (picture, &nal_unit_type))
    goto bs_error;
  bs_write_nal_header (&bs, nal_unit_type);

  bs_write_slice (&bs, slice_param, encoder, picture, nal_unit_type);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_slice_param.type = VAEncPackedHeaderSlice;
  packed_slice_param.bit_length = data_bit_size;
  packed_slice_param.has_emulation_bytes = 0;

  packed_slice = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_slice_param, sizeof (packed_slice_param),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_slice);

  gst_vaapi_enc_slice_add_packed_header (slice, packed_slice);
  gst_vaapi_codec_object_replace (&packed_slice, NULL);

  gst_bit_writer_clear (&bs, TRUE);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write Slice NAL unit header");
    gst_bit_writer_clear (&bs, TRUE);
    return FALSE;
  }
}

/* Reference picture management */
static void
reference_pic_free (GstVaapiEncoderH265 * encoder, GstVaapiEncoderH265Ref * ref)
{
  if (!ref)
    return;
  if (ref->pic)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder), ref->pic);
  g_slice_free (GstVaapiEncoderH265Ref, ref);
}

static inline GstVaapiEncoderH265Ref *
reference_pic_create (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * surface)
{
  GstVaapiEncoderH265Ref *const ref = g_slice_new0 (GstVaapiEncoderH265Ref);

  ref->pic = surface;
  ref->poc = picture->poc;
  return ref;
}

static gboolean
reference_list_update (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * surface)
{
  GstVaapiEncoderH265Ref *ref;
  GstVaapiH265RefPool *const ref_pool = &encoder->ref_pool;

  if (GST_VAAPI_PICTURE_TYPE_B == picture->type) {
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder), surface);
    return TRUE;
  }

  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture)) {
    while (!g_queue_is_empty (&ref_pool->ref_list))
      reference_pic_free (encoder, g_queue_pop_head (&ref_pool->ref_list));
  } else if (g_queue_get_length (&ref_pool->ref_list) >=
      ref_pool->max_ref_frames) {
    reference_pic_free (encoder, g_queue_pop_head (&ref_pool->ref_list));
  }
  ref = reference_pic_create (encoder, picture, surface);
  g_queue_push_tail (&ref_pool->ref_list, ref);
  g_assert (g_queue_get_length (&ref_pool->ref_list) <=
      ref_pool->max_ref_frames);
  return TRUE;
}

static gboolean
reference_list_init (GstVaapiEncoderH265 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiEncoderH265Ref ** reflist_0,
    guint * reflist_0_count,
    GstVaapiEncoderH265Ref ** reflist_1, guint * reflist_1_count)
{
  GstVaapiEncoderH265Ref *tmp;
  GstVaapiH265RefPool *const ref_pool = &encoder->ref_pool;
  GList *iter, *list_0_start = NULL, *list_1_start = NULL;
  guint count;

  *reflist_0_count = 0;
  *reflist_1_count = 0;
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  iter = g_queue_peek_tail_link (&ref_pool->ref_list);
  for (; iter; iter = g_list_previous (iter)) {
    tmp = (GstVaapiEncoderH265Ref *) iter->data;
    g_assert (tmp && tmp->poc != picture->poc);
    if (_poc_greater_than (picture->poc, tmp->poc, encoder->max_pic_order_cnt)) {
      list_0_start = iter;
      list_1_start = g_list_next (iter);
      break;
    }
  }

  /* order reflist_0 */
  g_assert (list_0_start);
  iter = list_0_start;
  count = 0;
  for (; iter; iter = g_list_previous (iter)) {
    reflist_0[count] = (GstVaapiEncoderH265Ref *) iter->data;
    ++count;
  }
  *reflist_0_count = count;

  if (picture->type != GST_VAAPI_PICTURE_TYPE_B)
    return TRUE;

  /* order reflist_1 */
  count = 0;
  iter = list_1_start;
  for (; iter; iter = g_list_next (iter)) {
    reflist_1[count] = (GstVaapiEncoderH265Ref *) iter->data;
    ++count;
  }
  *reflist_1_count = count;
  return TRUE;
}

/* Fills in VA sequence parameter buffer */
static gboolean
fill_sequence (GstVaapiEncoderH265 * encoder, GstVaapiEncSequence * sequence)
{
  VAEncSequenceParameterBufferHEVC *const seq_param = sequence->param;
  const GstVideoFormat format =
      GST_VIDEO_INFO_FORMAT (GST_VAAPI_ENCODER_VIDEO_INFO (encoder));
  guint bits_depth_luma_minus8 =
      GST_VIDEO_FORMAT_INFO_DEPTH (gst_video_format_get_info (format), 0);
  if (bits_depth_luma_minus8 < 8)
    return FALSE;
  bits_depth_luma_minus8 -= 8;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferHEVC));

  seq_param->general_profile_idc = encoder->profile_idc;
  seq_param->general_level_idc = encoder->level_idc;
  seq_param->general_tier_flag = 0;     /* Fixme: use the tier flag extracted from upstream caps or calcuted one */

  seq_param->intra_period = GST_VAAPI_ENCODER_KEYFRAME_PERIOD (encoder);
  seq_param->intra_idr_period = encoder->idr_period;
  seq_param->ip_period = 1 + encoder->num_bframes;
  seq_param->ip_period = seq_param->intra_period > 1 ?
      (1 + encoder->num_bframes) : 0;
  seq_param->bits_per_second = encoder->bitrate_bits;

  seq_param->pic_width_in_luma_samples = encoder->luma_width;
  seq_param->pic_height_in_luma_samples = encoder->luma_height;

  /*sequence field values */
  seq_param->seq_fields.value = 0;
  seq_param->seq_fields.bits.chroma_format_idc = 1;
  seq_param->seq_fields.bits.separate_colour_plane_flag = 0;
  seq_param->seq_fields.bits.bit_depth_luma_minus8 = bits_depth_luma_minus8;
  seq_param->seq_fields.bits.bit_depth_chroma_minus8 = bits_depth_luma_minus8;
  seq_param->seq_fields.bits.scaling_list_enabled_flag = FALSE;
  seq_param->seq_fields.bits.strong_intra_smoothing_enabled_flag = TRUE;
  seq_param->seq_fields.bits.amp_enabled_flag = TRUE;
  seq_param->seq_fields.bits.sample_adaptive_offset_enabled_flag =
      encoder->sample_adaptive_offset_enabled_flag = FALSE;
  seq_param->seq_fields.bits.pcm_enabled_flag = FALSE;
  seq_param->seq_fields.bits.pcm_loop_filter_disabled_flag = FALSE;
  seq_param->seq_fields.bits.sps_temporal_mvp_enabled_flag =
      encoder->sps_temporal_mvp_enabled_flag = TRUE;

  /* Based on 32x32 CTU */
  seq_param->log2_min_luma_coding_block_size_minus3 = 0;
  seq_param->log2_diff_max_min_luma_coding_block_size = 2;
  seq_param->log2_min_transform_block_size_minus2 = 0;
  seq_param->log2_diff_max_min_transform_block_size = 3;
  seq_param->max_transform_hierarchy_depth_inter = 3;
  seq_param->max_transform_hierarchy_depth_intra = 3;

  seq_param->pcm_sample_bit_depth_luma_minus1 = 0;
  seq_param->pcm_sample_bit_depth_chroma_minus1 = 0;
  seq_param->log2_min_pcm_luma_coding_block_size_minus3 = 0;
  seq_param->log2_max_pcm_luma_coding_block_size_minus3 = 0;

  /* VUI parameters are always set, at least for timing_info (framerate) */
  seq_param->vui_parameters_present_flag = TRUE;
  if (seq_param->vui_parameters_present_flag) {
    seq_param->vui_fields.bits.aspect_ratio_info_present_flag = TRUE;
    if (seq_param->vui_fields.bits.aspect_ratio_info_present_flag) {
      const GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);
      seq_param->aspect_ratio_idc = 0xff;
      seq_param->sar_width = GST_VIDEO_INFO_PAR_N (vip);
      seq_param->sar_height = GST_VIDEO_INFO_PAR_D (vip);
    }
    seq_param->vui_fields.bits.bitstream_restriction_flag = FALSE;
    seq_param->vui_fields.bits.vui_timing_info_present_flag = TRUE;
    if (seq_param->vui_fields.bits.vui_timing_info_present_flag) {
      seq_param->vui_num_units_in_tick = GST_VAAPI_ENCODER_FPS_D (encoder);
      seq_param->vui_time_scale = GST_VAAPI_ENCODER_FPS_N (encoder);
    }
  }
  return TRUE;
}

/* Fills in VA picture parameter buffer */
static gboolean
fill_picture (GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferHEVC *const pic_param = picture->param;
  GstVaapiH265RefPool *const ref_pool = &encoder->ref_pool;
  GstVaapiEncoderH265Ref *ref_pic;
  GList *reflist;
  guint i;
  guint8 nal_unit_type, no_output_of_prior_pics_flag = 0;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferHEVC));

  pic_param->decoded_curr_pic.picture_id =
      GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->decoded_curr_pic.pic_order_cnt = picture->poc;
  pic_param->decoded_curr_pic.flags = 0;

  i = 0;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
    for (reflist = g_queue_peek_head_link (&ref_pool->ref_list);
        reflist; reflist = g_list_next (reflist)) {
      ref_pic = reflist->data;
      g_assert (ref_pic && ref_pic->pic &&
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic) != VA_INVALID_ID);

      pic_param->reference_frames[i].picture_id =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic);
      ++i;
    }
    g_assert (i <= 15 && i <= ref_pool->max_ref_frames);
  }
  for (; i < 15; ++i) {
    pic_param->reference_frames[i].picture_id = VA_INVALID_SURFACE;
    pic_param->reference_frames[i].flags = 0;
  }
  pic_param->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

  /* slice_temporal_mvp_enable_flag == FALSE */
  pic_param->collocated_ref_pic_index = 0xFF;

  pic_param->last_picture = 0;
  pic_param->pic_init_qp = encoder->init_qp;
  pic_param->num_ref_idx_l0_default_active_minus1 =
      (ref_pool->max_reflist0_count ? (ref_pool->max_reflist0_count - 1) : 0);
  pic_param->num_ref_idx_l1_default_active_minus1 =
      (ref_pool->max_reflist1_count ? (ref_pool->max_reflist1_count - 1) : 0);

  if (!get_nal_unit_type (picture, &nal_unit_type))
    return FALSE;
  pic_param->nal_unit_type = nal_unit_type;

  /* set picture fields */
  pic_param->pic_fields.value = 0;
  pic_param->pic_fields.bits.idr_pic_flag =
      GST_VAAPI_ENC_PICTURE_IS_IDR (picture);
  pic_param->pic_fields.bits.coding_type = picture->type;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_B)
    pic_param->pic_fields.bits.reference_pic_flag = TRUE;
  pic_param->pic_fields.bits.sign_data_hiding_enabled_flag = FALSE;
  pic_param->pic_fields.bits.transform_skip_enabled_flag = TRUE;
  /* it seems driver requires enablement of cu_qp_delta_enabled_flag
   * to modifiy QP values in CBR mode encoding */
  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CBR)
    pic_param->pic_fields.bits.cu_qp_delta_enabled_flag = TRUE;
  pic_param->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag = TRUE;

  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture))
    no_output_of_prior_pics_flag = 1;
  pic_param->pic_fields.bits.no_output_of_prior_pics_flag =
      no_output_of_prior_pics_flag;

  return TRUE;
}

/* Adds slice headers to picture */
static gboolean
add_slice_headers (GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture,
    GstVaapiEncoderH265Ref ** reflist_0, guint reflist_0_count,
    GstVaapiEncoderH265Ref ** reflist_1, guint reflist_1_count)
{
  VAEncSliceParameterBufferHEVC *slice_param;
  GstVaapiEncSlice *slice;
  guint slice_of_ctus, slice_mod_ctus, cur_slice_ctus;
  guint ctu_size;
  guint ctu_width_round_factor;
  guint last_ctu_index;
  guint i_slice, i_ref;

  g_assert (picture);

  ctu_size = encoder->ctu_width * encoder->ctu_height;

  g_assert (encoder->num_slices && encoder->num_slices < ctu_size);
  slice_of_ctus = ctu_size / encoder->num_slices;
  slice_mod_ctus = ctu_size % encoder->num_slices;
  last_ctu_index = 0;

  for (i_slice = 0;
      i_slice < encoder->num_slices && (last_ctu_index < ctu_size); ++i_slice) {
    cur_slice_ctus = slice_of_ctus;
    if (slice_mod_ctus) {
      ++cur_slice_ctus;
      --slice_mod_ctus;
    }

    /* Work-around for satisfying the VA-Intel driver.
     * The driver only support multi slice begin from row start address */
    ctu_width_round_factor =
        encoder->ctu_width - (cur_slice_ctus % encoder->ctu_width);
    cur_slice_ctus += ctu_width_round_factor;
    if ((last_ctu_index + cur_slice_ctus) > ctu_size)
      cur_slice_ctus = ctu_size - last_ctu_index;

    slice = GST_VAAPI_ENC_SLICE_NEW (HEVC, encoder);
    g_assert (slice && slice->param_id != VA_INVALID_ID);
    slice_param = slice->param;

    memset (slice_param, 0, sizeof (VAEncSliceParameterBufferHEVC));
    if (i_slice == 0) {
      encoder->first_slice_segment_in_pic_flag = TRUE;
      slice_param->slice_segment_address = 0;
    } else {
      encoder->first_slice_segment_in_pic_flag = FALSE;
      slice_param->slice_segment_address = last_ctu_index;
    }
    slice_param->num_ctu_in_slice = cur_slice_ctus;
    slice_param->slice_type = h265_get_slice_type (picture->type);
    slice_param->slice_pic_parameter_set_id = 0;

    if (picture->type != GST_VAAPI_PICTURE_TYPE_I && reflist_0_count > 0)
      slice_param->num_ref_idx_l0_active_minus1 = reflist_0_count - 1;
    else
      slice_param->num_ref_idx_l0_active_minus1 = 0;
    if (picture->type == GST_VAAPI_PICTURE_TYPE_B && reflist_1_count > 0)
      slice_param->num_ref_idx_l1_active_minus1 = reflist_1_count - 1;
    else
      slice_param->num_ref_idx_l1_active_minus1 = 0;
    g_assert (slice_param->num_ref_idx_l0_active_minus1 == 0);
    g_assert (slice_param->num_ref_idx_l1_active_minus1 == 0);

    i_ref = 0;
    if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
      for (; i_ref < reflist_0_count; ++i_ref) {
        slice_param->ref_pic_list0[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_0[i_ref]->pic);
        slice_param->ref_pic_list0[i_ref].pic_order_cnt = reflist_0[i_ref]->poc;
      }
      g_assert (i_ref == 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->ref_pic_list0); ++i_ref) {
      slice_param->ref_pic_list0[i_ref].picture_id = VA_INVALID_SURFACE;
      slice_param->ref_pic_list0[i_ref].flags = 0;
    }

    i_ref = 0;
    if (picture->type == GST_VAAPI_PICTURE_TYPE_B) {
      for (; i_ref < reflist_1_count; ++i_ref) {
        slice_param->ref_pic_list1[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_1[i_ref]->pic);
        slice_param->ref_pic_list1[i_ref].pic_order_cnt = reflist_1[i_ref]->poc;
      }
      g_assert (i_ref == 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->ref_pic_list1); ++i_ref) {
      slice_param->ref_pic_list1[i_ref].picture_id = VA_INVALID_SURFACE;
      slice_param->ref_pic_list1[i_ref].flags = 0;
    }

    slice_param->max_num_merge_cand = 5;        /* MaxNumMergeCand  */
    slice_param->slice_qp_delta = encoder->init_qp - encoder->min_qp;

    slice_param->slice_fields.value = 0;

    slice_param->slice_fields.bits.
        slice_loop_filter_across_slices_enabled_flag = TRUE;

    /* set calculation for next slice */
    last_ctu_index += cur_slice_ctus;

    if ((i_slice == encoder->num_slices - 1) || (last_ctu_index == ctu_size))
      slice_param->slice_fields.bits.last_slice_of_pic_flag = 1;

    if ((GST_VAAPI_ENCODER_PACKED_HEADERS (encoder) &
            VA_ENC_PACKED_HEADER_SLICE)
        && !add_packed_slice_header (encoder, picture, slice))
      goto error_create_packed_slice_hdr;

    gst_vaapi_enc_picture_add_slice (picture, slice);
    gst_vaapi_codec_object_replace (&slice, NULL);
  }
  if (i_slice < encoder->num_slices)
    GST_WARNING
        ("Using less number of slices than requested, Number of slices per pictures is %d",
        i_slice);
  g_assert (last_ctu_index == ctu_size);

  return TRUE;

error_create_packed_slice_hdr:
  {
    GST_ERROR ("failed to create packed slice header buffer");
    gst_vaapi_codec_object_replace (&slice, NULL);
    return FALSE;
  }
}

/* Generates and submits SPS header accordingly into the bitstream */
static gboolean
ensure_sequence (GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence = NULL;

  /* submit an SPS header before every new I-frame, if codec config changed */
  if (!encoder->config_changed || picture->type != GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (HEVC, encoder);
  if (!sequence || !fill_sequence (encoder, sequence))
    goto error_create_seq_param;

  /* add packed vps and sps headers */
  if ((GST_VAAPI_ENCODER_PACKED_HEADERS (encoder) &
          VA_ENC_PACKED_HEADER_SEQUENCE)
      && !(add_packed_vps_header (encoder, picture, sequence)
          && add_packed_sequence_header (encoder, picture, sequence))) {
    goto error_create_packed_seq_hdr;
  }

  if (sequence) {
    gst_vaapi_enc_picture_set_sequence (picture, sequence);
    gst_vaapi_codec_object_replace (&sequence, NULL);
  }

  encoder->config_changed = FALSE;
  return TRUE;

  /* ERRORS */
error_create_seq_param:
  {
    GST_ERROR ("failed to create sequence parameter buffer (SPS)");
    gst_vaapi_codec_object_replace (&sequence, NULL);
    return FALSE;
  }
error_create_packed_seq_hdr:
  {
    GST_ERROR ("failed to create packed sequence header buffer");
    gst_vaapi_codec_object_replace (&sequence, NULL);
    return FALSE;
  }
}

static gboolean
ensure_misc_params (GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncMiscParam *misc = NULL;

  /* HRD params for rate control */
  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CBR) {
    misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, encoder);
    g_assert (misc);
    if (!misc)
      return FALSE;
    fill_hrd_params (encoder, misc->data);
    gst_vaapi_enc_picture_add_misc_param (picture, misc);
    gst_vaapi_codec_object_replace (&misc, NULL);
  }

  return TRUE;
}

/* Generates and submits PPS header accordingly into the bitstream */
static gboolean
ensure_picture (GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);
  gboolean res = FALSE;

  res = fill_picture (encoder, picture, codedbuf, surface);

  if (!res)
    return FALSE;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I &&
      (GST_VAAPI_ENCODER_PACKED_HEADERS (encoder) &
          VA_ENC_PACKED_HEADER_PICTURE)
      && !add_packed_picture_header (encoder, picture)) {
    GST_ERROR ("set picture packed header failed");
    return FALSE;
  }
  return TRUE;
}


/* Generates slice headers */
static gboolean
ensure_slices (GstVaapiEncoderH265 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncoderH265Ref *reflist_0[15];
  GstVaapiEncoderH265Ref *reflist_1[15];
  GstVaapiH265RefPool *const ref_pool = &encoder->ref_pool;
  guint reflist_0_count = 0, reflist_1_count = 0;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I &&
      !reference_list_init (encoder, picture,
          reflist_0, &reflist_0_count, reflist_1, &reflist_1_count)) {
    GST_ERROR ("reference list reorder failed");
    return FALSE;
  }

  g_assert (reflist_0_count + reflist_1_count <= ref_pool->max_ref_frames);
  if (reflist_0_count > ref_pool->max_reflist0_count)
    reflist_0_count = ref_pool->max_reflist0_count;
  if (reflist_1_count > ref_pool->max_reflist1_count)
    reflist_1_count = ref_pool->max_reflist1_count;

  if (!add_slice_headers (encoder, picture,
          reflist_0, reflist_0_count, reflist_1, reflist_1_count))
    return FALSE;

  return TRUE;
}

/* Normalizes bitrate (and CPB size) for HRD conformance */
static void
ensure_bitrate_hrd (GstVaapiEncoderH265 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  guint bitrate, cpb_size;

  if (!base_encoder->bitrate) {
    encoder->bitrate_bits = 0;
    return;
  }

  /* Round down bitrate. This is a hard limit mandated by the user */
  g_assert (SX_BITRATE >= 6);
  bitrate = (base_encoder->bitrate * 1000) & ~((1U << SX_BITRATE) - 1);
  if (bitrate != encoder->bitrate_bits) {
    GST_DEBUG ("HRD bitrate: %u bits/sec", bitrate);
    encoder->bitrate_bits = bitrate;
    encoder->config_changed = TRUE;
  }

  /* Round up CPB size. This is an HRD compliance detail */
  g_assert (SX_CPB_SIZE >= 4);
  cpb_size = gst_util_uint64_scale (bitrate, encoder->cpb_length, 1000) &
      ~((1U << SX_CPB_SIZE) - 1);
  if (cpb_size != encoder->cpb_length_bits) {
    GST_DEBUG ("HRD CPB size: %u bits", cpb_size);
    encoder->cpb_length_bits = cpb_size;
    encoder->config_changed = TRUE;
  }
}

/* Estimates a good enough bitrate if none was supplied */
static void
ensure_bitrate (GstVaapiEncoderH265 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  switch (GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    case GST_VAAPI_RATECONTROL_CBR:
      if (!base_encoder->bitrate) {
        /* Fixme: Provide better estimation */
        /* Using a 1/6 compression ratio */
        /* 12 bits per pixel fro yuv420 */
        guint64 factor = encoder->luma_width * encoder->luma_height * 12 / 6;
        base_encoder->bitrate =
            gst_util_uint64_scale (factor, GST_VAAPI_ENCODER_FPS_N (encoder),
            GST_VAAPI_ENCODER_FPS_D (encoder)) / 1000;
        GST_INFO ("target bitrate computed to %u kbps", base_encoder->bitrate);
      }
      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }
  ensure_bitrate_hrd (encoder);
}

/* Constructs profile, tier and level information based on user-defined limits */
static GstVaapiEncoderStatus
ensure_profile_tier_level (GstVaapiEncoderH265 * encoder)
{
  const GstVaapiProfile profile = encoder->profile;
  const GstVaapiTierH265 tier = encoder->tier;
  const GstVaapiLevelH265 level = encoder->level;

  ensure_tuning (encoder);

  if (!ensure_profile (encoder) || !ensure_profile_limits (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  /* Check HW constraints */
  if (!ensure_hw_profile_limits (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  if (encoder->profile_idc > encoder->hw_max_profile_idc)
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  /* ensure tier */
  if (!ensure_tier (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;

  /* Ensure bitrate if not set already and derive the right level to use */
  ensure_bitrate (encoder);

  if (!ensure_level (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;

  if (encoder->profile != profile || encoder->level != level
      || encoder->tier != tier) {
    GST_DEBUG ("selected %s profile at tier %s and level %s",
        gst_vaapi_utils_h265_get_profile_string (encoder->profile),
        gst_vaapi_utils_h265_get_tier_string (encoder->tier),
        gst_vaapi_utils_h265_get_level_string (encoder->level));
    encoder->config_changed = TRUE;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static void
reset_properties (GstVaapiEncoderH265 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  GstVaapiH265ReorderPool *reorder_pool;
  GstVaapiH265RefPool *ref_pool;
  guint ctu_size;

  if (encoder->idr_period < base_encoder->keyframe_period)
    encoder->idr_period = base_encoder->keyframe_period;
  if (encoder->idr_period > MAX_IDR_PERIOD)
    encoder->idr_period = MAX_IDR_PERIOD;

  /*Fixme: provide user control for idr_period ?? */
  encoder->idr_period = base_encoder->keyframe_period * 2;

  if (encoder->min_qp > encoder->init_qp ||
      (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CQP &&
          encoder->min_qp < encoder->init_qp))
    encoder->min_qp = encoder->init_qp;

  ctu_size = encoder->ctu_width * encoder->ctu_height;
  if (encoder->num_slices > (ctu_size + 1) / 2)
    encoder->num_slices = (ctu_size + 1) / 2;
  g_assert (encoder->num_slices);

  if (encoder->num_bframes > (base_encoder->keyframe_period + 1) / 2)
    encoder->num_bframes = (base_encoder->keyframe_period + 1) / 2;

  if (encoder->num_bframes > 0 && GST_VAAPI_ENCODER_FPS_N (encoder) > 0)
    encoder->cts_offset = gst_util_uint64_scale (GST_SECOND,
        GST_VAAPI_ENCODER_FPS_D (encoder), GST_VAAPI_ENCODER_FPS_N (encoder));
  else
    encoder->cts_offset = 0;

  /* init max_poc */
  encoder->log2_max_pic_order_cnt =
      h265_get_log2_max_pic_order_cnt (encoder->idr_period);
  g_assert (encoder->log2_max_pic_order_cnt >= 4);
  encoder->max_pic_order_cnt = (1 << encoder->log2_max_pic_order_cnt);
  encoder->idr_num = 0;

  /* Only Supporting a maximum of two reference frames */
  if (encoder->num_bframes) {
    encoder->max_dec_pic_buffering = 3;
    encoder->max_num_reorder_pics = 1;
  } else {
    encoder->max_dec_pic_buffering =
        (GST_VAAPI_ENCODER_KEYFRAME_PERIOD (encoder) == 1) ? 1 : 2;
    encoder->max_num_reorder_pics = 0;
  }

  ref_pool = &encoder->ref_pool;
  ref_pool->max_reflist0_count = 1;
  ref_pool->max_reflist1_count = encoder->num_bframes > 0;
  ref_pool->max_ref_frames = ref_pool->max_reflist0_count
      + ref_pool->max_reflist1_count;

  reorder_pool = &encoder->reorder_pool;
  reorder_pool->frame_index = 0;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h265_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base_encoder);

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));

  if (!ensure_sequence (encoder, picture))
    goto error;
  if (!ensure_misc_params (encoder, picture))
    goto error;
  if (!ensure_picture (encoder, picture, codedbuf, reconstruct))
    goto error;
  if (!ensure_slices (encoder, picture))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;

  if (!reference_list_update (encoder, picture, reconstruct))
    goto error;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    if (reconstruct)
      gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
          reconstruct);
    return ret;
  }
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h265_flush (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVaapiH265ReorderPool *reorder_pool;
  GstVaapiEncPicture *pic;

  reorder_pool = &encoder->reorder_pool;
  reorder_pool->frame_index = 0;
  reorder_pool->cur_present_index = 0;

  while (!g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
    pic = (GstVaapiEncPicture *)
        g_queue_pop_head (&reorder_pool->reorder_frame_list);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&reorder_pool->reorder_frame_list);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/* Generate "codec-data" buffer */
static GstVaapiEncoderStatus
gst_vaapi_encoder_h265_get_codec_data (GstVaapiEncoder * base_encoder,
    GstBuffer ** out_buffer_ptr)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  const guint32 configuration_version = 0x01;
  const guint32 nal_length_size = 4;
  GstMapInfo vps_info, sps_info, pps_info;
  GstBitWriter bs;
  GstBuffer *buffer;
  guint min_spatial_segmentation_idc = 0;
  guint num_arrays = 3;

  if (!encoder->vps_data || !encoder->sps_data || !encoder->pps_data)
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER;
  if (gst_buffer_get_size (encoder->sps_data) < 4)
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER;

  if (!gst_buffer_map (encoder->vps_data, &vps_info, GST_MAP_READ))
    goto error_map_vps_buffer;

  if (!gst_buffer_map (encoder->sps_data, &sps_info, GST_MAP_READ))
    goto error_map_sps_buffer;

  if (!gst_buffer_map (encoder->pps_data, &pps_info, GST_MAP_READ))
    goto error_map_pps_buffer;

  /* Header */
  gst_bit_writer_init (&bs,
      (vps_info.size + sps_info.size + pps_info.size + 64) * 8);
  WRITE_UINT32 (&bs, configuration_version, 8);
  WRITE_UINT32 (&bs, sps_info.data[4], 8);      /* profile_space | tier_flag | profile_idc */
  WRITE_UINT32 (&bs, sps_info.data[5], 32);     /* profile_compatibility_flag [0-31] */
  /* progressive_source_flag | interlaced_source_flag | non_packed_constraint_flag |
   * frame_only_constraint_flag | reserved_zero_bits[0-27] */
  WRITE_UINT32 (&bs, sps_info.data[9], 32);
  WRITE_UINT32 (&bs, sps_info.data[13], 16);    /* reserved_zero_bits [28-43] */
  WRITE_UINT32 (&bs, sps_info.data[15], 8);     /* level_idc */
  WRITE_UINT32 (&bs, 0x0f, 4);  /* 1111 */
  WRITE_UINT32 (&bs, min_spatial_segmentation_idc, 12); /* min_spatial_segmentation_idc */
  WRITE_UINT32 (&bs, 0x3f, 6);  /* 111111 */
  WRITE_UINT32 (&bs, 0x00, 2);  /* parallelismType */
  WRITE_UINT32 (&bs, 0x3f, 6);  /* 111111 */
  WRITE_UINT32 (&bs, 0x01, 2);  /* chroma_format_idc */
  WRITE_UINT32 (&bs, 0x1f, 5);  /* 11111 */
  WRITE_UINT32 (&bs, 0x01, 3);  /* bit_depth_luma_minus8 */
  WRITE_UINT32 (&bs, 0x1f, 5);  /* 11111 */
  WRITE_UINT32 (&bs, 0x01, 3);  /* bit_depth_chroma_minus8 */
  WRITE_UINT32 (&bs, 0x00, 16); /* avgFramerate */
  WRITE_UINT32 (&bs, 0x00, 2);  /* constatnFramerate */
  WRITE_UINT32 (&bs, 0x00, 3);  /* numTemporalLayers */
  WRITE_UINT32 (&bs, 0x00, 1);  /* temporalIdNested */
  WRITE_UINT32 (&bs, nal_length_size - 1, 2);   /* lengthSizeMinusOne */
  WRITE_UINT32 (&bs, 0x00, 8);  /* numOfArrays */

  WRITE_UINT32 (&bs, num_arrays, 8);    /* numOfArrays */

  /* Write VPS */
  WRITE_UINT32 (&bs, 0x00, 1);  /* array_completeness */
  WRITE_UINT32 (&bs, 0x00, 1);  /* reserved zero */
  WRITE_UINT32 (&bs, GST_H265_NAL_VPS, 6);      /* Nal_unit_type */
  WRITE_UINT32 (&bs, 0x01, 16); /* numNalus, VPS count = 1 */
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  /* Write Nal unit length and data of VPS */
  if (!gst_vaapi_utils_h26x_write_nal_unit (&bs, vps_info.data, vps_info.size))
    goto nal_to_byte_stream_error;

  /* Write SPS */
  WRITE_UINT32 (&bs, 0x00, 1);  /* array_completeness */
  WRITE_UINT32 (&bs, 0x00, 1);  /* reserved zero */
  WRITE_UINT32 (&bs, GST_H265_NAL_SPS, 6);      /* Nal_unit_type */
  WRITE_UINT32 (&bs, 0x01, 16); /* numNalus, SPS count = 1 */
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  /* Write Nal unit length and data of SPS */
  if (!gst_vaapi_utils_h26x_write_nal_unit (&bs, sps_info.data, sps_info.size))
    goto nal_to_byte_stream_error;

  /* Write PPS */
  WRITE_UINT32 (&bs, 0x00, 1);  /* array_completeness */
  WRITE_UINT32 (&bs, 0x00, 1);  /* reserved zero */
  WRITE_UINT32 (&bs, GST_H265_NAL_PPS, 6);      /* Nal_unit_type */
  WRITE_UINT32 (&bs, 0x01, 16); /* numNalus, PPS count = 1 */
  /* Write Nal unit length and data of PPS */
  if (!gst_vaapi_utils_h26x_write_nal_unit (&bs, pps_info.data, pps_info.size))
    goto nal_to_byte_stream_error;

  gst_buffer_unmap (encoder->pps_data, &pps_info);
  gst_buffer_unmap (encoder->sps_data, &sps_info);
  gst_buffer_unmap (encoder->vps_data, &vps_info);

  buffer = gst_buffer_new_wrapped (GST_BIT_WRITER_DATA (&bs),
      GST_BIT_WRITER_BIT_SIZE (&bs) / 8);
  if (!buffer)
    goto error_alloc_buffer;
  *out_buffer_ptr = buffer;

  gst_bit_writer_clear (&bs, FALSE);
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
bs_error:
  {
    GST_ERROR ("failed to write codec-data");
    gst_buffer_unmap (encoder->vps_data, &vps_info);
    gst_buffer_unmap (encoder->sps_data, &sps_info);
    gst_buffer_unmap (encoder->pps_data, &pps_info);
    gst_bit_writer_clear (&bs, TRUE);
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
nal_to_byte_stream_error:
  {
    GST_ERROR ("failed to write nal unit");
    gst_buffer_unmap (encoder->vps_data, &vps_info);
    gst_buffer_unmap (encoder->sps_data, &sps_info);
    gst_buffer_unmap (encoder->pps_data, &pps_info);
    gst_bit_writer_clear (&bs, TRUE);
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
error_map_vps_buffer:
  {
    GST_ERROR ("failed to map VPS packed header");
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
error_map_sps_buffer:
  {
    GST_ERROR ("failed to map SPS packed header");
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
error_map_pps_buffer:
  {
    GST_ERROR ("failed to map PPS packed header");
    gst_buffer_unmap (encoder->sps_data, &sps_info);
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
error_alloc_buffer:
  {
    GST_ERROR ("failed to allocate codec-data buffer");
    gst_bit_writer_clear (&bs, TRUE);
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
}

/* TODO */
/* The re-ordering algorithm is similar to what we implemented for
 * h264 encoder. But We could have a better algorithm for hevc encoder
 * by having B-frames as reference pictures */
static GstVaapiEncoderStatus
gst_vaapi_encoder_h265_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVaapiH265ReorderPool *reorder_pool = NULL;
  GstVaapiEncPicture *picture;
  gboolean is_idr = FALSE;

  *output = NULL;

  reorder_pool = &encoder->reorder_pool;

  if (!frame) {
    if (reorder_pool->reorder_state != GST_VAAPI_ENC_H265_REORD_DUMP_FRAMES)
      return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

    /* reorder_state = GST_VAAPI_ENC_H265_REORD_DUMP_FRAMES
       dump B frames from queue, sometime, there may also have P frame or I frame */
    g_assert (encoder->num_bframes > 0);
    g_return_val_if_fail (!g_queue_is_empty (&reorder_pool->reorder_frame_list),
        GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN);
    picture = g_queue_pop_head (&reorder_pool->reorder_frame_list);
    g_assert (picture);
    if (g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
      reorder_pool->reorder_state = GST_VAAPI_ENC_H265_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new frame coming */
  picture = GST_VAAPI_ENC_PICTURE_NEW (HEVC, encoder, frame);
  if (!picture) {
    GST_WARNING ("create H265 picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  ++reorder_pool->cur_present_index;
  picture->poc = ((reorder_pool->cur_present_index * 1) %
      encoder->max_pic_order_cnt);

  is_idr = (reorder_pool->frame_index == 0 ||
      reorder_pool->frame_index >= encoder->idr_period);

  /* check key frames */
  if (is_idr || GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame) ||
      (reorder_pool->frame_index %
          GST_VAAPI_ENCODER_KEYFRAME_PERIOD (encoder)) == 0) {
    ++reorder_pool->frame_index;

    /* b frame enabled,  check queue of reorder_frame_list */
    if (encoder->num_bframes
        && !g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
      GstVaapiEncPicture *p_pic;

      p_pic = g_queue_pop_tail (&reorder_pool->reorder_frame_list);
      set_p_frame (p_pic, encoder);
      g_queue_foreach (&reorder_pool->reorder_frame_list,
          (GFunc) set_b_frame, encoder);
      set_key_frame (picture, encoder, is_idr);
      g_queue_push_tail (&reorder_pool->reorder_frame_list, picture);
      picture = p_pic;
      reorder_pool->reorder_state = GST_VAAPI_ENC_H265_REORD_DUMP_FRAMES;
    } else {                    /* no b frames in queue */
      set_key_frame (picture, encoder, is_idr);
      g_assert (g_queue_is_empty (&reorder_pool->reorder_frame_list));
      if (encoder->num_bframes)
        reorder_pool->reorder_state = GST_VAAPI_ENC_H265_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new p/b frames coming */
  ++reorder_pool->frame_index;
  if (reorder_pool->reorder_state == GST_VAAPI_ENC_H265_REORD_WAIT_FRAMES &&
      g_queue_get_length (&reorder_pool->reorder_frame_list) <
      encoder->num_bframes) {
    g_queue_push_tail (&reorder_pool->reorder_frame_list, picture);
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;
  }

  set_p_frame (picture, encoder);

  if (reorder_pool->reorder_state == GST_VAAPI_ENC_H265_REORD_WAIT_FRAMES) {
    g_queue_foreach (&reorder_pool->reorder_frame_list, (GFunc) set_b_frame,
        encoder);
    reorder_pool->reorder_state = GST_VAAPI_ENC_H265_REORD_DUMP_FRAMES;
    g_assert (!g_queue_is_empty (&reorder_pool->reorder_frame_list));
  }

end:
  g_assert (picture);
  frame = picture->frame;
  if (GST_CLOCK_TIME_IS_VALID (frame->pts))
    frame->pts += encoder->cts_offset;
  *output = picture;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);
  const guint DEFAULT_SURFACES_COUNT = 3;

  /* Fixme: Using only a rough approximation for bitstream headers..
   * Fixme: Not taken into account: ScalingList, RefPicListModification, PredWeightTable */
  /* Maximum sizes for common headers (in bits) */
  enum
  {
    MAX_PROFILE_TIER_LEVEL_SIZE = 684,
    MAX_VPS_HDR_SIZE = 13781,
    MAX_SPS_HDR_SIZE = 615,
    MAX_SHORT_TERM_REFPICSET_SIZE = 55,
    MAX_VUI_PARAMS_SIZE = 267,
    MAX_HRD_PARAMS_SIZE = 8196,
    MAX_PPS_HDR_SIZE = 274,
    MAX_SLICE_HDR_SIZE = 33660
  };

  /* Account for VPS header */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_VPS_HDR_SIZE +
      MAX_PROFILE_TIER_LEVEL_SIZE + MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for SPS header */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE +
      MAX_PROFILE_TIER_LEVEL_SIZE + 64 * MAX_SHORT_TERM_REFPICSET_SIZE +
      MAX_VUI_PARAMS_SIZE + MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8;

  /* Account for slice header */
  base_encoder->codedbuf_size += encoder->num_slices * (4 +
      GST_ROUND_UP_8 (MAX_SLICE_HDR_SIZE + MAX_SHORT_TERM_REFPICSET_SIZE) / 8);

  if (!ensure_hw_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames =
      ((encoder->num_bframes ? 2 : 1) + DEFAULT_SURFACES_COUNT);

  /* Only YUV 4:2:0 formats are supported for now. */
  base_encoder->codedbuf_size += GST_ROUND_UP_32 (vip->width) *
      GST_ROUND_UP_32 (vip->height) * 3 / 2;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h265_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVaapiEncoderStatus status;
  guint luma_width, luma_height;

  luma_width = GST_VAAPI_ENCODER_WIDTH (encoder);
  luma_height = GST_VAAPI_ENCODER_HEIGHT (encoder);

  if (luma_width != encoder->luma_width || luma_height != encoder->luma_height) {
    GST_DEBUG ("resolution: %d %d", GST_VAAPI_ENCODER_WIDTH (encoder),
        GST_VAAPI_ENCODER_HEIGHT (encoder));
    encoder->luma_width = GST_ROUND_UP_32 (luma_width);
    encoder->luma_height = GST_ROUND_UP_32 (luma_height);
    encoder->ctu_width = (encoder->luma_width + 31) / 32;
    encoder->ctu_height = (encoder->luma_height + 31) / 32;
    encoder->config_changed = TRUE;

    /* Frame Cropping */
    if ((GST_VAAPI_ENCODER_WIDTH (encoder) & 31) ||
        (GST_VAAPI_ENCODER_HEIGHT (encoder) & 31)) {
      static const guint SubWidthC[] = { 1, 2, 2, 1 };
      static const guint SubHeightC[] = { 1, 2, 1, 1 };
      encoder->conformance_window_flag = 1;
      encoder->conf_win_left_offset = 0;
      encoder->conf_win_right_offset =
          (encoder->luma_width -
          GST_VAAPI_ENCODER_WIDTH (encoder)) / SubWidthC[1];
      encoder->conf_win_top_offset = 0;
      encoder->conf_win_bottom_offset =
          (encoder->luma_height -
          GST_VAAPI_ENCODER_HEIGHT (encoder)) / SubHeightC[1];
    }
  }

  status = ensure_profile_tier_level (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  reset_properties (encoder);
  return set_context_info (base_encoder);
}

static gboolean
gst_vaapi_encoder_h265_init (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVaapiH265ReorderPool *reorder_pool;
  GstVaapiH265RefPool *ref_pool;

  encoder->conformance_window_flag = 0;
  encoder->num_slices = 1;

  /* re-ordering  list initialize */
  reorder_pool = &encoder->reorder_pool;
  g_queue_init (&reorder_pool->reorder_frame_list);
  reorder_pool->reorder_state = GST_VAAPI_ENC_H265_REORD_NONE;
  reorder_pool->frame_index = 0;
  reorder_pool->cur_present_index = 0;

  /* reference list info initialize */
  ref_pool = &encoder->ref_pool;
  g_queue_init (&ref_pool->ref_list);
  ref_pool->max_ref_frames = 0;
  ref_pool->max_reflist0_count = 1;
  ref_pool->max_reflist1_count = 1;

  return TRUE;
}

static void
gst_vaapi_encoder_h265_finalize (GstVaapiEncoder * base_encoder)
{
  /*free private buffers */
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);
  GstVaapiEncPicture *pic;
  GstVaapiEncoderH265Ref *ref;
  GstVaapiH265RefPool *ref_pool;
  GstVaapiH265ReorderPool *reorder_pool;

  gst_buffer_replace (&encoder->vps_data, NULL);
  gst_buffer_replace (&encoder->sps_data, NULL);
  gst_buffer_replace (&encoder->pps_data, NULL);

  /* reference list info de-init */
  ref_pool = &encoder->ref_pool;
  while (!g_queue_is_empty (&ref_pool->ref_list)) {
    ref = (GstVaapiEncoderH265Ref *) g_queue_pop_head (&ref_pool->ref_list);
    reference_pic_free (encoder, ref);
  }
  g_queue_clear (&ref_pool->ref_list);

  /* re-ordering  list initialize */
  reorder_pool = &encoder->reorder_pool;
  while (!g_queue_is_empty (&reorder_pool->reorder_frame_list)) {
    pic = (GstVaapiEncPicture *)
        g_queue_pop_head (&reorder_pool->reorder_frame_list);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&reorder_pool->reorder_frame_list);
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h265_set_property (GstVaapiEncoder * base_encoder,
    gint prop_id, const GValue * value)
{
  GstVaapiEncoderH265 *const encoder =
      GST_VAAPI_ENCODER_H265_CAST (base_encoder);

  switch (prop_id) {
    case GST_VAAPI_ENCODER_H265_PROP_MAX_BFRAMES:
      encoder->num_bframes = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H265_PROP_INIT_QP:
      encoder->init_qp = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H265_PROP_MIN_QP:
      encoder->min_qp = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H265_PROP_NUM_SLICES:
      encoder->num_slices = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H265_PROP_CPB_LENGTH:
      encoder->cpb_length = g_value_get_uint (value);
      break;
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (H265);

static inline const GstVaapiEncoderClass *
gst_vaapi_encoder_h265_class (void)
{
  static const GstVaapiEncoderClass GstVaapiEncoderH265Class = {
    GST_VAAPI_ENCODER_CLASS_INIT (H265, h265),
    .set_property = gst_vaapi_encoder_h265_set_property,
    .get_codec_data = gst_vaapi_encoder_h265_get_codec_data
  };
  return &GstVaapiEncoderH265Class;
}

/**
 * gst_vaapi_encoder_h265_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for H.265 encoding. Note that the
 * only supported output stream format is "byte-stream" format.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_h265_new (GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_new (gst_vaapi_encoder_h265_class (), display);
}

/**
 * gst_vaapi_encoder_h265_get_default_properties:
 *
 * Determines the set of common and H.265 specific encoder properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of encoder properties for #GstVaapiEncoderH265,
 *   or %NULL if an error occurred.
 */
GPtrArray *
gst_vaapi_encoder_h265_get_default_properties (void)
{
  const GstVaapiEncoderClass *const klass = gst_vaapi_encoder_h265_class ();
  GPtrArray *props;

  props = gst_vaapi_encoder_properties_get_default (klass);
  if (!props)
    return NULL;

  /**
   * GstVaapiEncoderH265:max-bframes:
   *
   * The number of B-frames between I and P.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H265_PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes",
          "Max B-Frames", "Number of B-frames between I and P", 0, 10, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH265:init-qp:
   *
   * The initial quantizer value.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H265_PROP_INIT_QP,
      g_param_spec_uint ("init-qp",
          "Initial QP", "Initial quantizer value", 1, 51, 26,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH265:min-qp:
   *
   * The minimum quantizer value.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H265_PROP_MIN_QP,
      g_param_spec_uint ("min-qp",
          "Minimum QP", "Minimum quantizer value", 1, 51, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Fixme: there seems to be issues with multi-slice encoding */
  /**
   * GstVaapiEncoderH265:num-slices:
   *
   * The number of slices per frame.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H265_PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices",
          "Number of Slices",
          "Number of slices per frame",
          1, 200, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH265:cpb-length:
   *
   * The size of the CPB buffer in milliseconds.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H265_PROP_CPB_LENGTH,
      g_param_spec_uint ("cpb-length",
          "CPB Length", "Length of the CPB buffer in milliseconds",
          1, 10000, DEFAULT_CPB_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  return props;
}

/**
 * gst_vaapi_encoder_h265_set_max_profile:
 * @encoder: a #GstVaapiEncoderH265
 * @profile: an H.265 #GstVaapiProfile
 *
 * Notifies the @encoder to use coding tools from the supplied
 * @profile at most.
 *
 * This means that if the minimal profile derived to
 * support the specified coding tools is greater than this @profile,
 * then an error is returned when the @encoder is configured.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_encoder_h265_set_max_profile (GstVaapiEncoderH265 * encoder,
    GstVaapiProfile profile)
{
  guint8 profile_idc;

  g_return_val_if_fail (encoder != NULL, FALSE);
  g_return_val_if_fail (profile != GST_VAAPI_PROFILE_UNKNOWN, FALSE);

  if (gst_vaapi_profile_get_codec (profile) != GST_VAAPI_CODEC_H265)
    return FALSE;

  profile_idc = gst_vaapi_utils_h265_get_profile_idc (profile);
  if (!profile_idc)
    return FALSE;

  encoder->max_profile_idc = profile_idc;
  return TRUE;
}

/**
 * gst_vaapi_encoder_h265_get_profile_tier_level:
 * @encoder: a #GstVaapiEncoderH265
 * @out_profile_ptr: return location for the #GstVaapiProfile
 * @out_level_ptr: return location for the #GstVaapiLevelH265
 * @out_tier_ptr: return location for the #GstVaapiTierH265
 *
 * Queries the H.265 @encoder for the active profile and level. That
 * information is only constructed and valid after the encoder is
 * configured, i.e. after the gst_vaapi_encoder_set_codec_state()
 * function is called.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_encoder_h265_get_profile_tier_level (GstVaapiEncoderH265 * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiTierH265 * out_tier_ptr,
    GstVaapiLevelH265 * out_level_ptr)
{
  g_return_val_if_fail (encoder != NULL, FALSE);

  if (!encoder->profile || !encoder->tier || !encoder->level)
    return FALSE;

  if (out_profile_ptr)
    *out_profile_ptr = encoder->profile;
  if (out_level_ptr)
    *out_level_ptr = encoder->level;
  if (out_tier_ptr)
    *out_tier_ptr = encoder->tier;

  return TRUE;
}

/*
 *  gstvaapifeipak_h264.c - H.264 FEI PAK
 *
 *  Copyright (C) 2012-2016 Intel Corporation
 *    Author: Chen, Xiaomin <xiaomin.chen@intel.com>
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

/* GValueArray has deprecated without providing an alternative in glib >= 2.32
 * See https://bugzilla.gnome.org/show_bug.cgi?id=667228
 */

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "sysdeps.h"
#include <va/va.h>
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapifeipak_h264.h"
#include "gstvaapiutils_h264_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"
#define DEBUG 1
#include "gstvaapidebug.h"

/* Define the maximum number of views supported */
#define MAX_NUM_VIEWS 10

/* Define the maximum value for view-id */
#define MAX_VIEW_ID 1023

/* Default CPB length (in milliseconds) */
#define DEFAULT_CPB_LENGTH 1500

/* Scale factor for CPB size (HRD cpb_size_scale: min = 4) */
#define SX_CPB_SIZE 4

/* Scale factor for bitrate (HRD bit_rate_scale: min = 6) */
#define SX_BITRATE 6

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                          \
  (GST_VAAPI_RATECONTROL_MASK (CQP)  |                  \
   GST_VAAPI_RATECONTROL_MASK (CBR)  |                  \
   GST_VAAPI_RATECONTROL_MASK (VBR)  |                  \
   GST_VAAPI_RATECONTROL_MASK (VBR_CONSTRAINED))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS                          \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE) |                 \
   GST_VAAPI_ENCODER_TUNE_MASK (HIGH_COMPRESSION) |     \
   GST_VAAPI_ENCODER_TUNE_MASK (LOW_POWER))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_SEQUENCE |              \
   VA_ENC_PACKED_HEADER_PICTURE  |              \
   VA_ENC_PACKED_HEADER_SLICE    |              \
   VA_ENC_PACKED_HEADER_RAW_DATA)

#define GST_H264_NAL_REF_IDC_NONE        0
#define GST_H264_NAL_REF_IDC_LOW         1
#define GST_H264_NAL_REF_IDC_MEDIUM      2
#define GST_H264_NAL_REF_IDC_HIGH        3

typedef struct
{
  GstVaapiSurfaceProxy *pic;
  guint poc;
  guint frame_num;
} GstVaapiFEIPakH264Ref;

typedef enum
{
  GST_VAAPI_FEIPAK_H264_REORD_NONE = 0,
  GST_VAAPI_FEIPAK_H264_REORD_DUMP_FRAMES = 1,
  GST_VAAPI_FEIPAK_H264_REORD_WAIT_FRAMES = 2
} GstVaapiFEIPakH264ReorderState;

typedef struct _GstVaapiH264FEIPakViewRefPool
{
  GQueue ref_list;
  guint max_ref_frames;
  guint max_reflist0_count;
  guint max_reflist1_count;
} GstVaapiH264FEIPakViewRefPool;

typedef struct _GstVaapiH264FEIPakViewReorderPool
{
  GQueue reorder_frame_list;
  guint reorder_state;
  guint frame_index;
  guint frame_count;            /* monotonically increasing with in every idr period */
  guint cur_frame_num;
  guint cur_present_index;
} GstVaapiH264FEIPakViewReorderPool;

/* ------------------------------------------------------------------------- */
/* --- H.264 FEI PAK                                                     --- */
/* ------------------------------------------------------------------------- */

struct _GstVaapiFEIPakH264
{
  GstVaapiMiniObject parent_instance;

  GstVaapiEncoder *encoder;

  VAEncSequenceParameterBufferH264 h264_sps;
  VAEncPictureParameterBufferH264 h264_pps;
  GArray *h264_slice_params;

  GstVaapiProfile profile;
  GstVaapiEntrypoint entrypoint;
  GstVaapiDisplay *display;
  VAContextID va_context;
  guint8 profile_idc;
  guint8 hw_max_profile_idc;
  guint32 num_slices;
  guint slice_type;
  gboolean is_idr;
  guint32 num_bframes;
  guint32 mb_width;
  guint32 mb_height;
  gboolean props_reconfigured;
  gboolean config_changed;

  guint32 max_pic_order_cnt;
  guint32 log2_max_pic_order_cnt;

  GstBuffer *sps_data;
  GstBuffer *subset_sps_data;
  GstBuffer *pps_data;
  guint32 num_ref_frames;       // set reference frame num

  /* MVC */
  gboolean is_mvc;
  guint32 view_idx;             /* View Order Index (VOIdx) */
  guint32 num_views;
  guint16 view_ids[MAX_NUM_VIEWS];
  GstVaapiH264FEIPakViewRefPool ref_pools[MAX_NUM_VIEWS];
};

static inline gboolean
_poc_greater_than (guint poc1, guint poc2, guint max_poc)
{
  return (((poc1 - poc2) & (max_poc - 1)) < max_poc / 2);
}

/* ------------------------------------------------------------------------- */
/* --- H.264 Bitstream Writer                                            --- */
/* ------------------------------------------------------------------------- */

#define WRITE_UINT32(bs, val, nbits) do {                       \
    if (!gst_bit_writer_put_bits_uint32 (bs, val, nbits)) {     \
      GST_WARNING ("failed to write uint32, nbits: %d", nbits); \
      goto bs_error;                                            \
    }                                                           \
  } while (0)

#define WRITE_UE(bs, val) do {                  \
    if (!bs_write_ue (bs, val)) {               \
      GST_WARNING ("failed to write ue(v)");    \
      goto bs_error;                            \
    }                                           \
  } while (0)

#define WRITE_SE(bs, val) do {                  \
    if (!bs_write_se (bs, val)) {               \
      GST_WARNING ("failed to write se(v)");    \
      goto bs_error;                            \
    }                                           \
  } while (0)

/* Write an unsigned integer Exp-Golomb-coded syntax element. i.e. ue(v) */
static gboolean
bs_write_ue (GstBitWriter * bs, guint32 value)
{
  guint32 size_in_bits = 0;
  guint32 tmp_value = ++value;

  while (tmp_value) {
    ++size_in_bits;
    tmp_value >>= 1;
  }
  if (size_in_bits > 1
      && !gst_bit_writer_put_bits_uint32 (bs, 0, size_in_bits - 1))
    return FALSE;
  if (!gst_bit_writer_put_bits_uint32 (bs, value, size_in_bits))
    return FALSE;
  return TRUE;
}

/* Write a signed integer Exp-Golomb-coded syntax element. i.e. se(v) */
static gboolean
bs_write_se (GstBitWriter * bs, gint32 value)
{
  guint32 new_val;

  if (value <= 0)
    new_val = -(value << 1);
  else
    new_val = (value << 1) - 1;

  if (!bs_write_ue (bs, new_val))
    return FALSE;
  return TRUE;
}

/* Write the NAL unit header */
static gboolean
bs_write_nal_header (GstBitWriter * bs, guint32 nal_ref_idc,
    guint32 nal_unit_type)
{
  WRITE_UINT32 (bs, 0, 1);
  WRITE_UINT32 (bs, nal_ref_idc, 2);
  WRITE_UINT32 (bs, nal_unit_type, 5);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write NAL unit header");
    return FALSE;
  }
}

/* Write the MVC NAL unit header extension */
static gboolean
bs_write_nal_header_mvc_extension (GstBitWriter * bs,
    GstVaapiEncPicture * picture, guint32 view_id)
{
  guint32 svc_extension_flag = 0;
  guint32 non_idr_flag = 1;
  guint32 priority_id = 0;
  guint32 temporal_id = 0;
  guint32 anchor_pic_flag = 0;
  guint32 inter_view_flag = 0;

  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture))
    non_idr_flag = 0;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    anchor_pic_flag = 1;
  /* svc_extension_flag == 0 for mvc stream */
  WRITE_UINT32 (bs, svc_extension_flag, 1);

  WRITE_UINT32 (bs, non_idr_flag, 1);
  WRITE_UINT32 (bs, priority_id, 6);
  WRITE_UINT32 (bs, view_id, 10);
  WRITE_UINT32 (bs, temporal_id, 3);
  WRITE_UINT32 (bs, anchor_pic_flag, 1);
  WRITE_UINT32 (bs, inter_view_flag, 1);
  WRITE_UINT32 (bs, 1, 1);

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

/* Write an SPS NAL unit */
static gboolean
bs_write_sps_data (GstBitWriter * bs,
    const VAEncSequenceParameterBufferH264 * seq_param, GstVaapiProfile profile,
    const VAEncMiscParameterHRD * hrd_params)
{
  guint8 profile_idc;
  guint32 constraint_set0_flag, constraint_set1_flag;
  guint32 constraint_set2_flag, constraint_set3_flag;
  guint32 gaps_in_frame_num_value_allowed_flag = 0;     // ??
  gboolean nal_hrd_parameters_present_flag;

  guint32 b_qpprime_y_zero_transform_bypass = 0;
  guint32 residual_color_transform_flag = 0;
  guint32 pic_height_in_map_units =
      (seq_param->seq_fields.bits.frame_mbs_only_flag ?
      seq_param->picture_height_in_mbs : seq_param->picture_height_in_mbs / 2);
  guint32 mb_adaptive_frame_field =
      !seq_param->seq_fields.bits.frame_mbs_only_flag;
  guint32 i = 0;

  profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
  constraint_set0_flag =        /* A.2.1 (baseline profile constraints) */
      profile == GST_VAAPI_PROFILE_H264_BASELINE ||
      profile == GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;
  constraint_set1_flag =        /* A.2.2 (main profile constraints) */
      profile == GST_VAAPI_PROFILE_H264_MAIN ||
      profile == GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;
  constraint_set2_flag = 0;
  constraint_set3_flag = 0;

  /* profile_idc */
  WRITE_UINT32 (bs, profile_idc, 8);
  /* constraint_set0_flag */
  WRITE_UINT32 (bs, constraint_set0_flag, 1);
  /* constraint_set1_flag */
  WRITE_UINT32 (bs, constraint_set1_flag, 1);
  /* constraint_set2_flag */
  WRITE_UINT32 (bs, constraint_set2_flag, 1);
  /* constraint_set3_flag */
  WRITE_UINT32 (bs, constraint_set3_flag, 1);
  /* reserved_zero_4bits */
  WRITE_UINT32 (bs, 0, 4);
  /* level_idc */
  WRITE_UINT32 (bs, seq_param->level_idc, 8);
  /* seq_parameter_set_id */
  WRITE_UE (bs, seq_param->seq_parameter_set_id);

  if (profile == GST_VAAPI_PROFILE_H264_HIGH ||
      profile == GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH ||
      profile == GST_VAAPI_PROFILE_H264_STEREO_HIGH) {
    /* for high profile */
    /* chroma_format_idc  = 1, 4:2:0 */
    WRITE_UE (bs, seq_param->seq_fields.bits.chroma_format_idc);
    if (3 == seq_param->seq_fields.bits.chroma_format_idc) {
      WRITE_UINT32 (bs, residual_color_transform_flag, 1);
    }
    /* bit_depth_luma_minus8 */
    WRITE_UE (bs, seq_param->bit_depth_luma_minus8);
    /* bit_depth_chroma_minus8 */
    WRITE_UE (bs, seq_param->bit_depth_chroma_minus8);
    /* b_qpprime_y_zero_transform_bypass */
    WRITE_UINT32 (bs, b_qpprime_y_zero_transform_bypass, 1);

    /* seq_scaling_matrix_present_flag  */
    g_assert (seq_param->seq_fields.bits.seq_scaling_matrix_present_flag == 0);
    WRITE_UINT32 (bs,
        seq_param->seq_fields.bits.seq_scaling_matrix_present_flag, 1);

#if 0
    if (seq_param->seq_fields.bits.seq_scaling_matrix_present_flag) {
      for (i = 0;
          i < (seq_param->seq_fields.bits.chroma_format_idc != 3 ? 8 : 12);
          i++) {
        gst_bit_writer_put_bits_uint8 (bs,
            seq_param->seq_fields.bits.seq_scaling_list_present_flag, 1);
        if (seq_param->seq_fields.bits.seq_scaling_list_present_flag) {
          g_assert (0);
          /* FIXME, need write scaling list if seq_scaling_matrix_present_flag ==1 */
        }
      }
    }
#endif
  }

  /* log2_max_frame_num_minus4 */
  WRITE_UE (bs, seq_param->seq_fields.bits.log2_max_frame_num_minus4);
  /* pic_order_cnt_type */
  WRITE_UE (bs, seq_param->seq_fields.bits.pic_order_cnt_type);

  if (seq_param->seq_fields.bits.pic_order_cnt_type == 0) {
    /* log2_max_pic_order_cnt_lsb_minus4 */
    WRITE_UE (bs, seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
  } else if (seq_param->seq_fields.bits.pic_order_cnt_type == 1) {
    g_assert (0 && "only POC type 0 is supported");
    WRITE_UINT32 (bs,
        seq_param->seq_fields.bits.delta_pic_order_always_zero_flag, 1);
    WRITE_SE (bs, seq_param->offset_for_non_ref_pic);
    WRITE_SE (bs, seq_param->offset_for_top_to_bottom_field);
    WRITE_UE (bs, seq_param->num_ref_frames_in_pic_order_cnt_cycle);
    for (i = 0; i < seq_param->num_ref_frames_in_pic_order_cnt_cycle; i++) {
      WRITE_SE (bs, seq_param->offset_for_ref_frame[i]);
    }
  }

  /* num_ref_frames */
  WRITE_UE (bs, seq_param->max_num_ref_frames);
  /* gaps_in_frame_num_value_allowed_flag */
  WRITE_UINT32 (bs, gaps_in_frame_num_value_allowed_flag, 1);

  /* pic_width_in_mbs_minus1 */
  WRITE_UE (bs, seq_param->picture_width_in_mbs - 1);
  /* pic_height_in_map_units_minus1 */
  WRITE_UE (bs, pic_height_in_map_units - 1);
  /* frame_mbs_only_flag */
  WRITE_UINT32 (bs, seq_param->seq_fields.bits.frame_mbs_only_flag, 1);

  if (!seq_param->seq_fields.bits.frame_mbs_only_flag) {        //ONLY mbs
    g_assert (0 && "only progressive frames encoding is supported");
    WRITE_UINT32 (bs, mb_adaptive_frame_field, 1);
  }

  /* direct_8x8_inference_flag */
  WRITE_UINT32 (bs, 0, 1);
  /* frame_cropping_flag */
  WRITE_UINT32 (bs, seq_param->frame_cropping_flag, 1);

  if (seq_param->frame_cropping_flag) {
    /* frame_crop_left_offset */
    WRITE_UE (bs, seq_param->frame_crop_left_offset);
    /* frame_crop_right_offset */
    WRITE_UE (bs, seq_param->frame_crop_right_offset);
    /* frame_crop_top_offset */
    WRITE_UE (bs, seq_param->frame_crop_top_offset);
    /* frame_crop_bottom_offset */
    WRITE_UE (bs, seq_param->frame_crop_bottom_offset);
  }

  /* vui_parameters_present_flag */
  WRITE_UINT32 (bs, seq_param->vui_parameters_present_flag, 1);
  if (seq_param->vui_parameters_present_flag) {
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

    /* timing_info_present_flag */
    WRITE_UINT32 (bs, seq_param->vui_fields.bits.timing_info_present_flag, 1);
    if (seq_param->vui_fields.bits.timing_info_present_flag) {
      WRITE_UINT32 (bs, seq_param->num_units_in_tick, 32);
      WRITE_UINT32 (bs, seq_param->time_scale, 32);
      WRITE_UINT32 (bs, 1, 1);  /* fixed_frame_rate_flag */
    }

    /* nal_hrd_parameters_present_flag */
    nal_hrd_parameters_present_flag = seq_param->bits_per_second > 0;
    WRITE_UINT32 (bs, nal_hrd_parameters_present_flag, 1);
    if (nal_hrd_parameters_present_flag) {
      /* hrd_parameters */
      /* cpb_cnt_minus1 */
      WRITE_UE (bs, 0);
      WRITE_UINT32 (bs, SX_BITRATE - 6, 4);     /* bit_rate_scale */
      WRITE_UINT32 (bs, SX_CPB_SIZE - 4, 4);    /* cpb_size_scale */

      for (i = 0; i < 1; ++i) {
        /* bit_rate_value_minus1[0] */
        WRITE_UE (bs, (seq_param->bits_per_second >> SX_BITRATE) - 1);
        /* cpb_size_value_minus1[0] */
        WRITE_UE (bs, (hrd_params->buffer_size >> SX_CPB_SIZE) - 1);
        /* cbr_flag[0] */
        WRITE_UINT32 (bs, 1, 1);
      }
      /* initial_cpb_removal_delay_length_minus1 */
      WRITE_UINT32 (bs, 23, 5);
      /* cpb_removal_delay_length_minus1 */
      WRITE_UINT32 (bs, 23, 5);
      /* dpb_output_delay_length_minus1 */
      WRITE_UINT32 (bs, 23, 5);
      /* time_offset_length  */
      WRITE_UINT32 (bs, 23, 5);
    }

    /* vcl_hrd_parameters_present_flag */
    WRITE_UINT32 (bs, 0, 1);

    if (nal_hrd_parameters_present_flag
        || 0 /*vcl_hrd_parameters_present_flag */ ) {
      /* low_delay_hrd_flag */
      WRITE_UINT32 (bs, 0, 1);
    }
    /* pic_struct_present_flag */
    WRITE_UINT32 (bs, 1, 1);
    /* bs_restriction_flag */
    WRITE_UINT32 (bs, 0, 1);
  }
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write SPS NAL unit");
    return FALSE;
  }
}

static gboolean
bs_write_sps (GstVaapiFEIPakH264 * feipak, GstBitWriter * bs,
    const VAEncSequenceParameterBufferH264 * seq_param, GstVaapiProfile profile,
    const VAEncMiscParameterHRD * hrd_params)
{
  if (!bs_write_sps_data (bs, seq_param, profile, hrd_params))
    return FALSE;
  /* rbsp_trailing_bits */
  bs_write_trailing_bits (bs);

  return FALSE;
}

static gboolean
bs_write_subset_sps (GstVaapiFEIPakH264 * feipak, GstBitWriter * bs,
    const VAEncSequenceParameterBufferH264 * seq_param, GstVaapiProfile profile,
    guint num_views, guint16 * view_ids,
    const VAEncMiscParameterHRD * hrd_params)
{
  guint32 i, j, k;

  if (!bs_write_sps_data (bs, seq_param, profile, hrd_params))
    return FALSE;

  if (profile == GST_VAAPI_PROFILE_H264_STEREO_HIGH ||
      profile == GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH) {
    guint32 num_views_minus1, num_level_values_signalled_minus1;

    num_views_minus1 = num_views - 1;
    g_assert (num_views_minus1 < 1024);

    /* bit equal to one */
    WRITE_UINT32 (bs, 1, 1);

    WRITE_UE (bs, num_views_minus1);

    for (i = 0; i <= num_views_minus1; i++)
      WRITE_UE (bs, view_ids[i]);

    for (i = 1; i <= num_views_minus1; i++) {
      guint32 num_anchor_refs_l0 = 0;
      guint32 num_anchor_refs_l1 = 0;

      WRITE_UE (bs, num_anchor_refs_l0);
      for (j = 0; j < num_anchor_refs_l0; j++)
        WRITE_UE (bs, 0);

      WRITE_UE (bs, num_anchor_refs_l1);
      for (j = 0; j < num_anchor_refs_l1; j++)
        WRITE_UE (bs, 0);
    }

    for (i = 1; i <= num_views_minus1; i++) {
      guint32 num_non_anchor_refs_l0 = 0;
      guint32 num_non_anchor_refs_l1 = 0;

      WRITE_UE (bs, num_non_anchor_refs_l0);
      for (j = 0; j < num_non_anchor_refs_l0; j++)
        WRITE_UE (bs, 0);

      WRITE_UE (bs, num_non_anchor_refs_l1);
      for (j = 0; j < num_non_anchor_refs_l1; j++)
        WRITE_UE (bs, 0);
    }

    /* num level values signalled minus1 */
    num_level_values_signalled_minus1 = 0;
    g_assert (num_level_values_signalled_minus1 < 64);
    WRITE_UE (bs, num_level_values_signalled_minus1);

    for (i = 0; i <= num_level_values_signalled_minus1; i++) {
      guint16 num_applicable_ops_minus1 = 0;
      g_assert (num_applicable_ops_minus1 < 1024);

      WRITE_UINT32 (bs, seq_param->level_idc, 8);
      WRITE_UE (bs, num_applicable_ops_minus1);

      for (j = 0; j <= num_applicable_ops_minus1; j++) {
        guint8 temporal_id = 0;
        guint16 num_target_views_minus1 = 1;

        WRITE_UINT32 (bs, temporal_id, 3);
        WRITE_UE (bs, num_target_views_minus1);

        for (k = 0; k <= num_target_views_minus1; k++)
          WRITE_UE (bs, k);

        WRITE_UE (bs, num_views_minus1);
      }
    }

    /* mvc_vui_parameters_present_flag */
    WRITE_UINT32 (bs, 0, 1);
  }

  /* additional_extension2_flag */
  WRITE_UINT32 (bs, 0, 1);

  /* rbsp_trailing_bits */
  bs_write_trailing_bits (bs);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write subset SPS NAL unit");
    return FALSE;
  }
  return FALSE;
}

/* Write a PPS NAL unit */
static gboolean
bs_write_pps (GstBitWriter * bs,
    const VAEncPictureParameterBufferH264 * pic_param, GstVaapiProfile profile)
{
  guint32 num_slice_groups_minus1 = 0;
  guint32 pic_init_qs_minus26 = 0;
  guint32 redundant_pic_cnt_present_flag = 0;

  /* pic_parameter_set_id */
  WRITE_UE (bs, pic_param->pic_parameter_set_id);
  /* seq_parameter_set_id */
  WRITE_UE (bs, pic_param->seq_parameter_set_id);
  /* entropy_coding_mode_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.entropy_coding_mode_flag, 1);
  /* pic_order_present_flag */
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.pic_order_present_flag, 1);
  /* slice_groups-1 */
  WRITE_UE (bs, num_slice_groups_minus1);

  if (num_slice_groups_minus1 > 0) {
     /*FIXME*/ g_assert (0 && "unsupported arbitrary slice ordering (ASO)");
  }
  WRITE_UE (bs, pic_param->num_ref_idx_l0_active_minus1);
  WRITE_UE (bs, pic_param->num_ref_idx_l1_active_minus1);
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.weighted_pred_flag, 1);
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.weighted_bipred_idc, 2);
  /* pic_init_qp_minus26 */
  WRITE_SE (bs, pic_param->pic_init_qp - 26);
  /* pic_init_qs_minus26 */
  WRITE_SE (bs, pic_init_qs_minus26);
  /* chroma_qp_index_offset */
  WRITE_SE (bs, pic_param->chroma_qp_index_offset);

  WRITE_UINT32 (bs,
      pic_param->pic_fields.bits.deblocking_filter_control_present_flag, 1);
  WRITE_UINT32 (bs, pic_param->pic_fields.bits.constrained_intra_pred_flag, 1);
  WRITE_UINT32 (bs, redundant_pic_cnt_present_flag, 1);

  /* more_rbsp_data */
  if (profile == GST_VAAPI_PROFILE_H264_HIGH) {
    WRITE_UINT32 (bs, pic_param->pic_fields.bits.transform_8x8_mode_flag, 1);
    WRITE_UINT32 (bs,
        pic_param->pic_fields.bits.pic_scaling_matrix_present_flag, 1);
    if (pic_param->pic_fields.bits.pic_scaling_matrix_present_flag) {
      g_assert (0 && "unsupported scaling lists");
      /* FIXME */
      /*
         for (i = 0; i <
         (6+(-( (chroma_format_idc ! = 3) ? 2 : 6) * -pic_param->pic_fields.bits.transform_8x8_mode_flag));
         i++) {
         gst_bit_writer_put_bits_uint8(bs, pic_param->pic_fields.bits.pic_scaling_list_present_flag, 1);
         }
       */
    }
    WRITE_SE (bs, pic_param->second_chroma_qp_index_offset);
  }

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
    const VAEncSliceParameterBufferH264 * slice_param,
    GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture)
{
  const VAEncPictureParameterBufferH264 *const pic_param = picture->param;
  guint32 field_pic_flag = 0;
  guint32 ref_pic_list_modification_flag_l0 = 0;
  guint32 ref_pic_list_modification_flag_l1 = 0;
  guint32 no_output_of_prior_pics_flag = 0;
  guint32 long_term_reference_flag = 0;
  guint32 adaptive_ref_pic_marking_mode_flag = 0;

  /* first_mb_in_slice */
  WRITE_UE (bs, slice_param->macroblock_address);
  /* slice_type */
  WRITE_UE (bs, slice_param->slice_type);
  /* pic_parameter_set_id */
  WRITE_UE (bs, slice_param->pic_parameter_set_id);
  /* frame_num */
  WRITE_UINT32 (bs, picture->frame_num,
      feipak->h264_sps.seq_fields.bits.log2_max_frame_num_minus4 + 4);

  /* XXX: only frames (i.e. non-interlaced) are supported for now */
  /* frame_mbs_only_flag == 0 */

  /* idr_pic_id */
  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture))
    WRITE_UE (bs, slice_param->idr_pic_id);

  /* XXX: only POC type 0 is supported */
  if (!feipak->h264_sps.seq_fields.bits.pic_order_cnt_type) {
    WRITE_UINT32 (bs, slice_param->pic_order_cnt_lsb,
        feipak->h264_sps.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 + 4);
    /* bottom_field_pic_order_in_frame_present_flag is FALSE */
    if (pic_param->pic_fields.bits.pic_order_present_flag && !field_pic_flag)
      WRITE_SE (bs, slice_param->delta_pic_order_cnt_bottom);
  } else if (feipak->h264_sps.seq_fields.bits.pic_order_cnt_type == 1 &&
      !feipak->h264_sps.seq_fields.bits.delta_pic_order_always_zero_flag) {
    WRITE_SE (bs, slice_param->delta_pic_order_cnt[0]);
    if (pic_param->pic_fields.bits.pic_order_present_flag && !field_pic_flag)
      WRITE_SE (bs, slice_param->delta_pic_order_cnt[1]);
  }
  /* redundant_pic_cnt_present_flag is FALSE, no redundant coded pictures */

  /* only works for B-frames */
  if (slice_param->slice_type == GST_H264_B_SLICE)
    WRITE_UINT32 (bs, slice_param->direct_spatial_mv_pred_flag, 1);

  /* not supporting SP slices */
  if (slice_param->slice_type == 0 || slice_param->slice_type == 1) {
    WRITE_UINT32 (bs, slice_param->num_ref_idx_active_override_flag, 1);
    if (slice_param->num_ref_idx_active_override_flag) {
      WRITE_UE (bs, slice_param->num_ref_idx_l0_active_minus1);
      if (slice_param->slice_type == 1)
        WRITE_UE (bs, slice_param->num_ref_idx_l1_active_minus1);
    }
  }
  /* XXX: not supporting custom reference picture list modifications */
  if ((slice_param->slice_type != 2) && (slice_param->slice_type != 4))
    WRITE_UINT32 (bs, ref_pic_list_modification_flag_l0, 1);
  if (slice_param->slice_type == 1)
    WRITE_UINT32 (bs, ref_pic_list_modification_flag_l1, 1);

  /* we have: weighted_pred_flag == FALSE and */
  /*        : weighted_bipred_idc == FALSE */
  if ((pic_param->pic_fields.bits.weighted_pred_flag &&
          (slice_param->slice_type == 0)) ||
      ((pic_param->pic_fields.bits.weighted_bipred_idc == 1) &&
          (slice_param->slice_type == 1))) {
    /* XXXX: add pred_weight_table() */
  }

  /* dec_ref_pic_marking() */
  if (slice_param->slice_type == 0 || slice_param->slice_type == 2) {
    if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture)) {
      /* no_output_of_prior_pics_flag = 0 */
      WRITE_UINT32 (bs, no_output_of_prior_pics_flag, 1);
      /* long_term_reference_flag = 0 */
      WRITE_UINT32 (bs, long_term_reference_flag, 1);
    } else {
      /* only sliding_window reference picture marking mode is supported */
      /* adpative_ref_pic_marking_mode_flag = 0 */
      WRITE_UINT32 (bs, adaptive_ref_pic_marking_mode_flag, 1);
    }
  }

  /* cabac_init_idc */
  if (pic_param->pic_fields.bits.entropy_coding_mode_flag &&
      slice_param->slice_type != 2)
    WRITE_UE (bs, slice_param->cabac_init_idc);
  /*slice_qp_delta */
  WRITE_SE (bs, slice_param->slice_qp_delta);

  /* XXX: only supporting I, P and B type slices */
  /* no sp_for_switch_flag and no slice_qs_delta */

  if (pic_param->pic_fields.bits.deblocking_filter_control_present_flag) {
    /* disable_deblocking_filter_idc */
    WRITE_UE (bs, slice_param->disable_deblocking_filter_idc);
    if (slice_param->disable_deblocking_filter_idc != 1) {
      WRITE_SE (bs, slice_param->slice_alpha_c0_offset_div2);
      WRITE_SE (bs, slice_param->slice_beta_offset_div2);
    }
  }

  /* XXX: unsupported arbitrary slice ordering (ASO) */
  /* num_slic_groups_minus1 should be zero */
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write Slice NAL unit");
    return FALSE;
  }
}

static inline void
_check_sps_pps_status (GstVaapiFEIPakH264 * feipak,
    const guint8 * nal, guint32 size)
{
  guint8 nal_type;
  G_GNUC_UNUSED gsize ret;      /* FIXME */
  gboolean has_subset_sps;

  g_assert (size);

  has_subset_sps = !feipak->is_mvc || (feipak->subset_sps_data != NULL);
  if (feipak->sps_data && feipak->pps_data && has_subset_sps)
    return;

  nal_type = nal[0] & 0x1F;
  switch (nal_type) {
    case GST_H264_NAL_SPS:
      feipak->sps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (feipak->sps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    case GST_H264_NAL_SUBSET_SPS:
      feipak->subset_sps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (feipak->subset_sps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    case GST_H264_NAL_PPS:
      feipak->pps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (feipak->pps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    default:
      break;
  }
}

/* Determines the largest supported profile by the underlying hardware */
static gboolean
ensure_hw_profile_limits (GstVaapiFEIPakH264 * feipak)
{
  GstVaapiDisplay *const display = feipak->display;
  GArray *profiles;
  guint i, profile_idc, max_profile_idc;

  if (feipak->hw_max_profile_idc)
    return TRUE;

  profiles = gst_vaapi_display_get_encode_profiles (display);
  if (!profiles)
    return FALSE;

  max_profile_idc = 0;
  for (i = 0; i < profiles->len; i++) {
    const GstVaapiProfile profile =
        g_array_index (profiles, GstVaapiProfile, i);
    profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
    if (!profile_idc)
      continue;
    if (max_profile_idc < profile_idc)
      max_profile_idc = profile_idc;
  }
  g_array_unref (profiles);

  feipak->hw_max_profile_idc = max_profile_idc;
  return TRUE;
}

/* Fills in VA HRD parameters */
static void
fill_hrd_params (GstVaapiFEIPakH264 * feipak, VAEncMiscParameterHRD * hrd)
{
  hrd->buffer_size = 0;
  hrd->initial_buffer_fullness = 0;
}

/* Adds the supplied sequence header (SPS) to the list of packed
   headers to pass down as-is to the feipak */
static gboolean
add_packed_sequence_header (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_seq;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_seq_param = { 0 };
  const VAEncSequenceParameterBufferH264 *const seq_param = sequence->param;
  GstVaapiProfile profile = feipak->profile;

  VAEncMiscParameterHRD hrd_params;
  guint32 data_bit_size;
  guint8 *data;

  fill_hrd_params (feipak, &hrd_params);
  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */
  bs_write_nal_header (&bs, GST_H264_NAL_REF_IDC_HIGH, GST_H264_NAL_SPS);
  /* Set High profile for encoding the MVC base view. Otherwise, some
     traditional decoder cannot recognize MVC profile streams with
     only the base view in there */
  if (profile == GST_VAAPI_PROFILE_H264_MULTIVIEW_HIGH ||
      profile == GST_VAAPI_PROFILE_H264_STEREO_HIGH)
    profile = GST_VAAPI_PROFILE_H264_HIGH;

  bs_write_sps (feipak, &bs, seq_param, profile, &hrd_params);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_seq_param.type = VAEncPackedHeaderSequence;
  packed_seq_param.bit_length = data_bit_size;
  packed_seq_param.has_emulation_bytes = 0;

  packed_seq =
      gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (feipak->encoder),
      &packed_seq_param, sizeof (packed_seq_param), data,
      (data_bit_size + 7) / 8);
  g_assert (packed_seq);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_seq);
  gst_vaapi_codec_object_replace (&packed_seq, NULL);

  /* store sps data */
  _check_sps_pps_status (feipak, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_reset (&bs);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write SPS NAL unit");
    gst_bit_writer_reset (&bs);
    return FALSE;
  }
}

static gboolean
add_packed_sequence_header_mvc (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_seq;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  const VAEncSequenceParameterBufferH264 *const seq_param = sequence->param;
  VAEncMiscParameterHRD hrd_params;
  guint32 data_bit_size;
  guint8 *data;

  fill_hrd_params (feipak, &hrd_params);

  /* non-base layer, pack one subset sps */
  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */
  bs_write_nal_header (&bs, GST_H264_NAL_REF_IDC_HIGH, GST_H264_NAL_SUBSET_SPS);

  bs_write_subset_sps (feipak, &bs, seq_param, feipak->profile,
      feipak->num_views, feipak->view_ids, &hrd_params);

  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_header_param_buffer.type = VAEncPackedHeaderSequence;
  packed_header_param_buffer.bit_length = data_bit_size;
  packed_header_param_buffer.has_emulation_bytes = 0;

  packed_seq =
      gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (feipak->encoder),
      &packed_header_param_buffer, sizeof (packed_header_param_buffer), data,
      (data_bit_size + 7) / 8);
  g_assert (packed_seq);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_seq);
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & packed_seq, NULL);

  /* store subset sps data */
  _check_sps_pps_status (feipak, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_reset (&bs);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write SPS NAL unit");
    gst_bit_writer_reset (&bs);
    return FALSE;
  }
}

/* Adds the supplied picture header (PPS) to the list of packed
   headers to pass down as-is to the feipak */
static gboolean
add_packed_picture_header (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncPackedHeader *packed_pic;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_pic_param = { 0 };
  const VAEncPictureParameterBufferH264 *const pic_param = picture->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */
  bs_write_nal_header (&bs, GST_H264_NAL_REF_IDC_HIGH, GST_H264_NAL_PPS);
  bs_write_pps (&bs, pic_param, feipak->profile);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_pic_param.type = VAEncPackedHeaderPicture;
  packed_pic_param.bit_length = data_bit_size;
  packed_pic_param.has_emulation_bytes = 0;

  packed_pic =
      gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (feipak->encoder),
      &packed_pic_param, sizeof (packed_pic_param), data,
      (data_bit_size + 7) / 8);
  g_assert (packed_pic);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_pic);
  gst_vaapi_codec_object_replace (&packed_pic, NULL);

  /* store pps data */
  _check_sps_pps_status (feipak, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_reset (&bs);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write PPS NAL unit");
    gst_bit_writer_reset (&bs);
    return FALSE;
  }
}

static gboolean
get_nal_hdr_attributes (GstVaapiEncPicture * picture,
    guint8 * nal_ref_idc, guint8 * nal_unit_type)
{
  switch (picture->type) {
    case GST_VAAPI_PICTURE_TYPE_I:
      *nal_ref_idc = GST_H264_NAL_REF_IDC_HIGH;
      if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture))
        *nal_unit_type = GST_H264_NAL_SLICE_IDR;
      else
        *nal_unit_type = GST_H264_NAL_SLICE;
      break;
    case GST_VAAPI_PICTURE_TYPE_P:
      *nal_ref_idc = GST_H264_NAL_REF_IDC_MEDIUM;
      *nal_unit_type = GST_H264_NAL_SLICE;
      break;
    case GST_VAAPI_PICTURE_TYPE_B:
      *nal_ref_idc = GST_H264_NAL_REF_IDC_NONE;
      *nal_unit_type = GST_H264_NAL_SLICE;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

/* Adds the supplied prefix nal header to the list of packed
   headers to pass down as-is to the feipak */
static gboolean
add_packed_prefix_nal_header (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiEncSlice * slice)
{
  GstVaapiEncPackedHeader *packed_prefix_nal;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_prefix_nal_param = { 0 };
  guint32 data_bit_size;
  guint8 *data;
  guint8 nal_ref_idc, nal_unit_type;

  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */

  if (!get_nal_hdr_attributes (picture, &nal_ref_idc, &nal_unit_type))
    goto bs_error;
  nal_unit_type = GST_H264_NAL_PREFIX_UNIT;

  bs_write_nal_header (&bs, nal_ref_idc, nal_unit_type);
  bs_write_nal_header_mvc_extension (&bs, picture, feipak->view_idx);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&bs) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_prefix_nal_param.type = VAEncPackedHeaderRawData;
  packed_prefix_nal_param.bit_length = data_bit_size;
  packed_prefix_nal_param.has_emulation_bytes = 0;

  packed_prefix_nal =
      gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (feipak->encoder),
      &packed_prefix_nal_param, sizeof (packed_prefix_nal_param), data,
      (data_bit_size + 7) / 8);
  g_assert (packed_prefix_nal);

  gst_vaapi_enc_slice_add_packed_header (slice, packed_prefix_nal);
  gst_vaapi_codec_object_replace (&packed_prefix_nal, NULL);

  gst_bit_writer_reset (&bs);

  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write Prefix NAL unit header");
    gst_bit_writer_reset (&bs);
    return FALSE;
  }
}

/* Adds the supplied slice header to the list of packed
   headers to pass down as-is to the feipak */
static gboolean
add_packed_slice_header (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiEncSlice * slice)
{
  GstVaapiEncPackedHeader *packed_slice;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_slice_param = { 0 };
  const VAEncSliceParameterBufferH264 *const slice_param = slice->param;
  guint32 data_bit_size;
  guint8 *data;
  guint8 nal_ref_idc, nal_unit_type;

  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  WRITE_UINT32 (&bs, 0x00000001, 32);   /* start code */

  if (!get_nal_hdr_attributes (picture, &nal_ref_idc, &nal_unit_type))
    goto bs_error;
  /* pack nal_unit_header_mvc_extension() for the non base view */
  if (feipak->is_mvc && feipak->view_idx) {
    bs_write_nal_header (&bs, nal_ref_idc, GST_H264_NAL_SLICE_EXT);
    bs_write_nal_header_mvc_extension (&bs, picture,
        feipak->view_ids[feipak->view_idx]);
  } else
    bs_write_nal_header (&bs, nal_ref_idc, nal_unit_type);

  bs_write_slice (&bs, slice_param, feipak, picture);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_slice_param.type = VAEncPackedHeaderSlice;
  packed_slice_param.bit_length = data_bit_size;
  packed_slice_param.has_emulation_bytes = 0;

  packed_slice =
      gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (feipak->encoder),
      &packed_slice_param, sizeof (packed_slice_param), data,
      (data_bit_size + 7) / 8);
  g_assert (packed_slice);

  gst_vaapi_enc_slice_add_packed_header (slice, packed_slice);
  gst_vaapi_codec_object_replace (&packed_slice, NULL);

  gst_bit_writer_reset (&bs);
  return TRUE;

  /* ERRORS */
bs_error:
  {
    GST_WARNING ("failed to write Slice NAL unit header");
    gst_bit_writer_reset (&bs);
    return FALSE;
  }
}

/* Reference picture management */
static void
reference_pic_free (GstVaapiFEIPakH264 * feipak, GstVaapiFEIPakH264Ref * ref)
{
  if (!ref)
    return;
  if (ref->pic)
    gst_vaapi_surface_proxy_unref (ref->pic);
  g_slice_free (GstVaapiFEIPakH264Ref, ref);
}

static inline GstVaapiFEIPakH264Ref *
reference_pic_create (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * surface)
{
  GstVaapiFEIPakH264Ref *const ref = g_slice_new0 (GstVaapiFEIPakH264Ref);

  ref->pic = surface;
  ref->frame_num = picture->frame_num;
  ref->poc = picture->poc;
  return ref;
}

static gboolean
reference_list_update (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * surface)
{
  GstVaapiFEIPakH264Ref *ref;
  GstVaapiH264FEIPakViewRefPool *const ref_pool =
      &feipak->ref_pools[feipak->view_idx];

  if (GST_VAAPI_PICTURE_TYPE_B == picture->type) {
    gst_vaapi_surface_proxy_unref (surface);
    return TRUE;
  }
  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture)) {
    while (!g_queue_is_empty (&ref_pool->ref_list))
      reference_pic_free (feipak, g_queue_pop_head (&ref_pool->ref_list));
  } else if (g_queue_get_length (&ref_pool->ref_list) >=
      ref_pool->max_ref_frames) {
    reference_pic_free (feipak, g_queue_pop_head (&ref_pool->ref_list));
  }
  ref = reference_pic_create (feipak, picture, surface);
  g_queue_push_tail (&ref_pool->ref_list, ref);
  g_assert (g_queue_get_length (&ref_pool->ref_list) <=
      ref_pool->max_ref_frames);
  return TRUE;
}

static gboolean
reference_list_init (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture,
    GstVaapiFEIPakH264Ref ** reflist_0,
    guint * reflist_0_count,
    GstVaapiFEIPakH264Ref ** reflist_1, guint * reflist_1_count)
{
  GstVaapiFEIPakH264Ref *tmp;
  GstVaapiH264FEIPakViewRefPool *const ref_pool =
      &feipak->ref_pools[feipak->view_idx];
  GList *iter, *list_0_start = NULL, *list_1_start = NULL;
  guint count;

  *reflist_0_count = 0;
  *reflist_1_count = 0;
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  iter = g_queue_peek_tail_link (&ref_pool->ref_list);
  for (; iter; iter = g_list_previous (iter)) {
    tmp = (GstVaapiFEIPakH264Ref *) iter->data;
    g_assert (tmp && tmp->poc != picture->poc);
    if (_poc_greater_than (picture->poc, tmp->poc,
            1 << (feipak->h264_sps.seq_fields.bits.
                log2_max_pic_order_cnt_lsb_minus4 + 4))) {
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
    reflist_0[count] = (GstVaapiFEIPakH264Ref *) iter->data;
    ++count;
  }
  *reflist_0_count = count;

  if (picture->type != GST_VAAPI_PICTURE_TYPE_B)
    return TRUE;

  /* order reflist_1 */
  count = 0;
  iter = list_1_start;
  for (; iter; iter = g_list_next (iter)) {
    reflist_1[count] = (GstVaapiFEIPakH264Ref *) iter->data;
    ++count;
  }
  *reflist_1_count = count;
  return TRUE;
}

/* Fills in VA sequence parameter buffer */
static gboolean
fill_sequence (GstVaapiFEIPakH264 * feipak, GstVaapiEncSequence * sequence)
{
  VAEncSequenceParameterBufferH264 *const seq_param = sequence->param;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferH264));
  *seq_param = feipak->h264_sps;
  return TRUE;
}

/* Fills in VA picture parameter buffer */
static gboolean
fill_picture (GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferH264 *const pic_param = picture->param;
  GstVaapiH264FEIPakViewRefPool *const ref_pool =
      &feipak->ref_pools[feipak->view_idx];
  GstVaapiFEIPakH264Ref *ref_pic;
  GList *reflist;
  guint i;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferH264));
  *pic_param = feipak->h264_pps;
  feipak->is_idr = feipak->h264_pps.pic_fields.bits.idr_pic_flag;
  /* reference list,  */
  pic_param->CurrPic.picture_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->CurrPic.TopFieldOrderCnt = picture->poc;
  pic_param->CurrPic.frame_idx = picture->frame_num;
  i = 0;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
    for (reflist = g_queue_peek_head_link (&ref_pool->ref_list);
        reflist; reflist = g_list_next (reflist)) {
      ref_pic = reflist->data;
      g_assert (ref_pic && ref_pic->pic &&
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic) != VA_INVALID_ID);

      pic_param->ReferenceFrames[i].picture_id =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic);
      pic_param->ReferenceFrames[i].TopFieldOrderCnt = ref_pic->poc;
      pic_param->ReferenceFrames[i].flags |=
          VA_PICTURE_H264_SHORT_TERM_REFERENCE;
      pic_param->ReferenceFrames[i].frame_idx = ref_pic->frame_num;
      ++i;
    }
    g_assert (i <= 16 && i <= ref_pool->max_ref_frames);
  }
  for (; i < 16; ++i) {
    pic_param->ReferenceFrames[i].picture_id = VA_INVALID_ID;
    pic_param->ReferenceFrames[i].frame_idx = VA_PICTURE_H264_INVALID;
  }
  pic_param->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

  return TRUE;
}

/* Adds slice headers to picture */
static gboolean
add_slice_headers (GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture,
    GstVaapiFEIPakH264Ref ** reflist_0, guint reflist_0_count,
    GstVaapiFEIPakH264Ref ** reflist_1, guint reflist_1_count)
{
  VAEncSliceParameterBufferH264 *slice_param;
  GstVaapiEncSlice *slice;
  guint slice_of_mbs, slice_mod_mbs, cur_slice_mbs;
  guint mb_size;
  guint last_mb_index;
  guint i_slice, i_ref;
  GArray *h264_slice_params = feipak->h264_slice_params;

  g_assert (picture);

  mb_size = feipak->mb_width * feipak->mb_height;

  g_assert (feipak->num_slices && feipak->num_slices < mb_size);
  slice_of_mbs = mb_size / feipak->num_slices;
  slice_mod_mbs = mb_size % feipak->num_slices;
  last_mb_index = 0;
  for (i_slice = 0; i_slice < feipak->num_slices; ++i_slice) {
    cur_slice_mbs = slice_of_mbs;
    if (slice_mod_mbs) {
      ++cur_slice_mbs;
      --slice_mod_mbs;
    }
    slice = GST_VAAPI_ENC_SLICE_NEW (H264, feipak->encoder);
    g_assert (slice && slice->param_id != VA_INVALID_ID);
    slice_param = slice->param;

    memset (slice_param, 0, sizeof (VAEncSliceParameterBufferH264));
    *slice_param =
        g_array_index (h264_slice_params, VAEncSliceParameterBufferH264,
        i_slice);
    g_assert ((gint8) slice_param->slice_type != -1);
    g_assert (slice_param->num_ref_idx_l0_active_minus1 >= 0);
    g_assert (slice_param->num_ref_idx_l1_active_minus1 == 0);

    i_ref = 0;
    if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
      for (; i_ref < reflist_0_count; ++i_ref) {
        slice_param->RefPicList0[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_0[i_ref]->pic);
        slice_param->RefPicList0[i_ref].TopFieldOrderCnt =
            reflist_0[i_ref]->poc;
        slice_param->RefPicList0[i_ref].flags |=
            VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        slice_param->RefPicList0[i_ref].frame_idx = reflist_0[i_ref]->frame_num;
      }
      g_assert (i_ref >= 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->RefPicList0); ++i_ref) {
      slice_param->RefPicList0[i_ref].picture_id = VA_INVALID_SURFACE;
      slice_param->RefPicList0[i_ref].frame_idx = VA_PICTURE_H264_INVALID;
    }

    i_ref = 0;
    if (picture->type == GST_VAAPI_PICTURE_TYPE_B) {
      for (; i_ref < reflist_1_count; ++i_ref) {
        slice_param->RefPicList1[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_1[i_ref]->pic);
        slice_param->RefPicList1[i_ref].TopFieldOrderCnt =
            reflist_1[i_ref]->poc;
        slice_param->RefPicList1[i_ref].flags |=
            VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        slice_param->RefPicList1[i_ref].frame_idx = reflist_1[i_ref]->frame_num;
      }
      g_assert (i_ref == 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->RefPicList1); ++i_ref) {
      slice_param->RefPicList1[i_ref].picture_id = VA_INVALID_SURFACE;
      slice_param->RefPicList1[i_ref].frame_idx = VA_PICTURE_H264_INVALID;
    }

    /* set calculation for next slice */
    last_mb_index += cur_slice_mbs;

    /* add packed Prefix NAL unit before each Coded slice NAL in base view */
    if (feipak->is_mvc && !feipak->view_idx
        && !add_packed_prefix_nal_header (feipak, picture, slice))
      goto error_create_packed_prefix_nal_hdr;
    if (!add_packed_slice_header (feipak, picture, slice))
      goto error_create_packed_slice_hdr;

    gst_vaapi_enc_picture_add_slice (picture, slice);
    gst_vaapi_codec_object_replace (&slice, NULL);
  }
  g_assert (last_mb_index == mb_size);
  return TRUE;

error_create_packed_slice_hdr:
  {
    GST_ERROR ("failed to create packed slice header buffer");
    gst_vaapi_codec_object_replace (&slice, NULL);
    return FALSE;
  }
error_create_packed_prefix_nal_hdr:
  {
    GST_ERROR ("failed to create packed prefix nal header buffer");
    gst_vaapi_codec_object_replace (&slice, NULL);
    return FALSE;
  }
}

/* Generates and submits SPS header accordingly into the bitstream */
static gboolean
ensure_sequence (GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence = NULL;

  if (!feipak->config_changed || picture->type != GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (H264, feipak->encoder);
  if (!sequence || !fill_sequence (feipak, sequence))
    goto error_create_seq_param;

  /* add subset sps for non-base view and sps for base view */
  if (feipak->is_mvc && feipak->view_idx) {
    if (!add_packed_sequence_header_mvc (feipak, picture, sequence))
      goto error_create_packed_seq_hdr;
  } else {
    if (!add_packed_sequence_header (feipak, picture, sequence))
      goto error_create_packed_seq_hdr;
  }

  if (sequence) {
    gst_vaapi_enc_picture_set_sequence (picture, sequence);
    gst_vaapi_codec_object_replace (&sequence, NULL);
  }

  if (!feipak->is_mvc || feipak->view_idx > 0)
    feipak->config_changed = FALSE;
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

/* Generates additional fei control parameters */
static gboolean
ensure_fei_misc_params (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf_proxy)
{
  GstVaapiEncMiscParam *misc = NULL;
  VAEncMiscParameterFEIFrameControlH264 *misc_fei_pic_control_param;

  /*fei pic control params */
  misc = GST_VAAPI_ENC_FEI_MISC_PARAM_NEW (H264, feipak->encoder);
  g_assert (misc);
  if (!misc)
    return FALSE;

  misc_fei_pic_control_param = misc->data;
  misc_fei_pic_control_param->function = VA_FEI_FUNCTION_PAK;
  misc_fei_pic_control_param->mv_predictor = VA_INVALID_ID;
  misc_fei_pic_control_param->qp = VA_INVALID_ID;
  misc_fei_pic_control_param->mb_ctrl = VA_INVALID_ID;

  g_assert (codedbuf_proxy->mbcode != NULL);
  g_assert (codedbuf_proxy->mv != NULL);

  misc_fei_pic_control_param->mb_code_data =
      GST_VAAPI_FEI_CODEC_OBJECT (codedbuf_proxy->mbcode)->param_id;
  misc_fei_pic_control_param->mv_data =
      GST_VAAPI_FEI_CODEC_OBJECT (codedbuf_proxy->mv)->param_id;

  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);
  return TRUE;
}

/* Generates additional control parameters */
static gboolean
ensure_misc_params (GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture)
{
  GstVaapiEncMiscParam *misc = NULL;

  /* HRD params */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, feipak->encoder);
  g_assert (misc);
  if (!misc)
    return FALSE;
  fill_hrd_params (feipak, misc->data);
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  return TRUE;
}

/* Generates and submits PPS header accordingly into the bitstream */
static gboolean
ensure_picture (GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);
  gboolean res = FALSE;
  res = fill_picture (feipak, picture, codedbuf, surface);

  if (!res)
    return FALSE;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I
      && !add_packed_picture_header (feipak, picture)) {
    GST_ERROR ("set picture packed header failed");
    return FALSE;
  }
  return TRUE;
}

/* Generates slice headers */
static gboolean
ensure_slices (GstVaapiFEIPakH264 * feipak, GstVaapiEncPicture * picture)
{
  GstVaapiFEIPakH264Ref *reflist_0[16];
  GstVaapiFEIPakH264Ref *reflist_1[16];
  GstVaapiH264FEIPakViewRefPool *const ref_pool =
      &feipak->ref_pools[feipak->view_idx];
  guint reflist_0_count = 0, reflist_1_count = 0;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I &&
      !reference_list_init (feipak, picture,
          reflist_0, &reflist_0_count, reflist_1, &reflist_1_count)) {
    GST_ERROR ("reference list reorder failed");
    return FALSE;
  }

  g_assert (reflist_0_count + reflist_1_count <= ref_pool->max_ref_frames);
  if (reflist_0_count > ref_pool->max_reflist0_count)
    reflist_0_count = ref_pool->max_reflist0_count;
  if (reflist_1_count > ref_pool->max_reflist1_count)
    reflist_1_count = ref_pool->max_reflist1_count;

  if (!add_slice_headers (feipak, picture,
          reflist_0, reflist_0_count, reflist_1, reflist_1_count))
    return FALSE;

  return TRUE;
}

/* Constructs profile and level information based on user-defined limits */
static GstVaapiEncoderStatus
ensure_profile_and_level (GstVaapiFEIPakH264 * feipak)
{
  const GstVaapiProfile profile = feipak->profile;

  /* Check HW constraints */
  if (!ensure_hw_profile_limits (feipak))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  if (feipak->profile_idc > feipak->hw_max_profile_idc)
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  if (feipak->profile != profile) {
    feipak->config_changed = TRUE;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static void
reset_properties (GstVaapiFEIPakH264 * feipak)
{
  guint i;
  guint max_reflist0_count;
  if (feipak->num_bframes > 0) {
    if (feipak->num_ref_frames == 1) {
      GST_INFO ("num ref frames is modified as 2 as b frame is set");
      feipak->num_ref_frames = 2;
    }
    max_reflist0_count = feipak->num_ref_frames - 1;
  } else {
    max_reflist0_count = feipak->num_ref_frames;
  }
  max_reflist0_count = max_reflist0_count > 5 ? 5 : max_reflist0_count;

  for (i = 0; i < feipak->num_views; i++) {
    GstVaapiH264FEIPakViewRefPool *const ref_pool = &feipak->ref_pools[i];

    ref_pool->max_reflist0_count = max_reflist0_count;
    ref_pool->max_reflist1_count = feipak->num_bframes > 0;
    ref_pool->max_ref_frames = ref_pool->max_reflist0_count
        + ref_pool->max_reflist1_count;

  }
}

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_encode (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf,
    GstVaapiSurfaceProxy * surface, GstVaapiFeiInfoToPakH264 * info_to_pak)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = surface;
  GstVaapiSurfaceProxy *proxy = picture->proxy;
  VAEncSliceParameterBufferH264 slice_header;

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));
  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (proxy));

  g_assert (info_to_pak != NULL);

  feipak->h264_sps = info_to_pak->h264_enc_sps;
  feipak->h264_pps = info_to_pak->h264_enc_pps;
  feipak->h264_slice_params = info_to_pak->h264_slice_headers;

  feipak->mb_width = feipak->h264_sps.picture_width_in_mbs;
  feipak->mb_height = feipak->h264_sps.picture_height_in_mbs;

  slice_header =
      g_array_index (feipak->h264_slice_params, VAEncSliceParameterBufferH264,
      0);
  feipak->slice_type = slice_header.slice_type;

  if (!ensure_sequence (feipak, picture))
    goto error;
  if (!ensure_misc_params (feipak, picture))
    goto error;
  if (!ensure_fei_misc_params (feipak, picture, codedbuf))
    goto error;
  if (!ensure_picture (feipak, picture, codedbuf, reconstruct))
    goto error;
  if (!ensure_slices (feipak, picture))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;

  if (!reference_list_update (feipak, picture, reconstruct))
    goto error;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
error:
  return ret;
}

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_flush (GstVaapiFEIPakH264 * feipak)
{
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_reconfigure (GstVaapiFEIPakH264 * feipak,
    VAContextID va_context, GstVaapiProfile profile,
    guint8 profile_idc, guint mb_width, guint mb_height,
    guint32 num_views, guint slices_num, guint32 num_ref_frames)
{
  GstVaapiEncoderStatus status;

  if (mb_width != feipak->mb_width || mb_height != feipak->mb_height) {
    feipak->mb_width = mb_width;
    feipak->mb_height = mb_height;
    feipak->config_changed = TRUE;
  }

  feipak->va_context = va_context;

  /* Take number of MVC views from input caps if provided */
  feipak->num_views = num_views;

  feipak->is_mvc = feipak->num_views > 1;

  feipak->profile_idc = profile_idc;
  feipak->profile = profile;
  feipak->num_slices = slices_num;
  feipak->num_ref_frames = num_ref_frames;

  status = ensure_profile_and_level (feipak);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  reset_properties (feipak);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static gboolean
gst_vaapi_feipak_h264_init (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncoder * encoder, GstVaapiDisplay * display,
    VAContextID va_context)
{
  guint32 i;

  feipak->encoder = encoder;
  /* Default encoding entrypoint */
  feipak->entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_FEI;

  feipak->h264_slice_params = NULL;

  /* Multi-view coding information */
  feipak->is_mvc = FALSE;
  feipak->num_views = 1;
  feipak->view_idx = 0;
  feipak->display = display;
  feipak->va_context = va_context;

  feipak->num_bframes = 0;
  feipak->is_idr = FALSE;
  /* default num ref frames */
  feipak->num_ref_frames = 1;
  memset (feipak->view_ids, 0, sizeof (feipak->view_ids));

  feipak->props_reconfigured = FALSE;

  /* reference list info initialize */
  for (i = 0; i < MAX_NUM_VIEWS; i++) {
    GstVaapiH264FEIPakViewRefPool *const ref_pool = &feipak->ref_pools[i];
    g_queue_init (&ref_pool->ref_list);
    ref_pool->max_ref_frames = 0;
    ref_pool->max_reflist0_count = 1;
    ref_pool->max_reflist1_count = 1;
  }

  return TRUE;
}

static void
gst_vaapi_feipak_h264_finalize (GstVaapiFEIPakH264 * feipak)
{
  GstVaapiFEIPakH264Ref *ref;
  guint32 i;

  gst_buffer_replace (&feipak->sps_data, NULL);
  gst_buffer_replace (&feipak->subset_sps_data, NULL);
  gst_buffer_replace (&feipak->pps_data, NULL);

  /* reference list info de-init */
  for (i = 0; i < MAX_NUM_VIEWS; i++) {
    GstVaapiH264FEIPakViewRefPool *const ref_pool = &feipak->ref_pools[i];
    while (!g_queue_is_empty (&ref_pool->ref_list)) {
      ref = (GstVaapiFEIPakH264Ref *) g_queue_pop_head (&ref_pool->ref_list);
      reference_pic_free (feipak, ref);
    }
    g_queue_clear (&ref_pool->ref_list);
  }

}

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_set_property (GstVaapiFEIPakH264 * feipak,
    gint prop_id, const GValue * value)
{

  switch (prop_id) {
    case GST_VAAPI_FEIPAK_H264_PROP_MAX_BFRAMES:
      feipak->num_bframes = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEIPAK_H264_PROP_NUM_VIEWS:
      feipak->num_views = g_value_get_uint (value);
      break;
    case GST_VAAPI_FEIPAK_H264_PROP_VIEW_IDS:{
      guint i;
      GValueArray *view_ids = g_value_get_boxed (value);

      if (view_ids == NULL) {
        for (i = 0; i < feipak->num_views; i++)
          feipak->view_ids[i] = i;
      } else {
        g_assert (view_ids->n_values <= feipak->num_views);

        for (i = 0; i < feipak->num_views; i++) {
          GValue *val = g_value_array_get_nth (view_ids, i);
          feipak->view_ids[i] = g_value_get_uint (val);
        }
      }
      break;
    }
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_feipak_h264_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiFEIPakH264Class = {
    .size = sizeof (GstVaapiFEIPakH264),
    .finalize = (GDestroyNotify) gst_vaapi_feipak_h264_finalize
  };
  return &GstVaapiFEIPakH264Class;
}

/**
 * gst_vaapi_feipak_h264_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for H.264 encoding. Note that the
 * only supported output stream format is "byte-stream" format.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiFEIPakH264 *
gst_vaapi_feipak_h264_new (GstVaapiEncoder * encoder, GstVaapiDisplay * display,
    VAContextID va_context)
{
  GstVaapiFEIPakH264 *feipak;

  feipak = (GstVaapiFEIPakH264 *)
      gst_vaapi_mini_object_new0 (GST_VAAPI_MINI_OBJECT_CLASS
      (gst_vaapi_feipak_h264_class ()));
  if (!feipak)
    return NULL;

  if (!gst_vaapi_feipak_h264_init (feipak, encoder, display, va_context))
    goto error;
  return feipak;

error:
  gst_vaapi_object_unref (feipak);
  return NULL;
}

gboolean
gst_vaapi_feipak_h264_get_ref_pool (GstVaapiFEIPakH264 * feipak,
    gpointer * ref_pool_ptr)
{
  g_return_val_if_fail (feipak != NULL, FALSE);
  if (!(&feipak->ref_pools[0]))
    return FALSE;

  if (ref_pool_ptr)
    *ref_pool_ptr = (gpointer) (&feipak->ref_pools[0]);

  return TRUE;
}

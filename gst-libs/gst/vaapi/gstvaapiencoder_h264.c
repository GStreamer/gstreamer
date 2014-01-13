/*
 *  gstvaapiencoder_h264.c - H.264 encoder
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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
#include <va/va.h>
#include <va/va_enc_h264.h>
#include <gst/base/gstbitwriter.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_h264.h"
#include "gstvaapiencoder_h264_priv.h"
#include "gstvaapiutils_h264_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                          \
  (GST_VAAPI_RATECONTROL_MASK (NONE) |                  \
   GST_VAAPI_RATECONTROL_MASK (CQP)  |                  \
   GST_VAAPI_RATECONTROL_MASK (CBR)  |                  \
   GST_VAAPI_RATECONTROL_MASK (VBR)  |                  \
   GST_VAAPI_RATECONTROL_MASK (VBR_CONSTRAINED))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS                          \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE) |                 \
   GST_VAAPI_ENCODER_TUNE_MASK (HIGH_COMPRESSION))

#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_NONE        0
#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_LOW         1
#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_MEDIUM      2
#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_HIGH        3

typedef enum
{
  GST_VAAPI_ENCODER_H264_NAL_UNKNOWN = 0,
  GST_VAAPI_ENCODER_H264_NAL_NON_IDR = 1,
  GST_VAAPI_ENCODER_H264_NAL_IDR = 5,   /* ref_idc != 0 */
  GST_VAAPI_ENCODER_H264_NAL_SEI = 6,   /* ref_idc == 0 */
  GST_VAAPI_ENCODER_H264_NAL_SPS = 7,
  GST_VAAPI_ENCODER_H264_NAL_PPS = 8
} GstVaapiEncoderH264NalType;

typedef enum
{
  SLICE_TYPE_P = 0,
  SLICE_TYPE_B = 1,
  SLICE_TYPE_I = 2
} H264_SLICE_TYPE;

typedef struct
{
  GstVaapiSurfaceProxy *pic;
  guint poc;
  guint frame_num;
} GstVaapiEncoderH264Ref;

typedef enum
{
  GST_VAAPI_ENC_H264_REORD_NONE = 0,
  GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES = 1,
  GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES = 2
} GstVaapiEncH264ReorderState;

static inline gboolean
_poc_greater_than (guint poc1, guint poc2, guint max_poc)
{
  return (((poc1 - poc2) & (max_poc - 1)) < max_poc / 2);
}

/* Get slice_type value for H.264 specification */
static guint8
h264_get_slice_type (GstVaapiPictureType type)
{
  switch (type) {
    case GST_VAAPI_PICTURE_TYPE_I:
      return 2;
    case GST_VAAPI_PICTURE_TYPE_P:
      return 0;
    case GST_VAAPI_PICTURE_TYPE_B:
      return 1;
    default:
      return -1;
  }
  return -1;
}

/* Get log2_max_frame_num value for H.264 specification */
static guint
h264_get_log2_max_frame_num (guint num)
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
  /* must greater than 4 */
  return ret;
}

static inline void
_check_sps_pps_status (GstVaapiEncoderH264 * encoder,
    const guint8 * nal, guint32 size)
{
  guint8 nal_type;
  gsize ret;

  g_assert (size);

  if (encoder->sps_data && encoder->pps_data)
    return;

  nal_type = nal[0] & 0x1F;
  switch (nal_type) {
    case GST_VAAPI_ENCODER_H264_NAL_SPS:
      encoder->sps_data = gst_buffer_new_allocate (NULL, size, NULL);
      ret = gst_buffer_fill (encoder->sps_data, 0, nal, size);
      g_assert (ret == size);
      break;
    case GST_VAAPI_ENCODER_H264_NAL_PPS:
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
ensure_hw_profile_limits (GstVaapiEncoderH264 * encoder)
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
    profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
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
ensure_hw_profile (GstVaapiEncoderH264 * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  GstVaapiProfile profile, profiles[4];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = encoder->profile;
  switch (encoder->profile) {
    case GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H264_BASELINE;
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H264_MAIN;
      // fall-through
    case GST_VAAPI_PROFILE_H264_MAIN:
      profiles[num_profiles++] = GST_VAAPI_PROFILE_H264_HIGH;
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
ensure_profile_limits (GstVaapiEncoderH264 * encoder)
{
  GstVaapiProfile profile;

  if (!encoder->max_profile_idc
      || encoder->profile_idc <= encoder->max_profile_idc)
    return TRUE;

  GST_WARNING ("lowering coding tools to meet target decoder constraints");

  /* Try Main profile coding tools */
  if (encoder->max_profile_idc < 100) {
    encoder->use_dct8x8 = FALSE;
    profile = GST_VAAPI_PROFILE_H264_MAIN;
  }

  /* Try Constrained Baseline profile coding tools */
  if (encoder->max_profile_idc < 77) {
    encoder->num_bframes = 0;
    encoder->use_cabac = FALSE;
    profile = GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;
  }

  encoder->profile = profile;
  encoder->profile_idc = encoder->max_profile_idc;
  return TRUE;
}

/* Derives the minimum profile from the active coding tools */
static gboolean
ensure_profile (GstVaapiEncoderH264 * encoder)
{
  GstVaapiProfile profile;

  /* Always start from "constrained-baseline" profile for maximum
     compatibility */
  profile = GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE;

  /* Main profile coding tools */
  if (encoder->num_bframes > 0 || encoder->use_cabac)
    profile = GST_VAAPI_PROFILE_H264_MAIN;

  /* High profile coding tools */
  if (encoder->use_dct8x8)
    profile = GST_VAAPI_PROFILE_H264_HIGH;

  encoder->profile = profile;
  encoder->profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
  return TRUE;
}

static gboolean
ensure_level (GstVaapiEncoderH264 * encoder)
{
  const guint bitrate = GST_VAAPI_ENCODER_CAST (encoder)->bitrate;
  const GstVaapiH264LevelLimits *limits_table;
  guint i, num_limits, PicSizeMbs, MaxDpbMbs, MaxMBPS;

  PicSizeMbs = encoder->mb_width * encoder->mb_height;
  MaxDpbMbs = PicSizeMbs * ((encoder->num_bframes) ? 2 : 1);
  MaxMBPS = gst_util_uint64_scale_int_ceil (PicSizeMbs,
      GST_VAAPI_ENCODER_FPS_N (encoder), GST_VAAPI_ENCODER_FPS_D (encoder));

  limits_table = gst_vaapi_utils_h264_get_level_limits_table (&num_limits);
  for (i = 0; i < num_limits; i++) {
    const GstVaapiH264LevelLimits *const limits = &limits_table[i];
    if (PicSizeMbs <= limits->MaxFS &&
        MaxDpbMbs <= limits->MaxDpbMbs &&
        MaxMBPS <= limits->MaxMBPS && (!bitrate || bitrate <= limits->MaxBR))
      break;
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
ensure_tuning_high_compression (GstVaapiEncoderH264 * encoder)
{
  guint8 profile_idc;

  if (!ensure_hw_profile_limits (encoder))
    return FALSE;

  profile_idc = encoder->hw_max_profile_idc;
  if (encoder->max_profile_idc && encoder->max_profile_idc < profile_idc)
    profile_idc = encoder->max_profile_idc;

  /* Tuning options to enable Main profile */
  if (profile_idc >= 77) {
    encoder->use_cabac = TRUE;
    if (!encoder->num_bframes)
      encoder->num_bframes = 1;
  }

  /* Tuning options to enable High profile */
  if (profile_idc >= 100) {
    encoder->use_dct8x8 = TRUE;
  }
  return TRUE;
}

/* Ensure tuning options */
static gboolean
ensure_tuning (GstVaapiEncoderH264 * encoder)
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

static inline void
_reset_gop_start (GstVaapiEncoderH264 * encoder)
{
  ++encoder->idr_num;
  encoder->frame_index = 1;
  encoder->cur_frame_num = 0;
  encoder->cur_present_index = 0;
}

static void
_set_b_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH264 * encoder)
{
  g_assert (pic && encoder);
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_B;
  pic->frame_num = (encoder->cur_frame_num % encoder->max_frame_num);
}

static inline void
_set_p_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH264 * encoder)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_P;
  pic->frame_num = (encoder->cur_frame_num % encoder->max_frame_num);
}

static inline void
_set_i_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH264 * encoder)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_I;
  pic->frame_num = (encoder->cur_frame_num % encoder->max_frame_num);
  g_assert (GST_VAAPI_ENC_PICTURE_GET_FRAME (pic));
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (GST_VAAPI_ENC_PICTURE_GET_FRAME (pic));
}

static inline void
_set_idr_frame (GstVaapiEncPicture * pic, GstVaapiEncoderH264 * encoder)
{
  g_return_if_fail (pic->type == GST_VAAPI_PICTURE_TYPE_NONE);
  pic->type = GST_VAAPI_PICTURE_TYPE_I;
  pic->frame_num = 0;
  pic->poc = 0;
  GST_VAAPI_ENC_PICTURE_FLAG_SET (pic, GST_VAAPI_ENC_PICTURE_FLAG_IDR);

  g_assert (GST_VAAPI_ENC_PICTURE_GET_FRAME (pic));
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (GST_VAAPI_ENC_PICTURE_GET_FRAME (pic));
}

static inline void
_set_key_frame (GstVaapiEncPicture * picture,
    GstVaapiEncoderH264 * encoder, gboolean is_idr)
{
  if (is_idr) {
    _reset_gop_start (encoder);
    _set_idr_frame (picture, encoder);
  } else
    _set_i_frame (picture, encoder);
}

gboolean
gst_bit_writer_put_ue (GstBitWriter * bitwriter, guint32 value)
{
  guint32 size_in_bits = 0;
  guint32 tmp_value = ++value;

  while (tmp_value) {
    ++size_in_bits;
    tmp_value >>= 1;
  }
  if (size_in_bits > 1
      && !gst_bit_writer_put_bits_uint32 (bitwriter, 0, size_in_bits - 1))
    return FALSE;
  if (!gst_bit_writer_put_bits_uint32 (bitwriter, value, size_in_bits))
    return FALSE;
  return TRUE;
}

gboolean
gst_bit_writer_put_se (GstBitWriter * bitwriter, gint32 value)
{
  guint32 new_val;

  if (value <= 0)
    new_val = -(value << 1);
  else
    new_val = (value << 1) - 1;

  if (!gst_bit_writer_put_ue (bitwriter, new_val))
    return FALSE;
  return TRUE;
}


static gboolean
gst_bit_writer_write_nal_header (GstBitWriter * bitwriter,
    guint32 nal_ref_idc, guint32 nal_unit_type)
{
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter, nal_ref_idc, 2);
  gst_bit_writer_put_bits_uint32 (bitwriter, nal_unit_type, 5);
  return TRUE;
}

static gboolean
gst_bit_writer_write_trailing_bits (GstBitWriter * bitwriter)
{
  gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);
  gst_bit_writer_align_bytes_unchecked (bitwriter, 0);
  return TRUE;
}

static gboolean
gst_bit_writer_write_sps (GstBitWriter * bitwriter,
    const VAEncSequenceParameterBufferH264 * seq_param, GstVaapiProfile profile)
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
  gst_bit_writer_put_bits_uint32 (bitwriter, profile_idc, 8);
  /* constraint_set0_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, constraint_set0_flag, 1);
  /* constraint_set1_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, constraint_set1_flag, 1);
  /* constraint_set2_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, constraint_set2_flag, 1);
  /* constraint_set3_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, constraint_set3_flag, 1);
  /* reserved_zero_4bits */
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 4);
  /* level_idc */
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->level_idc, 8);
  /* seq_parameter_set_id */
  gst_bit_writer_put_ue (bitwriter, seq_param->seq_parameter_set_id);

  if (profile == GST_VAAPI_PROFILE_H264_HIGH) {
    /* for high profile */
    /* chroma_format_idc  = 1, 4:2:0 */
    gst_bit_writer_put_ue (bitwriter,
        seq_param->seq_fields.bits.chroma_format_idc);
    if (3 == seq_param->seq_fields.bits.chroma_format_idc) {
      gst_bit_writer_put_bits_uint32 (bitwriter, residual_color_transform_flag,
          1);
    }
    /* bit_depth_luma_minus8 */
    gst_bit_writer_put_ue (bitwriter, seq_param->bit_depth_luma_minus8);
    /* bit_depth_chroma_minus8 */
    gst_bit_writer_put_ue (bitwriter, seq_param->bit_depth_chroma_minus8);
    /* b_qpprime_y_zero_transform_bypass */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        b_qpprime_y_zero_transform_bypass, 1);
    g_assert (seq_param->seq_fields.bits.seq_scaling_matrix_present_flag == 0);
    /* seq_scaling_matrix_present_flag  */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq_param->seq_fields.bits.seq_scaling_matrix_present_flag, 1);

#if 0
    if (seq_param->seq_fields.bits.seq_scaling_matrix_present_flag) {
      for (i = 0;
          i < (seq_param->seq_fields.bits.chroma_format_idc != 3 ? 8 : 12);
          i++) {
        gst_bit_writer_put_bits_uint8 (bitwriter,
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
  gst_bit_writer_put_ue (bitwriter,
      seq_param->seq_fields.bits.log2_max_frame_num_minus4);
  /* pic_order_cnt_type */
  gst_bit_writer_put_ue (bitwriter,
      seq_param->seq_fields.bits.pic_order_cnt_type);

  if (seq_param->seq_fields.bits.pic_order_cnt_type == 0) {
    /* log2_max_pic_order_cnt_lsb_minus4 */
    gst_bit_writer_put_ue (bitwriter,
        seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
  } else if (seq_param->seq_fields.bits.pic_order_cnt_type == 1) {
    g_assert (0);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq_param->seq_fields.bits.delta_pic_order_always_zero_flag, 1);
    gst_bit_writer_put_se (bitwriter, seq_param->offset_for_non_ref_pic);
    gst_bit_writer_put_se (bitwriter,
        seq_param->offset_for_top_to_bottom_field);
    gst_bit_writer_put_ue (bitwriter,
        seq_param->num_ref_frames_in_pic_order_cnt_cycle);
    for (i = 0; i < seq_param->num_ref_frames_in_pic_order_cnt_cycle; i++) {
      gst_bit_writer_put_se (bitwriter, seq_param->offset_for_ref_frame[i]);
    }
  }

  /* num_ref_frames */
  gst_bit_writer_put_ue (bitwriter, seq_param->max_num_ref_frames);
  /* gaps_in_frame_num_value_allowed_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      gaps_in_frame_num_value_allowed_flag, 1);

  /* pic_width_in_mbs_minus1 */
  gst_bit_writer_put_ue (bitwriter, seq_param->picture_width_in_mbs - 1);
  /* pic_height_in_map_units_minus1 */
  gst_bit_writer_put_ue (bitwriter, pic_height_in_map_units - 1);
  /* frame_mbs_only_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->seq_fields.bits.frame_mbs_only_flag, 1);

  if (!seq_param->seq_fields.bits.frame_mbs_only_flag) {        //ONLY mbs
    g_assert (0);
    gst_bit_writer_put_bits_uint32 (bitwriter, mb_adaptive_frame_field, 1);
  }

  /* direct_8x8_inference_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
  /* frame_cropping_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->frame_cropping_flag, 1);

  if (seq_param->frame_cropping_flag) {
    /* frame_crop_left_offset */
    gst_bit_writer_put_ue (bitwriter, seq_param->frame_crop_left_offset);
    /* frame_crop_right_offset */
    gst_bit_writer_put_ue (bitwriter, seq_param->frame_crop_right_offset);
    /* frame_crop_top_offset */
    gst_bit_writer_put_ue (bitwriter, seq_param->frame_crop_top_offset);
    /* frame_crop_bottom_offset */
    gst_bit_writer_put_ue (bitwriter, seq_param->frame_crop_bottom_offset);
  }

  /* vui_parameters_present_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq_param->vui_parameters_present_flag, 1);
  if (seq_param->vui_parameters_present_flag) {
    /* aspect_ratio_info_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq_param->vui_fields.bits.aspect_ratio_info_present_flag, 1);
    if (seq_param->vui_fields.bits.aspect_ratio_info_present_flag) {
      gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->aspect_ratio_idc,
          8);
      if (seq_param->aspect_ratio_idc == 0xFF) {
        gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->sar_width, 16);
        gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->sar_height, 16);
      }
    }

    /* overscan_info_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
    /* video_signal_type_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
    /* chroma_loc_info_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);

    /* timing_info_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq_param->vui_fields.bits.timing_info_present_flag, 1);
    if (seq_param->vui_fields.bits.timing_info_present_flag) {
      gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->num_units_in_tick,
          32);
      gst_bit_writer_put_bits_uint32 (bitwriter, seq_param->time_scale, 32);
      gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1); /* fixed_frame_rate_flag */
    }

    nal_hrd_parameters_present_flag =
        (seq_param->bits_per_second > 0 ? TRUE : FALSE);
    /* nal_hrd_parameters_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, nal_hrd_parameters_present_flag,
        1);
    if (nal_hrd_parameters_present_flag) {
      /* hrd_parameters */
      /* cpb_cnt_minus1 */
      gst_bit_writer_put_ue (bitwriter, 0);
      gst_bit_writer_put_bits_uint32 (bitwriter, 4, 4); /* bit_rate_scale */
      gst_bit_writer_put_bits_uint32 (bitwriter, 6, 4); /* cpb_size_scale */

      for (i = 0; i < 1; ++i) {
        /* bit_rate_value_minus1[0] */
        gst_bit_writer_put_ue (bitwriter,
            seq_param->bits_per_second / 1000 - 1);
        /* cpb_size_value_minus1[0] */
        gst_bit_writer_put_ue (bitwriter,
            seq_param->bits_per_second / 1000 * 8 - 1);
        /* cbr_flag[0] */
        gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1);
      }
      /* initial_cpb_removal_delay_length_minus1 */
      gst_bit_writer_put_bits_uint32 (bitwriter, 23, 5);
      /* cpb_removal_delay_length_minus1 */
      gst_bit_writer_put_bits_uint32 (bitwriter, 23, 5);
      /* dpb_output_delay_length_minus1 */
      gst_bit_writer_put_bits_uint32 (bitwriter, 23, 5);
      /* time_offset_length  */
      gst_bit_writer_put_bits_uint32 (bitwriter, 23, 5);
    }
    /* vcl_hrd_parameters_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
    if (nal_hrd_parameters_present_flag
        || 0 /*vcl_hrd_parameters_present_flag */ ) {
      /* low_delay_hrd_flag */
      gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
    }
    /* pic_struct_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
    /* bitwriter_restriction_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
  }

  /* rbsp_trailing_bits */
  gst_bit_writer_write_trailing_bits (bitwriter);
  return TRUE;
}

static gboolean
gst_bit_writer_write_pps (GstBitWriter * bitwriter,
    const VAEncPictureParameterBufferH264 * pic_param)
{
  guint32 num_slice_groups_minus1 = 0;
  guint32 pic_init_qs_minus26 = 0;
  guint32 redundant_pic_cnt_present_flag = 0;

  /* pic_parameter_set_id */
  gst_bit_writer_put_ue (bitwriter, pic_param->pic_parameter_set_id);
  /* seq_parameter_set_id */
  gst_bit_writer_put_ue (bitwriter, pic_param->seq_parameter_set_id);
  /* entropy_coding_mode_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.entropy_coding_mode_flag, 1);
  /* pic_order_present_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.pic_order_present_flag, 1);
  /* slice_groups-1 */
  gst_bit_writer_put_ue (bitwriter, num_slice_groups_minus1);

  if (num_slice_groups_minus1 > 0) {
     /*FIXME*/ g_assert (0);
  }
  gst_bit_writer_put_ue (bitwriter, pic_param->num_ref_idx_l0_active_minus1);
  gst_bit_writer_put_ue (bitwriter, pic_param->num_ref_idx_l1_active_minus1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.weighted_pred_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.weighted_bipred_idc, 2);
  /* pic_init_qp_minus26 */
  gst_bit_writer_put_se (bitwriter, pic_param->pic_init_qp - 26);
  /* pic_init_qs_minus26 */
  gst_bit_writer_put_se (bitwriter, pic_init_qs_minus26);
  /* chroma_qp_index_offset */
  gst_bit_writer_put_se (bitwriter, pic_param->chroma_qp_index_offset);

  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.deblocking_filter_control_present_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.constrained_intra_pred_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter, redundant_pic_cnt_present_flag, 1);

  /* more_rbsp_data */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.transform_8x8_mode_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic_param->pic_fields.bits.pic_scaling_matrix_present_flag, 1);
  if (pic_param->pic_fields.bits.pic_scaling_matrix_present_flag) {
    g_assert (0);
    /* FIXME */
    /*
       for (i = 0; i <
       (6+(-( (chroma_format_idc ! = 3) ? 2 : 6) * -pic_param->pic_fields.bits.transform_8x8_mode_flag));
       i++) {
       gst_bit_writer_put_bits_uint8(bitwriter, pic_param->pic_fields.bits.pic_scaling_list_present_flag, 1);
       }
     */
  }

  gst_bit_writer_put_se (bitwriter, pic_param->second_chroma_qp_index_offset);
  gst_bit_writer_write_trailing_bits (bitwriter);

  return TRUE;
}

static gboolean
add_sequence_packed_header (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture, GstVaapiEncSequence * sequence)
{
  GstVaapiEncPackedHeader *packed_seq;
  GstBitWriter writer;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  const VAEncSequenceParameterBufferH264 *const seq_param = sequence->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init (&writer, 128 * 8);
  gst_bit_writer_put_bits_uint32 (&writer, 0x00000001, 32);     /* start code */
  gst_bit_writer_write_nal_header (&writer,
      GST_VAAPI_ENCODER_H264_NAL_REF_IDC_HIGH, GST_VAAPI_ENCODER_H264_NAL_SPS);
  gst_bit_writer_write_sps (&writer, seq_param, encoder->profile);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&writer) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&writer);
  data = GST_BIT_WRITER_DATA (&writer);

  packed_header_param_buffer.type = VAEncPackedHeaderSequence;
  packed_header_param_buffer.bit_length = data_bit_size;
  packed_header_param_buffer.has_emulation_bytes = 0;

  packed_seq = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_header_param_buffer, sizeof (packed_header_param_buffer),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_seq);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_seq);
  gst_vaapi_codec_object_replace (&packed_seq, NULL);

  /* store sps data */
  _check_sps_pps_status (encoder, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_clear (&writer, TRUE);

  return TRUE;
}

static gboolean
add_picture_packed_header (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncPackedHeader *packed_pic;
  GstBitWriter writer;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  const VAEncPictureParameterBufferH264 *const pic_param = picture->param;
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init (&writer, 128 * 8);
  gst_bit_writer_put_bits_uint32 (&writer, 0x00000001, 32);     /* start code */
  gst_bit_writer_write_nal_header (&writer,
      GST_VAAPI_ENCODER_H264_NAL_REF_IDC_HIGH, GST_VAAPI_ENCODER_H264_NAL_PPS);
  gst_bit_writer_write_pps (&writer, pic_param);
  g_assert (GST_BIT_WRITER_BIT_SIZE (&writer) % 8 == 0);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&writer);
  data = GST_BIT_WRITER_DATA (&writer);

  packed_header_param_buffer.type = VAEncPackedHeaderPicture;
  packed_header_param_buffer.bit_length = data_bit_size;
  packed_header_param_buffer.has_emulation_bytes = 0;

  packed_pic = gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_header_param_buffer, sizeof (packed_header_param_buffer),
      data, (data_bit_size + 7) / 8);
  g_assert (packed_pic);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_pic);
  gst_vaapi_codec_object_replace (&packed_pic, NULL);

  /* store pps data */
  _check_sps_pps_status (encoder, data + 4, data_bit_size / 8 - 4);
  gst_bit_writer_clear (&writer, TRUE);

  return TRUE;
}

/*  reference picture management */
static void
reference_pic_free (GstVaapiEncoderH264 * encoder, GstVaapiEncoderH264Ref * ref)
{
  if (!ref)
    return;
  if (ref->pic)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder), ref->pic);
  g_slice_free (GstVaapiEncoderH264Ref, ref);
}

static inline GstVaapiEncoderH264Ref *
reference_pic_create (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * surface)
{
  GstVaapiEncoderH264Ref *const ref = g_slice_new0 (GstVaapiEncoderH264Ref);

  ref->pic = surface;
  ref->frame_num = picture->frame_num;
  ref->poc = picture->poc;
  return ref;
}

static gboolean
reference_list_update (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture, GstVaapiSurfaceProxy * surface)
{
  GstVaapiEncoderH264Ref *ref;

  if (GST_VAAPI_PICTURE_TYPE_B == picture->type) {
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder), surface);
    return TRUE;
  }
  if (GST_VAAPI_ENC_PICTURE_IS_IDR (picture)) {
    while (!g_queue_is_empty (&encoder->ref_list))
      reference_pic_free (encoder, g_queue_pop_head (&encoder->ref_list));
  } else if (g_queue_get_length (&encoder->ref_list) >= encoder->max_ref_frames) {
    reference_pic_free (encoder, g_queue_pop_head (&encoder->ref_list));
  }
  ref = reference_pic_create (encoder, picture, surface);
  g_queue_push_tail (&encoder->ref_list, ref);
  g_assert (g_queue_get_length (&encoder->ref_list) <= encoder->max_ref_frames);
  return TRUE;
}

static gboolean
reference_list_init (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiEncoderH264Ref ** reflist_0,
    guint * reflist_0_count,
    GstVaapiEncoderH264Ref ** reflist_1, guint * reflist_1_count)
{
  GstVaapiEncoderH264Ref *tmp;
  GList *iter, *list_0_start = NULL, *list_1_start = NULL;
  guint max_pic_order_cnt = (1 << encoder->log2_max_pic_order_cnt);
  guint count;

  *reflist_0_count = 0;
  *reflist_1_count = 0;
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  iter = g_queue_peek_tail_link (&encoder->ref_list);
  for (; iter; iter = g_list_previous (iter)) {
    tmp = (GstVaapiEncoderH264Ref *) iter->data;
    g_assert (tmp && tmp->poc != picture->poc);
    if (_poc_greater_than (picture->poc, tmp->poc, max_pic_order_cnt)) {
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
    reflist_0[count] = (GstVaapiEncoderH264Ref *) iter->data;
    ++count;
  }
  *reflist_0_count = count;

  if (picture->type != GST_VAAPI_PICTURE_TYPE_B)
    return TRUE;

  /* order reflist_1 */
  count = 0;
  iter = list_1_start;
  for (; iter; iter = g_list_next (iter)) {
    reflist_1[count] = (GstVaapiEncoderH264Ref *) iter->data;
    ++count;
  }
  *reflist_1_count = count;
  return TRUE;
}

/* fill the  H264 VA encoding parameters */
static gboolean
fill_va_sequence_param (GstVaapiEncoderH264 * encoder,
    GstVaapiEncSequence * sequence)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  VAEncSequenceParameterBufferH264 *const seq_param = sequence->param;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferH264));
  seq_param->seq_parameter_set_id = 0;
  seq_param->level_idc = encoder->level_idc;
  seq_param->intra_period = GST_VAAPI_ENCODER_KEYFRAME_PERIOD (encoder);
  seq_param->ip_period = 0;     // ?
  if (base_encoder->bitrate > 0)
    seq_param->bits_per_second = base_encoder->bitrate * 1000;
  else
    seq_param->bits_per_second = 0;

  seq_param->max_num_ref_frames = encoder->max_ref_frames;
  seq_param->picture_width_in_mbs = encoder->mb_width;
  seq_param->picture_height_in_mbs = encoder->mb_height;

  /*sequence field values */
  seq_param->seq_fields.value = 0;
  seq_param->seq_fields.bits.chroma_format_idc = 1;
  seq_param->seq_fields.bits.frame_mbs_only_flag = 1;
  seq_param->seq_fields.bits.mb_adaptive_frame_field_flag = FALSE;
  seq_param->seq_fields.bits.seq_scaling_matrix_present_flag = FALSE;
  /* direct_8x8_inference_flag default false */
  seq_param->seq_fields.bits.direct_8x8_inference_flag = FALSE;
  g_assert (encoder->log2_max_frame_num >= 4);
  seq_param->seq_fields.bits.log2_max_frame_num_minus4 =
      encoder->log2_max_frame_num - 4;
  /* picture order count */
  seq_param->seq_fields.bits.pic_order_cnt_type = 0;
  g_assert (encoder->log2_max_pic_order_cnt >= 4);
  seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 =
      encoder->log2_max_pic_order_cnt - 4;

  seq_param->bit_depth_luma_minus8 = 0;
  seq_param->bit_depth_chroma_minus8 = 0;

  /* not used if pic_order_cnt_type == 0 */
  if (seq_param->seq_fields.bits.pic_order_cnt_type == 1) {
    seq_param->seq_fields.bits.delta_pic_order_always_zero_flag = TRUE;
    seq_param->num_ref_frames_in_pic_order_cnt_cycle = 0;
    seq_param->offset_for_non_ref_pic = 0;
    seq_param->offset_for_top_to_bottom_field = 0;
    memset (seq_param->offset_for_ref_frame, 0,
        sizeof (seq_param->offset_for_ref_frame));
  }

  /* frame_cropping_flag */
  if ((GST_VAAPI_ENCODER_WIDTH (encoder) & 15) ||
      (GST_VAAPI_ENCODER_HEIGHT (encoder) & 15)) {
    seq_param->frame_cropping_flag = 1;
    seq_param->frame_crop_left_offset = 0;
    seq_param->frame_crop_right_offset =
        16 * encoder->mb_width - GST_VAAPI_ENCODER_WIDTH (encoder);
    seq_param->frame_crop_top_offset = 0;
    seq_param->frame_crop_bottom_offset =
        (16 * encoder->mb_height - GST_VAAPI_ENCODER_HEIGHT (encoder)) /
        (2 - seq_param->seq_fields.bits.frame_mbs_only_flag);
  }

  /* vui not set */
  seq_param->vui_parameters_present_flag =
      (base_encoder->bitrate > 0 ? TRUE : FALSE);
  if (seq_param->vui_parameters_present_flag) {
    seq_param->vui_fields.bits.aspect_ratio_info_present_flag = FALSE;
    seq_param->vui_fields.bits.bitstream_restriction_flag = FALSE;
    seq_param->vui_fields.bits.timing_info_present_flag =
        (base_encoder->bitrate > 0 ? TRUE : FALSE);
    if (seq_param->vui_fields.bits.timing_info_present_flag) {
      seq_param->num_units_in_tick = GST_VAAPI_ENCODER_FPS_D (encoder);
      seq_param->time_scale = GST_VAAPI_ENCODER_FPS_N (encoder) * 2;
    }
  }

  return TRUE;
}

static gboolean
fill_va_picture_param (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferH264 *const pic_param = picture->param;
  GstVaapiEncoderH264Ref *ref_pic;
  GList *reflist;
  guint i;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferH264));

  /* reference list,  */
  pic_param->CurrPic.picture_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->CurrPic.TopFieldOrderCnt = picture->poc;
  i = 0;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
    for (reflist = g_queue_peek_head_link (&encoder->ref_list);
        reflist; reflist = g_list_next (reflist)) {
      ref_pic = reflist->data;
      g_assert (ref_pic && ref_pic->pic &&
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic) != VA_INVALID_ID);

      pic_param->ReferenceFrames[i].picture_id =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic);
      ++i;
    }
    g_assert (i <= 16 && i <= encoder->max_ref_frames);
  }
  for (; i < 16; ++i) {
    pic_param->ReferenceFrames[i].picture_id = VA_INVALID_ID;
  }
  pic_param->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

  pic_param->pic_parameter_set_id = 0;
  pic_param->seq_parameter_set_id = 0;
  pic_param->last_picture = 0;  /* means last encoding picture */
  pic_param->frame_num = picture->frame_num;
  pic_param->pic_init_qp = encoder->init_qp;
  pic_param->num_ref_idx_l0_active_minus1 =
      (encoder->max_reflist0_count ? (encoder->max_reflist0_count - 1) : 0);
  pic_param->num_ref_idx_l1_active_minus1 =
      (encoder->max_reflist1_count ? (encoder->max_reflist1_count - 1) : 0);
  pic_param->chroma_qp_index_offset = 0;
  pic_param->second_chroma_qp_index_offset = 0;

  /* set picture fields */
  pic_param->pic_fields.value = 0;
  pic_param->pic_fields.bits.idr_pic_flag =
      GST_VAAPI_ENC_PICTURE_IS_IDR (picture);
  pic_param->pic_fields.bits.reference_pic_flag =
      (picture->type != GST_VAAPI_PICTURE_TYPE_B);
  pic_param->pic_fields.bits.entropy_coding_mode_flag = encoder->use_cabac;
  pic_param->pic_fields.bits.weighted_pred_flag = FALSE;
  pic_param->pic_fields.bits.weighted_bipred_idc = 0;
  pic_param->pic_fields.bits.constrained_intra_pred_flag = 0;
  pic_param->pic_fields.bits.transform_8x8_mode_flag = encoder->use_dct8x8;
  /* enable debloking */
  pic_param->pic_fields.bits.deblocking_filter_control_present_flag = TRUE;
  pic_param->pic_fields.bits.redundant_pic_cnt_present_flag = FALSE;
  /* bottom_field_pic_order_in_frame_present_flag */
  pic_param->pic_fields.bits.pic_order_present_flag = FALSE;
  pic_param->pic_fields.bits.pic_scaling_matrix_present_flag = FALSE;

  return TRUE;
}

static gboolean
fill_va_slices_param (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiEncoderH264Ref ** reflist_0,
    guint reflist_0_count,
    GstVaapiEncoderH264Ref ** reflist_1, guint reflist_1_count)
{
  VAEncSliceParameterBufferH264 *slice_param;
  GstVaapiEncSlice *slice;
  guint slice_of_mbs, slice_mod_mbs, cur_slice_mbs;
  guint mb_size;
  guint last_mb_index;
  guint i_slice, i_ref;

  g_assert (picture);

  mb_size = encoder->mb_width * encoder->mb_height;

  g_assert (encoder->num_slices && encoder->num_slices < mb_size);
  slice_of_mbs = mb_size / encoder->num_slices;
  slice_mod_mbs = mb_size % encoder->num_slices;
  last_mb_index = 0;
  for (i_slice = 0; i_slice < encoder->num_slices; ++i_slice) {
    cur_slice_mbs = slice_of_mbs;
    if (slice_mod_mbs) {
      ++cur_slice_mbs;
      --slice_mod_mbs;
    }
    slice = GST_VAAPI_ENC_SLICE_NEW (H264, encoder);
    g_assert (slice && slice->param_id != VA_INVALID_ID);
    slice_param = slice->param;

    memset (slice_param, 0, sizeof (VAEncSliceParameterBufferH264));
    slice_param->macroblock_address = last_mb_index;
    slice_param->num_macroblocks = cur_slice_mbs;
    slice_param->macroblock_info = VA_INVALID_ID;
    slice_param->slice_type = h264_get_slice_type (picture->type);
    g_assert (slice_param->slice_type != -1);
    slice_param->pic_parameter_set_id = 0;
    slice_param->idr_pic_id = encoder->idr_num;
    slice_param->pic_order_cnt_lsb = picture->poc;

    /* not used if pic_order_cnt_type = 0 */
    slice_param->delta_pic_order_cnt_bottom = 0;
    memset (slice_param->delta_pic_order_cnt, 0,
        sizeof (slice_param->delta_pic_order_cnt));

    /* only works for B frames */
    slice_param->direct_spatial_mv_pred_flag = FALSE;
    /* default equal to picture parameters */
    slice_param->num_ref_idx_active_override_flag = FALSE;
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
        slice_param->RefPicList0[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_0[i_ref]->pic);
      }
      g_assert (i_ref == 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->RefPicList0); ++i_ref) {
      slice_param->RefPicList0[i_ref].picture_id = VA_INVALID_SURFACE;
    }

    i_ref = 0;
    if (picture->type == GST_VAAPI_PICTURE_TYPE_B) {
      for (; i_ref < reflist_1_count; ++i_ref) {
        slice_param->RefPicList1[i_ref].picture_id =
            GST_VAAPI_SURFACE_PROXY_SURFACE_ID (reflist_1[i_ref]->pic);
      }
      g_assert (i_ref == 1);
    }
    for (; i_ref < G_N_ELEMENTS (slice_param->RefPicList1); ++i_ref) {
      slice_param->RefPicList1[i_ref].picture_id = VA_INVALID_SURFACE;
    }

    /* not used if  pic_param.pic_fields.bits.weighted_pred_flag == FALSE */
    slice_param->luma_log2_weight_denom = 0;
    slice_param->chroma_log2_weight_denom = 0;
    slice_param->luma_weight_l0_flag = FALSE;
    memset (slice_param->luma_weight_l0, 0,
        sizeof (slice_param->luma_weight_l0));
    memset (slice_param->luma_offset_l0, 0,
        sizeof (slice_param->luma_offset_l0));
    slice_param->chroma_weight_l0_flag = FALSE;
    memset (slice_param->chroma_weight_l0, 0,
        sizeof (slice_param->chroma_weight_l0));
    memset (slice_param->chroma_offset_l0, 0,
        sizeof (slice_param->chroma_offset_l0));
    slice_param->luma_weight_l1_flag = FALSE;
    memset (slice_param->luma_weight_l1, 0,
        sizeof (slice_param->luma_weight_l1));
    memset (slice_param->luma_offset_l1, 0,
        sizeof (slice_param->luma_offset_l1));
    slice_param->chroma_weight_l1_flag = FALSE;
    memset (slice_param->chroma_weight_l1, 0,
        sizeof (slice_param->chroma_weight_l1));
    memset (slice_param->chroma_offset_l1, 0,
        sizeof (slice_param->chroma_offset_l1));

    slice_param->cabac_init_idc = 0;
    slice_param->slice_qp_delta = encoder->init_qp - encoder->min_qp;
    if (slice_param->slice_qp_delta > 4)
      slice_param->slice_qp_delta = 4;
    slice_param->disable_deblocking_filter_idc = 0;
    slice_param->slice_alpha_c0_offset_div2 = 2;
    slice_param->slice_beta_offset_div2 = 2;

    /* set calculation for next slice */
    last_mb_index += cur_slice_mbs;

    gst_vaapi_enc_picture_add_slice (picture, slice);
    gst_vaapi_codec_object_replace (&slice, NULL);
  }
  g_assert (last_mb_index == mb_size);
  return TRUE;
}

static gboolean
ensure_sequence (GstVaapiEncoderH264 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence;

  g_assert (picture);
  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (H264, encoder);
  g_assert (sequence);
  if (!sequence)
    goto error;

  if (!fill_va_sequence_param (encoder, sequence))
    goto error;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I &&
      !add_sequence_packed_header (encoder, picture, sequence))
    goto error;
  gst_vaapi_enc_picture_set_sequence (picture, sequence);
  gst_vaapi_codec_object_replace (&sequence, NULL);
  return TRUE;

error:
  gst_vaapi_codec_object_replace (&sequence, NULL);
  return FALSE;
}

static gboolean
ensure_picture (GstVaapiEncoderH264 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_va_picture_param (encoder, picture, codedbuf, surface))
    return FALSE;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I &&
      !add_picture_packed_header (encoder, picture)) {
    GST_ERROR ("set picture packed header failed");
    return FALSE;
  }

  return TRUE;
}

static gboolean
ensure_slices (GstVaapiEncoderH264 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncoderH264Ref *reflist_0[16];
  GstVaapiEncoderH264Ref *reflist_1[16];
  guint reflist_0_count = 0, reflist_1_count = 0;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I &&
      !reference_list_init (encoder, picture,
          reflist_0, &reflist_0_count, reflist_1, &reflist_1_count)) {
    GST_ERROR ("reference list reorder failed");
    return FALSE;
  }

  g_assert (reflist_0_count + reflist_1_count <= encoder->max_ref_frames);
  if (reflist_0_count > encoder->max_reflist0_count)
    reflist_0_count = encoder->max_reflist0_count;
  if (reflist_1_count > encoder->max_reflist1_count)
    reflist_1_count = encoder->max_reflist1_count;

  if (!fill_va_slices_param (encoder, picture,
          reflist_0, reflist_0_count, reflist_1, reflist_1_count))
    return FALSE;

  return TRUE;
}

static gboolean
ensure_misc (GstVaapiEncoderH264 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  GstVaapiEncMiscParam *misc = NULL;
  VAEncMiscParameterHRD *hrd;
  VAEncMiscParameterRateControl *rate_control;

  /* add hrd */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, encoder);
  g_assert (misc);
  if (!misc)
    return FALSE;
  gst_vaapi_enc_picture_add_misc_buffer (picture, misc);
  hrd = misc->impl;
  if (base_encoder->bitrate > 0) {
    hrd->initial_buffer_fullness = base_encoder->bitrate * 1000 * 4;
    hrd->buffer_size = base_encoder->bitrate * 1000 * 8;
  } else {
    hrd->initial_buffer_fullness = 0;
    hrd->buffer_size = 0;
  }
  gst_vaapi_codec_object_replace (&misc, NULL);

  /* add ratecontrol */
  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CBR ||
      GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_VBR) {
    misc = GST_VAAPI_ENC_MISC_PARAM_NEW (RateControl, encoder);
    g_assert (misc);
    if (!misc)
      return FALSE;
    gst_vaapi_enc_picture_add_misc_buffer (picture, misc);
    rate_control = misc->impl;
    memset (rate_control, 0, sizeof (VAEncMiscParameterRateControl));
    if (base_encoder->bitrate)
      rate_control->bits_per_second = base_encoder->bitrate * 1000;
    else
      rate_control->bits_per_second = 0;
    rate_control->target_percentage = 70;
    rate_control->window_size = 500;
    rate_control->initial_qp = encoder->init_qp;
    rate_control->min_qp = encoder->min_qp;
    rate_control->basic_unit_size = 0;
    gst_vaapi_codec_object_replace (&misc, NULL);
  }

  return TRUE;
}

static GstVaapiEncoderStatus
ensure_profile_and_level (GstVaapiEncoderH264 * encoder)
{
  ensure_tuning (encoder);

  if (!ensure_profile (encoder) || !ensure_profile_limits (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  if (!ensure_level (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;

  /* Check HW constraints */
  if (!ensure_hw_profile_limits (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  if (encoder->profile_idc > encoder->hw_max_profile_idc)
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static gboolean
ensure_bitrate (GstVaapiEncoderH264 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);

  /* Default compression: 48 bits per macroblock in "high-compression" mode */
  switch (GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    case GST_VAAPI_RATECONTROL_CBR:
    case GST_VAAPI_RATECONTROL_VBR:
    case GST_VAAPI_RATECONTROL_VBR_CONSTRAINED:
      if (!base_encoder->bitrate) {
        /* According to the literature and testing, CABAC entropy coding
           mode could provide for +10% to +18% improvement in general,
           thus estimating +15% here ; and using adaptive 8x8 transforms
           in I-frames could bring up to +10% improvement. */
        guint bits_per_mb = 48;
        if (!encoder->use_cabac)
          bits_per_mb += (bits_per_mb * 15) / 100;
        if (!encoder->use_dct8x8)
          bits_per_mb += (bits_per_mb * 10) / 100;

        base_encoder->bitrate =
            encoder->mb_width * encoder->mb_height * bits_per_mb *
            GST_VAAPI_ENCODER_FPS_N (encoder) /
            GST_VAAPI_ENCODER_FPS_D (encoder) / 1000;
        GST_INFO ("target bitrate computed to %u kbps", base_encoder->bitrate);
      }
      break;
    default:
      base_encoder->bitrate = 0;
      break;
  }
  return TRUE;
}

static void
reset_properties (GstVaapiEncoderH264 * encoder)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  guint mb_size;

  if (encoder->idr_period < base_encoder->keyframe_period)
    encoder->idr_period = base_encoder->keyframe_period;
  if (encoder->idr_period > GST_VAAPI_ENCODER_H264_MAX_IDR_PERIOD)
    encoder->idr_period = GST_VAAPI_ENCODER_H264_MAX_IDR_PERIOD;

  if (encoder->min_qp > encoder->init_qp ||
      (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CQP &&
          encoder->min_qp < encoder->init_qp))
    encoder->min_qp = encoder->init_qp;

  mb_size = encoder->mb_width * encoder->mb_height;
  if (encoder->num_slices > (mb_size + 1) / 2)
    encoder->num_slices = (mb_size + 1) / 2;
  g_assert (encoder->num_slices);

  if (encoder->num_bframes > (base_encoder->keyframe_period + 1) / 2)
    encoder->num_bframes = (base_encoder->keyframe_period + 1) / 2;

  if (encoder->num_bframes > 50)
    encoder->num_bframes = 50;

  if (encoder->num_bframes)
    encoder->cts_offset = GST_SECOND * GST_VAAPI_ENCODER_FPS_D (encoder) /
        GST_VAAPI_ENCODER_FPS_N (encoder);
  else
    encoder->cts_offset = 0;

  /* init max_frame_num, max_poc */
  encoder->log2_max_frame_num =
      h264_get_log2_max_frame_num (encoder->idr_period);
  g_assert (encoder->log2_max_frame_num >= 4);
  encoder->max_frame_num = (1 << encoder->log2_max_frame_num);
  encoder->log2_max_pic_order_cnt = encoder->log2_max_frame_num + 1;
  encoder->max_pic_order_cnt = (1 << encoder->log2_max_pic_order_cnt);

  encoder->frame_index = 0;
  encoder->idr_num = 0;
  encoder->max_reflist0_count = 1;
  encoder->max_reflist1_count = encoder->num_bframes > 0;
  encoder->max_ref_frames =
      encoder->max_reflist0_count + encoder->max_reflist1_count;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base_encoder);

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));

  if (!ensure_sequence (encoder, picture))
    goto error;
  if (!ensure_picture (encoder, picture, codedbuf, reconstruct))
    goto error;
  if (!ensure_misc (encoder, picture))
    goto error;
  if (!ensure_slices (encoder, picture))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;

  if (!reference_list_update (encoder, picture, reconstruct))
    goto error;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
error:
  if (reconstruct)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
        reconstruct);
  return ret;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_flush (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  GstVaapiEncPicture *pic;

  encoder->frame_index = 0;
  encoder->cur_frame_num = 0;
  encoder->cur_present_index = 0;
  while (!g_queue_is_empty (&encoder->reorder_frame_list)) {
    pic = g_queue_pop_head (&encoder->reorder_frame_list);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->reorder_frame_list);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/* Generate "codec-data" buffer */
static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_get_codec_data (GstVaapiEncoder * base_encoder,
    GstBuffer ** out_buffer_ptr)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  const guint32 configuration_version = 0x01;
  const guint32 nal_length_size = 4;
  guint8 profile_idc, profile_comp, level_idc;
  GstMapInfo sps_info, pps_info;
  GstBitWriter writer;
  GstBuffer *buffer;

  if (!encoder->sps_data || !encoder->pps_data)
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER;
  if (gst_buffer_get_size (encoder->sps_data) < 4)
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER;

  if (!gst_buffer_map (encoder->sps_data, &sps_info, GST_MAP_READ))
    goto error_map_sps_buffer;

  if (!gst_buffer_map (encoder->pps_data, &pps_info, GST_MAP_READ))
    goto error_map_pps_buffer;

  /* skip sps_data[0], which is the nal_unit_type */
  profile_idc = sps_info.data[1];
  profile_comp = sps_info.data[2];
  level_idc = sps_info.data[3];

  /* Header */
  gst_bit_writer_init (&writer, (sps_info.size + pps_info.size + 64) * 8);
  gst_bit_writer_put_bits_uint32 (&writer, configuration_version, 8);
  gst_bit_writer_put_bits_uint32 (&writer, profile_idc, 8);
  gst_bit_writer_put_bits_uint32 (&writer, profile_comp, 8);
  gst_bit_writer_put_bits_uint32 (&writer, level_idc, 8);
  gst_bit_writer_put_bits_uint32 (&writer, 0x3f, 6);    /* 111111 */
  gst_bit_writer_put_bits_uint32 (&writer, nal_length_size - 1, 2);
  gst_bit_writer_put_bits_uint32 (&writer, 0x07, 3);    /* 111 */

  /* Write SPS */
  gst_bit_writer_put_bits_uint32 (&writer, 1, 5);       /* SPS count = 1 */
  g_assert (GST_BIT_WRITER_BIT_SIZE (&writer) % 8 == 0);
  gst_bit_writer_put_bits_uint32 (&writer, sps_info.size, 16);
  gst_bit_writer_put_bytes (&writer, sps_info.data, sps_info.size);

  /* Write PPS */
  gst_bit_writer_put_bits_uint32 (&writer, 1, 8);       /* PPS count = 1 */
  gst_bit_writer_put_bits_uint32 (&writer, pps_info.size, 16);
  gst_bit_writer_put_bytes (&writer, pps_info.data, pps_info.size);

  gst_buffer_unmap (encoder->pps_data, &pps_info);
  gst_buffer_unmap (encoder->sps_data, &sps_info);

  buffer = gst_buffer_new_wrapped (GST_BIT_WRITER_DATA (&writer),
      GST_BIT_WRITER_BIT_SIZE (&writer) / 8);
  if (!buffer)
    goto error_alloc_buffer;
  *out_buffer_ptr = buffer;

  gst_bit_writer_clear (&writer, FALSE);
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
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
    gst_bit_writer_clear (&writer, TRUE);
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  GstVaapiEncPicture *picture;
  gboolean is_idr = FALSE;

  *output = NULL;

  if (!frame) {
    if (encoder->reorder_state != GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES)
      return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

    /* reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES
       dump B frames from queue, sometime, there may also have P frame or I frame */
    g_assert (encoder->num_bframes > 0);
    g_return_val_if_fail (!g_queue_is_empty (&encoder->reorder_frame_list),
        GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN);
    picture = g_queue_pop_head (&encoder->reorder_frame_list);
    g_assert (picture);
    if (g_queue_is_empty (&encoder->reorder_frame_list)) {
      encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new frame coming */
  picture = GST_VAAPI_ENC_PICTURE_NEW (H264, encoder, frame);
  if (!picture) {
    GST_WARNING ("create H264 picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  ++encoder->cur_present_index;
  picture->poc = ((encoder->cur_present_index * 2) %
      encoder->max_pic_order_cnt);

  is_idr = (encoder->frame_index == 0 ||
      encoder->frame_index >= encoder->idr_period);

  /* check key frames */
  if (is_idr || GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame) ||
      (encoder->frame_index % GST_VAAPI_ENCODER_KEYFRAME_PERIOD (encoder)) ==
      0) {
    ++encoder->cur_frame_num;
    ++encoder->frame_index;

    /* b frame enabled,  check queue of reorder_frame_list */
    if (encoder->num_bframes
        && !g_queue_is_empty (&encoder->reorder_frame_list)) {
      GstVaapiEncPicture *p_pic;

      p_pic = g_queue_pop_tail (&encoder->reorder_frame_list);
      _set_p_frame (p_pic, encoder);
      g_queue_foreach (&encoder->reorder_frame_list,
          (GFunc) _set_b_frame, encoder);
      ++encoder->cur_frame_num;
      _set_key_frame (picture, encoder, is_idr);
      g_queue_push_tail (&encoder->reorder_frame_list, picture);
      picture = p_pic;
      encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES;
    } else {                    /* no b frames in queue */
      _set_key_frame (picture, encoder, is_idr);
      g_assert (g_queue_is_empty (&encoder->reorder_frame_list));
      if (encoder->num_bframes)
        encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new p/b frames coming */
  ++encoder->frame_index;
  if (encoder->reorder_state == GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES &&
      g_queue_get_length (&encoder->reorder_frame_list) <
      encoder->num_bframes) {
    g_queue_push_tail (&encoder->reorder_frame_list, picture);
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;
  }

  ++encoder->cur_frame_num;
  _set_p_frame (picture, encoder);

  if (encoder->reorder_state == GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES) {
    g_queue_foreach (&encoder->reorder_frame_list, (GFunc) _set_b_frame,
        encoder);
    encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES;
    g_assert (!g_queue_is_empty (&encoder->reorder_frame_list));
  }

end:
  g_assert (picture);
  frame = GST_VAAPI_ENC_PICTURE_GET_FRAME (picture);
  if (GST_CLOCK_TIME_IS_VALID (frame->pts))
    frame->pts += encoder->cts_offset;
  *output = picture;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);
  const guint DEFAULT_SURFACES_COUNT = 3;

  /* Maximum sizes for common headers (in bits) */
  enum
  {
    MAX_SPS_HDR_SIZE = 16473,
    MAX_VUI_PARAMS_SIZE = 210,
    MAX_HRD_PARAMS_SIZE = 4103,
    MAX_PPS_HDR_SIZE = 101,
    MAX_SLICE_HDR_SIZE = 397 + 2572 + 6670 + 2402,
  };

  if (!ensure_hw_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames =
      (encoder->num_bframes ? 2 : 1) + DEFAULT_SURFACES_COUNT;

  /* Only YUV 4:2:0 formats are supported for now. This means that we
     have a limit of 3200 bits per macroblock. */
  /* XXX: check profile and compute RawMbBits */
  base_encoder->codedbuf_size = (GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) / 256) * 400;

  /* Account for SPS header */
  /* XXX: exclude scaling lists, MVC/SVC extensions */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_SPS_HDR_SIZE +
      MAX_VUI_PARAMS_SIZE + 2 * MAX_HRD_PARAMS_SIZE) / 8;

  /* Account for PPS header */
  /* XXX: exclude slice groups, scaling lists, MVC/SVC extensions */
  base_encoder->codedbuf_size += 4 + GST_ROUND_UP_8 (MAX_PPS_HDR_SIZE) / 8;

  /* Account for slice header */
  base_encoder->codedbuf_size += encoder->num_slices * (4 +
      GST_ROUND_UP_8 (MAX_SLICE_HDR_SIZE) / 8);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  GstVaapiEncoderStatus status;

  encoder->mb_width = (GST_VAAPI_ENCODER_WIDTH (encoder) + 15) / 16;
  encoder->mb_height = (GST_VAAPI_ENCODER_HEIGHT (encoder) + 15) / 16;

  status = ensure_profile_and_level (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  if (!ensure_bitrate (encoder))
    goto error;

  reset_properties (encoder);
  return set_context_info (base_encoder);

error:
  return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
}

static gboolean
gst_vaapi_encoder_h264_init (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);

  /* re-ordering */
  g_queue_init (&encoder->reorder_frame_list);
  encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_NONE;

  /* reference frames */
  g_queue_init (&encoder->ref_list);
  encoder->max_ref_frames = 0;
  encoder->max_reflist0_count = 1;
  encoder->max_reflist1_count = 1;

  return TRUE;
}

static void
gst_vaapi_encoder_h264_finalize (GstVaapiEncoder * base_encoder)
{
  /*free private buffers */
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);
  GstVaapiEncPicture *pic;
  GstVaapiEncoderH264Ref *ref;

  gst_buffer_replace (&encoder->sps_data, NULL);
  gst_buffer_replace (&encoder->pps_data, NULL);

  while (!g_queue_is_empty (&encoder->ref_list)) {
    ref = g_queue_pop_head (&encoder->ref_list);
    reference_pic_free (encoder, ref);
  }
  g_queue_clear (&encoder->ref_list);

  while (!g_queue_is_empty (&encoder->reorder_frame_list)) {
    pic = g_queue_pop_head (&encoder->reorder_frame_list);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->reorder_frame_list);

}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_set_property (GstVaapiEncoder * base_encoder,
    gint prop_id, const GValue * value)
{
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264_CAST (base_encoder);

  switch (prop_id) {
    case GST_VAAPI_ENCODER_H264_PROP_MAX_BFRAMES:
      encoder->num_bframes = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H264_PROP_INIT_QP:
      encoder->init_qp = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H264_PROP_MIN_QP:
      encoder->min_qp = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H264_PROP_NUM_SLICES:
      encoder->num_slices = g_value_get_uint (value);
      break;
    case GST_VAAPI_ENCODER_H264_PROP_CABAC:
      encoder->use_cabac = g_value_get_boolean (value);
      break;
    case GST_VAAPI_ENCODER_H264_PROP_DCT8X8:
      encoder->use_dct8x8 = g_value_get_boolean (value);
      break;
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (H264);

static inline const GstVaapiEncoderClass *
gst_vaapi_encoder_h264_class (void)
{
  static const GstVaapiEncoderClass GstVaapiEncoderH264Class = {
    GST_VAAPI_ENCODER_CLASS_INIT (H264, h264),
    .set_property = gst_vaapi_encoder_h264_set_property,
    .get_codec_data = gst_vaapi_encoder_h264_get_codec_data
  };
  return &GstVaapiEncoderH264Class;
}

/**
 * gst_vaapi_encoder_h264_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for H.264 encoding. Note that the
 * only supported output stream format is "byte-stream" format.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_h264_new (GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_new (gst_vaapi_encoder_h264_class (), display);
}

/**
 * gst_vaapi_encoder_h264_get_default_properties:
 *
 * Determines the set of common and H.264 specific encoder properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of encoder properties for #GstVaapiEncoderH264,
 *   or %NULL if an error occurred.
 */
GPtrArray *
gst_vaapi_encoder_h264_get_default_properties (void)
{
  const GstVaapiEncoderClass *const klass = gst_vaapi_encoder_h264_class ();
  GPtrArray *props;

  props = gst_vaapi_encoder_properties_get_default (klass);
  if (!props)
    return NULL;

  /**
   * GstVaapiEncoderH264:max-bframes:
   *
   * The number of B-frames between I and P.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H264_PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes",
          "Max B-Frames", "Number of B-frames between I and P", 0, 10, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH264:init-qp:
   *
   * The initial quantizer value.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H264_PROP_INIT_QP,
      g_param_spec_uint ("init-qp",
          "Initial QP", "Initial quantizer value", 1, 51, 26,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH264:min-qp:
   *
   * The minimum quantizer value.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H264_PROP_MIN_QP,
      g_param_spec_uint ("min-qp",
          "Minimum QP", "Minimum quantizer value", 1, 51, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH264:num-slices:
   *
   * The number of slices per frame.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H264_PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices",
          "Number of Slices",
          "Number of slices per frame",
          1, 200, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH264:cabac:
   *
   * Enable CABAC entropy coding mode for improved compression ratio,
   * at the expense that the minimum target profile is Main. Default
   * is CAVLC entropy coding mode.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H264_PROP_CABAC,
      g_param_spec_boolean ("cabac",
          "Enable CABAC",
          "Enable CABAC entropy coding mode",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVaapiEncoderH264:dct8x8:
   *
   * Enable adaptive use of 8x8 transforms in I-frames. This improves
   * the compression ratio by the minimum target profile is High.
   * Default is to use 4x4 DCT only.
   */
  GST_VAAPI_ENCODER_PROPERTIES_APPEND (props,
      GST_VAAPI_ENCODER_H264_PROP_DCT8X8,
      g_param_spec_boolean ("dct8x8",
          "Enable 8x8 DCT",
          "Enable adaptive use of 8x8 transforms in I-frames",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  return props;
}

/**
 * gst_vaapi_encoder_h264_set_max_profile:
 * @encoder: a #GstVaapiEncoderH264
 * @profile: an H.264 #GstVaapiProfile
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
gst_vaapi_encoder_h264_set_max_profile (GstVaapiEncoderH264 * encoder,
    GstVaapiProfile profile)
{
  guint8 profile_idc;

  g_return_val_if_fail (encoder != NULL, FALSE);
  g_return_val_if_fail (profile != GST_VAAPI_PROFILE_UNKNOWN, FALSE);

  if (gst_vaapi_profile_get_codec (profile) != GST_VAAPI_CODEC_H264)
    return FALSE;

  profile_idc = gst_vaapi_utils_h264_get_profile_idc (profile);
  if (!profile_idc)
    return FALSE;

  encoder->max_profile_idc = profile_idc;
  return TRUE;
}

/**
 * gst_vaapi_encoder_h264_get_profile_and_level:
 * @encoder: a #GstVaapiEncoderH264
 * @out_profile_ptr: return location for the #GstVaapiProfile
 * @out_level_ptr: return location for the #GstVaapiLevelH264
 *
 * Queries the H.264 @encoder for the active profile and level. That
 * information is only constructed and valid after the encoder is
 * configured, i.e. after the gst_vaapi_encoder_set_codec_state()
 * function is called.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_encoder_h264_get_profile_and_level (GstVaapiEncoderH264 * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiLevelH264 * out_level_ptr)
{
  g_return_val_if_fail (encoder != NULL, FALSE);

  if (!encoder->profile || !encoder->level)
    return FALSE;

  if (out_profile_ptr)
    *out_profile_ptr = encoder->profile;
  if (out_level_ptr)
    *out_level_ptr = encoder->level;
  return TRUE;
}

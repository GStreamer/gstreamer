/*
 *  gstvaapiencoder_h264.c - H.264 encoder
 *
 *  Copyright (C) 2012 -2013 Intel Corporation
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
#include "gstvaapicompat.h"
#include "gstvaapiencoder_h264.h"
#include "gstvaapiencoder_h264_priv.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapicodedbufferproxy_priv.h"

#include <va/va.h>
#include <va/va_enc_h264.h>

#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_NONE        0
#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_LOW         1
#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_MEDIUM      2
#define GST_VAAPI_ENCODER_H264_NAL_REF_IDC_HIGH        3

typedef enum
{
  GST_VAAPI_ENCODER_H264_ENTROPY_MODE_CAVLC = 0,
  GST_VAAPI_ENCODER_H264_ENTROPY_MODE_CABAC = 1
} GstVaapiEncoderH264EntropyMode;

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

#define GST_VAAPI_H264_CAPS                                \
        "video/x-h264, "                                   \
        "framerate = (fraction) [0/1, MAX], "              \
        "width = (int) [ 1, MAX ], "                       \
        "height = (int) [ 1, MAX ], "                      \
        "stream-format = (string) { avc, byte-stream }, "  \
        "alignment = (string) { au } "

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

static inline guint8
_get_va_slice_type (GstVaapiPictureType type)
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

static inline gboolean
_read_sps_attributes (const guint8 * sps_data,
    guint32 sps_size,
    guint32 * profile_idc, guint32 * profile_comp, guint32 * level_idc)
{
  g_assert (profile_idc && profile_comp && level_idc);
  g_assert (sps_size >= 4);
  if (sps_size < 4) {
    return FALSE;
  }
  /* skip sps_data[0], nal_type */
  *profile_idc = sps_data[1];
  *profile_comp = sps_data[2];
  *level_idc = sps_data[3];
  return TRUE;
}

static inline guint
_get_log2_max_frame_num (guint num)
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

static inline guint
_profile_to_value (GstVaapiProfile profile)
{
  switch (profile) {
    case GST_VAAPI_PROFILE_H264_BASELINE:
      return 66;
    case GST_VAAPI_PROFILE_H264_MAIN:
      return 77;
    case GST_VAAPI_PROFILE_H264_HIGH:
      return 100;
    default:
      break;
  }
  return 0;
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

static void
_set_level (GstVaapiEncoderH264 * encoder)
{
  guint pic_mb_size;
  guint MaxDpbMbs, MaxMBPS;
  guint dbp_level, mbps_level, profile_level;

  if (encoder->level) {
    if (encoder->level < GST_VAAPI_ENCODER_H264_LEVEL_10)
      encoder->level = GST_VAAPI_ENCODER_H264_LEVEL_10;
    else if (encoder->level > GST_VAAPI_ENCODER_H264_LEVEL_51)
      encoder->level = GST_VAAPI_ENCODER_H264_LEVEL_51;
    return;
  }

  /* calculate level */
  pic_mb_size = ((GST_VAAPI_ENCODER_WIDTH (encoder) + 15) / 16) *
      ((GST_VAAPI_ENCODER_HEIGHT (encoder) + 15) / 16);
  MaxDpbMbs = pic_mb_size * ((encoder->b_frame_num) ? 2 : 1);
  MaxMBPS = pic_mb_size * GST_VAAPI_ENCODER_FPS_N (encoder) /
      GST_VAAPI_ENCODER_FPS_D (encoder);

  /* calculate from MaxDbpMbs */
  if (MaxDpbMbs > 110400)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_51;
  else if (MaxDpbMbs > 34816)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_50;
  else if (MaxDpbMbs > 32768)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_42;
  else if (MaxDpbMbs > 20480)   /* 41 or 40 */
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_41;
  else if (MaxDpbMbs > 18000)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_32;
  else if (MaxDpbMbs > 8100)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_31;
  else if (MaxDpbMbs > 4752)    /* 30 or 22 */
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_30;
  else if (MaxDpbMbs > 2376)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_21;
  else if (MaxDpbMbs > 900)     /* 20, 13, 12 */
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_20;
  else if (MaxDpbMbs > 396)
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_11;
  else
    dbp_level = GST_VAAPI_ENCODER_H264_LEVEL_10;

  /* calculate from Max Mb processing rate */
  if (MaxMBPS > 589824)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_51;
  else if (MaxMBPS > 522240)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_50;
  else if (MaxMBPS > 245760)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_42;
  else if (MaxMBPS > 216000)    /* 40 or 41 */
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_41;
  else if (MaxMBPS > 108000)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_32;
  else if (MaxMBPS > 40500)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_31;
  else if (MaxMBPS > 20250)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_30;
  else if (MaxMBPS > 19800)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_22;
  else if (MaxMBPS > 11800)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_21;
  else if (MaxMBPS > 6000)      /*13 or 20 */
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_20;
  else if (MaxMBPS > 3000)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_12;
  else if (MaxMBPS > 1485)
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_11;
  else
    mbps_level = GST_VAAPI_ENCODER_H264_LEVEL_10;

  if (encoder->profile == GST_VAAPI_PROFILE_H264_HIGH)
    profile_level = GST_VAAPI_ENCODER_H264_LEVEL_41;
  else if (encoder->profile == GST_VAAPI_PROFILE_H264_MAIN)
    profile_level = GST_VAAPI_ENCODER_H264_LEVEL_30;
  else
    profile_level = GST_VAAPI_ENCODER_H264_LEVEL_20;

  encoder->level = (dbp_level > mbps_level ? dbp_level : mbps_level);
  if (encoder->level < profile_level)
    encoder->level = profile_level;
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
    VAEncSequenceParameterBufferH264 * seq, GstVaapiProfile profile)
{
  guint32 constraint_set0_flag, constraint_set1_flag;
  guint32 constraint_set2_flag, constraint_set3_flag;
  guint32 gaps_in_frame_num_value_allowed_flag = 0;     // ??
  gboolean nal_hrd_parameters_present_flag;

  guint32 b_qpprime_y_zero_transform_bypass = 0;
  guint32 residual_color_transform_flag = 0;
  guint32 pic_height_in_map_units =
      (seq->seq_fields.bits.frame_mbs_only_flag ?
      seq->picture_height_in_mbs : seq->picture_height_in_mbs / 2);
  guint32 mb_adaptive_frame_field = !seq->seq_fields.bits.frame_mbs_only_flag;
  guint32 i = 0;

  constraint_set0_flag = profile == GST_VAAPI_PROFILE_H264_BASELINE;
  constraint_set1_flag = profile <= GST_VAAPI_PROFILE_H264_MAIN;
  constraint_set2_flag = 0;
  constraint_set3_flag = 0;

  /* profile_idc */
  gst_bit_writer_put_bits_uint32 (bitwriter, _profile_to_value (profile), 8);
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
  gst_bit_writer_put_bits_uint32 (bitwriter, seq->level_idc, 8);
  /* seq_parameter_set_id */
  gst_bit_writer_put_ue (bitwriter, seq->seq_parameter_set_id);

  if (profile == GST_VAAPI_PROFILE_H264_HIGH) {
    /* for high profile */
    /* chroma_format_idc  = 1, 4:2:0 */
    gst_bit_writer_put_ue (bitwriter, seq->seq_fields.bits.chroma_format_idc);
    if (3 == seq->seq_fields.bits.chroma_format_idc) {
      gst_bit_writer_put_bits_uint32 (bitwriter, residual_color_transform_flag,
          1);
    }
    /* bit_depth_luma_minus8 */
    gst_bit_writer_put_ue (bitwriter, seq->bit_depth_luma_minus8);
    /* bit_depth_chroma_minus8 */
    gst_bit_writer_put_ue (bitwriter, seq->bit_depth_chroma_minus8);
    /* b_qpprime_y_zero_transform_bypass */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        b_qpprime_y_zero_transform_bypass, 1);
    g_assert (seq->seq_fields.bits.seq_scaling_matrix_present_flag == 0);
    /*seq_scaling_matrix_present_flag  */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->seq_fields.bits.seq_scaling_matrix_present_flag, 1);

#if 0
    if (seq->seq_fields.bits.seq_scaling_matrix_present_flag) {
      for (i = 0; i < (seq->seq_fields.bits.chroma_format_idc != 3 ? 8 : 12);
          i++) {
        gst_bit_writer_put_bits_uint8 (bitwriter,
            seq->seq_fields.bits.seq_scaling_list_present_flag, 1);
        if (seq->seq_fields.bits.seq_scaling_list_present_flag) {
          g_assert (0);
          /* FIXME, need write scaling list if seq_scaling_matrix_present_flag ==1 */
        }
      }
    }
#endif
  }

  /* log2_max_frame_num_minus4 */
  gst_bit_writer_put_ue (bitwriter,
      seq->seq_fields.bits.log2_max_frame_num_minus4);
  /* pic_order_cnt_type */
  gst_bit_writer_put_ue (bitwriter, seq->seq_fields.bits.pic_order_cnt_type);

  if (seq->seq_fields.bits.pic_order_cnt_type == 0) {
    /* log2_max_pic_order_cnt_lsb_minus4 */
    gst_bit_writer_put_ue (bitwriter,
        seq->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
  } else if (seq->seq_fields.bits.pic_order_cnt_type == 1) {
    g_assert (0);
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->seq_fields.bits.delta_pic_order_always_zero_flag, 1);
    gst_bit_writer_put_se (bitwriter, seq->offset_for_non_ref_pic);
    gst_bit_writer_put_se (bitwriter, seq->offset_for_top_to_bottom_field);
    gst_bit_writer_put_ue (bitwriter,
        seq->num_ref_frames_in_pic_order_cnt_cycle);
    for (i = 0; i < seq->num_ref_frames_in_pic_order_cnt_cycle; i++) {
      gst_bit_writer_put_se (bitwriter, seq->offset_for_ref_frame[i]);
    }
  }

  /* num_ref_frames */
  gst_bit_writer_put_ue (bitwriter, seq->max_num_ref_frames);
  /* gaps_in_frame_num_value_allowed_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      gaps_in_frame_num_value_allowed_flag, 1);

  /* pic_width_in_mbs_minus1 */
  gst_bit_writer_put_ue (bitwriter, seq->picture_width_in_mbs - 1);
  /* pic_height_in_map_units_minus1 */
  gst_bit_writer_put_ue (bitwriter, pic_height_in_map_units - 1);
  /* frame_mbs_only_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      seq->seq_fields.bits.frame_mbs_only_flag, 1);

  if (!seq->seq_fields.bits.frame_mbs_only_flag) {      //ONLY mbs
    g_assert (0);
    gst_bit_writer_put_bits_uint32 (bitwriter, mb_adaptive_frame_field, 1);
  }

  /* direct_8x8_inference_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, 0, 1);
  /* frame_cropping_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, seq->frame_cropping_flag, 1);

  if (seq->frame_cropping_flag) {
    /* frame_crop_left_offset */
    gst_bit_writer_put_ue (bitwriter, seq->frame_crop_left_offset);
    /* frame_crop_right_offset */
    gst_bit_writer_put_ue (bitwriter, seq->frame_crop_right_offset);
    /* frame_crop_top_offset */
    gst_bit_writer_put_ue (bitwriter, seq->frame_crop_top_offset);
    /* frame_crop_bottom_offset */
    gst_bit_writer_put_ue (bitwriter, seq->frame_crop_bottom_offset);
  }

  /* vui_parameters_present_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter, seq->vui_parameters_present_flag,
      1);
  if (seq->vui_parameters_present_flag) {
    /* aspect_ratio_info_present_flag */
    gst_bit_writer_put_bits_uint32 (bitwriter,
        seq->vui_fields.bits.aspect_ratio_info_present_flag, 1);
    if (seq->vui_fields.bits.aspect_ratio_info_present_flag) {
      gst_bit_writer_put_bits_uint32 (bitwriter, seq->aspect_ratio_idc, 8);
      if (seq->aspect_ratio_idc == 0xFF) {
        gst_bit_writer_put_bits_uint32 (bitwriter, seq->sar_width, 16);
        gst_bit_writer_put_bits_uint32 (bitwriter, seq->sar_height, 16);
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
        seq->vui_fields.bits.timing_info_present_flag, 1);
    if (seq->vui_fields.bits.timing_info_present_flag) {
      gst_bit_writer_put_bits_uint32 (bitwriter, seq->num_units_in_tick, 32);
      gst_bit_writer_put_bits_uint32 (bitwriter, seq->time_scale, 32);
      gst_bit_writer_put_bits_uint32 (bitwriter, 1, 1); /* fixed_frame_rate_flag */
    }

    nal_hrd_parameters_present_flag = (seq->bits_per_second > 0 ? TRUE : FALSE);
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
        gst_bit_writer_put_ue (bitwriter, seq->bits_per_second / 1024 - 1);
        /* cpb_size_value_minus1[0] */
        gst_bit_writer_put_ue (bitwriter, seq->bits_per_second / 1024 * 8 - 1);
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
    VAEncPictureParameterBufferH264 * pic)
{
  guint32 num_slice_groups_minus1 = 0;
  guint32 pic_init_qs_minus26 = 0;
  guint32 redundant_pic_cnt_present_flag = 0;

  /* pic_parameter_set_id */
  gst_bit_writer_put_ue (bitwriter, pic->pic_parameter_set_id);
  /* seq_parameter_set_id */
  gst_bit_writer_put_ue (bitwriter, pic->seq_parameter_set_id);
  /* entropy_coding_mode_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.entropy_coding_mode_flag, 1);
  /* pic_order_present_flag */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.pic_order_present_flag, 1);
  /*slice_groups-1 */
  gst_bit_writer_put_ue (bitwriter, num_slice_groups_minus1);

  if (num_slice_groups_minus1 > 0) {
     /*FIXME*/ g_assert (0);
  }
  gst_bit_writer_put_ue (bitwriter, pic->num_ref_idx_l0_active_minus1);
  gst_bit_writer_put_ue (bitwriter, pic->num_ref_idx_l1_active_minus1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.weighted_pred_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.weighted_bipred_idc, 2);
  /* pic_init_qp_minus26 */
  gst_bit_writer_put_se (bitwriter, pic->pic_init_qp - 26);
  /* pic_init_qs_minus26 */
  gst_bit_writer_put_se (bitwriter, pic_init_qs_minus26);
  /*chroma_qp_index_offset */
  gst_bit_writer_put_se (bitwriter, pic->chroma_qp_index_offset);

  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.deblocking_filter_control_present_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.constrained_intra_pred_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter, redundant_pic_cnt_present_flag, 1);

  /*more_rbsp_data */
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.transform_8x8_mode_flag, 1);
  gst_bit_writer_put_bits_uint32 (bitwriter,
      pic->pic_fields.bits.pic_scaling_matrix_present_flag, 1);
  if (pic->pic_fields.bits.pic_scaling_matrix_present_flag) {
    g_assert (0);
    /* FIXME */
    /*
       for (i = 0; i <
       (6+(-( (chroma_format_idc ! = 3) ? 2 : 6) * -pic->pic_fields.bits.transform_8x8_mode_flag));
       i++) {
       gst_bit_writer_put_bits_uint8(bitwriter, pic->pic_fields.bits.pic_scaling_list_present_flag, 1);
       }
     */
  }

  gst_bit_writer_put_se (bitwriter, pic->second_chroma_qp_index_offset);
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
  VAEncSequenceParameterBufferH264 *seq_param = sequence->param;
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
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & packed_seq, NULL);

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
  VAEncPictureParameterBufferH264 *pic_param = picture->param;
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
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & packed_pic, NULL);

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
  GstVaapiEncoderH264Ref *ref = g_slice_new0 (GstVaapiEncoderH264Ref);

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
  } else if (g_queue_get_length (&encoder->ref_list) >= encoder->max_ref_num) {
    reference_pic_free (encoder, g_queue_pop_head (&encoder->ref_list));
  }
  ref = reference_pic_create (encoder, picture, surface);
  g_queue_push_tail (&encoder->ref_list, ref);
  g_assert (g_queue_get_length (&encoder->ref_list) <= encoder->max_ref_num);
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
  VAEncSequenceParameterBufferH264 *seq = sequence->param;
  guint width_in_mbs, height_in_mbs;

  width_in_mbs = (GST_VAAPI_ENCODER_WIDTH (encoder) + 15) / 16;
  height_in_mbs = (GST_VAAPI_ENCODER_HEIGHT (encoder) + 15) / 16;

  memset (seq, 0, sizeof (VAEncSequenceParameterBufferH264));
  seq->seq_parameter_set_id = 0;
  seq->level_idc = encoder->level;
  seq->intra_period = encoder->intra_period;
  seq->ip_period = 0;           // ?
  if (encoder->bitrate > 0)
    seq->bits_per_second = encoder->bitrate * 1024;
  else
    seq->bits_per_second = 0;

  seq->max_num_ref_frames = encoder->max_ref_num;
  seq->picture_width_in_mbs = width_in_mbs;
  seq->picture_height_in_mbs = height_in_mbs;

  /*sequence field values */
  seq->seq_fields.value = 0;
  seq->seq_fields.bits.chroma_format_idc = 1;
  seq->seq_fields.bits.frame_mbs_only_flag = 1;
  seq->seq_fields.bits.mb_adaptive_frame_field_flag = FALSE;
  seq->seq_fields.bits.seq_scaling_matrix_present_flag = FALSE;
  /* direct_8x8_inference_flag default false */
  seq->seq_fields.bits.direct_8x8_inference_flag = FALSE;
  g_assert (encoder->log2_max_frame_num >= 4);
  seq->seq_fields.bits.log2_max_frame_num_minus4 =
      encoder->log2_max_frame_num - 4;
  /* picture order count */
  seq->seq_fields.bits.pic_order_cnt_type = 0;
  g_assert (encoder->log2_max_pic_order_cnt >= 4);
  seq->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 =
      encoder->log2_max_pic_order_cnt - 4;

  seq->bit_depth_luma_minus8 = 0;
  seq->bit_depth_chroma_minus8 = 0;

  /* not used if pic_order_cnt_type == 0 */
  if (seq->seq_fields.bits.pic_order_cnt_type == 1) {
    seq->seq_fields.bits.delta_pic_order_always_zero_flag = TRUE;
    seq->num_ref_frames_in_pic_order_cnt_cycle = 0;
    seq->offset_for_non_ref_pic = 0;
    seq->offset_for_top_to_bottom_field = 0;
    memset (seq->offset_for_ref_frame, 0, sizeof (seq->offset_for_ref_frame));
  }

  if (height_in_mbs * 16 - GST_VAAPI_ENCODER_HEIGHT (encoder)) {
    seq->frame_cropping_flag = 1;
    seq->frame_crop_left_offset = 0;
    seq->frame_crop_right_offset = 0;
    seq->frame_crop_top_offset = 0;
    seq->frame_crop_bottom_offset =
        ((height_in_mbs * 16 - GST_VAAPI_ENCODER_HEIGHT (encoder)) /
        (2 * (!seq->seq_fields.bits.frame_mbs_only_flag + 1)));
  }

  /*vui not set */
  seq->vui_parameters_present_flag = (encoder->bitrate > 0 ? TRUE : FALSE);
  if (seq->vui_parameters_present_flag) {
    seq->vui_fields.bits.aspect_ratio_info_present_flag = FALSE;
    seq->vui_fields.bits.bitstream_restriction_flag = FALSE;
    seq->vui_fields.bits.timing_info_present_flag =
        (encoder->bitrate > 0 ? TRUE : FALSE);
    if (seq->vui_fields.bits.timing_info_present_flag) {
      seq->num_units_in_tick = GST_VAAPI_ENCODER_FPS_D (encoder);
      seq->time_scale = GST_VAAPI_ENCODER_FPS_N (encoder) * 2;
    }
  }

  return TRUE;
}

static gboolean
fill_va_picture_param (GstVaapiEncoderH264 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferH264 *pic = picture->param;
  GstVaapiEncoderH264Ref *ref_pic;
  GList *reflist;
  guint i;

  memset (pic, 0, sizeof (VAEncPictureParameterBufferH264));

  /* reference list,  */
  pic->CurrPic.picture_id = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic->CurrPic.TopFieldOrderCnt = picture->poc;
  i = 0;
  if (picture->type != GST_VAAPI_PICTURE_TYPE_I) {
    for (reflist = g_queue_peek_head_link (&encoder->ref_list);
        reflist; reflist = g_list_next (reflist)) {
      ref_pic = reflist->data;
      g_assert (ref_pic && ref_pic->pic &&
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic) != VA_INVALID_ID);

      pic->ReferenceFrames[i].picture_id =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (ref_pic->pic);
      ++i;
    }
    g_assert (i <= 16 && i <= encoder->max_ref_num);
  }
  for (; i < 16; ++i) {
    pic->ReferenceFrames[i].picture_id = VA_INVALID_ID;
  }
  pic->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

  pic->pic_parameter_set_id = 0;
  pic->seq_parameter_set_id = 0;
  pic->last_picture = 0;        /* means last encoding picture */
  pic->frame_num = picture->frame_num;
  pic->pic_init_qp = encoder->init_qp;
  pic->num_ref_idx_l0_active_minus1 =
      (encoder->max_reflist0_count ? (encoder->max_reflist0_count - 1) : 0);
  pic->num_ref_idx_l1_active_minus1 =
      (encoder->max_reflist1_count ? (encoder->max_reflist1_count - 1) : 0);
  pic->chroma_qp_index_offset = 0;
  pic->second_chroma_qp_index_offset = 0;

  /* set picture fields */
  pic->pic_fields.value = 0;
  pic->pic_fields.bits.idr_pic_flag = GST_VAAPI_ENC_PICTURE_IS_IDR (picture);
  pic->pic_fields.bits.reference_pic_flag =
      (picture->type != GST_VAAPI_PICTURE_TYPE_B);
  pic->pic_fields.bits.entropy_coding_mode_flag =
      GST_VAAPI_ENCODER_H264_ENTROPY_MODE_CABAC;
  pic->pic_fields.bits.weighted_pred_flag = FALSE;
  pic->pic_fields.bits.weighted_bipred_idc = 0;
  pic->pic_fields.bits.constrained_intra_pred_flag = 0;
  pic->pic_fields.bits.transform_8x8_mode_flag = (encoder->profile >= GST_VAAPI_PROFILE_H264_HIGH);     /* enable 8x8 */
  /* enable debloking */
  pic->pic_fields.bits.deblocking_filter_control_present_flag = TRUE;
  pic->pic_fields.bits.redundant_pic_cnt_present_flag = FALSE;
  /* bottom_field_pic_order_in_frame_present_flag */
  pic->pic_fields.bits.pic_order_present_flag = FALSE;
  pic->pic_fields.bits.pic_scaling_matrix_present_flag = FALSE;

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
  guint width_in_mbs, height_in_mbs;
  guint slice_of_mbs, slice_mod_mbs, cur_slice_mbs;
  guint total_mbs;
  guint last_mb_index;
  guint i_slice, i_ref;

  g_assert (picture);

  width_in_mbs = (GST_VAAPI_ENCODER_WIDTH (encoder) + 15) / 16;
  height_in_mbs = (GST_VAAPI_ENCODER_HEIGHT (encoder) + 15) / 16;
  total_mbs = width_in_mbs * height_in_mbs;

  g_assert (encoder->slice_num && encoder->slice_num < total_mbs);
  slice_of_mbs = total_mbs / encoder->slice_num;
  slice_mod_mbs = total_mbs % encoder->slice_num;
  last_mb_index = 0;
  for (i_slice = 0; i_slice < encoder->slice_num; ++i_slice) {
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
    slice_param->slice_type = _get_va_slice_type (picture->type);
    g_assert (slice_param->slice_type != -1);
    slice_param->pic_parameter_set_id = 0;
    slice_param->idr_pic_id = encoder->idr_num;
    slice_param->pic_order_cnt_lsb = picture->poc;

    /* not used if pic_order_cnt_type = 0 */
    slice_param->delta_pic_order_cnt_bottom = 0;
    memset (slice_param->delta_pic_order_cnt,
        0, sizeof (slice_param->delta_pic_order_cnt));

    /*only works for B frames */
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
    for (;
        i_ref <
        sizeof (slice_param->RefPicList0) /
        sizeof (slice_param->RefPicList0[0]); ++i_ref) {
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
    for (;
        i_ref <
        sizeof (slice_param->RefPicList1) /
        sizeof (slice_param->RefPicList1[0]); ++i_ref) {
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
    gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & slice, NULL);

  }
  g_assert (last_mb_index == total_mbs);
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
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (&sequence), NULL);
  return TRUE;

error:
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (&sequence), NULL);
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

  g_assert (reflist_0_count + reflist_1_count <= encoder->max_ref_num);
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
  if (encoder->bitrate > 0) {
    hrd->initial_buffer_fullness = encoder->bitrate * 1024 * 4;
    hrd->buffer_size = encoder->bitrate * 1024 * 8;
  } else {
    hrd->initial_buffer_fullness = 0;
    hrd->buffer_size = 0;
  }
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & misc, NULL);

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
    if (encoder->bitrate)
      rate_control->bits_per_second = encoder->bitrate * 1024;
    else
      rate_control->bits_per_second = 0;
    rate_control->target_percentage = 70;
    rate_control->window_size = 500;
    rate_control->initial_qp = encoder->init_qp;
    rate_control->min_qp = encoder->min_qp;
    rate_control->basic_unit_size = 0;
    gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) & misc, NULL);
  }

  return TRUE;
}

gboolean
init_encoder_public_attributes (GstVaapiEncoderH264 * encoder)
{
  guint width_mbs, height_mbs, total_mbs;

  if (!GST_VAAPI_ENCODER_WIDTH (encoder) ||
      !GST_VAAPI_ENCODER_HEIGHT (encoder) ||
      !GST_VAAPI_ENCODER_FPS_N (encoder) ||
      !GST_VAAPI_ENCODER_FPS_D (encoder)) {
    return FALSE;
  }
  if (!encoder->profile)
    encoder->profile = GST_VAAPI_ENCODER_H264_DEFAULT_PROFILE;

  _set_level (encoder);

  if (!encoder->intra_period)
    encoder->intra_period = GST_VAAPI_ENCODER_H264_DEFAULT_INTRA_PERIOD;
  else if (encoder->intra_period > GST_VAAPI_ENCODER_H264_MAX_INTRA_PERIOD)
    encoder->intra_period = GST_VAAPI_ENCODER_H264_MAX_INTRA_PERIOD;

  if (encoder->idr_period < encoder->intra_period)
    encoder->idr_period = encoder->intra_period;
  if (encoder->idr_period > GST_VAAPI_ENCODER_H264_MAX_IDR_PERIOD)
    encoder->idr_period = GST_VAAPI_ENCODER_H264_MAX_IDR_PERIOD;

  if (-1 == encoder->init_qp)
    encoder->init_qp = GST_VAAPI_ENCODER_H264_DEFAULT_INIT_QP;

  if (-1 == encoder->min_qp) {
    if (GST_VAAPI_RATECONTROL_CQP == GST_VAAPI_ENCODER_RATE_CONTROL (encoder))
      encoder->min_qp = encoder->init_qp;
    else
      encoder->min_qp = GST_VAAPI_ENCODER_H264_DEFAULT_MIN_QP;
  }

  if (encoder->min_qp > encoder->init_qp)
    encoder->min_qp = encoder->init_qp;

  /* default compress ratio 1: (4*8*1.5) */
  if (GST_VAAPI_RATECONTROL_CBR == GST_VAAPI_ENCODER_RATE_CONTROL (encoder) ||
      GST_VAAPI_RATECONTROL_VBR == GST_VAAPI_ENCODER_RATE_CONTROL (encoder) ||
      GST_VAAPI_RATECONTROL_VBR_CONSTRAINED ==
      GST_VAAPI_ENCODER_RATE_CONTROL (encoder)) {
    if (!encoder->bitrate)
      encoder->bitrate = GST_VAAPI_ENCODER_WIDTH (encoder) *
          GST_VAAPI_ENCODER_HEIGHT (encoder) *
          GST_VAAPI_ENCODER_FPS_N (encoder) /
          GST_VAAPI_ENCODER_FPS_D (encoder) / 4 / 1024;
  } else
    encoder->bitrate = 0;

  if (!encoder->slice_num)
    encoder->slice_num = GST_VAAPI_ENCODER_H264_DEFAULT_SLICE_NUM;

  width_mbs = (GST_VAAPI_ENCODER_WIDTH (encoder) + 15) / 16;
  height_mbs = (GST_VAAPI_ENCODER_HEIGHT (encoder) + 15) / 16;
  total_mbs = width_mbs * height_mbs;

  if (encoder->slice_num > (total_mbs + 1) / 2)
    encoder->slice_num = (total_mbs + 1) / 2;
  g_assert (encoder->slice_num);

  if (encoder->b_frame_num > (encoder->intra_period + 1) / 2)
    encoder->b_frame_num = (encoder->intra_period + 1) / 2;

  if (encoder->b_frame_num > 50)
    encoder->b_frame_num = 50;

  return TRUE;
}

static gboolean
init_encoder_private_attributes (GstVaapiEncoderH264 * encoder, GstCaps * caps)
{
  if (encoder->b_frame_num)
    encoder->cts_offset = GST_SECOND * GST_VAAPI_ENCODER_FPS_D (encoder) /
        GST_VAAPI_ENCODER_FPS_N (encoder);
  else
    encoder->cts_offset = 0;

  /* init max_frame_num, max_poc */
  encoder->log2_max_frame_num = _get_log2_max_frame_num (encoder->idr_period);
  g_assert (encoder->log2_max_frame_num >= 4);
  encoder->max_frame_num = (1 << encoder->log2_max_frame_num);
  encoder->log2_max_pic_order_cnt = encoder->log2_max_frame_num + 1;
  encoder->max_pic_order_cnt = (1 << encoder->log2_max_pic_order_cnt);

  encoder->frame_index = 0;
  encoder->idr_num = 0;
  encoder->max_reflist0_count = 1;
  if (encoder->b_frame_num)
    encoder->max_reflist1_count = 1;
  else
    encoder->max_reflist1_count = 0;
  encoder->max_ref_num =
      encoder->max_reflist0_count + encoder->max_reflist1_count;
  return TRUE;
}


static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_encode (GstVaapiEncoder * base,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST (base);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base);

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
gst_vaapi_encoder_h264_flush (GstVaapiEncoder * base)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST (base);
  GstVaapiEncPicture *pic;

  encoder->frame_index = 0;
  encoder->cur_frame_num = 0;
  encoder->cur_present_index = 0;
  while (!g_queue_is_empty (&encoder->reorder_frame_list)) {
    pic =
        (GstVaapiEncPicture *) g_queue_pop_head (&encoder->reorder_frame_list);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->reorder_frame_list);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_get_avcC_codec_data (GstVaapiEncoderH264 * encoder,
    GstBuffer ** buffer)
{
  GstBuffer *avc_codec;
  const guint32 configuration_version = 0x01;
  const guint32 length_size_minus_one = 0x03;
  guint32 profile, profile_comp, level_idc;
  GstMapInfo sps_info, pps_info;
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstBitWriter writer;

  g_assert (buffer);
  if (!encoder->sps_data || !encoder->pps_data)
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER;

  if (!gst_buffer_map (encoder->sps_data, &sps_info, GST_MAP_READ))
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;

  if (FALSE == _read_sps_attributes (sps_info.data, sps_info.size,
          &profile, &profile_comp, &level_idc)) {
    ret = GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER;
    goto end;
  }

  if (!gst_buffer_map (encoder->pps_data, &pps_info, GST_MAP_READ)) {
    ret = GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
    goto end;
  }

  gst_bit_writer_init (&writer, (sps_info.size + pps_info.size + 64) * 8);
  /* codec_data */
  gst_bit_writer_put_bits_uint32 (&writer, configuration_version, 8);
  gst_bit_writer_put_bits_uint32 (&writer, profile, 8);
  gst_bit_writer_put_bits_uint32 (&writer, profile_comp, 8);
  gst_bit_writer_put_bits_uint32 (&writer, level_idc, 8);
  gst_bit_writer_put_bits_uint32 (&writer, 0x3F, 6);    /*111111 */
  gst_bit_writer_put_bits_uint32 (&writer, length_size_minus_one, 2);
  gst_bit_writer_put_bits_uint32 (&writer, 0x07, 3);    /*111 */

  /* write sps */
  gst_bit_writer_put_bits_uint32 (&writer, 1, 5);       /* sps count = 1 */
  g_assert (GST_BIT_WRITER_BIT_SIZE (&writer) % 8 == 0);
  gst_bit_writer_put_bits_uint32 (&writer, sps_info.size, 16);
  gst_bit_writer_put_bytes (&writer, sps_info.data, sps_info.size);

  /* write pps */
  gst_bit_writer_put_bits_uint32 (&writer, 1, 8);       /*pps count = 1 */
  gst_bit_writer_put_bits_uint32 (&writer, pps_info.size, 16);
  gst_bit_writer_put_bytes (&writer, pps_info.data, pps_info.size);

  avc_codec = gst_buffer_new_wrapped (GST_BIT_WRITER_DATA (&writer),
      GST_BIT_WRITER_BIT_SIZE (&writer) / 8);
  g_assert (avc_codec);
  if (!avc_codec) {
    ret = GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
    goto clear_writer;
  }
  *buffer = avc_codec;

  gst_buffer_unmap (encoder->pps_data, &pps_info);
  gst_bit_writer_clear (&writer, FALSE);
  ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  goto end;

clear_writer:
  gst_bit_writer_clear (&writer, TRUE);

end:
  gst_buffer_unmap (encoder->sps_data, &sps_info);

  return ret;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_get_codec_data (GstVaapiEncoder * base,
    GstBuffer ** buffer)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST (base);

  *buffer = NULL;

  if (!encoder->is_avc)
    return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  return gst_vaapi_encoder_h264_get_avcC_codec_data (encoder, buffer);
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_h264_reordering (GstVaapiEncoder * base,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264 (base);
  GstVaapiEncPicture *picture;
  gboolean is_idr = FALSE;

  *output = NULL;

  if (!frame) {
    if (encoder->reorder_state != GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES)
      return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

    /* reorder_state = GST_VAAPI_ENC_H264_REORD_DUMP_FRAMES
       dump B frames from queue, sometime, there may also have P frame or I frame */
    g_assert (encoder->b_frame_num > 0);
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
      (encoder->frame_index % encoder->intra_period) == 0) {
    ++encoder->cur_frame_num;
    ++encoder->frame_index;

    /* b frame enabled,  check queue of reorder_frame_list */
    if (encoder->b_frame_num
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
      if (encoder->b_frame_num)
        encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES;
    }
    goto end;
  }

  /* new p/b frames coming */
  ++encoder->frame_index;
  if (encoder->reorder_state == GST_VAAPI_ENC_H264_REORD_WAIT_FRAMES &&
      g_queue_get_length (&encoder->reorder_frame_list) <
      encoder->b_frame_num) {
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

static gboolean
gst_vaapi_encoder_h264_get_context_info (GstVaapiEncoder * base,
    GstVaapiContextInfo * info)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264 (base);
  const static guint default_surface_num = 3;

  g_return_val_if_fail (info, FALSE);

  info->profile = encoder->profile;
  info->entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  info->width = GST_VAAPI_ENCODER_WIDTH (encoder);
  info->height = GST_VAAPI_ENCODER_HEIGHT (encoder);
  info->ref_frames = (encoder->b_frame_num ? 2 : 1) + default_surface_num;
  info->rc_mode = GST_VAAPI_ENCODER_RATE_CONTROL (encoder);

  return TRUE;
}

static GstCaps *
gst_vaapi_encoder_h264_set_format (GstVaapiEncoder * base,
    GstVideoCodecState * in_state, GstCaps * ref_caps)
{
  GstVaapiEncoderH264 *encoder;
  GstCaps *result = NULL, *tmp;
  GstStructure *structure;
  const GValue *value;
  const gchar *stream_format;

  encoder = GST_VAAPI_ENCODER_H264 (base);

  tmp = gst_caps_from_string ("video/x-h264");
  gst_caps_set_simple (tmp,
      "width", G_TYPE_INT, GST_VAAPI_ENCODER_WIDTH (encoder),
      "height", G_TYPE_INT, GST_VAAPI_ENCODER_HEIGHT (encoder),
      "framerate", GST_TYPE_FRACTION,
      GST_VAAPI_ENCODER_FPS_N (encoder), GST_VAAPI_ENCODER_FPS_D (encoder),
      NULL);
  result = gst_caps_intersect (tmp, ref_caps);
  gst_caps_unref (tmp);

  /* fixed stream-format and choose byte-stream first */
  structure = gst_caps_get_structure (result, 0);
  value = gst_structure_get_value (structure, "stream-format");
  if (value) {
    gst_structure_fixate_field_string (structure, "stream-format",
        "byte-stream");
    stream_format = gst_structure_get_string (structure, "stream-format");
  } else {
    stream_format = "byte-stream";
    gst_structure_set (structure, "stream-format", G_TYPE_STRING, stream_format,
        NULL);
  }

  if (strcmp (stream_format, "byte-stream") == 0)
    encoder->is_avc = FALSE;
  else                          /* need codec data later */
    encoder->is_avc = TRUE;

#if GST_CHECK_VERSION(1,0,0)
  result = gst_caps_fixate (result);
#endif

  if (!init_encoder_public_attributes (encoder)) {
    GST_WARNING ("encoder ensure public attributes failed ");
    goto error;
  }

  if (!init_encoder_private_attributes (encoder, result)) {
    GST_WARNING ("prepare encoding failed ");
    goto error;
  }

  return result;

error:
  gst_caps_unref (result);
  return NULL;
}

static gboolean
gst_vaapi_encoder_h264_init (GstVaapiEncoder * base)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264 (base);

  /* init attributes */
  encoder->profile = 0;
  encoder->level = 0;
  encoder->bitrate = 0;
  encoder->idr_period = 0;
  encoder->intra_period = 0;
  encoder->init_qp = -1;
  encoder->min_qp = -1;
  encoder->slice_num = 0;
  encoder->b_frame_num = 0;
  //gst_vaapi_base_encoder_set_frame_notify(GST_VAAPI_BASE_ENCODER(encoder), TRUE);

  /* init private values */
  encoder->is_avc = FALSE;
  /* re-ordering */
  g_queue_init (&encoder->reorder_frame_list);
  encoder->reorder_state = GST_VAAPI_ENC_H264_REORD_NONE;
  encoder->frame_index = 0;
  encoder->cur_frame_num = 0;
  encoder->cur_present_index = 0;

  g_queue_init (&encoder->ref_list);
  encoder->max_ref_num = 0;
  encoder->max_reflist0_count = 1;
  encoder->max_reflist1_count = 1;

  encoder->sps_data = NULL;
  encoder->pps_data = NULL;

  encoder->cts_offset = 0;

  encoder->max_frame_num = 0;
  encoder->log2_max_frame_num = 0;
  encoder->max_pic_order_cnt = 0;
  encoder->log2_max_pic_order_cnt = 0;
  encoder->idr_num = 0;

  return TRUE;
}

static void
gst_vaapi_encoder_h264_finalize (GstVaapiEncoder * base)
{
  /*free private buffers */
  GstVaapiEncoderH264 *const encoder = GST_VAAPI_ENCODER_H264 (base);
  GstVaapiEncPicture *pic;
  GstVaapiEncoderH264Ref *ref;

  gst_buffer_replace (&encoder->sps_data, NULL);
  gst_buffer_replace (&encoder->pps_data, NULL);

  while (!g_queue_is_empty (&encoder->ref_list)) {
    ref = (GstVaapiEncoderH264Ref *) g_queue_pop_head (&encoder->ref_list);
    reference_pic_free (encoder, ref);
  }
  g_queue_clear (&encoder->ref_list);

  while (!g_queue_is_empty (&encoder->reorder_frame_list)) {
    pic =
        (GstVaapiEncPicture *) g_queue_pop_head (&encoder->reorder_frame_list);
    gst_vaapi_enc_picture_unref (pic);
  }
  g_queue_clear (&encoder->reorder_frame_list);

}

static inline const GstVaapiEncoderClass *
gst_vaapi_encoder_h264_class ()
{
  static const GstVaapiEncoderClass GstVaapiEncoderH264Class = {
    GST_VAAPI_ENCODER_CLASS_INIT (H264, h264),
    .get_codec_data = gst_vaapi_encoder_h264_get_codec_data
  };
  return &GstVaapiEncoderH264Class;
}

GstVaapiEncoder *
gst_vaapi_encoder_h264_new (GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_new (gst_vaapi_encoder_h264_class (), display);
}

void
gst_vaapi_encoder_h264_set_avc (GstVaapiEncoderH264 * encoder, gboolean is_avc)
{
  encoder->is_avc = is_avc;
}

gboolean
gst_vaapi_encoder_h264_is_avc (GstVaapiEncoderH264 * encoder)
{
  return encoder->is_avc;
}

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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include "gstnalreader.h"

#include "gsth264parser.h"

/* default scaling_lists according to Table 7-2 */
const guint8 default_4x4_intra[16] =
    { 6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32,
  32, 37, 37, 42
};

const guint8 default_4x4_inter[16] =
    { 10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27,
  27, 30, 30, 34
};

const guint8 default_8x8_intra[64] =
    { 6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18,
  18, 18, 18, 23, 23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27,
  27, 27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31, 31, 33,
  33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

const guint8 default_8x8_inter[64] =
    { 9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19,
  19, 19, 19, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24,
  24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27, 27, 28,
  28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35
};

const guint8 zigzag_8x8[64] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

const guint8 zigzag_4x4[16] = {
  0, 1, 4, 8,
  5, 2, 3, 6,
  9, 12, 13, 10,
  7, 11, 14, 15,
};

#define CHECK_ALLOWED(val, min, max) { \
  if (val < min || val > max) { \
    GST_WARNING ("value not in allowed range. value: %d, range %d-%d", \
                     val, min, max); \
    goto error; \
  } \
}

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint8 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint16 (reader, &val, nbits)) { \
  GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT32(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint32 (reader, &val, nbits)) { \
  GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT64(reader, val, nbits) { \
  if (!gst_nal_reader_get_bits_uint64 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UE(reader, val) { \
  if (!gst_nal_reader_get_ue (reader, &val)) { \
    GST_WARNING ("failed to read UE"); \
    goto error; \
  } \
}

#define READ_UE_ALLOWED(reader, val, min, max) { \
  guint32 tmp; \
  READ_UE (reader, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

#define READ_SE(reader, val) { \
  if (!gst_nal_reader_get_se (reader, &val)) { \
    GST_WARNING ("failed to read SE"); \
    goto error; \
  } \
}

#define READ_SE_ALLOWED(reader, val, min, max) { \
  gint32 tmp; \
  READ_SE (reader, tmp); \
  CHECK_ALLOWED (tmp, min, max); \
  val = tmp; \
}

GST_DEBUG_CATEGORY_STATIC (h264parser_debug);
#define GST_CAT_DEFAULT h264parser_debug

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (h264parser_debug, "h264parser", 0, \
    "H264 parser");

G_DEFINE_TYPE_WITH_CODE (GstH264Parser, gst_h264_parser, G_TYPE_OBJECT,
    _do_init);

static void
gst_h264_sequence_free (void *data)
{
  g_slice_free (GstH264Sequence, data);
}

static gboolean
gst_h264_parse_hrd_parameters (GstH264HRDParameters * hrd,
    GstNalReader * reader)
{
  guint SchedSelIdx;

  GST_DEBUG ("parsing \"HRD Parameters\"");

  READ_UE_ALLOWED (reader, hrd->cpb_cnt_minus1, 0, 31);
  READ_UINT8 (reader, hrd->bit_rate_scale, 4);
  READ_UINT8 (reader, hrd->cpb_size_scale, 4);

  for (SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
    READ_UE (reader, hrd->bit_rate_value_minus1[SchedSelIdx]);
    READ_UE (reader, hrd->cpb_size_value_minus1[SchedSelIdx]);
  }

  READ_UINT8 (reader, hrd->initial_cpb_removal_delay_length_minus1, 5);
  READ_UINT8 (reader, hrd->cpb_removal_delay_length_minus1, 5);
  READ_UINT8 (reader, hrd->dpb_output_delay_length_minus1, 5);
  READ_UINT8 (reader, hrd->time_offset_length, 5);

  return TRUE;

error:
  GST_WARNING ("error parsing \"HRD Parameters\"");
  return FALSE;

}

static gboolean
gst_h264_parse_vui_parameters (GstH264VUIParameters * vui,
    GstNalReader * reader)
{
  guint8 aspect_ratio_info_present_flag;
  guint8 video_signal_type_present_flag;
  guint8 chroma_loc_info_present_flag;

  GST_DEBUG ("parsing \"VUI Parameters\"");

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  vui->aspect_ratio_idc = 0;
  vui->video_format = 5;
  vui->video_full_range_flag = 0;
  vui->colour_primaries = 2;
  vui->transfer_characteristics = 2;
  vui->matrix_coefficients = 2;
  vui->chroma_sample_loc_type_top_field = 0;
  vui->chroma_sample_loc_type_bottom_field = 0;
  vui->low_delay_hrd_flag = 0;

  READ_UINT8 (reader, aspect_ratio_info_present_flag, 1);
  if (aspect_ratio_info_present_flag) {
    READ_UINT8 (reader, vui->aspect_ratio_idc, 8);
    if (vui->aspect_ratio_idc == 255) {
      READ_UINT16 (reader, vui->sar_width, 16);
      READ_UINT16 (reader, vui->sar_height, 16);
    }
  }

  READ_UINT8 (reader, vui->overscan_info_present_flag, 1);
  if (vui->overscan_info_present_flag)
    READ_UINT8 (reader, vui->overscan_appropriate_flag, 1);

  READ_UINT8 (reader, video_signal_type_present_flag, 1);
  if (video_signal_type_present_flag) {
    guint8 colour_description_present_flag;

    READ_UINT8 (reader, vui->video_format, 3);
    READ_UINT8 (reader, vui->video_full_range_flag, 1);
    READ_UINT8 (reader, colour_description_present_flag, 1);
    if (colour_description_present_flag) {
      READ_UINT8 (reader, vui->colour_primaries, 8);
      READ_UINT8 (reader, vui->transfer_characteristics, 8);
      READ_UINT8 (reader, vui->matrix_coefficients, 8);
    }
  }

  READ_UINT8 (reader, chroma_loc_info_present_flag, 1);
  if (chroma_loc_info_present_flag) {
    READ_UE_ALLOWED (reader, vui->chroma_sample_loc_type_top_field, 0, 5);
    READ_UE_ALLOWED (reader, vui->chroma_sample_loc_type_bottom_field, 0, 5);
  }

  READ_UINT8 (reader, vui->timing_info_present_flag, 1);
  if (vui->timing_info_present_flag) {
    READ_UINT32 (reader, vui->num_units_in_tick, 32);
    if (vui->num_units_in_tick == 0)
      GST_WARNING
          ("num_units_in_tick = 0 detected in stream (incompliant to H.264 E.2.1).");

    READ_UINT32 (reader, vui->time_scale, 32);
    if (vui->time_scale == 0)
      GST_WARNING
          ("time_scale = 0 detected in stream (incompliant to H.264 E.2.1).");

    READ_UINT8 (reader, vui->fixed_frame_rate_flag, 1);
  }

  READ_UINT8 (reader, vui->nal_hrd_parameters_present_flag, 1);
  if (vui->nal_hrd_parameters_present_flag) {
    if (!gst_h264_parse_hrd_parameters (&vui->nal_hrd_parameters, reader))
      goto error;
  }

  READ_UINT8 (reader, vui->vcl_hrd_parameters_present_flag, 1);
  if (vui->vcl_hrd_parameters_present_flag) {
    if (!gst_h264_parse_hrd_parameters (&vui->vcl_hrd_parameters, reader))
      goto error;
  }

  if (vui->nal_hrd_parameters_present_flag ||
      vui->vcl_hrd_parameters_present_flag)
    READ_UINT8 (reader, vui->low_delay_hrd_flag, 1);

  READ_UINT8 (reader, vui->pic_struct_present_flag, 1);

  return TRUE;

error:
  GST_WARNING ("error parsing \"VUI Parameters\"");
  return FALSE;
}

static gboolean
gst_h264_parser_parse_scaling_list (GstNalReader * reader,
    guint8 scaling_lists_4x4[6][16], guint8 scaling_lists_8x8[6][64],
    const guint8 fallback_4x4_inter[16], const guint8 fallback_4x4_intra[16],
    const guint8 fallback_8x8_inter[64], const guint8 fallback_8x8_intra[64],
    guint8 n_lists)
{
  guint i;

  GST_DEBUG ("parsing scaling lists");

  for (i = 0; i < 12; i++) {
    gboolean use_default = FALSE;

    if (i < n_lists) {
      guint8 scaling_list_present_flag;

      READ_UINT8 (reader, scaling_list_present_flag, 1);
      if (scaling_list_present_flag) {
        guint8 *scaling_list;
        const guint8 *scan;
        guint size;
        guint j;
        guint8 last_scale, next_scale;

        if (i < 6) {
          scaling_list = scaling_lists_4x4[i];
          scan = zigzag_4x4;
          size = 16;
        } else {
          scaling_list = scaling_lists_8x8[i - 6];
          scan = zigzag_8x8;
          size = 64;
        }

        last_scale = 8;
        next_scale = 8;
        for (j = 0; j < size; j++) {
          if (next_scale != 0) {
            gint32 delta_scale;

            READ_SE (reader, delta_scale);
            next_scale = (last_scale + delta_scale) & 0xff;
          }
          if (j == 0 && next_scale == 0) {
            use_default = TRUE;
            break;
          }
          last_scale = scaling_list[scan[j]] =
              (next_scale == 0) ? last_scale : next_scale;
        }
      } else
        use_default = TRUE;
    } else
      use_default = TRUE;

    if (use_default) {
      switch (i) {
        case 0:
          memcpy (scaling_lists_4x4[0], fallback_4x4_intra, 16);
          break;
        case 1:
          memcpy (scaling_lists_4x4[1], scaling_lists_4x4[0], 16);
          break;
        case 2:
          memcpy (scaling_lists_4x4[2], scaling_lists_4x4[1], 16);
          break;
        case 3:
          memcpy (scaling_lists_4x4[3], fallback_4x4_inter, 16);
          break;
        case 4:
          memcpy (scaling_lists_4x4[4], scaling_lists_4x4[3], 16);
          break;
        case 5:
          memcpy (scaling_lists_4x4[5], scaling_lists_4x4[4], 16);
          break;
        case 6:
          memcpy (scaling_lists_8x8[0], fallback_8x8_intra, 64);
          break;
        case 7:
          memcpy (scaling_lists_8x8[1], fallback_8x8_inter, 64);
          break;
        case 8:
          memcpy (scaling_lists_8x8[2], scaling_lists_8x8[0], 64);
          break;
        case 9:
          memcpy (scaling_lists_8x8[3], scaling_lists_8x8[1], 64);
          break;
        case 10:
          memcpy (scaling_lists_8x8[4], scaling_lists_8x8[2], 64);
          break;
        case 11:
          memcpy (scaling_lists_8x8[5], scaling_lists_8x8[3], 64);
          break;

        default:
          break;
      }
    }
  }

  return TRUE;

error:

  GST_WARNING ("error parsing scaling lists");
  return FALSE;
}

GstH264Sequence *
gst_h264_parser_parse_sequence (GstH264Parser * parser, guint8 * data,
    guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  GstH264Sequence *seq;
  guint8 frame_cropping_flag;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);

  GST_DEBUG ("parsing \"Sequence parameter set\"");

  seq = g_slice_new (GstH264Sequence);

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  seq->chroma_format_idc = 1;
  seq->separate_colour_plane_flag = 0;
  seq->bit_depth_luma_minus8 = 0;
  seq->bit_depth_chroma_minus8 = 0;
  memset (seq->scaling_lists_4x4, 16, 96);
  memset (seq->scaling_lists_8x8, 16, 384);
  seq->mb_adaptive_frame_field_flag = 0;
  seq->frame_crop_left_offset = 0;
  seq->frame_crop_right_offset = 0;
  seq->frame_crop_top_offset = 0;
  seq->frame_crop_bottom_offset = 0;

  READ_UINT8 (&reader, seq->profile_idc, 8);
  READ_UINT8 (&reader, seq->constraint_set0_flag, 1);
  READ_UINT8 (&reader, seq->constraint_set1_flag, 1);
  READ_UINT8 (&reader, seq->constraint_set2_flag, 1);
  READ_UINT8 (&reader, seq->constraint_set3_flag, 1);

  /* skip reserved_zero_4bits */
  if (!gst_nal_reader_skip (&reader, 4))
    goto error;

  READ_UINT8 (&reader, seq->level_idc, 8);

  READ_UE_ALLOWED (&reader, seq->id, 0, 31);

  if (seq->profile_idc == 100 || seq->profile_idc == 110 ||
      seq->profile_idc == 122 || seq->profile_idc == 244 ||
      seq->profile_idc == 44 || seq->profile_idc == 83 ||
      seq->profile_idc == 86) {
    READ_UE_ALLOWED (&reader, seq->chroma_format_idc, 0, 3);
    if (seq->chroma_format_idc == 3)
      READ_UINT8 (&reader, seq->separate_colour_plane_flag, 1);

    READ_UE_ALLOWED (&reader, seq->bit_depth_luma_minus8, 0, 6);
    READ_UE_ALLOWED (&reader, seq->bit_depth_chroma_minus8, 0, 6);
    READ_UINT8 (&reader, seq->qpprime_y_zero_transform_bypass_flag, 1);

    READ_UINT8 (&reader, seq->scaling_matrix_present_flag, 1);
    if (seq->scaling_matrix_present_flag) {
      guint8 n_lists;

      n_lists = (seq->chroma_format_idc != 3) ? 8 : 12;
      if (!gst_h264_parser_parse_scaling_list (&reader,
              seq->scaling_lists_4x4, seq->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, n_lists))
        goto error;
    }
  }

  READ_UE_ALLOWED (&reader, seq->log2_max_frame_num_minus4, 0, 12);
  /* calculate MaxFrameNum */
  seq->MaxFrameNum = 1 << (seq->log2_max_frame_num_minus4 + 4);

  READ_UE_ALLOWED (&reader, seq->pic_order_cnt_type, 0, 2);
  if (seq->pic_order_cnt_type == 0) {
    READ_UE_ALLOWED (&reader, seq->log2_max_pic_order_cnt_lsb_minus4, 0, 12);
  } else if (seq->pic_order_cnt_type == 1) {
    guint i;

    READ_UINT8 (&reader, seq->delta_pic_order_always_zero_flag, 1);
    READ_SE (&reader, seq->offset_for_non_ref_pic);
    READ_SE (&reader, seq->offset_for_top_to_bottom_field);
    READ_UE_ALLOWED (&reader, seq->num_ref_frames_in_pic_order_cnt_cycle, 0,
        255);
    for (i = 0; i < seq->num_ref_frames_in_pic_order_cnt_cycle; i++)
      READ_SE (&reader, seq->offset_for_ref_frame[i]);
  }

  READ_UE (&reader, seq->num_ref_frames);
  READ_UINT8 (&reader, seq->gaps_in_frame_num_value_allowed_flag, 1);
  READ_UE (&reader, seq->pic_width_in_mbs_minus1);
  READ_UE (&reader, seq->pic_height_in_map_units_minus1);
  READ_UINT8 (&reader, seq->frame_mbs_only_flag, 1);

  if (!seq->frame_mbs_only_flag)
    READ_UINT8 (&reader, seq->mb_adaptive_frame_field_flag, 1);

  READ_UINT8 (&reader, seq->direct_8x8_inference_flag, 1);
  READ_UINT8 (&reader, frame_cropping_flag, 1);
  if (frame_cropping_flag) {
    READ_UE (&reader, seq->frame_crop_left_offset);
    READ_UE (&reader, seq->frame_crop_right_offset);
    READ_UE (&reader, seq->frame_crop_top_offset);
    READ_UE (&reader, seq->frame_crop_bottom_offset);
  }

  READ_UINT8 (&reader, seq->vui_parameters_present_flag, 1);
  if (seq->vui_parameters_present_flag) {
    if (!gst_h264_parse_vui_parameters (&seq->vui_parameters, &reader))
      goto error;
  }

  /* calculate ChromaArrayType */
  if (seq->separate_colour_plane_flag)
    seq->ChromaArrayType = 0;
  else
    seq->ChromaArrayType = seq->chroma_format_idc;

  GST_DEBUG ("adding sequence parameter set with id: %d to hash table",
      seq->id);
  g_hash_table_replace (parser->sequences, &seq->id, seq);
  return seq;

error:
  GST_WARNING ("error parsing \"Sequence parameter set\"");

  gst_h264_sequence_free (seq);
  return NULL;
}

static void
gst_h264_picture_free (void *data)
{
  GstH264Picture *pic = (GstH264Picture *) data;

  if (pic->slice_group_id)
    g_free (pic->slice_group_id);

  g_slice_free (GstH264Picture, data);
}

static gboolean
gst_h264_parser_more_data (GstNalReader * reader)
{
  guint remaining;

  remaining = gst_nal_reader_get_remaining (reader);
  if (remaining == 0)
    return FALSE;

  if (remaining <= 8) {
    guint8 rbsp_stop_one_bit;

    if (!gst_nal_reader_peek_bits_uint8 (reader, &rbsp_stop_one_bit, 1))
      return FALSE;

    if (rbsp_stop_one_bit == 1) {
      guint8 zero_bits;

      if (remaining == 1)
        return FALSE;

      if (!gst_nal_reader_peek_bits_uint8 (reader, &zero_bits, remaining))
        return FALSE;

      if ((zero_bits - (1 << (remaining - 1))) == 0)
        return FALSE;
    }
  }

  return TRUE;
}

GstH264Picture *
gst_h264_parser_parse_picture (GstH264Parser * parser, guint8 * data,
    guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  GstH264Picture *pic;
  gint seq_parameter_set_id;
  GstH264Sequence *seq;
  guint8 pic_scaling_matrix_present_flag;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), NULL);
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (size > 0, NULL);

  GST_DEBUG ("parsing \"Picture parameter set\"");

  pic = g_slice_new (GstH264Picture);

  READ_UE_ALLOWED (&reader, pic->id, 0, 255);
  READ_UE_ALLOWED (&reader, seq_parameter_set_id, 0, 31);
  seq = g_hash_table_lookup (parser->sequences, &seq_parameter_set_id);
  if (!seq) {
    GST_WARNING ("couldn't find associated sequence parameter set with id: %d",
        seq_parameter_set_id);
    goto error;
  }
  pic->sequence = seq;

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  pic->slice_group_id = NULL;
  pic->transform_8x8_mode_flag = 0;
  memcpy (&pic->scaling_lists_4x4, &seq->scaling_lists_4x4, 96);
  memcpy (&pic->scaling_lists_8x8, &seq->scaling_lists_8x8, 384);

  READ_UINT8 (&reader, pic->entropy_coding_mode_flag, 1);
  READ_UINT8 (&reader, pic->pic_order_present_flag, 1);
  READ_UE_ALLOWED (&reader, pic->num_slice_groups_minus1, 0, 7);
  if (pic->num_slice_groups_minus1 > 0) {
    READ_UE_ALLOWED (&reader, pic->slice_group_map_type, 0, 6);
    if (pic->slice_group_map_type == 0) {
      gint i;

      for (i = 0; i <= pic->num_slice_groups_minus1; i++)
        READ_UE (&reader, pic->run_length_minus1[i]);
    } else if (pic->slice_group_map_type == 2) {
      gint i;

      for (i = 0; i <= pic->num_slice_groups_minus1; i++) {
        READ_UE (&reader, pic->top_left[i]);
        READ_UE (&reader, pic->bottom_right[i]);
      }
    } else if (pic->slice_group_map_type >= 3 && pic->slice_group_map_type <= 5) {
      READ_UINT8 (&reader, pic->slice_group_change_direction_flag, 1);
      READ_UE (&reader, pic->slice_group_change_rate_minus1);
    } else if (pic->slice_group_map_type == 6) {
      gint bits;
      gint i;

      READ_UE (&reader, pic->pic_size_in_map_units_minus1);
      bits = g_bit_storage (pic->num_slice_groups_minus1);

      pic->slice_group_id =
          g_new (guint8, pic->pic_size_in_map_units_minus1 + 1);
      for (i = 0; i <= pic->pic_size_in_map_units_minus1; i++)
        READ_UINT8 (&reader, pic->slice_group_id[i], bits);
    }
  }

  READ_UE_ALLOWED (&reader, pic->num_ref_idx_l0_active_minus1, 0, 31);
  READ_UE_ALLOWED (&reader, pic->num_ref_idx_l1_active_minus1, 0, 31);
  READ_UINT8 (&reader, pic->weighted_pred_flag, 1);
  READ_UINT8 (&reader, pic->weighted_bipred_idc, 2);
  READ_SE_ALLOWED (&reader, pic->pic_init_qp_minus26, -26, 25);
  READ_SE_ALLOWED (&reader, pic->pic_init_qs_minus26, -26, 25);
  READ_SE_ALLOWED (&reader, pic->chroma_qp_index_offset, -12, 12);
  pic->second_chroma_qp_index_offset = pic->chroma_qp_index_offset;
  READ_UINT8 (&reader, pic->deblocking_filter_control_present_flag, 1);
  READ_UINT8 (&reader, pic->constrained_intra_pred_flag, 1);
  READ_UINT8 (&reader, pic->redundant_pic_cnt_present_flag, 1);

  if (!gst_h264_parser_more_data (&reader))
    goto done;

  READ_UINT8 (&reader, pic->transform_8x8_mode_flag, 1);

  READ_UINT8 (&reader, pic_scaling_matrix_present_flag, 1);
  if (pic_scaling_matrix_present_flag) {
    guint8 n_lists;

    n_lists = 6 + ((seq->chroma_format_idc != 3) ? 2 : 6) *
        pic->transform_8x8_mode_flag;

    if (seq->scaling_matrix_present_flag) {
      if (!gst_h264_parser_parse_scaling_list (&reader,
              pic->scaling_lists_4x4, pic->scaling_lists_8x8,
              seq->scaling_lists_4x4[0], seq->scaling_lists_4x4[3],
              seq->scaling_lists_8x8[0], seq->scaling_lists_8x8[3], n_lists))
        goto error;
    } else {
      if (!gst_h264_parser_parse_scaling_list (&reader,
              pic->scaling_lists_4x4, pic->scaling_lists_8x8,
              default_4x4_inter, default_4x4_intra,
              default_8x8_inter, default_8x8_intra, n_lists))
        goto error;
    }
  }

  READ_SE_ALLOWED (&reader, pic->second_chroma_qp_index_offset, -12, 12);

done:
  GST_DEBUG ("adding picture parameter set with id: %d to hash table", pic->id);
  g_hash_table_replace (parser->pictures, &pic->id, pic);
  return pic;

error:
  GST_WARNING ("error parsing \"Picture parameter set\"");

  gst_h264_picture_free (pic);
  return NULL;
}

static gboolean
gst_h264_slice_parse_pred_weight_table (GstH264Slice * slice,
    GstNalReader * reader,
    const GstH264Sequence * seq, const GstH264Picture * pic)
{
  GstH264PredWeightTable *p;
  gint i;

  GST_DEBUG ("parsing \"Prediction weight table\"");

  p = &slice->pred_weight_table;

  READ_UE_ALLOWED (reader, p->luma_log2_weight_denom, 0, 7);
  /* set default values */
  memset (p->luma_weight_l0, 1 << p->luma_log2_weight_denom, 32);
  memset (p->luma_offset_l0, 0, 32);

  if (seq->ChromaArrayType != 0) {
    READ_UE_ALLOWED (reader, p->chroma_log2_weight_denom, 0, 7);
    /* set default values */
    memset (p->chroma_weight_l0, 1 << p->chroma_log2_weight_denom, 64);
    memset (p->chroma_offset_l0, 0, 64);
  }

  for (i = 0; i <= slice->num_ref_idx_l0_active_minus1; i++) {
    guint8 luma_weight_l0_flag;

    READ_UINT8 (reader, luma_weight_l0_flag, 1);
    if (luma_weight_l0_flag) {
      READ_SE_ALLOWED (reader, p->luma_weight_l0[i], -128, 127);
      READ_SE_ALLOWED (reader, p->luma_offset_l0[i], -128, 127);
    }
    if (seq->ChromaArrayType != 0) {
      guint8 chroma_weight_l0_flag;
      gint j;

      READ_UINT8 (reader, chroma_weight_l0_flag, 1);
      if (chroma_weight_l0_flag) {
        for (j = 0; j < 2; j++) {
          READ_SE_ALLOWED (reader, p->chroma_weight_l0[i][j], -128, 127);
          READ_SE_ALLOWED (reader, p->chroma_offset_l0[i][j], -128, 127);
        }
      }
    }
  }

  if (GST_H264_IS_B_SLICE (slice->type)) {
    for (i = 0; i <= slice->num_ref_idx_l1_active_minus1; i++) {
      guint8 luma_weight_l1_flag;

      READ_UINT8 (reader, luma_weight_l1_flag, 1);
      if (luma_weight_l1_flag) {
        READ_SE_ALLOWED (reader, p->luma_weight_l1[i], -128, 127);
        READ_SE_ALLOWED (reader, p->luma_offset_l1[i], -128, 127);
      }
      if (seq->ChromaArrayType != 0) {
        guint8 chroma_weight_l1_flag;
        gint j;

        READ_UINT8 (reader, chroma_weight_l1_flag, 1);
        if (chroma_weight_l1_flag) {
          for (j = 0; j < 2; j++) {
            READ_SE_ALLOWED (reader, p->chroma_weight_l1[i][j], -128, 127);
            READ_SE_ALLOWED (reader, p->chroma_offset_l1[i][j], -128, 127);
          }
        }
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Prediction weight table\"");
  return FALSE;
}

static gboolean
gst_h264_slice_parse_ref_pic_list_reordering (GstH264Slice * slice,
    GstNalReader * reader)
{
  GST_DEBUG ("parsing \"Reference picture list reordering\"");

  if (!GST_H264_IS_I_SLICE (slice->type) && !GST_H264_IS_SI_SLICE (slice->type)) {
    guint8 ref_pic_list_reordering_flag_l0;
    guint8 reordering_of_pic_nums_idc;

    READ_UINT8 (reader, ref_pic_list_reordering_flag_l0, 1);
    if (ref_pic_list_reordering_flag_l0)
      do {
        READ_UE_ALLOWED (reader, reordering_of_pic_nums_idc, 0, 3);
        if (reordering_of_pic_nums_idc == 0 || reordering_of_pic_nums_idc == 1) {
          guint32 abs_diff_pic_num_minus1 G_GNUC_UNUSED;

          READ_UE_ALLOWED (reader, abs_diff_pic_num_minus1, 0,
              slice->MaxPicNum - 1);
        } else if (reordering_of_pic_nums_idc == 2) {
          guint32 long_term_pic_num;

          READ_UE (reader, long_term_pic_num);
        }
      } while (reordering_of_pic_nums_idc != 3);
  }

  if (GST_H264_IS_B_SLICE (slice->type)) {
    guint8 ref_pic_list_reordering_flag_l1;
    guint8 reordering_of_pic_nums_idc;

    READ_UINT8 (reader, ref_pic_list_reordering_flag_l1, 1);
    if (ref_pic_list_reordering_flag_l1)
      do {
        READ_UE_ALLOWED (reader, reordering_of_pic_nums_idc, 0, 3);
        if (reordering_of_pic_nums_idc == 0 || reordering_of_pic_nums_idc == 1) {
          guint32 abs_diff_num_minus1;
          READ_UE (reader, abs_diff_num_minus1);
        } else if (reordering_of_pic_nums_idc == 2) {
          guint32 long_term_pic_num;

          READ_UE (reader, long_term_pic_num);
        }
      } while (reordering_of_pic_nums_idc != 3);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Reference picture list reordering\"");
  return FALSE;
}

static gboolean
gst_h264_slice_parse_dec_ref_pic_marking (GstH264Slice * slice,
    GstNalReader * reader)
{
  GstH264DecRefPicMarking *m;

  GST_DEBUG ("parsing \"Decoded reference picture marking\"");

  m = &slice->dec_ref_pic_marking;

  if (slice->nal_unit.IdrPicFlag) {
    READ_UINT8 (reader, m->no_output_of_prior_pics_flag, 1);
    READ_UINT8 (reader, m->long_term_reference_flag, 1);
  } else {
    READ_UINT8 (reader, m->adaptive_ref_pic_marking_mode_flag, 1);
    if (m->adaptive_ref_pic_marking_mode_flag) {
      guint8 memory_management_control_operation;

      m->n_ref_pic_marking = 0;
      while (1) {
        READ_UE_ALLOWED (reader, memory_management_control_operation, 0, 6);
        if (memory_management_control_operation == 0)
          break;

        m->ref_pic_marking[m->
            n_ref_pic_marking].memory_management_control_operation =
            memory_management_control_operation;

        if (memory_management_control_operation == 1 ||
            memory_management_control_operation == 3)
          READ_UE (reader,
              m->ref_pic_marking[m->
                  n_ref_pic_marking].difference_of_pic_nums_minus1);

        if (memory_management_control_operation == 2)
          READ_UE (reader,
              m->ref_pic_marking[m->n_ref_pic_marking].long_term_pic_num);

        if (memory_management_control_operation == 3 ||
            memory_management_control_operation == 6)
          READ_UE (reader,
              m->ref_pic_marking[m->n_ref_pic_marking].long_term_frame_idx);

        if (memory_management_control_operation == 4)
          READ_UE (reader,
              m->ref_pic_marking[m->
                  n_ref_pic_marking].max_long_term_frame_idx_plus1);

        m->n_ref_pic_marking++;
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Decoded reference picture marking\"");
  return FALSE;
}

gboolean
gst_h264_parser_parse_slice_header (GstH264Parser * parser,
    GstH264Slice * slice, guint8 * data, guint size, GstNalUnit nal_unit)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);
  gint pic_parameter_set_id;
  GstH264Picture *pic;
  GstH264Sequence *seq;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), FALSE);
  g_return_val_if_fail (slice != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > 0, FALSE);

  GST_DEBUG ("parsing \"Slice header\"");

  memcpy (&slice->nal_unit, &nal_unit, sizeof (GstNalUnit));

  READ_UE (&reader, slice->first_mb_in_slice);
  READ_UE (&reader, slice->type);

  READ_UE_ALLOWED (&reader, pic_parameter_set_id, 0, 255);
  pic = g_hash_table_lookup (parser->pictures, &pic_parameter_set_id);
  if (!pic) {
    GST_WARNING ("couldn't find associated picture parameter set with id: %d",
        pic_parameter_set_id);
    goto error;
  }
  slice->picture = pic;
  seq = pic->sequence;

  /* set default values for fields that might not be present in the bitstream
     and have valid defaults */
  slice->field_pic_flag = 0;
  slice->bottom_field_flag = 0;
  slice->delta_pic_order_cnt_bottom = 0;
  slice->delta_pic_order_cnt[0] = 0;
  slice->delta_pic_order_cnt[1] = 0;
  slice->redundant_pic_cnt = 0;
  slice->num_ref_idx_l0_active_minus1 = pic->num_ref_idx_l0_active_minus1;
  slice->num_ref_idx_l1_active_minus1 = pic->num_ref_idx_l1_active_minus1;

  if (seq->separate_colour_plane_flag)
    READ_UINT8 (&reader, slice->colour_plane_id, 2);

  READ_UINT16 (&reader, slice->frame_num, seq->log2_max_frame_num_minus4 + 4);

  if (!seq->frame_mbs_only_flag) {
    READ_UINT8 (&reader, slice->field_pic_flag, 1);
    if (slice->field_pic_flag)
      READ_UINT8 (&reader, slice->bottom_field_flag, 1);
  }

  /* calculate MaxPicNum */
  if (slice->field_pic_flag)
    slice->MaxPicNum = seq->MaxFrameNum;
  else
    slice->MaxPicNum = 2 * seq->MaxFrameNum;

  if (nal_unit.type == 5)
    READ_UE_ALLOWED (&reader, slice->idr_pic_id, 0, 65535);

  if (seq->pic_order_cnt_type == 0) {
    READ_UINT16 (&reader, slice->pic_order_cnt_lsb,
        seq->log2_max_pic_order_cnt_lsb_minus4 + 4);
    if (pic->pic_order_present_flag && !slice->field_pic_flag)
      READ_SE (&reader, slice->delta_pic_order_cnt_bottom);
  }

  if (seq->pic_order_cnt_type == 1 && !seq->delta_pic_order_always_zero_flag) {
    READ_SE (&reader, slice->delta_pic_order_cnt[0]);
    if (pic->pic_order_present_flag && !slice->field_pic_flag)
      READ_SE (&reader, slice->delta_pic_order_cnt[1]);
  }

  if (pic->redundant_pic_cnt_present_flag)
    READ_UE_ALLOWED (&reader, slice->redundant_pic_cnt, 0, 127);

  if (GST_H264_IS_B_SLICE (slice->type))
    READ_UINT8 (&reader, slice->direct_spatial_mv_pred_flag, 1);

  if (GST_H264_IS_P_SLICE (slice->type) ||
      GST_H264_IS_SP_SLICE (slice->type) || GST_H264_IS_B_SLICE (slice->type)) {
    guint8 num_ref_idx_active_override_flag;

    READ_UINT8 (&reader, num_ref_idx_active_override_flag, 1);
    if (num_ref_idx_active_override_flag) {
      READ_UE_ALLOWED (&reader, slice->num_ref_idx_l0_active_minus1, 0, 31);

      if (GST_H264_IS_B_SLICE (slice->type))
        READ_UE_ALLOWED (&reader, slice->num_ref_idx_l1_active_minus1, 0, 31);
    }
  }

  if (!gst_h264_slice_parse_ref_pic_list_reordering (slice, &reader))
    return FALSE;

  if ((pic->weighted_pred_flag && (GST_H264_IS_P_SLICE (slice->type) ||
              GST_H264_IS_SP_SLICE (slice->type)))
      || (pic->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE (slice->type))) {
    if (!gst_h264_slice_parse_pred_weight_table (slice, &reader, seq, pic))
      return FALSE;
  }

  if (nal_unit.ref_idc != 0) {
    if (!gst_h264_slice_parse_dec_ref_pic_marking (slice, &reader))
      return FALSE;
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Slice header\"");
  return FALSE;
}

static gboolean
gst_h264_parser_parse_buffering_period (GstH264Parser * parser,
    GstH264BufferingPeriod * per, guint8 * data, guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);

  GstH264Sequence *seq;
  guint8 seq_parameter_set_id;

  GST_DEBUG ("parsing \"Buffering period\"");

  READ_UE_ALLOWED (&reader, seq_parameter_set_id, 0, 31);
  seq = g_hash_table_lookup (parser->sequences, &seq_parameter_set_id);
  if (!seq) {
    GST_WARNING ("couldn't find associated sequence parameter set with id: %d",
        seq_parameter_set_id);
    goto error;
  }
  per->seq = seq;

  if (seq->vui_parameters_present_flag) {
    GstH264VUIParameters *vui = &seq->vui_parameters;

    if (vui->nal_hrd_parameters_present_flag) {
      GstH264HRDParameters *hrd = &vui->nal_hrd_parameters;
      guint8 SchedSelIdx;

      for (SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
        READ_UINT8 (&reader, per->nal_initial_cpb_removal_delay[SchedSelIdx],
            5);
        READ_UINT8 (&reader,
            per->nal_initial_cpb_removal_delay_offset[SchedSelIdx], 5);
      }
    }

    if (vui->vcl_hrd_parameters_present_flag) {
      GstH264HRDParameters *hrd = &vui->vcl_hrd_parameters;
      guint8 SchedSelIdx;

      for (SchedSelIdx = 0; SchedSelIdx <= hrd->cpb_cnt_minus1; SchedSelIdx++) {
        READ_UINT8 (&reader, per->vcl_initial_cpb_removal_delay[SchedSelIdx],
            5);
        READ_UINT8 (&reader,
            per->vcl_initial_cpb_removal_delay_offset[SchedSelIdx], 5);
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Buffering period\"");
  return FALSE;
}

static gboolean
gst_h264_parse_clock_timestamp (GstH264ClockTimestamp * tim,
    GstH264VUIParameters * vui, GstNalReader * reader)
{
  guint8 full_timestamp_flag;
  guint8 time_offset_length;

  GST_DEBUG ("parsing \"Clock timestamp\"");

  /* defalt values */
  tim->time_offset = 0;

  READ_UINT8 (reader, tim->ct_type, 2);
  READ_UINT8 (reader, tim->nuit_field_based_flag, 1);
  READ_UINT8 (reader, tim->counting_type, 5);
  READ_UINT8 (reader, full_timestamp_flag, 1);
  READ_UINT8 (reader, tim->discontinuity_flag, 1);
  READ_UINT8 (reader, tim->cnt_dropped_flag, 1);
  READ_UINT8 (reader, tim->n_frames, 8);

  if (full_timestamp_flag) {
    tim->seconds_flag = TRUE;
    READ_UINT8 (reader, tim->seconds_value, 6);

    tim->minutes_flag = TRUE;
    READ_UINT8 (reader, tim->minutes_value, 6);

    tim->hours_flag = TRUE;
    READ_UINT8 (reader, tim->hours_value, 5);
  } else {
    READ_UINT8 (reader, tim->seconds_flag, 1);
    if (tim->seconds_flag) {
      READ_UINT8 (reader, tim->seconds_value, 6);
      READ_UINT8 (reader, tim->minutes_flag, 1);
      if (tim->minutes_flag) {
        READ_UINT8 (reader, tim->minutes_value, 6);
        READ_UINT8 (reader, tim->hours_flag, 1);
        if (tim->hours_flag)
          READ_UINT8 (reader, tim->hours_value, 5);
      }
    }
  }

  time_offset_length = 0;
  if (vui->nal_hrd_parameters_present_flag)
    time_offset_length = vui->nal_hrd_parameters.time_offset_length;
  else if (vui->vcl_hrd_parameters_present_flag)
    time_offset_length = vui->vcl_hrd_parameters.time_offset_length;

  if (time_offset_length > 0)
    READ_UINT32 (reader, tim->time_offset, time_offset_length);

error:
  GST_WARNING ("error parsing \"Clock timestamp\"");
  return FALSE;
}

static gboolean
gst_h264_parser_parse_pic_timing (GstH264Parser * parser, GstH264Sequence * seq,
    GstH264PicTiming * tim, guint8 * data, guint size)
{
  GstNalReader reader = GST_NAL_READER_INIT (data, size);

  GST_DEBUG ("parsing \"Picture timing\"");

  if (!seq) {
    GST_WARNING ("didn't get the associated sequence paramater set for the "
        "current access unit");
    goto error;
  }

  /* default values */
  memset (tim->clock_timestamp_flag, 0, 3);

  if (seq->vui_parameters_present_flag) {
    GstH264VUIParameters *vui = &seq->vui_parameters;

    if (vui->nal_hrd_parameters_present_flag) {
      READ_UINT8 (&reader, tim->cpb_removal_delay,
          vui->nal_hrd_parameters.cpb_removal_delay_length_minus1 + 1);
      READ_UINT8 (&reader, tim->dpb_output_delay,
          vui->nal_hrd_parameters.dpb_output_delay_length_minus1 + 1);
    } else if (vui->nal_hrd_parameters_present_flag) {
      READ_UINT8 (&reader, tim->cpb_removal_delay,
          vui->vcl_hrd_parameters.cpb_removal_delay_length_minus1 + 1);
      READ_UINT8 (&reader, tim->dpb_output_delay,
          vui->vcl_hrd_parameters.dpb_output_delay_length_minus1 + 1);
    }

    if (vui->pic_struct_present_flag) {
      const guint8 num_clock_ts_table[9] = {
        1, 1, 1, 2, 2, 3, 3, 2, 3
      };
      guint8 NumClockTs;
      guint i;

      READ_UINT8 (&reader, tim->pic_struct, 4);
      CHECK_ALLOWED (tim->pic_struct, 0, 8);

      NumClockTs = num_clock_ts_table[tim->pic_struct];
      for (i = 0; i < NumClockTs; i++) {
        READ_UINT8 (&reader, tim->clock_timestamp_flag[i], 1);
        if (tim->clock_timestamp_flag[i]) {
          if (!gst_h264_parse_clock_timestamp (&tim->clock_timestamp[i], vui,
                  &reader))
            goto error;
        }
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Picture timing\"");
  return FALSE;
}

gboolean
gst_h264_parser_parse_sei_message (GstH264Parser * parser,
    GstH264Sequence * seq, GstH264SEIMessage * sei, guint8 * data, guint size)
{
  GstNalReader reader;

  guint32 payloadSize;
  guint8 payload_type_byte, payload_size_byte;

  guint8 *payload_data;
  guint remaining, payload_size;
  gboolean res;

  g_return_val_if_fail (GST_IS_H264_PARSER (parser), FALSE);
  g_return_val_if_fail (sei != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size > 0, FALSE);

  GST_DEBUG ("parsing \"Sei message\"");

  gst_nal_reader_init (&reader, data, size);

  sei->payloadType = 0;
  do {
    READ_UINT8 (&reader, payload_type_byte, 8);
    sei->payloadType += payload_type_byte;
  }
  while (payload_type_byte == 0xff);

  payloadSize = 0;
  do {
    READ_UINT8 (&reader, payload_size_byte, 8);
    payloadSize += payload_size_byte;
  }
  while (payload_size_byte == 0xff);

  payload_data = data + gst_nal_reader_get_pos (&reader) * 8;
  remaining = gst_nal_reader_get_remaining (&reader) * 8;
  payload_size = payloadSize < remaining ? payloadSize : remaining;

  if (sei->payloadType == 0)
    res =
        gst_h264_parser_parse_buffering_period (parser,
        &sei->buffering_period, payload_data, payload_size);
  else if (sei->payloadType == 1)
    res = gst_h264_parser_parse_pic_timing (parser, seq, &sei->pic_timing,
        payload_data, payload_size);
  else
    res = TRUE;

  return res;

error:
  GST_WARNING ("error parsing \"Sei message\"");
  return FALSE;
}

#undef CHECK_ALLOWED
#undef READ_UINT8
#undef READ_UINT16
#undef READ_UINT32
#undef READ_UINT64
#undef READ_UE
#undef READ_UE_ALLOWED
#undef READ_SE
#undef READ_SE_ALLOWED

static void
gst_h264_parser_init (GstH264Parser * object)
{
  GstH264Parser *parser = GST_H264_PARSER (object);

  parser->sequences = g_hash_table_new_full (g_int_hash, g_int_equal, NULL,
      gst_h264_sequence_free);
  parser->pictures = g_hash_table_new_full (g_int_hash, g_int_equal, NULL,
      gst_h264_picture_free);
}

static void
gst_h264_parser_finalize (GObject * object)
{
  GstH264Parser *parser = GST_H264_PARSER (object);

  g_hash_table_destroy (parser->sequences);
  g_hash_table_destroy (parser->pictures);

  G_OBJECT_CLASS (gst_h264_parser_parent_class)->finalize (object);
}

static void
gst_h264_parser_class_init (GstH264ParserClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_h264_parser_finalize;
}

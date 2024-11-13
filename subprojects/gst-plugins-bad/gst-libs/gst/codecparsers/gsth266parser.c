/* Gstreamer H.266 bitstream parser
 *
 * Copyright (C) 2023 Intel Corporation
 *    Author: Zhong Hongcheng <spartazhc@gmail.com>
 *    Author: He Junyan <junyan.he@intel.com>
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

/**
 * SECTION:gsth266parser
 * @title: GstH266Parser
 * @short_description: Convenience library for h266 video bitstream parsing.
 *
 * To identify Nals in a bitstream and parse its headers, you should call:
 *
 *   * gst_h266_parser_identify_nalu() to identify the following nalu in
 *        VVC bitstreams
 *
 * Then, depending on the #GstH266NalUnitType of the newly parsed #GstH266NalUnit,
 * you should call the different functions to parse the structure:
 *
 *   * From #GST_H266_NAL_SLICE_TRAIL to #GST_H266_NAL_SLICE_GDR:
 *     gst_h266_parser_parse_slice_hdr()
 *
 *   * `GST_H266_NAL_*_SEI`: gst_h266_parser_parse_sei()
 *
 *   * #GST_H266_NAL_VPS: gst_h266_parser_parse_vps()
 *
 *   * #GST_H266_NAL_SPS: gst_h266_parser_parse_sps()
 *
 *   * #GST_H266_NAL_PPS: #gst_h266_parser_parse_pps()
 *
 *   * Any other: gst_h266_parser_parse_nal()
 *
 * Note: You should always call gst_h266_parser_parse_nal() if you don't
 * actually need #GstH266NalUnitType to be parsed for your personal use, in
 * order to guarantee that the #GstH266Parser is always up to date.
 *
 * For more details about the structures, look at the ITU-T H.266
 * specifications, you can download them from:
 *
 *   * ITU-T H.266: http://www.itu.int/rec/T-REC-H.266
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth266parser.h"
#include "nalutils.h"

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_h266_debug_category_get()
static GstDebugCategory *
gst_h266_debug_category_get (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    GstDebugCategory *cat = NULL;

    GST_DEBUG_CATEGORY_INIT (cat, "codecparsers_h266", 0, "h266 parse library");

    g_once_init_leave (&cat_gonce, (gsize) cat);
  }
  return (GstDebugCategory *) cat_gonce;
}
#endif /* GST_DISABLE_GST_DEBUG */

const guint8 scaling_pred_all_8[8 * 8] = {
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
};

const guint8 scaling_pred_all_16[8 * 8] = {
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
  16, 16, 16, 16, 16, 16, 16, 16,
};

/* *INDENT-OFF* */
/* Only need square matrix at most 8x8 for syntax level. */
static const guint8 square_DiagScanOrder_x[4][8 * 8] = {
  /* 1x1 */
  { 0, },
  /* 2x2 */
  { 0,  0,  1,  1, },
  /* 4x4 */
  { 0,  0,  1,  0,  1,  2,  0,  1,  2,  3,  1,  2,  3,  2,  3,  3, },
  /* 8x8 */
  { 0, 0, 1, 0, 1, 2, 0, 1, 2, 3, 0, 1, 2, 3, 4, 0,
    1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3,
    4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 7, 2, 3, 4, 5, 6,
    7, 3, 4, 5, 6, 7, 4, 5, 6, 7, 5, 6, 7, 6, 7, 7, },
};

/* Only need square matrix at most 8x8 for syntax level. */
static const guint8 square_DiagScanOrder_y[4][8 * 8] = {
  /* 1x1 */
  { 0, },
  /* 2x2 */
  { 0,  1,  0,  1, },
  /* 4x4 */
  { 0,  1,  0,  2,  1,  0,  3,  2,  1,  0,  3,  2,  1,  3,  2,  3, },
  /* 8x8 */
  { 0, 1, 0, 2, 1, 0, 3, 2, 1, 0, 4, 3, 2, 1, 0, 5,
    4, 3, 2, 1, 0, 6, 5, 4, 3, 2, 1, 0, 7, 6, 5, 4,
    3, 2, 1, 0, 7, 6, 5, 4, 3, 2, 1, 7, 6, 5, 4, 3,
    2, 7, 6, 5, 4, 3, 7, 6, 5, 4, 7, 6, 5, 7, 6, 7, }
};
/* *INDENT-ON* */

typedef struct
{
  guint par_n, par_d;
} PAR;

/* ITU-T Rec. H.273 | ISO/IEC 23091-2. Table 7
 * Meaning of sample aspect ratio indicator (SampleAspectRatio) */
static const PAR aspect_ratios[17] = {
  {0, 0},
  {1, 1},
  {12, 11},
  {10, 11},
  {16, 11},
  {40, 33},
  {24, 11},
  {20, 11},
  {32, 11},
  {80, 33},
  {18, 11},
  {15, 11},
  {64, 33},
  {160, 99},
  {4, 3},
  {3, 2},
  {2, 1}
};

/*****  Utils ****/
#define EXTENDED_SAR 255

static inline GstH266VPS *
gst_h266_parser_get_vps (GstH266Parser * parser, guint8 id)
{
  return (parser->vps[id].valid) ? &parser->vps[id] : NULL;
}

static inline GstH266SPS *
gst_h266_parser_get_sps (GstH266Parser * parser, guint8 id)
{
  return (parser->sps[id].valid) ? &parser->sps[id] : NULL;
}

static inline GstH266PPS *
gst_h266_parser_get_pps (GstH266Parser * parser, guint8 id)
{
  return (parser->pps[id].valid) ? &parser->pps[id] : NULL;
}

static gboolean
gst_h266_parse_nalu_header (GstH266NalUnit * nalu)
{
  guint8 *data = nalu->data + nalu->offset;
  GstBitReader br;

  if (nalu->size < 2)
    return FALSE;

  gst_bit_reader_init (&br, data, nalu->size - nalu->offset);

  /* skip the forbidden_zero_bit and nuh_reserved_zero_bit */
  gst_bit_reader_skip_unchecked (&br, 2);

  nalu->layer_id = gst_bit_reader_get_bits_uint8_unchecked (&br, 6);
  nalu->type = gst_bit_reader_get_bits_uint8_unchecked (&br, 5);
  nalu->temporal_id_plus1 = gst_bit_reader_get_bits_uint8_unchecked (&br, 3);
  nalu->header_bytes = 2;

  if (nalu->layer_id > 55) {
    GST_WARNING ("The value of nuh_layer_id shall be in the "
        "range of 0 to 55, inclusive");
    return FALSE;
  }

  /* rules for base layer */
  if (nalu->layer_id == 0) {
    if (nalu->temporal_id_plus1 - 1 == 0 &&
        nalu->type == GST_H266_NAL_SLICE_STSA) {
      GST_WARNING ("When NAL unit type is equal to STSA_NUT, "
          "TemporalId shall not be equal to 0");
      return FALSE;
    }
  }

  return TRUE;
}

struct h266_profile_string
{
  GstH266Profile profile;
  const gchar *name;
};

static const struct h266_profile_string h266_profiles[] = {
  /* keep in sync with definition in the header */
  {GST_H266_PROFILE_STILL_PICTURE, "still-picture"},
  {GST_H266_PROFILE_MAIN_10, "main-10"},
  {GST_H266_PROFILE_MAIN_10_STILL_PICTURE, "main-10-still-picture"},
  {GST_H266_PROFILE_MULTILAYER_MAIN_10, "multilayer-main-10"},
  {GST_H266_PROFILE_MULTILAYER_MAIN_10_STILL_PICTURE,
      "multilayer-main-10-still-picture"},
  {GST_H266_PROFILE_MAIN_10_444, "main-10-444"},
  {GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE, "main-10-444-still-picture"},
  {GST_H266_PROFILE_MULTILAYER_MAIN_10_444, "multilayer-main-10-444"},
  {GST_H266_PROFILE_MULTILAYER_MAIN_10_444_STILL_PICTURE,
      "multilayer-main-10-444-still-picture"},
  {GST_H266_PROFILE_MAIN_12, "main-12"},
  {GST_H266_PROFILE_MAIN_12_444, "main-12-444"},
  {GST_H266_PROFILE_MAIN_16_444, "main-16-444"},
  {GST_H266_PROFILE_MAIN_12_INTRA, "main-12-intra"},
  {GST_H266_PROFILE_MAIN_12_444_INTRA, "main-12-444-intra"},
  {GST_H266_PROFILE_MAIN_16_444_INTRA, "main-16-444-intra"},
  {GST_H266_PROFILE_MAIN_12_STILL_PICTURE, "main-12-still-picture"},
  {GST_H266_PROFILE_MAIN_12_444_STILL_PICTURE, "main-12-444-still-picture"},
  {GST_H266_PROFILE_MAIN_16_444_STILL_PICTURE, "main-16-444-still-picture"},
};

static gboolean
gst_h266_parse_general_constraints_info (GstH266GeneralConstraintsInfo * gci,
    NalReader * nr)
{
  guint8 num_additional_bits = 0;
  guint8 num_additional_bits_used = 0;

  GST_LOG ("parsing \"General Constraints Info Parameters\"");

  READ_UINT8 (nr, gci->present_flag, 1);

  if (gci->present_flag) {
    /* general */
    READ_UINT8 (nr, gci->intra_only_constraint_flag, 1);
    READ_UINT8 (nr, gci->all_layers_independent_constraint_flag, 1);
    READ_UINT8 (nr, gci->one_au_only_constraint_flag, 1);
    /* picture format */
    READ_UINT8 (nr, gci->sixteen_minus_max_bitdepth_constraint_idc, 4);
    CHECK_ALLOWED_MAX (gci->sixteen_minus_max_bitdepth_constraint_idc, 8);
    READ_UINT8 (nr, gci->three_minus_max_chroma_format_constraint_idc, 2);
    /* NAL unit type related */
    READ_UINT8 (nr, gci->no_mixed_nalu_types_in_pic_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_trail_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_stsa_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_rasl_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_radl_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_idr_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_cra_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_gdr_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_aps_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_idr_rpl_constraint_flag, 1);
    /* tile, slice, subpicture partitioning */
    READ_UINT8 (nr, gci->one_tile_per_pic_constraint_flag, 1);
    READ_UINT8 (nr, gci->pic_header_in_slice_header_constraint_flag, 1);
    READ_UINT8 (nr, gci->one_slice_per_pic_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_rectangular_slice_constraint_flag, 1);
    READ_UINT8 (nr, gci->one_slice_per_subpic_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_subpic_info_constraint_flag, 1);
    /* CTU and block partitioning */
    READ_UINT8 (nr, gci->three_minus_max_log2_ctu_size_constraint_idc, 2);
    READ_UINT8 (nr, gci->no_partition_constraints_override_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_mtt_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_qtbtt_dual_tree_intra_constraint_flag, 1);
    /* intra */
    READ_UINT8 (nr, gci->no_palette_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_ibc_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_isp_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_mrl_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_mip_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_cclm_constraint_flag, 1);
    /* inter */
    READ_UINT8 (nr, gci->no_ref_pic_resampling_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_res_change_in_clvs_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_weighted_prediction_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_ref_wraparound_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_temporal_mvp_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_sbtmvp_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_amvr_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_bdof_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_smvd_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_dmvr_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_mmvd_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_affine_motion_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_prof_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_bcw_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_ciip_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_gpm_constraint_flag, 1);
    /* transform, quantization, residual */
    READ_UINT8 (nr, gci->no_luma_transform_size_64_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_transform_skip_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_bdpcm_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_mts_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_lfnst_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_joint_cbcr_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_sbt_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_act_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_explicit_scaling_list_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_dep_quant_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_sign_data_hiding_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_cu_qp_delta_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_chroma_qp_offset_constraint_flag, 1);
    /* loop fitler */
    READ_UINT8 (nr, gci->no_sao_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_alf_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_ccalf_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_lmcs_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_ladf_constraint_flag, 1);
    READ_UINT8 (nr, gci->no_virtual_boundaries_constraint_flag, 1);

    READ_UINT8 (nr, num_additional_bits, 8);
    if (num_additional_bits > 5) {
      READ_UINT8 (nr, gci->all_rap_pictures_constraint_flag, 1);
      READ_UINT8 (nr, gci->no_extended_precision_processing_constraint_flag, 1);
      READ_UINT8 (nr, gci->no_ts_residual_coding_rice_constraint_flag, 1);
      READ_UINT8 (nr, gci->no_rrc_rice_extension_constraint_flag, 1);
      READ_UINT8 (nr, gci->no_persistent_rice_adaptation_constraint_flag, 1);
      READ_UINT8 (nr, gci->no_reverse_last_sig_coeff_constraint_flag, 1);
      num_additional_bits_used = 6;
    } else if (num_additional_bits > 0) {
      GST_WARNING ("Invalid bitstream: gci_num_additional_bits set "
          "to value %d (must be 0 or >= 6)\n", num_additional_bits);
      goto error;
    }

    /* skip the reserved zero bits */
    if (!nal_reader_skip (nr, num_additional_bits - num_additional_bits_used))
      goto error;
  }

  while (!nal_reader_is_byte_aligned (nr)) {
    if (!nal_reader_skip (nr, 1))
      goto error;
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"General Constraints Info Parameters\"");
  return FALSE;
}

static gboolean
gst_h266_parse_profile_tier_level (GstH266ProfileTierLevel * ptl,
    NalReader * nr, guint8 profileTierPresentFlag, guint8 MaxNumSubLayersMinus1)
{
  gint8 i;

  GST_LOG ("parsing \"Profile Tier Level parameters\"");

  if (profileTierPresentFlag) {
    guint8 profile_idc;

    READ_UINT8 (nr, profile_idc, 7);
    READ_UINT8 (nr, ptl->tier_flag, 1);

    ptl->profile_idc = profile_idc;
  }

  READ_UINT8 (nr, ptl->level_idc, 8);

  if (ptl->profile_idc != GST_H266_PROFILE_NONE &&
      ptl->level_idc < /* level 4 */ 64 && ptl->tier_flag) {
    GST_WARNING ("High tier not defined for levels below 4");
    goto error;
  }

  READ_UINT8 (nr, ptl->frame_only_constraint_flag, 1);
  READ_UINT8 (nr, ptl->multilayer_enabled_flag, 1);
  if ((ptl->profile_idc == GST_H266_PROFILE_MAIN_10 ||
          ptl->profile_idc == GST_H266_PROFILE_MAIN_10_444 ||
          ptl->profile_idc == GST_H266_PROFILE_MAIN_10_STILL_PICTURE ||
          ptl->profile_idc == GST_H266_PROFILE_MAIN_10_444_STILL_PICTURE)
      && ptl->multilayer_enabled_flag) {
    GST_WARNING ("ptl_multilayer_enabled_flag shall be"
        " equal to 0 for non-multilayer profiles");
    goto error;
  }

  if (profileTierPresentFlag) {
    if (!gst_h266_parse_general_constraints_info
        (&ptl->general_constraints_info, nr))
      goto error;
  }

  for (i = MaxNumSubLayersMinus1 - 1; i >= 0; i--)
    READ_UINT8 (nr, ptl->sublayer_level_present_flag[i], 1);

  /* skip the reserved zero bits */
  while (!nal_reader_is_byte_aligned (nr)) {
    if (!nal_reader_skip (nr, 1))
      goto error;
  }

  for (i = MaxNumSubLayersMinus1 - 1; i >= 0; i--)
    if (ptl->sublayer_level_present_flag[i])
      READ_UINT8 (nr, ptl->sublayer_level_idc[i], 8);

  if (profileTierPresentFlag) {
    READ_UINT8 (nr, ptl->num_sub_profiles, 8);
    for (i = 0; i < ptl->num_sub_profiles; i++) {
      guint32 sub_profile_idc;

      READ_UINT32 (nr, sub_profile_idc, 32);
      ptl->sub_profile_idc[i] = sub_profile_idc;
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Profile Tier Level Parameters\"");
  return FALSE;
}

static void
gst_h266_vui_parameters_set_default (GstH266VUIParams * vui)
{
  GST_LOG ("setting \"VUI parameters set default\"");

  /* Annex D.8 */
  /* *INDENT-OFF* */
  *vui = (GstH266VUIParams) {
    .colour_primaries = 2,
    .transfer_characteristics = 2,
    .matrix_coeffs = 2,
    .chroma_sample_loc_type_frame = 6,
    .chroma_sample_loc_type_top_field = 6,
    .chroma_sample_loc_type_bottom_field = 6,
  };
  /* *INDENT-ON* */
}

static gboolean
gst_h266_parse_vui_parameters (GstH266VUIParams * vui, NalReader * nr)
{
  GST_LOG ("parsing \"VUI parameters\"");

  READ_UINT8 (nr, vui->progressive_source_flag, 1);
  READ_UINT8 (nr, vui->interlaced_source_flag, 1);
  READ_UINT8 (nr, vui->non_packed_constraint_flag, 1);
  READ_UINT8 (nr, vui->non_projected_constraint_flag, 1);

  READ_UINT8 (nr, vui->aspect_ratio_info_present_flag, 1);
  if (vui->aspect_ratio_info_present_flag) {
    READ_UINT8 (nr, vui->aspect_ratio_constant_flag, 1);
    READ_UINT8 (nr, vui->aspect_ratio_idc, 8);
    if (vui->aspect_ratio_idc == EXTENDED_SAR) {
      READ_UINT16 (nr, vui->sar_width, 16);
      READ_UINT16 (nr, vui->sar_height, 16);
      vui->par_n = vui->sar_width;
      vui->par_d = vui->sar_height;
    } else {
      vui->par_n = aspect_ratios[vui->aspect_ratio_idc].par_n;
      vui->par_d = aspect_ratios[vui->aspect_ratio_idc].par_d;
    }
  } else {
    vui->aspect_ratio_constant_flag = 0;
    vui->aspect_ratio_idc = 0;
  }

  READ_UINT8 (nr, vui->overscan_info_present_flag, 1);
  if (vui->overscan_info_present_flag)
    READ_UINT8 (nr, vui->overscan_appropriate_flag, 1);

  READ_UINT8 (nr, vui->colour_description_present_flag, 1);
  if (vui->colour_description_present_flag) {
    READ_UINT8 (nr, vui->colour_primaries, 8);
    READ_UINT8 (nr, vui->transfer_characteristics, 8);
    READ_UINT8 (nr, vui->matrix_coeffs, 8);
    READ_UINT8 (nr, vui->full_range_flag, 1);
  } else {
    vui->colour_primaries = 2;
    vui->transfer_characteristics = 2;
    vui->matrix_coeffs = 2;
    vui->full_range_flag = 0;
  }

  READ_UINT8 (nr, vui->chroma_loc_info_present_flag, 1);
  if (vui->chroma_loc_info_present_flag) {
    if (vui->progressive_source_flag && !vui->interlaced_source_flag) {
      READ_UE_MAX (nr, vui->chroma_sample_loc_type_frame, 6);
    } else {
      READ_UE_MAX (nr, vui->chroma_sample_loc_type_top_field, 6);
      READ_UE_MAX (nr, vui->chroma_sample_loc_type_bottom_field, 6);
    }
  } else {
    vui->chroma_sample_loc_type_frame = 6;
    vui->chroma_sample_loc_type_top_field = vui->chroma_sample_loc_type_frame;
    vui->chroma_sample_loc_type_bottom_field =
        vui->chroma_sample_loc_type_frame;
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"VUI parameters\"");
  return FALSE;
}

static gboolean
nal_reader_has_more_data_in_payload (NalReader * nr,
    guint32 payload_start_pos_bit, guint32 payloadSize)
{
  if (nal_reader_is_byte_aligned (nr) &&
      (nal_reader_get_pos (nr) >= (payload_start_pos_bit + 8 * payloadSize)))
    return FALSE;

  return TRUE;
}

static gboolean
gst_h266_parse_vui_payload (GstH266VUIParams * vui, NalReader * nr,
    guint16 vui_payload_size)
{
  guint32 payload_start_pos;

  GST_LOG ("parsing \"VUI payload\"");

  payload_start_pos = nal_reader_get_pos (nr);

  if (!gst_h266_parse_vui_parameters (vui, nr))
    goto error;

  if (nal_reader_has_more_data_in_payload (nr, payload_start_pos,
          vui_payload_size)) {
    if (!nal_reader_skip (nr, 1))
      goto error;
    while (!nal_reader_is_byte_aligned (nr)) {
      if (!nal_reader_skip (nr, 1))
        goto error;
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"VUI payload\"");
  return FALSE;
}

static gboolean
gst_h266_parse_dpb_parameters (GstH266DPBParameters * dpb,
    NalReader * nr, guint8 MaxSubLayersMinus1, guint8 subLayerInfoFlag)
{
  gint i;

  GST_LOG ("parsing \"DPB Parameters\"");

  for (i = (subLayerInfoFlag ? 0 : MaxSubLayersMinus1);
      i <= MaxSubLayersMinus1; i++) {
    READ_UE_MAX (nr, dpb->max_dec_pic_buffering_minus1[i],
        GST_H266_MAX_DPB_SIZE - 1);
    READ_UE_MAX (nr, dpb->max_num_reorder_pics[i],
        dpb->max_dec_pic_buffering_minus1[i]);
    READ_UE_MAX (nr, dpb->max_latency_increase_plus1[i], G_MAXINT32 - 1);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"DPB Parameters\"");
  return FALSE;
}

static gboolean
gst_h266_ref_pic_list_struct (GstH266RefPicListStruct * rpls, NalReader * nr,
    guint8 list_idx, guint8 rpls_idx, const GstH266SPS * sps)
{
  gint i;

  GST_LOG ("parsing \"ref_pic_list_struct\"");

  memset (rpls, 0, sizeof (*rpls));

  READ_UE_MAX (nr, rpls->num_ref_entries, GST_H266_MAX_REF_ENTRIES);

  if (sps->long_term_ref_pics_flag &&
      rpls_idx < sps->num_ref_pic_lists[list_idx] &&
      rpls->num_ref_entries > 0) {
    READ_UINT8 (nr, rpls->ltrp_in_header_flag, 1);
  } else if (sps->long_term_ref_pics_flag)
    rpls->ltrp_in_header_flag = 1;

  for (i = 0; i < rpls->num_ref_entries; i++) {
    if (sps->inter_layer_prediction_enabled_flag) {
      READ_UINT8 (nr, rpls->inter_layer_ref_pic_flag[i], 1);
    } else
      rpls->inter_layer_ref_pic_flag[i] = 0;

    if (rpls->inter_layer_ref_pic_flag[i]) {
      rpls->num_inter_layer_pic++;
      continue;
    }

    if (sps->long_term_ref_pics_flag) {
      READ_UINT8 (nr, rpls->st_ref_pic_flag[i], 1);
    } else
      rpls->st_ref_pic_flag[i] = 1;

    if (rpls->st_ref_pic_flag[i]) {
      gint abs_delta_poc_st;

      READ_UE_MAX (nr, rpls->abs_delta_poc_st[i], G_MAXUINT16 - 1);

      if ((sps->weighted_pred_flag || sps->weighted_bipred_flag) && i != 0)
        abs_delta_poc_st = rpls->abs_delta_poc_st[i];
      else
        abs_delta_poc_st = rpls->abs_delta_poc_st[i] + 1;

      if (abs_delta_poc_st > 0) {
        READ_UINT8 (nr, rpls->strp_entry_sign_flag[i], 1);
      }

      rpls->delta_poc_val_st[i] =
          (1 - 2 * rpls->strp_entry_sign_flag[i]) * abs_delta_poc_st;

      rpls->num_short_term_pic++;
    } else {
      if (!rpls->ltrp_in_header_flag) {
        READ_UINT8 (nr, rpls->rpls_poc_lsb_lt[i],
            sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
      }

      rpls->num_long_term_pic++;
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"ref_pic_list_struct \"");
  return FALSE;
}

static gboolean
gst_h266_parse_general_timing_hrd_parameters (GstH266GeneralHRDParameters * hrd,
    NalReader * nr)
{
  GST_LOG ("parsing \"General timing HRD Parameters\"");

  READ_UINT32 (nr, hrd->num_units_in_tick, 32);
  READ_UINT32 (nr, hrd->time_scale, 32);

  READ_UINT8 (nr, hrd->general_nal_hrd_params_present_flag, 1);
  READ_UINT8 (nr, hrd->general_vcl_hrd_params_present_flag, 1);
  if (hrd->general_nal_hrd_params_present_flag
      || hrd->general_vcl_hrd_params_present_flag) {
    READ_UINT8 (nr, hrd->general_same_pic_timing_in_all_ols_flag, 1);
    READ_UINT8 (nr, hrd->general_du_hrd_params_present_flag, 1);
    if (hrd->general_du_hrd_params_present_flag)
      READ_UINT8 (nr, hrd->tick_divisor_minus2, 8);

    READ_UINT8 (nr, hrd->bit_rate_scale, 4);
    READ_UINT8 (nr, hrd->cpb_size_scale, 4);
    if (hrd->general_du_hrd_params_present_flag)
      READ_UINT8 (nr, hrd->cpb_size_du_scale, 4);

    READ_UE_MAX (nr, hrd->hrd_cpb_cnt_minus1, 31);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"General timing HRD Parameters\"");
  return FALSE;
}

static gboolean
gst_h266_parse_sublayer_hrd_parameters (GstH266SubLayerHRDParameters * sub_hrd,
    NalReader * nr, guint8 subLayerId,
    const GstH266GeneralHRDParameters * general)
{
  guint i;

  GST_LOG ("parsing \"SubLayer HRD Parameters\"");

  for (i = 0; i <= general->hrd_cpb_cnt_minus1; i++) {
    READ_UE_MAX (nr, sub_hrd->bit_rate_value_minus1[i], G_MAXUINT32 - 1);
    READ_UE_MAX (nr, sub_hrd->cpb_size_value_minus1[i], G_MAXUINT32 - 1);

    sub_hrd->bit_rate[i] = (sub_hrd->bit_rate_value_minus1[i] + 1) *
        (2 << (6 + general->bit_rate_scale));
    sub_hrd->cpb_size[i] = (sub_hrd->cpb_size_value_minus1[i] + 1) *
        (2 << (4 + general->cpb_size_scale));

    if (general->general_du_hrd_params_present_flag) {
      READ_UE_MAX (nr, sub_hrd->cpb_size_du_value_minus1[i], G_MAXUINT32 - 1);
      READ_UE_MAX (nr, sub_hrd->bit_rate_du_value_minus1[i], G_MAXUINT32 - 1);
    }
    READ_UINT8 (nr, sub_hrd->cbr_flag[i], 1);
  }

  for (i = 1; i <= general->hrd_cpb_cnt_minus1; i++) {
    if (sub_hrd->bit_rate[i] <= sub_hrd->bit_rate[i - 1]) {
      GST_WARNING ("bit_rate_value_minus1[i][j] shall be greater than "
          "bit_rate_value_minus1[i][j-1], i=%u, j=%u", subLayerId, i);
      goto error;
    }
    if (sub_hrd->cpb_size[i] <= sub_hrd->cpb_size[i - 1]) {
      GST_WARNING ("cpb_size_value_minus1[i][j] shall be less than or equal "
          "to cpb_size_value_minus1[i][j-1], i=%u, j=%u", subLayerId, i);
      goto error;
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"SubLayer HRD Parameters\"");
  return FALSE;
}

static gboolean
gst_h266_parse_ols_timing_hrd_parameters (GstH266OLSHRDParameters * olsHrd,
    NalReader * nr, const GstH266GeneralHRDParameters * general,
    guint8 firstSubLayer, guint8 MaxSubLayersVal)
{
  guint i, j;

  GST_LOG ("parsing \"ols timing HRD Parameters\"");

  for (i = firstSubLayer; i <= MaxSubLayersVal; i++) {
    READ_UINT8 (nr, olsHrd->fixed_pic_rate_general_flag[i], 1);
    if (!olsHrd->fixed_pic_rate_general_flag[i]) {
      READ_UINT8 (nr, olsHrd->fixed_pic_rate_within_cvs_flag[i], 1);
    } else
      olsHrd->fixed_pic_rate_within_cvs_flag[i] = 1;

    if (olsHrd->fixed_pic_rate_within_cvs_flag[i]) {
      READ_UE_MAX (nr, olsHrd->elemental_duration_in_tc_minus1[i], 2047);
      olsHrd->low_delay_hrd_flag[i] = 0;
    } else if ((general->general_nal_hrd_params_present_flag ||
            general->general_vcl_hrd_params_present_flag)
        && general->hrd_cpb_cnt_minus1 == 0) {
      READ_UINT8 (nr, olsHrd->low_delay_hrd_flag[i], 1);
    } else {
      olsHrd->low_delay_hrd_flag[i] = 0;
    }

    if (general->general_nal_hrd_params_present_flag) {
      if (!gst_h266_parse_sublayer_hrd_parameters
          (&olsHrd->nal_sub_layer_hrd_parameters[i], nr, i, general))
        goto error;
    }

    if (general->general_vcl_hrd_params_present_flag) {
      if (!gst_h266_parse_sublayer_hrd_parameters
          (&olsHrd->vcl_sub_layer_hrd_parameters[i], nr, i, general))
        goto error;
    }
  }

  for (i = 0; i < firstSubLayer; i++) {
    GstH266SubLayerHRDParameters *sub_hrd, *max_sub_hrd;

    sub_hrd = &olsHrd->nal_sub_layer_hrd_parameters[i];
    max_sub_hrd = &olsHrd->nal_sub_layer_hrd_parameters[MaxSubLayersVal];

    if (general->general_nal_hrd_params_present_flag) {
      for (j = 0; j <= general->hrd_cpb_cnt_minus1; j++) {
        sub_hrd->bit_rate_value_minus1[j] =
            max_sub_hrd->bit_rate_value_minus1[j];

        if (general->general_du_hrd_params_present_flag) {
          sub_hrd->cpb_size_du_value_minus1[j] =
              max_sub_hrd->cpb_size_du_value_minus1[j];
          sub_hrd->bit_rate_du_value_minus1[j] =
              max_sub_hrd->bit_rate_du_value_minus1[j];
        }

        sub_hrd->cbr_flag[j] = max_sub_hrd->cbr_flag[j];
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"ols timing HRD Parameters\"");
  return FALSE;
}

static gboolean
gst_h266_parse_alf (GstH266ALF * alf, NalReader * nr,
    guint8 aps_chroma_present_flag)
{
  gint j, k;
  gint filtIdx, sfIdx, altIdx;

  GST_LOG ("parsing \"ALF\"");

  READ_UINT8 (nr, alf->luma_filter_signal_flag, 1);
  if (aps_chroma_present_flag) {
    READ_UINT8 (nr, alf->chroma_filter_signal_flag, 1);
    READ_UINT8 (nr, alf->cc_cb_filter_signal_flag, 1);
    READ_UINT8 (nr, alf->cc_cr_filter_signal_flag, 1);
  }
  if (alf->luma_filter_signal_flag == 0 &&
      alf->chroma_filter_signal_flag == 0 &&
      alf->cc_cb_filter_signal_flag == 0 &&
      alf->cc_cr_filter_signal_flag == 0) {
    GST_WARNING ("At least one of the values of alf_luma_filter_signal_flag, "
        "alf_chroma_filter_signal_flag, alf_cc_cb_filter_signal_flag, and "
        "alf_cc_cr_filter_signal_flag shall be equal to 1");
    goto error;
  }

  if (alf->luma_filter_signal_flag) {
    READ_UINT8 (nr, alf->luma_clip_flag, 1);
    READ_UE_MAX (nr, alf->luma_num_filters_signalled_minus1,
        GST_H266_NUM_ALF_FILTERS - 1);

    if (alf->luma_num_filters_signalled_minus1 > 0) {
      for (filtIdx = 0; filtIdx < GST_H266_NUM_ALF_FILTERS; filtIdx++) {
        guint length =
            gst_util_ceil_log2 (alf->luma_num_filters_signalled_minus1 + 1);

        READ_UINT8 (nr, alf->luma_coeff_delta_idx[filtIdx], length);
        CHECK_ALLOWED_MAX (alf->luma_coeff_delta_idx[filtIdx],
            alf->luma_num_filters_signalled_minus1 + 1);
      }
    }

    for (sfIdx = 0; sfIdx <= alf->luma_num_filters_signalled_minus1; sfIdx++) {
      for (j = 0; j < 12; j++) {
        READ_UE_MAX (nr, alf->luma_coeff_abs[sfIdx][j], 128);
        if (alf->luma_coeff_abs[sfIdx][j])
          READ_UINT8 (nr, alf->luma_coeff_sign[sfIdx][j], 1);
      }
    }

    if (alf->luma_clip_flag) {
      for (sfIdx = 0; sfIdx <= alf->luma_num_filters_signalled_minus1; sfIdx++) {
        for (j = 0; j < 12; j++)
          READ_UINT8 (nr, alf->luma_clip_idx[sfIdx][j], 2);
      }
    }
  }

  if (alf->chroma_filter_signal_flag) {
    READ_UINT8 (nr, alf->chroma_clip_flag, 1);
    READ_UE_MAX (nr, alf->chroma_num_alt_filters_minus1, 7);
    for (altIdx = 0; altIdx <= alf->chroma_num_alt_filters_minus1; altIdx++) {
      for (j = 0; j < 6; j++) {
        READ_UE_MAX (nr, alf->chroma_coeff_abs[altIdx][j], 128);
        if (alf->chroma_coeff_abs[altIdx][j] > 0)
          READ_UINT8 (nr, alf->chroma_coeff_sign[altIdx][j], 1);
      }

      if (alf->chroma_clip_flag)
        for (j = 0; j < 6; j++)
          READ_UINT8 (nr, alf->chroma_clip_idx[altIdx][j], 2);
    }
  }

  if (alf->cc_cb_filter_signal_flag) {
    READ_UE_MAX (nr, alf->cc_cb_filters_signalled_minus1, 3);
    for (k = 0; k < alf->cc_cb_filters_signalled_minus1 + 1; k++) {
      for (j = 0; j < 7; j++) {
        READ_UINT8 (nr, alf->cc_cb_mapped_coeff_abs[k][j], 3);
        if (alf->cc_cb_mapped_coeff_abs[k][j])
          READ_UINT8 (nr, alf->cc_cb_coeff_sign[k][j], 1);
      }
    }
  }

  if (alf->cc_cr_filter_signal_flag) {
    READ_UE_MAX (nr, alf->cc_cr_filters_signalled_minus1, 3);
    for (k = 0; k < alf->cc_cr_filters_signalled_minus1 + 1; k++) {
      for (j = 0; j < 7; j++) {
        READ_UINT8 (nr, alf->cc_cr_mapped_coeff_abs[k][j], 3);
        if (alf->cc_cr_mapped_coeff_abs[k][j])
          READ_UINT8 (nr, alf->cc_cr_coeff_sign[k][j], 1);
      }
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"ALF\"");
  return FALSE;
}

static gboolean
gst_h266_parse_lmcs (GstH266LMCS * lmcs, NalReader * nr,
    guint8 aps_chroma_present_flag)
{
  gint i, LmcsMaxBinIdx;

  GST_LOG ("parsing \"LMCS\"");

  READ_UE_MAX (nr, lmcs->min_bin_idx, 15);
  READ_UE_MAX (nr, lmcs->delta_max_bin_idx, 15);
  LmcsMaxBinIdx = 15 - lmcs->delta_max_bin_idx;
  if (LmcsMaxBinIdx < lmcs->min_bin_idx) {
    GST_WARNING ("The value of LmcsMaxBinIdx(%d) shall be "
        ">= lmcs_min_bin_idx(%d)", LmcsMaxBinIdx, lmcs->min_bin_idx);
    goto error;
  }

  READ_UE_MAX (nr, lmcs->delta_cw_prec_minus1, 14);
  for (i = lmcs->min_bin_idx; i <= LmcsMaxBinIdx; i++) {
    READ_UINT8 (nr, lmcs->delta_abs_cw[i], lmcs->delta_cw_prec_minus1 + 1);
    if (lmcs->delta_abs_cw[i] > 0)
      READ_UINT8 (nr, lmcs->delta_sign_cw_flag[i], 1);
  }

  if (aps_chroma_present_flag) {
    READ_UINT8 (nr, lmcs->delta_abs_crs, 3);
    if (lmcs->delta_abs_crs > 0)
      READ_UINT8 (nr, lmcs->delta_sign_crs_flag, 1);
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"LMCS\"");
  return FALSE;
}

static gboolean
gst_h266_parse_scaling_list (GstH266ScalingList * scaling_list, NalReader * nr,
    guint8 aps_chroma_present_flag)
{
  guint ScalingList[8 * 8];
  const guint8 *scalingMatrixPred;
  gint dc;
  gint i, x, y;
  gint id, nextCoef, matrixSize, maxIdDelta;
  guint refId, log2_size;

  GST_LOG ("parsing \"Scaling List\"");

  for (id = 0; id < 28; id++) {
    matrixSize = id < 2 ? 2 : (id < 8 ? 4 : 8);
    log2_size = id < 2 ? 1 : (id < 8 ? 2 : 3);
    maxIdDelta = (id < 2) ? id : ((id < 8) ? (id - 2) : (id - 8));
    dc = 0;
    memset (ScalingList, 0, sizeof (ScalingList));

    scaling_list->copy_mode_flag[id] = 1;
    scaling_list->pred_mode_flag[id] = 0;
    scaling_list->pred_id_delta[id] = 0;

    if (aps_chroma_present_flag || id % 3 == 2 || id == 27) {
      READ_UINT8 (nr, scaling_list->copy_mode_flag[id], 1);
      if (!scaling_list->copy_mode_flag[id])
        READ_UINT8 (nr, scaling_list->pred_mode_flag[id], 1);

      if ((scaling_list->copy_mode_flag[id] || scaling_list->pred_mode_flag[id])
          && id != 0 && id != 2 && id != 8)
        READ_UE_MAX (nr, scaling_list->pred_id_delta[id], maxIdDelta);

      if (!scaling_list->copy_mode_flag[id]) {
        nextCoef = 0;

        if (id > 13) {
          READ_SE_ALLOWED (nr, scaling_list->dc_coef[id - 14], -128, 127);
          nextCoef = scaling_list->dc_coef[id - 14];
          dc = scaling_list->dc_coef[id - 14];
        }

        for (i = 0; i < matrixSize * matrixSize; i++) {
          x = square_DiagScanOrder_x[3][i];
          y = square_DiagScanOrder_y[3][i];

          if (!(id >= 25 && x >= 4 && y >= 4)) {
            READ_SE_ALLOWED (nr, scaling_list->delta_coef[id][i], -128, 127);
            nextCoef += scaling_list->delta_coef[id][i];
          }

          ScalingList[i] = nextCoef;
        }
      }
    }

    /* DC */
    if (id > 13) {
      if (!scaling_list->copy_mode_flag[id]
          && !scaling_list->pred_mode_flag[id]) {
        scaling_list->scaling_list_DC[id - 14] = 8;
      } else if (!scaling_list->pred_id_delta[id]) {
        scaling_list->scaling_list_DC[id - 14] = 16;
      } else {
        if (id < scaling_list->pred_id_delta[id]) {
          GST_WARNING ("Wrong pred_id_delta for scaling list");
          goto error;
        }
        refId = id - scaling_list->pred_id_delta[id];

        if (refId >= 14) {
          dc += scaling_list->scaling_list_DC[refId - 14];
        } else {
          dc += scaling_list->scaling_list[refId][0];
        }

        scaling_list->scaling_list_DC[id - 14] = dc & 255;
      }
    }

    /* AC */
    if (!scaling_list->copy_mode_flag[id] && !scaling_list->pred_mode_flag[id]) {
      scalingMatrixPred = scaling_pred_all_8;
    } else if (!scaling_list->pred_id_delta[id]) {
      scalingMatrixPred = scaling_pred_all_16;
    } else {
      if (id < scaling_list->pred_id_delta[id]) {
        GST_WARNING ("Wrong pred_id_delta for scaling list");
        goto error;
      }
      refId = id - scaling_list->pred_id_delta[id];
      scalingMatrixPred = scaling_list->scaling_list[refId];
    }

    for (i = 0; i < matrixSize * matrixSize; i++) {
      guint offset;

      x = square_DiagScanOrder_x[log2_size][i];
      y = square_DiagScanOrder_y[log2_size][i];
      offset = y * matrixSize + x;
      if (offset > matrixSize * matrixSize) {
        GST_WARNING ("Wrong matrix coeff array index:%d", offset);
        goto error;
      }

      scaling_list->scaling_list[id][offset] =
          (scalingMatrixPred[offset] + ScalingList[i]) & 255;
    }
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Scaling List\"");
  return FALSE;
}

static gboolean
gst_h266_parse_range_extension (GstH266SPSRangeExtensionParams * range_params,
    NalReader * nr, guint8 transform_skip_enabled_flag)
{
  GST_LOG ("parsing \"Range Extension\"");

  READ_UINT8 (nr, range_params->extended_precision_flag, 1);

  if (transform_skip_enabled_flag)
    READ_UINT8 (nr,
        range_params->ts_residual_coding_rice_present_in_sh_flag, 1);

  READ_UINT8 (nr, range_params->rrc_rice_extension_flag, 1);
  READ_UINT8 (nr, range_params->persistent_rice_adaptation_enabled_flag, 1);
  READ_UINT8 (nr, range_params->reverse_last_sig_coeff_enabled_flag, 1);

  return TRUE;

error:
  GST_WARNING ("error parsing \"Range Extension\"");
  return FALSE;
}

static gboolean
gst_h266_parse_chroma_qp_table (GstH266SPS * sps, NalReader * nr)
{
  guint numQpTables;
  guint num_points_in_qp_table;
  gint qp_in[GST_H266_MAX_POINTS_IN_QP_TABLE + 1];
  gint qp_out[GST_H266_MAX_POINTS_IN_QP_TABLE + 1];
  guint delta_qp_in[GST_H266_MAX_POINTS_IN_QP_TABLE];
  gint qp_bd_offset = 6 * sps->bitdepth_minus8;
  gint i, j, k, m, index;

  GST_LOG ("parsing \"Chroma QP Table\"");

  READ_UINT8 (nr, sps->joint_cbcr_enabled_flag, 1);
  READ_UINT8 (nr, sps->same_qp_table_for_chroma_flag, 1);

  numQpTables = sps->same_qp_table_for_chroma_flag ?
      1 : (sps->joint_cbcr_enabled_flag ? 3 : 2);

  for (i = 0; i < numQpTables; i++) {
    gint QpBdOffset = 6 * sps->bitdepth_minus8;

    READ_SE_ALLOWED (nr, sps->qp_table_start_minus26[i], -26 - QpBdOffset, 36);

    READ_UE_MAX (nr, sps->num_points_in_qp_table_minus1[i],
        36 - sps->qp_table_start_minus26[i]);
    num_points_in_qp_table = sps->num_points_in_qp_table_minus1[i] + 1;
    if (num_points_in_qp_table > GST_H266_MAX_POINTS_IN_QP_TABLE) {
      GST_WARNING ("num_points_in_qp_table %d out of range",
          num_points_in_qp_table);
      goto error;
    }

    qp_out[0] = qp_in[0] = sps->qp_table_start_minus26[i] + 26;
    for (j = 0; j < num_points_in_qp_table; j++) {
      READ_UE_MAX (nr, sps->delta_qp_in_val_minus1[i][j], 128);
      READ_UE_MAX (nr, sps->delta_qp_diff_val[i][j], 128);

      delta_qp_in[j] = sps->delta_qp_in_val_minus1[i][j] + 1;
      qp_in[j + 1] = qp_in[j] + delta_qp_in[j];
      qp_out[j + 1] = qp_out[j] +
          (sps->delta_qp_in_val_minus1[i][j] ^ sps->delta_qp_diff_val[i][j]);
    }

    index = qp_in[0] + qp_bd_offset;
    if (index < 0 || index >= GST_H266_MAX_POINTS_IN_QP_TABLE) {
      GST_WARNING ("Invalid qp index %d", index);
      goto error;
    }
    sps->chroma_qp_table[i][index] = qp_out[0];

    k = qp_in[0] - 1 + qp_bd_offset;
    if (k < 0 || k >= GST_H266_MAX_POINTS_IN_QP_TABLE) {
      GST_WARNING ("Invalid qp index %d", k);
      goto error;
    }
    for (; k >= 0; k--) {
      sps->chroma_qp_table[i][k] =
          MAX (sps->chroma_qp_table[i][k + 1] - 1, -qp_bd_offset);
      sps->chroma_qp_table[i][k] = MIN (sps->chroma_qp_table[i][k], 63);
    }

    for (j = 0; j < num_points_in_qp_table; j++) {
      gint sh = delta_qp_in[j] >> 1;

      index = qp_in[j] + 1 + qp_bd_offset;
      if (index < 0 || index >= GST_H266_MAX_POINTS_IN_QP_TABLE) {
        GST_WARNING ("Invalid qp index %d", index);
        goto error;
      }
      index = qp_in[j + 1] + qp_bd_offset;
      if (index < 0 || index >= GST_H266_MAX_POINTS_IN_QP_TABLE) {
        GST_WARNING ("Invalid qp index %d", index);
        goto error;
      }

      for (k = qp_in[j] + 1 + qp_bd_offset, m = 1;
          k <= qp_in[j + 1] + qp_bd_offset; k++, m++) {
        index = qp_in[j] + qp_bd_offset;
        if (index < 0 || index >= GST_H266_MAX_POINTS_IN_QP_TABLE) {
          GST_WARNING ("Invalid qp index %d", index);
          goto error;
        }

        sps->chroma_qp_table[i][k] = sps->chroma_qp_table[i][index] +
            ((qp_out[j + 1] - qp_out[j]) * m + sh) / delta_qp_in[j];
      }
    }

    k = qp_in[num_points_in_qp_table] + 1 + qp_bd_offset;
    if (k < 1 || k >= GST_H266_MAX_POINTS_IN_QP_TABLE) {
      GST_WARNING ("Invalid qp index %d", k);
      goto error;
    }
    for (; k <= 63 + qp_bd_offset; k++) {
      sps->chroma_qp_table[i][k] =
          MAX (sps->chroma_qp_table[i][k - 1] + 1, -qp_bd_offset);
      sps->chroma_qp_table[i][k] = MIN (sps->chroma_qp_table[i][k], 63);
    }
  }

  if (sps->same_qp_table_for_chroma_flag) {
    memcpy (&sps->chroma_qp_table[1], &sps->chroma_qp_table[0],
        sizeof (sps->chroma_qp_table[0]));
    memcpy (&sps->chroma_qp_table[2], &sps->chroma_qp_table[0],
        sizeof (sps->chroma_qp_table[0]));
  }

  return TRUE;

error:
  GST_WARNING ("error parsing \"Chroma QP Table\"");
  return FALSE;
}

/**
 * gst_h266_parser_new:
 *
 * Creates a new #GstH266Parser. It should be freed with
 * gst_h266_parser_free after use.
 *
 * Returns: a new #GstH266Parser
 *
 * Since: 1.26
 */
GstH266Parser *
gst_h266_parser_new (void)
{
  GstH266Parser *parser;

  parser = g_new0 (GstH266Parser, 1);

  return parser;
}

/**
 * gst_h266_parser_free:
 * @parser: the #GstH266Parser to free
 *
 * Frees the @parser
 *
 * Since: 1.26
 */
void
gst_h266_parser_free (GstH266Parser * parser)
{
  g_free (parser);
}

/**
 * gst_h266_parser_identify_nalu_unchecked:
 * @parser: a #GstH266Parser
 * @data: The data to parse
 * @offset: the offset from which to parse @data
 * @size: the size of @data
 * @nalu: The #GstH266NalUnit where to store parsed nal headers
 *
 * Parses @data and fills @nalu from the next nalu data from @data.
 *
 * This differs from @gst_h266_parser_identify_nalu in that it doesn't
 * check whether the packet is complete or not.
 *
 * Note: Only use this function if you already know the provided @data
 * is a complete NALU, else use @gst_h266_parser_identify_nalu.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parser_identify_nalu_unchecked (GstH266Parser * parser,
    const guint8 * data, guint offset, gsize size, GstH266NalUnit * nalu)
{
  gint off1;

  memset (nalu, 0, sizeof (*nalu));

  if (size < offset + 4) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return GST_H266_PARSER_ERROR;
  }

  off1 = scan_for_start_codes (data + offset, size - offset);

  if (off1 < 0) {
    GST_DEBUG ("No start code prefix in this buffer");
    return GST_H266_PARSER_NO_NAL;
  }

  nalu->sc_offset = offset + off1;

  /* The scanner ensures one byte passed the start code but to
   * identify an VVC NAL, we need 2. */
  if (size - nalu->sc_offset - 3 < 2) {
    GST_DEBUG ("Not enough bytes after start code to identify");
    return GST_H266_PARSER_NO_NAL;
  }

  /* sc might have 2 or 3 0-bytes */
  if (nalu->sc_offset > 0 && data[nalu->sc_offset - 1] == 00)
    nalu->sc_offset--;

  nalu->offset = offset + off1 + 3;
  nalu->data = (guint8 *) data;
  nalu->size = size - nalu->offset;

  if (!gst_h266_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return GST_H266_PARSER_BROKEN_DATA;
  }

  nalu->valid = TRUE;

  if (nalu->type == GST_H266_NAL_EOS || nalu->type == GST_H266_NAL_EOB) {
    GST_LOG ("end-of-seq or end-of-stream nal found");
    nalu->size = 2;
    return GST_H266_PARSER_OK;
  }

  return GST_H266_PARSER_OK;
}

/**
 * gst_h266_parser_identify_nalu:
 * @parser: a #GstH266Parser
 * @data: The data to parse
 * @offset: the offset from which to parse @data
 * @size: the size of @data
 * @nalu: The #GstH266NalUnit where to store parsed nal headers
 *
 * Parses @data and fills @nalu from the next nalu data from @data
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parser_identify_nalu (GstH266Parser * parser,
    const guint8 * data, guint offset, gsize size, GstH266NalUnit * nalu)
{
  GstH266ParserResult res;
  gint off2;

  res = gst_h266_parser_identify_nalu_unchecked (parser, data,
      offset, size, nalu);

  if (res != GST_H266_PARSER_OK)
    goto beach;

  /* The two NALs are exactly 2 bytes size and are placed at the end of an AU,
   * there is no need to wait for the following */
  if (nalu->type == GST_H266_NAL_EOS || nalu->type == GST_H266_NAL_EOB)
    goto beach;

  off2 = scan_for_start_codes (data + nalu->offset, size - nalu->offset);
  if (off2 < 0) {
    GST_DEBUG ("Nal start %d, No end found", nalu->offset);
    return GST_H266_PARSER_NO_NAL_END;
  }

  /* Callers assumes that enough data will available to identify the next NAL,
   * but scan_for_start_codes() only ensure 1 extra byte is available. Ensure
   * we have the required two header bytes (3 bytes start code and 2 byte
   * header). */
  if (size - (nalu->offset + off2) < 5) {
    GST_DEBUG ("Not enough bytes identify the next NAL.");
    return GST_H266_PARSER_NO_NAL_END;
  }

  /* Mini performance improvement:
   * We could have a way to store how many 0s were skipped to avoid
   * parsing them again on the next NAL */
  while (off2 > 0 && data[nalu->offset + off2 - 1] == 00)
    off2--;

  nalu->size = off2;
  if (nalu->size < 3)
    return GST_H266_PARSER_BROKEN_DATA;

  GST_LOG ("Complete nal found. Off: %d, Size: %d", nalu->offset, nalu->size);

beach:
  return res;
}

/**
 * gst_h266_parser_identify_nalu_vvc:
 * @parser: a #GstH266Parser
 * @data: The data to parse, must be the beging of the Nal unit
 * @offset: the offset from which to parse @data
 * @size: the size of @data
 * @nal_length_size: the size in bytes of the VVC nal length prefix.
 * @nalu: The #GstH266NalUnit where to store parsed nal headers
 *
 * Parses @data and sets @nalu.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parser_identify_nalu_vvc (GstH266Parser * parser,
    const guint8 * data, guint offset, gsize size, guint8 nal_length_size,
    GstH266NalUnit * nalu)
{
  GstBitReader br;

  memset (nalu, 0, sizeof (*nalu));

  /* Would overflow guint below otherwise: the callers needs to ensure that
   * this never happens */
  if (offset > G_MAXUINT32 - nal_length_size) {
    GST_WARNING ("offset + nal_length_size overflow");
    nalu->size = 0;
    return GST_H266_PARSER_BROKEN_DATA;
  }

  if (size < offset + nal_length_size) {
    GST_DEBUG ("Can't parse, buffer has too small size %" G_GSIZE_FORMAT
        ", offset %u", size, offset);
    return GST_H266_PARSER_ERROR;
  }

  size = size - offset;
  gst_bit_reader_init (&br, data + offset, size);

  nalu->size = gst_bit_reader_get_bits_uint32_unchecked (&br,
      nal_length_size * 8);
  nalu->sc_offset = offset;
  nalu->offset = offset + nal_length_size;

  if (nalu->size > G_MAXUINT32 - nal_length_size) {
    GST_WARNING ("NALU size + nal_length_size overflow");
    nalu->size = 0;
    return GST_H266_PARSER_BROKEN_DATA;
  }

  if (size < (gsize) nalu->size + nal_length_size) {
    nalu->size = 0;

    return GST_H266_PARSER_NO_NAL_END;
  }

  nalu->data = (guint8 *) data;

  if (!gst_h266_parse_nalu_header (nalu)) {
    GST_WARNING ("error parsing \"NAL unit header\"");
    nalu->size = 0;
    return GST_H266_PARSER_BROKEN_DATA;
  }

  if (nalu->size < 2)
    return GST_H266_PARSER_BROKEN_DATA;

  nalu->valid = TRUE;

  return GST_H266_PARSER_OK;
}

/**
 * gst_h266_parser_parse_vps:
 * @parser: a #GstH266Parser
 * @nalu: The #GST_H266_NAL_VPS #GstH266NalUnit to parse
 * @vps: The #GstH266VPS to fill.
 *
 * Parses @data, and fills the @vps structure.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parser_parse_vps (GstH266Parser * parser, GstH266NalUnit * nalu,
    GstH266VPS * vps)
{
  GstH266ParserResult res = gst_h266_parse_vps (nalu, vps);

  if (res == GST_H266_PARSER_OK) {
    GST_LOG ("adding video parameter set with id: %d to array", vps->vps_id);

    if (parser->active_vps && parser->active_vps->vps_id == vps->vps_id)
      parser->active_vps = NULL;

    parser->vps[vps->vps_id] = *vps;
    parser->last_vps = &parser->vps[vps->vps_id];
  }

  return res;
}

static gboolean
gst_h266_parser_derive_output_layer_sets (GstH266VPS * vps)
{
  guint8 dependency_flag[GST_H266_MAX_LAYERS][GST_H266_MAX_LAYERS];
  guint reference_layer_idx[GST_H266_MAX_LAYERS][GST_H266_MAX_LAYERS];
  guint layer_included_in_ols_flag[GST_H266_MAX_TOTAL_NUM_OLSS]
      [GST_H266_MAX_LAYERS];
  guint num_ref_layers[GST_H266_MAX_LAYERS];
  guint8 output_layer_idx[GST_H266_MAX_TOTAL_NUM_OLSS][GST_H266_MAX_LAYERS];
  guint8 layer_used_as_ref_layer_flag[GST_H266_MAX_LAYERS];
  guint8 layer_used_as_output_layer_flag[GST_H266_MAX_LAYERS];
  guint i, j, m, r;
  gint k;

  GST_LOG ("deriving output layer sets");

  if (vps->max_layers_minus1 == 0) {
    g_assert (vps->total_num_olss == 1);
    vps->num_multi_layer_olss = 0;
    return TRUE;
  }

  memset (dependency_flag, 0, sizeof (dependency_flag));
  memset (reference_layer_idx, 0, sizeof (reference_layer_idx));
  memset (layer_included_in_ols_flag, 0, sizeof (layer_included_in_ols_flag));
  memset (num_ref_layers, 0, sizeof (num_ref_layers));
  memset (output_layer_idx, 0, sizeof (output_layer_idx));
  memset (layer_used_as_ref_layer_flag,
      0, sizeof (layer_used_as_ref_layer_flag));
  memset (layer_used_as_output_layer_flag,
      0, sizeof (layer_used_as_output_layer_flag));

  /* 7.4.3.3 vps_direct_ref_layer_flag section */
  for (i = 0; i <= vps->max_layers_minus1; i++) {
    for (j = 0; j <= vps->max_layers_minus1; j++) {
      dependency_flag[i][j] = vps->direct_ref_layer_flag[i][j];

      for (k = 0; k < i; k++) {
        if (vps->direct_ref_layer_flag[i][k] && dependency_flag[k][j])
          dependency_flag[i][j] = 1;
      }

      if (vps->direct_ref_layer_flag[i][j])
        layer_used_as_ref_layer_flag[j] = 1;
    }
  }

  for (i = 0; i <= vps->max_layers_minus1; i++) {
    for (j = 0, r = 0; j <= vps->max_layers_minus1; j++) {
      if (dependency_flag[i][j])
        reference_layer_idx[i][r++] = j;
    }

    num_ref_layers[i] = r;
  }

  /* 7.4.3.3 vps_ols_output_layer_flag section */
  vps->num_output_layers_in_ols[0] = 1;
  vps->num_sub_layers_in_layer_in_ols[0][0] =
      vps->ptl_max_tid[vps->ols_ptl_idx[0]] + 1;

  layer_used_as_output_layer_flag[0] = 1;
  for (i = 1; i <= vps->max_layers_minus1; i++) {
    if (vps->each_layer_is_an_ols_flag || vps->ols_mode_idc < 2)
      layer_used_as_output_layer_flag[i] = 1;
    else
      layer_used_as_output_layer_flag[i] = 0;
  }

  for (i = 1; i < vps->total_num_olss; i++) {
    if (vps->each_layer_is_an_ols_flag || vps->ols_mode_idc == 0) {
      vps->num_output_layers_in_ols[i] = 1;
      vps->output_layer_id_in_ols[i][0] = vps->layer_id[i];

      if (vps->each_layer_is_an_ols_flag) {
        vps->num_sub_layers_in_layer_in_ols[i][0] =
            vps->ptl_max_tid[vps->ols_ptl_idx[i]] + 1;
      } else {
        vps->num_sub_layers_in_layer_in_ols[i][i] =
            vps->ptl_max_tid[vps->ols_ptl_idx[i]] + 1;

        for (k = i - 1; k >= 0; k--) {
          vps->num_sub_layers_in_layer_in_ols[i][k] = 0;

          for (m = k + 1; m <= i; m++) {
            guint8 max_sublayer_needed =
                MIN (vps->num_sub_layers_in_layer_in_ols[i][m],
                vps->max_tid_il_ref_pics_plus1[m][k]);

            if (vps->direct_ref_layer_flag[m][k] &&
                vps->num_sub_layers_in_layer_in_ols[i][k] < max_sublayer_needed)
              vps->num_sub_layers_in_layer_in_ols[i][k] = max_sublayer_needed;
          }
        }
      }
    } else if (vps->ols_mode_idc == 1) {
      vps->num_output_layers_in_ols[i] = i + 1;

      for (j = 0; j < vps->num_output_layers_in_ols[i]; j++) {
        vps->output_layer_id_in_ols[i][j] = vps->layer_id[j];
        vps->num_sub_layers_in_layer_in_ols[i][j] =
            vps->ptl_max_tid[vps->ols_ptl_idx[i]] + 1;
      }
    } else if (vps->ols_mode_idc == 2) {
      gint8 highest_included_layer = 0;

      for (j = 0; j <= vps->max_layers_minus1; j++)
        vps->num_sub_layers_in_layer_in_ols[i][j] = 0;

      j = 0;
      for (k = 0; k <= vps->max_layers_minus1; k++) {
        if (vps->ols_output_layer_flag[i][k]) {
          layer_included_in_ols_flag[i][k] = 1;
          highest_included_layer = k;
          layer_used_as_output_layer_flag[k] = 1;
          output_layer_idx[i][j] = k;
          vps->output_layer_id_in_ols[i][j] = vps->layer_id[j];
          j++;
          vps->num_sub_layers_in_layer_in_ols[i][k] =
              vps->ptl_max_tid[vps->ols_ptl_idx[i]] + 1;
        }
      }

      vps->num_output_layers_in_ols[i] = j;
      for (j = 0; j < vps->num_output_layers_in_ols[i]; j++) {
        gint idx = output_layer_idx[i][j];

        for (k = 0; k < num_ref_layers[idx]; k++)
          layer_included_in_ols_flag[i][reference_layer_idx[idx][k]] = 1;
      }

      for (k = highest_included_layer - 1; k >= 0; k--) {
        if (layer_included_in_ols_flag[i][k]
            && !vps->ols_output_layer_flag[i][k]) {
          for (m = k + 1; m <= highest_included_layer; m++) {
            guint max_sublayer_needed =
                MIN (vps->num_sub_layers_in_layer_in_ols[i][m],
                vps->max_tid_il_ref_pics_plus1[m][k]);

            if (vps->direct_ref_layer_flag[m][k] &&
                layer_included_in_ols_flag[i][m] &&
                vps->num_sub_layers_in_layer_in_ols[i][k] < max_sublayer_needed)
              vps->num_sub_layers_in_layer_in_ols[i][k] = max_sublayer_needed;
          }
        }
      }
    }
  }

  for (i = 0; i <= vps->max_layers_minus1; i++) {
    if (layer_used_as_ref_layer_flag[i] == 0 &&
        layer_used_as_output_layer_flag[i] == 0) {
      GST_WARNING ("There shall be no layer that is neither an output"
          " layer nor a direct reference layer");
      return FALSE;
    }
  }

  vps->num_layers_in_ols[0] = 1;
  vps->layer_id_in_ols[0][0] = vps->layer_id[0];
  vps->num_multi_layer_olss = 0;
  for (i = 1; i < vps->total_num_olss; i++) {
    if (vps->each_layer_is_an_ols_flag) {
      vps->num_layers_in_ols[i] = 1;
      vps->layer_id_in_ols[i][0] = vps->layer_id[i];
    } else if (vps->ols_mode_idc == 0 || vps->ols_mode_idc == 1) {
      vps->num_layers_in_ols[i] = i + 1;

      for (j = 0; j < vps->num_layers_in_ols[i]; j++)
        vps->layer_id_in_ols[i][j] = vps->layer_id[j];
    } else if (vps->ols_mode_idc == 2) {
      j = 0;
      for (k = 0; k <= vps->max_layers_minus1; k++) {
        if (layer_included_in_ols_flag[i][k]) {
          vps->layer_id_in_ols[i][j] = vps->layer_id[k];
          j++;
        }
      }
      vps->num_layers_in_ols[i] = j;
    }

    if (vps->num_layers_in_ols[i] > 1) {
      vps->multi_layer_ols_idx[i] = vps->num_multi_layer_olss;
      vps->num_multi_layer_olss++;
    }
  }

  return TRUE;
}

static gboolean
gst_h266_parser_check_vps (GstH266VPS * vps)
{
  guint index;
  guint olsIdx, olsTimingHrdIdx, olsPtlIdx, olsDpbParamsIdx;

  for (index = 0; index < vps->num_multi_layer_olss; index++) {
    olsIdx = vps->multi_layer_ols_idx[index];
    olsTimingHrdIdx = vps->ols_timing_hrd_idx[index];
    olsPtlIdx = vps->ols_ptl_idx[olsIdx];

    if (vps->hrd_max_tid[olsTimingHrdIdx] < vps->ptl_max_tid[olsPtlIdx]) {
      GST_WARNING ("The value of vps_hrd_max_tid[vps_ols_timing_hrd_idx[m]] "
          "shall be greater than or equal to "
          "vps_ptl_max_tid[vps_ols_ptl_idx[n]] for each m-th multi-layer "
          "OLS for m from 0 to NumMultiLayerOlss - 1, inclusive, and n "
          "being the OLS index of the m-th multi-layer OLS among all OLSs.");
      return FALSE;
    }

    olsDpbParamsIdx = vps->ols_dpb_params_idx[olsIdx];
    if (vps->dpb_max_tid[olsDpbParamsIdx] < vps->ptl_max_tid[olsPtlIdx]) {
      GST_WARNING ("The value of vps_dpb_max_tid[vps_ols_dpb_params_idx[m]] "
          "shall be greater than or equal to "
          "vps_ptl_max_tid[vps_ols_ptl_idx[n]] for each m-th multi-layer "
          "OLS for m from 0 to NumMultiLayerOlss - 1, inclusive, and n "
          "being the OLS index of the m-th multi-layer OLS among all OLSs.");
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_h266_parse_vps:
 * @nalu: The #GST_H266_NAL_VPS #GstH266NalUnit to parse
 * @vps: The #GstH266VPS to fill.
 *
 * Parses @data, and fills the @vps structure.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parse_vps (GstH266NalUnit * nalu, GstH266VPS * vps)
{
  NalReader nr;
  gint i, j;
  gboolean isPTLReferred[GST_H266_MAX_PTLS];

  GST_LOG ("parsing \"Video parameter set\"");

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  memset (vps, 0, sizeof (*vps));
  memset (isPTLReferred, 0, sizeof (isPTLReferred));

  READ_UINT8 (&nr, vps->vps_id, 4);
  if (vps->vps_id == 0) {
    GST_WARNING ("vps_id equal to zero is reserved and shall"
        " not be used in a bitstream");
    goto error;
  }

  READ_UINT8 (&nr, vps->max_layers_minus1, 6);
  if (vps->max_layers_minus1 == 0)
    vps->each_layer_is_an_ols_flag = 1;

  READ_UINT8 (&nr, vps->max_sublayers_minus1, 3);
  CHECK_ALLOWED_MAX (vps->max_sublayers_minus1, GST_H266_MAX_SUBLAYERS - 1);
  if (vps->max_layers_minus1 > 0 && vps->max_sublayers_minus1 > 0) {
    READ_UINT8 (&nr, vps->default_ptl_dpb_hrd_max_tid_flag, 1);
  } else
    vps->default_ptl_dpb_hrd_max_tid_flag = 1;

  if (vps->max_layers_minus1 > 0) {
    READ_UINT8 (&nr, vps->all_independent_layers_flag, 1);
    if (vps->all_independent_layers_flag == 0)
      vps->each_layer_is_an_ols_flag = 0;
  } else
    vps->all_independent_layers_flag = 1;

  for (i = 0; i <= vps->max_layers_minus1; i++) {
    READ_UINT8 (&nr, vps->layer_id[i], 6);
    /* 7.4.3.2: For any two non-negative integer values of m and n,
       when m is less than n, the value of vps_layer_id[ m ] shall be
       less than vps_layer_id[ n ]. */
    if (i > 0 && vps->layer_id[i] <= vps->layer_id[i - 1]) {
      GST_WARNING ("vps_layer_id[%d](%d) should > vps_layer_id[%d](%d).\n",
          i, vps->layer_id[i], i - 1, vps->layer_id[i - 1]);
      goto error;
    }

    if (i > 0 && !vps->all_independent_layers_flag) {
      guint count = 0;

      READ_UINT8 (&nr, vps->independent_layer_flag[i], 1);
      if (!vps->independent_layer_flag[i]) {
        READ_UINT8 (&nr, vps->max_tid_ref_present_flag[i], 1);

        for (j = 0; j < i; j++) {
          READ_UINT8 (&nr, vps->direct_ref_layer_flag[i][j], 1);
          if (vps->direct_ref_layer_flag[i][j])
            count++;

          if (vps->max_tid_ref_present_flag[i]
              && vps->direct_ref_layer_flag[i][j]) {
            READ_UINT8 (&nr, vps->max_tid_il_ref_pics_plus1[i][j], 3);
          } else {
            vps->max_tid_il_ref_pics_plus1[i][j] =
                vps->max_sublayers_minus1 + 1;
          }
        }

        if (count == 0) {
          GST_WARNING ("There has to be at least one value of j such "
              "that the value of vps_direct_dependency_flag[%d][j] is "
              "equal to 1, when vps_independent_layer_flag[%d] is equal "
              "to 0", i, i);
          goto error;
        }
      }
    } else {
      vps->independent_layer_flag[i] = 1;
    }
  }

  if (vps->max_layers_minus1 > 0) {
    if (vps->all_independent_layers_flag)
      READ_UINT8 (&nr, vps->each_layer_is_an_ols_flag, 1);

    if (!vps->each_layer_is_an_ols_flag) {
      if (!vps->all_independent_layers_flag) {
        READ_UINT8 (&nr, vps->ols_mode_idc, 2);
        CHECK_ALLOWED_MAX (vps->ols_mode_idc, 2);
      } else
        vps->ols_mode_idc = 2;

      if (vps->ols_mode_idc == 2) {
        READ_UINT8 (&nr, vps->num_output_layer_sets_minus2, 8);
        for (i = 1; i <= vps->num_output_layer_sets_minus2 + 1; i++)
          for (j = 0; j <= vps->max_layers_minus1; j++)
            READ_UINT8 (&nr, vps->ols_output_layer_flag[i][j], 1);
      }
    }

    if (vps->each_layer_is_an_ols_flag ||
        vps->ols_mode_idc == 0 || vps->ols_mode_idc == 1) {
      vps->total_num_olss = vps->max_layers_minus1 + 1;
    } else if (vps->ols_mode_idc == 2) {
      vps->total_num_olss = vps->num_output_layer_sets_minus2 + 2;
    } else {
      g_assert_not_reached ();
    }

    READ_UINT8 (&nr, vps->num_ptls_minus1, 8);
    CHECK_ALLOWED_MAX (vps->num_ptls_minus1, vps->total_num_olss - 1);
  } else {
    vps->each_layer_is_an_ols_flag = 1;
    vps->num_ptls_minus1 = 0;
    vps->total_num_olss = 1;
  }

  if (!gst_h266_parser_derive_output_layer_sets (vps)) {
    GST_WARNING ("Fail to derive vps layer sets parameters");
    goto error;
  }

  if (vps->num_ptls_minus1 + 1 > vps->total_num_olss) {
    GST_WARNING ("The value of vps_num_ptls_minus1 shall"
        " be less than TotalNumOlss");
    goto error;
  }

  for (i = 0; i <= vps->num_ptls_minus1; i++) {
    if (i > 0) {
      READ_UINT8 (&nr, vps->pt_present_flag[i], 1);
    } else
      vps->pt_present_flag[i] = 1;

    if (!vps->default_ptl_dpb_hrd_max_tid_flag) {
      READ_UINT8 (&nr, vps->ptl_max_tid[i], 3);
      CHECK_ALLOWED_MAX (vps->ptl_max_tid[i], vps->max_sublayers_minus1);
    } else
      vps->ptl_max_tid[i] = vps->max_sublayers_minus1;
  }

  while (!nal_reader_is_byte_aligned (&nr))
    if (!nal_reader_skip (&nr, 1))
      goto error;

  for (i = 0; i <= vps->num_ptls_minus1; i++) {
    if (i == 0 && !vps->pt_present_flag[i]) {
      GST_WARNING ("Profile/Tier should always be present for the first entry");
      goto error;
    }

    if (!gst_h266_parse_profile_tier_level (&vps->profile_tier_level[i], &nr,
            vps->pt_present_flag[i], vps->ptl_max_tid[i]))
      goto error;
  }

  for (i = 0; i < vps->total_num_olss; i++) {
    if (vps->num_ptls_minus1 > 0 &&
        vps->num_ptls_minus1 + 1 != vps->total_num_olss) {
      READ_UINT8 (&nr, vps->ols_ptl_idx[i], 8);
    } else if (vps->num_ptls_minus1 + 1 == vps->total_num_olss)
      vps->ols_ptl_idx[i] = i;
    else
      vps->ols_ptl_idx[i] = 0;

    isPTLReferred[vps->ols_ptl_idx[i]] = TRUE;
  }

  for (i = 0; i <= vps->num_ptls_minus1; i++) {
    if (!isPTLReferred[i]) {
      GST_WARNING ("Each profile_tier_level() syntax structure in the "
          "VPS shall be referred to by at least one value of "
          "vps_ols_ptl_idx[i] for i in the range of 0 to TotalNumOlss ? 1, "
          "inclusive.");
      goto error;
    }
  }

  if (!vps->each_layer_is_an_ols_flag) {
    READ_UE_MAX (&nr, vps->num_dpb_params_minus1,
        vps->num_multi_layer_olss - 1);

    if (vps->max_sublayers_minus1 > 0)
      READ_UINT8 (&nr, vps->sublayer_dpb_params_present_flag, 1);

    for (i = 0; i <= vps->num_dpb_params_minus1; i++) {
      if (!vps->default_ptl_dpb_hrd_max_tid_flag) {
        READ_UINT8 (&nr, vps->dpb_max_tid[i], 3);
        CHECK_ALLOWED_MAX (vps->dpb_max_tid[i], vps->max_sublayers_minus1);
      } else
        vps->dpb_max_tid[i] = vps->max_sublayers_minus1;

      if (!gst_h266_parse_dpb_parameters (&vps->dpb[i], &nr,
              vps->dpb_max_tid[i], vps->sublayer_dpb_params_present_flag))
        goto error;

      for (j = (vps->sublayer_dpb_params_present_flag ?
              vps->dpb_max_tid[i] : 0); j < vps->dpb_max_tid[i]; j++) {
        /* When dpb_max_dec_pic_buffering_minus1[i] is not present for i in
           the range of 0 to maxSubLayersMinus1 - 1, inclusive, due to
           subLayerInfoFlag being equal to 0, it is inferred to be equal to
           dpb_max_dec_pic_buffering_minus1[maxSubLayersMinus1]. */
        vps->dpb[i].max_dec_pic_buffering_minus1[j] =
            vps->dpb[i].max_dec_pic_buffering_minus1[vps->dpb_max_tid[i]];

        /* When dpb_max_num_reorder_pics[i] is not present for i in
           the range of 0 to maxSubLayersMinus1 - 1, inclusive, due to
           subLayerInfoFlag being equal to 0, it is inferred to be equal
           to dpb_max_num_reorder_pics[maxSubLayersMinus1]. */
        vps->dpb[i].max_num_reorder_pics[j] =
            vps->dpb[i].max_num_reorder_pics[vps->dpb_max_tid[i]];

        /* When dpb_max_latency_increase_plus1[i] is not present for i in
           the range of 0 to maxSubLayersMinus1 - 1, inclusive, due to
           subLayerInfoFlag being equal to 0, it is inferred to be equal to
           dpb_max_latency_increase_plus1[maxSubLayersMinus1]. */
        vps->dpb[i].max_latency_increase_plus1[j] =
            vps->dpb[i].max_latency_increase_plus1[vps->dpb_max_tid[i]];
      }
    }

    for (i = 0; i < vps->num_multi_layer_olss; i++) {
      READ_UE_MAX (&nr, vps->ols_dpb_pic_width[i], G_MAXUINT16);
      READ_UE_MAX (&nr, vps->ols_dpb_pic_height[i], G_MAXUINT16);
      READ_UINT8 (&nr, vps->ols_dpb_chroma_format[i], 2);
      READ_UE_MAX (&nr, vps->ols_dpb_bitdepth_minus8[i], 2);

      if (vps->num_dpb_params_minus1 > 0
          && vps->num_dpb_params_minus1 + 1 != vps->num_multi_layer_olss) {
        READ_UE_MAX (&nr, vps->ols_dpb_params_idx[i],
            vps->num_dpb_params_minus1);
      } else if (vps->num_dpb_params_minus1 == 0)
        vps->ols_dpb_params_idx[i] = 0;
      else
        vps->ols_dpb_params_idx[i] = i;
    }
  }

  if (!vps->each_layer_is_an_ols_flag)
    READ_UINT8 (&nr, vps->timing_hrd_params_present_flag, 1);

  if (vps->timing_hrd_params_present_flag) {
    gboolean is_dpb_param_referred[GST_H266_MAX_TOTAL_NUM_OLSS];

    memset (is_dpb_param_referred, 0, sizeof (is_dpb_param_referred));

    if (!gst_h266_parse_general_timing_hrd_parameters
        (&vps->general_hrd_params, &nr))
      goto error;

    if (vps->max_sublayers_minus1 > 0) {
      READ_UINT8 (&nr, vps->sublayer_cpb_params_present_flag, 1);
    } else
      vps->sublayer_cpb_params_present_flag = 0;

    READ_UE_MAX (&nr, vps->num_ols_timing_hrd_params_minus1,
        vps->num_multi_layer_olss - 1);
    for (i = 0; i <= vps->num_ols_timing_hrd_params_minus1; i++) {
      guint firstSubLayer;

      if (!vps->default_ptl_dpb_hrd_max_tid_flag) {
        READ_UINT8 (&nr, vps->hrd_max_tid[i], 3);
        CHECK_ALLOWED_MAX (vps->hrd_max_tid[i], vps->max_sublayers_minus1);
      } else
        vps->hrd_max_tid[i] = vps->max_sublayers_minus1;

      firstSubLayer =
          vps->sublayer_cpb_params_present_flag ? 0 : vps->hrd_max_tid[i];

      if (!gst_h266_parse_ols_timing_hrd_parameters (&vps->ols_hrd_params[i],
              &nr, &vps->general_hrd_params, firstSubLayer,
              vps->hrd_max_tid[i]))
        goto error;
    }
    for (i = vps->num_ols_timing_hrd_params_minus1 + 1;
        i < vps->total_num_olss; i++) {
      vps->hrd_max_tid[i] = vps->max_sublayers_minus1;
    }

    for (i = 0; i < vps->num_multi_layer_olss; i++) {
      if (vps->num_ols_timing_hrd_params_minus1 > 0 &&
          vps->num_ols_timing_hrd_params_minus1 + 1 !=
          vps->num_multi_layer_olss) {
        READ_UE_MAX (&nr, vps->ols_timing_hrd_idx[i],
            vps->num_ols_timing_hrd_params_minus1);
      } else if (vps->num_ols_timing_hrd_params_minus1 == 0) {
        vps->ols_timing_hrd_idx[i] = 0;
      } else {
        vps->ols_timing_hrd_idx[i] = i;
      }

      is_dpb_param_referred[vps->ols_timing_hrd_idx[i]] = TRUE;
    }

    for (i = 0; i <= vps->num_ols_timing_hrd_params_minus1; i++) {
      if (!is_dpb_param_referred[i]) {
        GST_WARNING ("Each vps_ols_timing_hrd_parameters( ) syntax structure "
            "in the VPS shall be referred to by at least one value of "
            "vps_ols_timing_hrd_idx[i] for i in the range of 1 to "
            "NumMultiLayerOlss - 1, inclusive");
        goto error;
      }
    }

  } else {
    for (int i = 0; i < vps->total_num_olss; i++)
      vps->hrd_max_tid[i] = vps->max_sublayers_minus1;
  }

  READ_UINT8 (&nr, vps->extension_flag, 1);
  if (vps->extension_flag) {
    GST_WARNING ("extension_flag is not supported in current version VPS.");
    goto error;
  }

  if (!gst_h266_parser_check_vps (vps))
    goto error;

  vps->valid = TRUE;

  return GST_H266_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Video parameter set\"");
  vps->valid = FALSE;
  return GST_H266_PARSER_ERROR;
}

/**
 * gst_h266_parser_parse_sps:
 * @parser: a #GstH266Parser
 * @nalu: The #GST_H266_NAL_SPS #GstH266NalUnit to parse
 * @sps: The #GstH266SPS to fill.
 *
 * Parses @data, and fills the @sps structure.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parser_parse_sps (GstH266Parser * parser, GstH266NalUnit * nalu,
    GstH266SPS * sps)
{
  GstH266ParserResult res = gst_h266_parse_sps (parser, nalu, sps);

  if (res == GST_H266_PARSER_OK) {
    GST_LOG ("adding sequence parameter set with id: %d to array", sps->sps_id);

    if (parser->active_sps && parser->active_sps->sps_id == sps->sps_id)
      parser->active_sps = NULL;

    parser->sps[sps->sps_id] = *sps;
    parser->last_sps = &parser->sps[sps->sps_id];
  }

  return res;
}

/**
 * gst_h266_parse_sps:
 * @parser: The #GstH266Parser
 * @nalu: The #GST_H266_NAL_SPS #GstH266NalUnit to parse
 * @sps: The #GstH266SPS to fill.
 *
 * Parses @data, and fills the @sps structure.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parse_sps (GstH266Parser * parser, GstH266NalUnit * nalu,
    GstH266SPS * sps)
{
  NalReader nr;
  GstH266VPS *vps;
  GstH266ProfileTierLevel *ptl;
  guint i, j;
  guint8 ctbLog2SizeY;
  guint minQT[3] = { 0, 0, 0 };
  guint maxBTSize[3] = { 0, 0, 0 };
  guint maxTTSize[3] = { 0, 0, 0 };
  guint minCbLog2SizeY, minCuSize, MaxNumMergeCand,
      minQtLog2SizeIntraY, minQtLog2SizeInterY;
  guint8 sub_width_c, sub_height_c;
  const guint8 h266_sub_width_c[] = { 1, 2, 2, 1 };
  const guint8 h266_sub_height_c[] = { 1, 2, 1, 1 };

  GST_LOG ("parsing \"Sequence parameter set\"");

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  memset (sps, 0, sizeof (*sps));
  sps->nuh_layer_id = nalu->layer_id;

  READ_UINT8 (&nr, sps->sps_id, 4);
  READ_UINT8 (&nr, sps->vps_id, 4);

  /* 7.4.3.4: When sps_video_parameter_set_id is equal to 0, SPS
     does not refer to a VPS.
     We just make vps[0] as the default vps with default flags,
     and let the SPS refer to it when vps_id is 0. */
  if (sps->vps_id == 0) {
    memset (&parser->vps[0], 0, sizeof (*vps));
    parser->vps[0].vps_id = 0;

    parser->vps[0].max_layers_minus1 = 0;
    /* 7.4.3.4:
       The value of GeneralLayerIdx[nuh_layer_id] is set equal to 0.
       The value of vps_independent_layer_flag[GeneralLayerIdx[nuh_layer_id]]
       is inferred to be equal to 1.
       The value of TotalNumOlss is set equal to 1,
       the value of NumLayersInOls[0] is set equal to 1, and value of
       vps_layer_id[0] is inferred to be equal to the value of nuh_layer_id
       of all the VCL NAL units, and the value of LayerIdInOls[0][0]
       is set equal to vps_layer_id[0]. */
    parser->vps[0].independent_layer_flag[0] = 1;
    parser->vps[0].total_num_olss = 1;
    parser->vps[0].num_layers_in_ols[0] = 1;
    parser->vps[0].layer_id[0] = sps->nuh_layer_id;
    parser->vps[0].layer_id_in_ols[0][0] = parser->vps[0].layer_id[0];
    parser->vps[0].valid = TRUE;

    vps = &parser->vps[0];
  } else {
    vps = gst_h266_parser_get_vps (parser, sps->vps_id);
    if (!vps) {
      GST_DEBUG ("couldn't find associated video parameter set with id: %d",
          sps->vps_id);
    }
  }
  sps->vps = vps;

  READ_UINT8 (&nr, sps->max_sublayers_minus1, 3);
  CHECK_ALLOWED_MAX (sps->max_sublayers_minus1, GST_H266_MAX_SUBLAYERS - 1);

  READ_UINT8 (&nr, sps->chroma_format_idc, 2);

  READ_UINT8 (&nr, sps->log2_ctu_size_minus5, 2);
  CHECK_ALLOWED_MAX (sps->log2_ctu_size_minus5, 2);
  ctbLog2SizeY = sps->log2_ctu_size_minus5 + 5;
  sps->ctu_size = 1 << ctbLog2SizeY;

  READ_UINT8 (&nr, sps->ptl_dpb_hrd_params_present_flag, 1);
  if (!sps->vps_id && !sps->ptl_dpb_hrd_params_present_flag) {
    GST_WARNING ("When vps_id is equal to 0, the value of "
        "ptl_dpb_hrd_params_present_flag shall be equal to 1");
    goto error;
  }

  if (sps->ptl_dpb_hrd_params_present_flag) {
    if (!gst_h266_parse_profile_tier_level (&sps->profile_tier_level, &nr,
            1, sps->max_sublayers_minus1))
      goto error;
  }

  ptl = &sps->profile_tier_level;

  READ_UINT8 (&nr, sps->gdr_enabled_flag, 1);
  if (ptl->general_constraints_info.no_gdr_constraint_flag
      && sps->gdr_enabled_flag) {
    GST_WARNING ("When gci_no_gdr_constraint_flag equal to 1 , "
        "the value of gdr_enabled_flag shall be equal to 0");
    goto error;
  }

  READ_UINT8 (&nr, sps->ref_pic_resampling_enabled_flag, 1);
  if (ptl->general_constraints_info.no_ref_pic_resampling_constraint_flag
      && sps->ref_pic_resampling_enabled_flag) {
    GST_WARNING ("When gci_no_ref_pic_resampling_constraint_flag is "
        "equal to 1, ref_pic_resampling_enabled_flag shall be equal to 0");
    goto error;
  }

  if (sps->ref_pic_resampling_enabled_flag)
    READ_UINT8 (&nr, sps->res_change_in_clvs_allowed_flag, 1);

  if (ptl->general_constraints_info.no_res_change_in_clvs_constraint_flag
      && sps->res_change_in_clvs_allowed_flag) {
    GST_WARNING ("When no_res_change_in_clvs_constraint_flag is equal "
        "to 1, res_change_in_clvs_allowed_flag shall be equal to 0");
    goto error;
  }

  READ_UE_MAX (&nr, sps->pic_width_max_in_luma_samples, GST_H266_MAX_WIDTH);
  READ_UE_MAX (&nr, sps->pic_height_max_in_luma_samples, GST_H266_MAX_HEIGHT);
  sub_width_c = h266_sub_width_c[sps->chroma_format_idc];
  sub_height_c = h266_sub_height_c[sps->chroma_format_idc];

  READ_UINT8 (&nr, sps->conformance_window_flag, 1);
  if (sps->conformance_window_flag) {
    guint width, height;

    width = sps->pic_width_max_in_luma_samples / sub_width_c;
    height = sps->pic_height_max_in_luma_samples / sub_height_c;

    READ_UE_MAX (&nr, sps->conf_win_left_offset, width);
    READ_UE_MAX (&nr, sps->conf_win_right_offset, width);
    READ_UE_MAX (&nr, sps->conf_win_top_offset, height);
    READ_UE_MAX (&nr, sps->conf_win_bottom_offset, height);

    if (sub_width_c * (sps->conf_win_left_offset +
            sps->conf_win_right_offset) >= sps->pic_width_max_in_luma_samples
        || sub_height_c * (sps->conf_win_top_offset +
            sps->conf_win_bottom_offset) >=
        sps->pic_height_max_in_luma_samples) {
      GST_WARNING ("Invalid sps conformance window: (%u, %u, %u, %u), "
          "resolution is %ux%u, sub WxH is %ux%u.\n", sps->conf_win_left_offset,
          sps->conf_win_right_offset, sps->conf_win_top_offset,
          sps->conf_win_bottom_offset, sps->pic_width_max_in_luma_samples,
          sps->pic_height_max_in_luma_samples, sub_width_c, sub_height_c);
      goto error;
    }
  }

  READ_UINT8 (&nr, sps->subpic_info_present_flag, 1);
  if (ptl->general_constraints_info.no_subpic_info_constraint_flag
      && sps->subpic_info_present_flag) {
    GST_WARNING ("When gci_no_subpic_info_constraint_flag is equal to 1, "
        "the value of subpic_info_present_flag shall be equal to 0");
    goto error;
  }

  if (sps->subpic_info_present_flag) {
    guint32 maxPicWidthInCtus =
        ((sps->pic_width_max_in_luma_samples - 1) / sps->ctu_size) + 1;
    guint32 maxPicHeightInCtus =
        ((sps->pic_height_max_in_luma_samples - 1) / sps->ctu_size) + 1;

    READ_UE_MAX (&nr, sps->num_subpics_minus1, GST_H266_MAX_SLICES_PER_AU - 1);
    if (sps->num_subpics_minus1 == 0) {
      sps->subpic_ctu_top_left_x[0] = 0;
      sps->subpic_ctu_top_left_y[0] = 0;
      sps->subpic_width_minus1[0] = maxPicWidthInCtus;
      sps->subpic_height_minus1[0] = maxPicHeightInCtus;
      sps->independent_subpics_flag = 1;
      sps->subpic_same_size_flag = 0;
      sps->subpic_treated_as_pic_flag[0] = 1;
      sps->loop_filter_across_subpic_enabled_flag[0] = 0;
    } else {
      guint32 tmpWidthVal, tmpHeightVal, numSubpicCols;

      READ_UINT8 (&nr, sps->independent_subpics_flag, 1);
      READ_UINT8 (&nr, sps->subpic_same_size_flag, 1);

      tmpWidthVal = maxPicWidthInCtus;
      tmpHeightVal = maxPicHeightInCtus;
      numSubpicCols = 1;
      for (i = 0; i <= sps->num_subpics_minus1; i++) {
        if (!sps->subpic_same_size_flag || i == 0) {
          if (i > 0 && sps->pic_width_max_in_luma_samples > sps->ctu_size) {
            READ_UINT16 (&nr, sps->subpic_ctu_top_left_x[i],
                gst_util_ceil_log2 (tmpWidthVal));
          } else
            sps->subpic_ctu_top_left_x[i] = 0;

          if (i > 0 && sps->pic_height_max_in_luma_samples > sps->ctu_size) {
            READ_UINT16 (&nr, sps->subpic_ctu_top_left_y[i],
                gst_util_ceil_log2 (tmpHeightVal));
          } else
            sps->subpic_ctu_top_left_y[i] = 0;

          if (i < sps->num_subpics_minus1
              && sps->pic_width_max_in_luma_samples > sps->ctu_size) {
            READ_UINT16 (&nr, sps->subpic_width_minus1[i],
                gst_util_ceil_log2 (tmpWidthVal));
          } else
            sps->subpic_width_minus1[i] =
                tmpWidthVal - sps->subpic_ctu_top_left_x[i] - 1;

          if (i < sps->num_subpics_minus1
              && sps->pic_height_max_in_luma_samples > sps->ctu_size) {
            READ_UINT16 (&nr, sps->subpic_height_minus1[i],
                gst_util_ceil_log2 (tmpHeightVal));
          } else
            sps->subpic_height_minus1[i] =
                tmpHeightVal - sps->subpic_ctu_top_left_y[i] - 1;

          if (sps->subpic_same_size_flag) {
            numSubpicCols = tmpWidthVal / (sps->subpic_width_minus1[0] + 1);
            if (!(tmpWidthVal % (sps->subpic_width_minus1[0] + 1) == 0)) {
              GST_WARNING ("subpic_width_minus1[0] is invalid.");
              goto error;
            }
            if (!(tmpHeightVal % (sps->subpic_height_minus1[0] + 1) == 0)) {
              GST_WARNING ("subpic_height_minus1[0] is invalid.");
              goto error;
            }
            if (!(numSubpicCols * (tmpHeightVal /
                        (sps->subpic_height_minus1[0] + 1)) ==
                    (sps->num_subpics_minus1 + 1))) {
              GST_WARNING ("when subpic_same_size_flag is equal to, "
                  "num_subpics_minus1 is invalid");
              goto error;
            }
          }
        } else {
          numSubpicCols = tmpWidthVal / (sps->subpic_width_minus1[0] + 1);

          sps->subpic_ctu_top_left_x[i] =
              (i % numSubpicCols) * (sps->subpic_width_minus1[0] + 1);
          sps->subpic_ctu_top_left_y[i] =
              (i / numSubpicCols) * (sps->subpic_height_minus1[0] + 1);
          sps->subpic_width_minus1[i] = sps->subpic_width_minus1[0];
          sps->subpic_height_minus1[i] = sps->subpic_height_minus1[0];
        }

        if (!sps->independent_subpics_flag) {
          READ_UINT8 (&nr, sps->subpic_treated_as_pic_flag[i], 1);
          READ_UINT8 (&nr, sps->loop_filter_across_subpic_enabled_flag[i], 1);
        } else {
          sps->subpic_treated_as_pic_flag[i] = 1;
          sps->loop_filter_across_subpic_enabled_flag[i] = 0;
        }
      }
    }

    READ_UE_MAX (&nr, sps->subpic_id_len_minus1, 15);
    if ((1 << (sps->subpic_id_len_minus1 + 1)) < sps->num_subpics_minus1 + 1) {
      GST_WARNING ("Invalid subpic_id_len_minus1(%d) value",
          sps->subpic_id_len_minus1);
      goto error;
    }

    READ_UINT8 (&nr, sps->subpic_id_mapping_explicitly_signalled_flag, 1);
    if (sps->subpic_id_mapping_explicitly_signalled_flag) {
      READ_UINT8 (&nr, sps->subpic_id_mapping_present_flag, 1);
      if (sps->subpic_id_mapping_present_flag)
        for (i = 0; i <= sps->num_subpics_minus1; i++)
          READ_UINT32 (&nr, sps->subpic_id[i], sps->subpic_id_len_minus1 + 1);
    }
  } else {
    sps->subpic_id_mapping_explicitly_signalled_flag = 0;
    sps->num_subpics_minus1 = 0;
    sps->independent_subpics_flag = 1;
    sps->subpic_ctu_top_left_x[0] = 0;
    sps->subpic_ctu_top_left_y[0] = 0;
    sps->subpic_width_minus1[0] =
        (sps->pic_width_max_in_luma_samples + sps->ctu_size - 1) >>
        gst_util_floor_log2 (sps->ctu_size);
    sps->subpic_height_minus1[0] =
        (sps->pic_height_max_in_luma_samples + sps->ctu_size - 1) >>
        gst_util_floor_log2 (sps->ctu_size);
  }

  if (!sps->subpic_id_mapping_explicitly_signalled_flag ||
      !sps->subpic_id_mapping_present_flag)
    for (i = 0; i <= sps->num_subpics_minus1; i++)
      sps->subpic_id[i] = i;

  READ_UE_MAX (&nr, sps->bitdepth_minus8, 8);

  READ_UINT8 (&nr, sps->entropy_coding_sync_enabled_flag, 1);
  READ_UINT8 (&nr, sps->entry_point_offsets_present_flag, 1);

  READ_UINT8 (&nr, sps->log2_max_pic_order_cnt_lsb_minus4, 4);
  CHECK_ALLOWED_MAX (sps->log2_max_pic_order_cnt_lsb_minus4, 12);

  READ_UINT8 (&nr, sps->poc_msb_cycle_flag, 1);
  if (sps->poc_msb_cycle_flag)
    READ_UE_MAX (&nr, sps->poc_msb_cycle_len_minus1,
        32 - sps->log2_max_pic_order_cnt_lsb_minus4 - 5);

  READ_UINT8 (&nr, sps->num_extra_ph_bytes, 2);
  CHECK_ALLOWED_MAX (sps->num_extra_ph_bytes, 2);
  for (i = 0; i < (sps->num_extra_ph_bytes * 8); i++)
    READ_UINT8 (&nr, sps->extra_ph_bit_present_flag[i], 1);

  READ_UINT8 (&nr, sps->num_extra_sh_bytes, 2);
  CHECK_ALLOWED_MAX (sps->num_extra_sh_bytes, 2);
  for (i = 0; i < (sps->num_extra_sh_bytes * 8); i++)
    READ_UINT8 (&nr, sps->extra_sh_bit_present_flag[i], 1);

  if (sps->ptl_dpb_hrd_params_present_flag) {
    if (sps->max_sublayers_minus1 > 0)
      READ_UINT8 (&nr, sps->sublayer_dpb_params_flag, 1);

    if (!gst_h266_parse_dpb_parameters (&sps->dpb, &nr,
            sps->max_sublayers_minus1, sps->sublayer_dpb_params_flag))
      goto error;
  }

  READ_UE_MAX (&nr, sps->log2_min_luma_coding_block_size_minus2, MIN (4,
          sps->log2_ctu_size_minus5 + 3));
  minCbLog2SizeY = sps->log2_min_luma_coding_block_size_minus2 + 2;
  CHECK_ALLOWED_MAX (minCbLog2SizeY, MIN (6, ctbLog2SizeY));
  minCuSize = 1 << minCbLog2SizeY;
  if (sps->pic_width_max_in_luma_samples % MAX (8, minCuSize) != 0) {
    GST_WARNING ("Coded frame width must be a multiple of "
        "Max(8, the minimum unit size)");
    goto error;
  }
  if (sps->pic_height_max_in_luma_samples % MAX (8, minCuSize) != 0) {
    GST_WARNING ("Coded frame height must be a multiple of "
        "Max(8, the minimum unit size)");
    goto error;
  }

  READ_UINT8 (&nr, sps->partition_constraints_override_enabled_flag, 1);

  READ_UE_MAX (&nr, sps->log2_diff_min_qt_min_cb_intra_slice_luma, MIN (6,
          ctbLog2SizeY) - minCbLog2SizeY);
  minQtLog2SizeIntraY =
      sps->log2_diff_min_qt_min_cb_intra_slice_luma + minCbLog2SizeY;
  minQT[0] = 1 << minQtLog2SizeIntraY;
  CHECK_ALLOWED_MAX (minQT[0], 64);
  CHECK_ALLOWED_MAX (minQT[0], 1 << ctbLog2SizeY);

  READ_UE_MAX (&nr, sps->max_mtt_hierarchy_depth_intra_slice_luma,
      2 * (ctbLog2SizeY - minCbLog2SizeY));
  maxTTSize[0] = maxBTSize[0] = minQT[0];
  if (sps->max_mtt_hierarchy_depth_intra_slice_luma != 0) {
    READ_UE_MAX (&nr, sps->log2_diff_max_bt_min_qt_intra_slice_luma,
        ctbLog2SizeY - minQtLog2SizeIntraY);
    maxBTSize[0] <<= sps->log2_diff_max_bt_min_qt_intra_slice_luma;

    READ_UE_MAX (&nr, sps->log2_diff_max_tt_min_qt_intra_slice_luma, MIN (6,
            ctbLog2SizeY) - minQtLog2SizeIntraY);
    maxTTSize[0] <<= sps->log2_diff_max_tt_min_qt_intra_slice_luma;
  }

  if (sps->chroma_format_idc != 0)
    READ_UINT8 (&nr, sps->qtbtt_dual_tree_intra_flag, 1);

  if (sps->qtbtt_dual_tree_intra_flag) {
    READ_UE_MAX (&nr, sps->log2_diff_min_qt_min_cb_intra_slice_chroma, MIN (6,
            ctbLog2SizeY) - minCbLog2SizeY);
    minQT[2] =
        1 << (sps->log2_diff_min_qt_min_cb_intra_slice_chroma + minCbLog2SizeY);

    READ_UE_MAX (&nr, sps->max_mtt_hierarchy_depth_intra_slice_chroma,
        2 * (ctbLog2SizeY - minCbLog2SizeY));
    maxTTSize[2] = maxBTSize[2] = minQT[2];
    if (sps->max_mtt_hierarchy_depth_intra_slice_chroma != 0) {
      guint minQtLog2SizeIntraC =
          sps->log2_diff_min_qt_min_cb_intra_slice_chroma + minCbLog2SizeY;

      READ_UE_MAX (&nr, sps->log2_diff_max_bt_min_qt_intra_slice_chroma,
          MIN (6, ctbLog2SizeY) - minQtLog2SizeIntraC);
      maxBTSize[2] <<= sps->log2_diff_max_bt_min_qt_intra_slice_chroma;

      READ_UE_MAX (&nr, sps->log2_diff_max_tt_min_qt_intra_slice_chroma,
          MIN (6, ctbLog2SizeY) - minQtLog2SizeIntraC);
      maxTTSize[2] <<= sps->log2_diff_max_tt_min_qt_intra_slice_chroma;
    }
  }

  READ_UE_MAX (&nr, sps->log2_diff_min_qt_min_cb_inter_slice, MIN (6,
          ctbLog2SizeY) - minCbLog2SizeY);
  minQtLog2SizeInterY =
      sps->log2_diff_min_qt_min_cb_inter_slice + minCbLog2SizeY;
  minQT[1] = 1 << minQtLog2SizeInterY;

  READ_UE_MAX (&nr, sps->max_mtt_hierarchy_depth_inter_slice,
      2 * (ctbLog2SizeY - minCbLog2SizeY));
  maxTTSize[1] = maxBTSize[1] = minQT[1];
  if (sps->max_mtt_hierarchy_depth_inter_slice != 0) {
    READ_UE_MAX (&nr, sps->log2_diff_max_bt_min_qt_inter_slice,
        ctbLog2SizeY - minQtLog2SizeInterY);
    maxBTSize[1] <<= sps->log2_diff_max_bt_min_qt_inter_slice;

    READ_UE_MAX (&nr, sps->log2_diff_max_tt_min_qt_inter_slice,
        MIN (6, ctbLog2SizeY) - minQtLog2SizeInterY);
    maxTTSize[1] <<= sps->log2_diff_max_bt_min_qt_inter_slice;
  }

  if (sps->ctu_size > 32)
    READ_UINT8 (&nr, sps->max_luma_transform_size_64_flag, 1);

  READ_UINT8 (&nr, sps->transform_skip_enabled_flag, 1);
  if (sps->transform_skip_enabled_flag) {
    READ_UE_MAX (&nr, sps->log2_transform_skip_max_size_minus2, 3);
    READ_UINT8 (&nr, sps->bdpcm_enabled_flag, 1);
  }

  READ_UINT8 (&nr, sps->mts_enabled_flag, 1);
  if (sps->mts_enabled_flag) {
    READ_UINT8 (&nr, sps->explicit_mts_intra_enabled_flag, 1);
    READ_UINT8 (&nr, sps->explicit_mts_inter_enabled_flag, 1);
  }

  READ_UINT8 (&nr, sps->lfnst_enabled_flag, 1);

  if (sps->chroma_format_idc != 0) {
    if (!gst_h266_parse_chroma_qp_table (sps, &nr))
      goto error;
  } else {
    sps->joint_cbcr_enabled_flag = 0;
    sps->same_qp_table_for_chroma_flag = 0;
  }

  READ_UINT8 (&nr, sps->sao_enabled_flag, 1);

  READ_UINT8 (&nr, sps->alf_enabled_flag, 1);
  if (sps->alf_enabled_flag && sps->chroma_format_idc != 0) {
    READ_UINT8 (&nr, sps->ccalf_enabled_flag, 1);
  } else
    sps->ccalf_enabled_flag = 0;

  READ_UINT8 (&nr, sps->lmcs_enabled_flag, 1);
  READ_UINT8 (&nr, sps->weighted_pred_flag, 1);
  READ_UINT8 (&nr, sps->weighted_bipred_flag, 1);
  READ_UINT8 (&nr, sps->long_term_ref_pics_flag, 1);

  if (sps->vps_id > 0) {
    READ_UINT8 (&nr, sps->inter_layer_prediction_enabled_flag, 1);
  } else
    sps->inter_layer_prediction_enabled_flag = 0;

  READ_UINT8 (&nr, sps->idr_rpl_present_flag, 1);
  if (ptl->general_constraints_info.no_idr_constraint_flag &&
      sps->idr_rpl_present_flag) {
    GST_WARNING ("When gci_no_idr_rpl_constraint_flag equal to 1, "
        "the value of sps_idr_rpl_present_flag shall be equal to 0.");
    goto error;
  }

  READ_UINT8 (&nr, sps->rpl1_same_as_rpl0_flag, 1);
  for (i = 0; i < (sps->rpl1_same_as_rpl0_flag ? 1 : 2); i++) {
    READ_UE_MAX (&nr, sps->num_ref_pic_lists[i], GST_H266_MAX_REF_PIC_LISTS);
    for (j = 0; j < sps->num_ref_pic_lists[i]; j++)
      gst_h266_ref_pic_list_struct (&sps->ref_pic_list_struct[i][j], &nr,
          i, j, sps);
  }

  if (sps->rpl1_same_as_rpl0_flag) {
    sps->num_ref_pic_lists[1] = sps->num_ref_pic_lists[0];
    memcpy (&sps->ref_pic_list_struct[1], &sps->ref_pic_list_struct[0],
        sizeof (sps->ref_pic_list_struct[0]));
  }

  READ_UINT8 (&nr, sps->ref_wraparound_enabled_flag, 1);
  if (sps->ref_wraparound_enabled_flag) {
    for (i = 0; i <= sps->num_subpics_minus1; i++) {
      if (sps->subpic_treated_as_pic_flag[i] &&
          (sps->subpic_width_minus1[i] + 1 !=
              (sps->pic_width_max_in_luma_samples + sps->ctu_size - 1) /
              sps->ctu_size)) {
        GST_WARNING ("sps_ref_wraparound_enabled_flag cannot be equal "
            "to 1 when there is at least one subpicture with "
            "SubPicTreatedAsPicFlag equal to 1 and the subpicture's width "
            "is not equal to picture's width");
        goto error;
      }
    }
  }

  READ_UINT8 (&nr, sps->temporal_mvp_enabled_flag, 1);
  if (sps->temporal_mvp_enabled_flag)
    READ_UINT8 (&nr, sps->sbtmvp_enabled_flag, 1);

  READ_UINT8 (&nr, sps->amvr_enabled_flag, 1);

  READ_UINT8 (&nr, sps->bdof_enabled_flag, 1);
  if (sps->bdof_enabled_flag)
    READ_UINT8 (&nr, sps->bdof_control_present_in_ph_flag, 1);

  READ_UINT8 (&nr, sps->smvd_enabled_flag, 1);

  READ_UINT8 (&nr, sps->dmvr_enabled_flag, 1);
  if (sps->dmvr_enabled_flag)
    READ_UINT8 (&nr, sps->dmvr_control_present_in_ph_flag, 1);

  READ_UINT8 (&nr, sps->mmvd_enabled_flag, 1);
  if (sps->mmvd_enabled_flag)
    READ_UINT8 (&nr, sps->mmvd_fullpel_only_enabled_flag, 1);

  READ_UE_MAX (&nr, sps->six_minus_max_num_merge_cand, 5);
  MaxNumMergeCand = 6 - sps->six_minus_max_num_merge_cand;

  READ_UINT8 (&nr, sps->sbt_enabled_flag, 1);

  READ_UINT8 (&nr, sps->affine_enabled_flag, 1);
  if (sps->affine_enabled_flag) {
    READ_UE_MAX (&nr, sps->five_minus_max_num_subblock_merge_cand,
        5 - sps->sbtmvp_enabled_flag);
    READ_UINT8 (&nr, sps->sps_6param_affine_enabled_flag, 1);
    if (sps->amvr_enabled_flag)
      READ_UINT8 (&nr, sps->affine_amvr_enabled_flag, 1);

    READ_UINT8 (&nr, sps->affine_prof_enabled_flag, 1);
    if (sps->affine_prof_enabled_flag)
      READ_UINT8 (&nr, sps->prof_control_present_in_ph_flag, 1);
  }

  READ_UINT8 (&nr, sps->bcw_enabled_flag, 1);
  READ_UINT8 (&nr, sps->ciip_enabled_flag, 1);

  if (MaxNumMergeCand >= 2) {
    READ_UINT8 (&nr, sps->gpm_enabled_flag, 1);
    if (sps->gpm_enabled_flag && MaxNumMergeCand >= 3)
      READ_UE_MAX (&nr, sps->max_num_merge_cand_minus_max_num_gpm_cand,
          MaxNumMergeCand - 2);
  }

  READ_UE_MAX (&nr, sps->log2_parallel_merge_level_minus2, ctbLog2SizeY - 2);

  READ_UINT8 (&nr, sps->isp_enabled_flag, 1);
  READ_UINT8 (&nr, sps->mrl_enabled_flag, 1);
  READ_UINT8 (&nr, sps->mip_enabled_flag, 1);

  if (sps->chroma_format_idc != 0)
    READ_UINT8 (&nr, sps->cclm_enabled_flag, 1);

  if (sps->chroma_format_idc == 1) {
    READ_UINT8 (&nr, sps->chroma_horizontal_collocated_flag, 1);
    READ_UINT8 (&nr, sps->chroma_vertical_collocated_flag, 1);
  } else {
    sps->chroma_horizontal_collocated_flag = 1;
    sps->chroma_vertical_collocated_flag = 1;
  }

  READ_UINT8 (&nr, sps->palette_enabled_flag, 1);
  if ((ptl->profile_idc == GST_H266_PROFILE_MAIN_12 ||
          ptl->profile_idc == GST_H266_PROFILE_MAIN_12_INTRA ||
          ptl->profile_idc == GST_H266_PROFILE_MAIN_12_STILL_PICTURE) &&
      sps->palette_enabled_flag) {
    GST_WARNING ("sps_palette_enabled_flag shall be equal to 0 "
        "for Main 12 (420) profiles");
    goto error;
  }

  if (sps->chroma_format_idc == 3 && !sps->max_luma_transform_size_64_flag)
    READ_UINT8 (&nr, sps->act_enabled_flag, 1);

  if (sps->transform_skip_enabled_flag || sps->palette_enabled_flag)
    READ_UE_MAX (&nr, sps->min_qp_prime_ts, 8);

  READ_UINT8 (&nr, sps->ibc_enabled_flag, 1);
  if (sps->ibc_enabled_flag)
    READ_UE_MAX (&nr, sps->six_minus_max_num_ibc_merge_cand, 5);

  READ_UINT8 (&nr, sps->ladf_enabled_flag, 1);
  if (sps->ladf_enabled_flag) {
    READ_UINT8 (&nr, sps->num_ladf_intervals_minus2, 2);
    READ_SE_ALLOWED (&nr, sps->ladf_lowest_interval_qp_offset, -63, 63);
    for (i = 0; i < sps->num_ladf_intervals_minus2 + 1; i++) {
      READ_SE_ALLOWED (&nr, sps->ladf_qp_offset[i], -63, 63);
      READ_UE_MAX (&nr, sps->ladf_delta_threshold_minus1[i],
          (2 << (8 + sps->bitdepth_minus8)) - 3);
    }
  }

  READ_UINT8 (&nr, sps->explicit_scaling_list_enabled_flag, 1);
  if (sps->lfnst_enabled_flag && sps->explicit_scaling_list_enabled_flag)
    READ_UINT8 (&nr, sps->scaling_matrix_for_lfnst_disabled_flag, 1);

  if (sps->act_enabled_flag && sps->explicit_scaling_list_enabled_flag)
    READ_UINT8 (&nr,
        sps->scaling_matrix_for_alternative_colour_space_disabled_flag, 1);

  if (sps->scaling_matrix_for_alternative_colour_space_disabled_flag)
    READ_UINT8 (&nr, sps->scaling_matrix_designated_colour_space_flag, 1);

  READ_UINT8 (&nr, sps->dep_quant_enabled_flag, 1);
  READ_UINT8 (&nr, sps->sign_data_hiding_enabled_flag, 1);

  READ_UINT8 (&nr, sps->virtual_boundaries_enabled_flag, 1);
  if (ptl->general_constraints_info.no_virtual_boundaries_constraint_flag &&
      sps->virtual_boundaries_enabled_flag) {
    GST_WARNING ("When gci_no_virtual_boundaries_constraint_flag is "
        "equal to 1, sps_virtual_boundaries_enabled_flag shall be "
        "equal to 0");
    goto error;
  }

  if (sps->virtual_boundaries_enabled_flag) {
    READ_UINT8 (&nr, sps->virtual_boundaries_present_flag, 1);
    if (sps->virtual_boundaries_present_flag) {
      READ_UE (&nr, sps->num_ver_virtual_boundaries);
      if (sps->pic_width_max_in_luma_samples <= 8 &&
          sps->num_ver_virtual_boundaries != 0) {
        GST_WARNING ("SPS: When picture width is less than or equal to 8, "
            "the number of vertical virtual boundaries shall be equal to 0");
        goto error;
      }
      if (sps->num_ver_virtual_boundaries > 3) {
        GST_WARNING ("SPS: The number of vertical virtual boundaries "
            "shall be in the range of 0 to 3");
        goto error;
      }

      for (i = 0; i < sps->num_ver_virtual_boundaries; i++)
        READ_UE_MAX (&nr, sps->virtual_boundary_pos_x_minus1[i],
            (sps->pic_width_max_in_luma_samples + 7) / 8 - 2);

      READ_UE (&nr, sps->num_hor_virtual_boundaries);
      if (sps->pic_height_max_in_luma_samples <= 8 &&
          sps->num_hor_virtual_boundaries != 0) {
        GST_WARNING ("SPS: When picture height is less than or equal to 8, "
            "the number of horizontal virtual boundaries shall be equal to 0");
        goto error;
      }
      if (sps->num_hor_virtual_boundaries > 3) {
        GST_WARNING ("SPS: The number of horizontal virtual boundaries "
            "shall be in the range of 0 to 3");
        goto error;
      }

      for (i = 0; i < sps->num_hor_virtual_boundaries; i++)
        READ_UE_MAX (&nr, sps->virtual_boundary_pos_y_minus1[i],
            (sps->pic_height_max_in_luma_samples + 7) / 8 - 2);
    }
  }

  if (sps->ptl_dpb_hrd_params_present_flag) {
    READ_UINT8 (&nr, sps->timing_hrd_params_present_flag, 1);

    if (sps->timing_hrd_params_present_flag) {
      guint8 firstSubLayer;

      gst_h266_parse_general_timing_hrd_parameters (&sps->general_hrd_params,
          &nr);

      if (sps->max_sublayers_minus1 > 0)
        READ_UINT8 (&nr, sps->sublayer_cpb_params_present_flag, 1);

      firstSubLayer =
          sps->sublayer_cpb_params_present_flag ? 0 : sps->max_sublayers_minus1;
      gst_h266_parse_ols_timing_hrd_parameters (&sps->ols_hrd_params, &nr,
          &sps->general_hrd_params, firstSubLayer, sps->max_sublayers_minus1);
    }
  }

  READ_UINT8 (&nr, sps->field_seq_flag, 1);

  READ_UINT8 (&nr, sps->vui_parameters_present_flag, 1);
  if (sps->vui_parameters_present_flag) {
    READ_UE_MAX (&nr, sps->vui_payload_size_minus1, 1023);

    while (!nal_reader_is_byte_aligned (&nr))
      if (!nal_reader_skip (&nr, 1))
        goto error;

    if (!gst_h266_parse_vui_payload (&sps->vui_params, &nr,
            sps->vui_payload_size_minus1 + 1))
      goto error;
  } else {
    gst_h266_vui_parameters_set_default (&sps->vui_params);
  }

  READ_UINT8 (&nr, sps->extension_flag, 1);
  if (sps->extension_flag) {
    READ_UINT8 (&nr, sps->range_extension_flag, 1);

    for (i = 0; i < 7; i++) {
      READ_UINT8 (&nr, sps->extension_7_flags[i], 1);
      if (sps->extension_7_flags[i]) {
        GST_WARNING ("The value of sps_extension_7bits shall be equal "
            "to 0 in bitstreams conforming to this version of this document");
        goto error;
      }
    }

    if (sps->range_extension_flag) {
      if (sps->bitdepth_minus8 + 8 <= 10) {
        GST_WARNING ("The value of sps_range_extension_flag shall be 0 "
            "when BitDepth is less than or equal to 10.");
        goto error;
      }

      if (!gst_h266_parse_range_extension (&sps->range_params,
              &nr, sps->transform_skip_enabled_flag))
        goto error;
    }
  }

  sps->max_width = sps->pic_width_max_in_luma_samples;
  sps->max_height = sps->pic_height_max_in_luma_samples;
  if (sps->conformance_window_flag) {
    sps->crop_rect_width = sps->max_width -
        (sps->conf_win_left_offset + sps->conf_win_right_offset) * sub_width_c;
    sps->crop_rect_height = sps->max_height -
        (sps->conf_win_top_offset + sps->conf_win_bottom_offset) * sub_height_c;
    sps->crop_rect_x = sps->conf_win_left_offset * sub_width_c;
    sps->crop_rect_y = sps->conf_win_top_offset * sub_height_c;

    GST_LOG ("crop_rectangle x=%u y=%u width=%u, height=%u", sps->crop_rect_x,
        sps->crop_rect_y, sps->crop_rect_width, sps->crop_rect_height);
  }

  /* calculate fps_num fps_den */
  sps->fps_num = 0;
  sps->fps_den = 1;
  if (sps->ptl_dpb_hrd_params_present_flag
      && sps->timing_hrd_params_present_flag) {
    sps->fps_num = sps->general_hrd_params.time_scale;
    sps->fps_den = sps->general_hrd_params.num_units_in_tick;
    GST_LOG ("framerate %d/%d in SPS", sps->fps_num, sps->fps_den);
  } else if (vps && vps->timing_hrd_params_present_flag) {
    sps->fps_num = vps->general_hrd_params.time_scale;
    sps->fps_den = vps->general_hrd_params.num_units_in_tick;
    GST_LOG ("framerate %d/%d in VPS", sps->fps_num, sps->fps_den);
  } else {
    GST_LOG ("unknown framerate");
  }

  sps->valid = TRUE;

  return GST_H266_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Sequence parameter set\"");
  sps->valid = FALSE;
  return GST_H266_PARSER_ERROR;
}

/**
 * gst_h266_parse_aps:
 * @parser: a #GstH266Parser
 * @nalu: The #GST_H266_APS_NUT #GstH266NalUnit to parse
 * @pps: The #GstH266PPS to fill.
 *
 * Parses @data, and fills the @aps structure.
 *
 * Returns: a #GstH266ParserResult
 *
 * Since: 1.26
 */
GstH266ParserResult
gst_h266_parse_aps (GstH266Parser * parser, GstH266NalUnit * nalu,
    GstH266APS * aps)
{
  NalReader nr;
  guint8 params_type;

  GST_LOG ("parsing APS");

  nal_reader_init (&nr, nalu->data + nalu->offset + nalu->header_bytes,
      nalu->size - nalu->header_bytes);

  memset (aps, 0, sizeof (*aps));

  READ_UINT8 (&nr, params_type, 3);
  aps->params_type = params_type;
  READ_UINT8 (&nr, aps->aps_id, 5);
  CHECK_ALLOWED_MAX (aps->aps_id, GST_H266_MAX_APS_COUNT);
  READ_UINT8 (&nr, aps->chroma_present_flag, 1);

  switch (aps->params_type) {
    case GST_H266_ALF_APS:
      if (!gst_h266_parse_alf (&aps->alf, &nr, aps->chroma_present_flag))
        goto error;
      break;
    case GST_H266_LMCS_APS:
      if (!gst_h266_parse_lmcs (&aps->lmcs, &nr, aps->chroma_present_flag))
        goto error;
      break;
    case GST_H266_SCALING_APS:
      if (!gst_h266_parse_scaling_list (&aps->sl, &nr,
              aps->chroma_present_flag))
        goto error;
      break;
    default:
      GST_WARNING ("unknown APS params_type %d", aps->params_type);
      goto error;
      break;
  }

  READ_UINT8 (&nr, aps->extension_flag, 1);
  if (aps->extension_flag) {
    READ_UINT8 (&nr, aps->extension_data_flag, 1);
    if (aps->extension_data_flag) {
      GST_WARNING ("extension_data_flag shall be equal to 0 "
          "in current version aps.");
      goto error;
    }
  }

  aps->valid = TRUE;
  return GST_H266_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Adaptation parameter set\"");
  aps->valid = FALSE;
  return GST_H266_PARSER_ERROR;
}

/**
 * gst_h266_profile_to_string:
 * @profile: a #GstH266Profile
 *
 * Returns the descriptive name for the #GstH266Profile.
 *
 * Returns: (nullable): the name for @profile or %NULL on error
 *
 * Since: 1.26
 */
const gchar *
gst_h266_profile_to_string (GstH266Profile profile)
{
  guint i;

  if (profile <= GST_H266_PROFILE_INVALID || profile >= GST_H266_PROFILE_MAX)
    return NULL;

  for (i = 0; i < G_N_ELEMENTS (h266_profiles); i++) {
    if (profile == h266_profiles[i].profile)
      return h266_profiles[i].name;
  }

  return NULL;
}

/**
 * gst_h266_profile_from_string:
 * @string: the descriptive name for #GstH266Profile
 *
 * Returns a #GstH266Profile for the @string.
 *
 * Returns: the #GstH266Profile of @string or %GST_H266_PROFILE_INVALID on error
 *
 * Since: 1.26
 */
GstH266Profile
gst_h266_profile_from_string (const gchar * string)
{
  guint i;

  if (string == NULL)
    return GST_H266_PROFILE_INVALID;

  for (i = 0; i < G_N_ELEMENTS (h266_profiles); i++) {
    if (g_strcmp0 (string, h266_profiles[i].name) == 0) {
      return h266_profiles[i].profile;
    }
  }

  return GST_H266_PROFILE_INVALID;
}

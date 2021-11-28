/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#pragma once

#include <windows.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/* Might not be defined in dxva.h, copied from DXVA AV1 spec available at
 * https://www.microsoft.com/en-us/download/confirmation.aspx?id=101577
 * and modified with "GST_" prefix
 */
#pragma pack(push, 1)

/* AV1 picture entry data structure */
typedef struct _GST_DXVA_PicEntry_AV1
{
  UINT width;
  UINT height;

  // Global motion parameters
  INT wmmat[6];
  union
  {
    struct
    {
      UCHAR wminvalid:1;
      UCHAR wmtype:2;
      UCHAR Reserved:5;
    };
    UCHAR GlobalMotionFlags;
  } DUMMYUNIONNAME;

  UCHAR Index;
  UINT16 Reserved16Bits;

} GST_DXVA_PicEntry_AV1;

/* AV1 picture parameters structure */
typedef struct _GST_DXVA_PicParams_AV1
{
  UINT width;
  UINT height;

  UINT max_width;
  UINT max_height;

  UCHAR CurrPicTextureIndex;
  UCHAR superres_denom;
  UCHAR bitdepth;
  UCHAR seq_profile;

  // Tiles:
  struct
  {
    UCHAR cols;
    UCHAR rows;
    USHORT context_update_id;
    USHORT widths[64];
    USHORT heights[64];
  } tiles;

  // Coding Tools
  union
  {
    struct
    {
      UINT use_128x128_superblock:1;
      UINT intra_edge_filter:1;
      UINT interintra_compound:1;
      UINT masked_compound:1;
      UINT warped_motion:1;
      UINT dual_filter:1;
      UINT jnt_comp:1;
      UINT screen_content_tools:1;
      UINT integer_mv:1;
      UINT cdef:1;
      UINT restoration:1;
      UINT film_grain:1;
      UINT intrabc:1;
      UINT high_precision_mv:1;
      UINT switchable_motion_mode:1;
      UINT filter_intra:1;
      UINT disable_frame_end_update_cdf:1;
      UINT disable_cdf_update:1;
      UINT reference_mode:1;
      UINT skip_mode:1;
      UINT reduced_tx_set:1;
      UINT superres:1;
      UINT tx_mode:2;
      UINT use_ref_frame_mvs:1;
      UINT enable_ref_frame_mvs:1;
      UINT reference_frame_update:1;
      UINT Reserved:5;
    };
    UINT32 CodingParamToolFlags;
  } coding;

  // Format & Picture Info flags
  union
  {
    struct
    {
      UCHAR frame_type:2;
      UCHAR show_frame:1;
      UCHAR showable_frame:1;
      UCHAR subsampling_x:1;
      UCHAR subsampling_y:1;
      UCHAR mono_chrome:1;
      UCHAR Reserved:1;
    };
    UCHAR FormatAndPictureInfoFlags;
  } format;

  // References
  UCHAR primary_ref_frame;
  UCHAR order_hint;
  UCHAR order_hint_bits;

  GST_DXVA_PicEntry_AV1 frame_refs[7];
  UCHAR RefFrameMapTextureIndex[8];

  // Loop filter parameters
  struct
  {
    UCHAR filter_level[2];
    UCHAR filter_level_u;
    UCHAR filter_level_v;

    UCHAR sharpness_level;
    union
    {
      struct
      {
        UCHAR mode_ref_delta_enabled:1;
        UCHAR mode_ref_delta_update:1;
        UCHAR delta_lf_multi:1;
        UCHAR delta_lf_present:1;
        UCHAR Reserved:4;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;
    CHAR ref_deltas[8];
    CHAR mode_deltas[2];
    UCHAR delta_lf_res;
    UCHAR frame_restoration_type[3];
    USHORT log2_restoration_unit_size[3];
    UINT16 Reserved16Bits;
  } loop_filter;

  // Quantization
  struct
  {
    union
    {
      struct
      {
        UCHAR delta_q_present:1;
        UCHAR delta_q_res:2;
        UCHAR Reserved:5;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;

    UCHAR base_qindex;
    CHAR y_dc_delta_q;
    CHAR u_dc_delta_q;
    CHAR v_dc_delta_q;
    CHAR u_ac_delta_q;
    CHAR v_ac_delta_q;
    // using_qmatrix:
    UCHAR qm_y;
    UCHAR qm_u;
    UCHAR qm_v;
    UINT16 Reserved16Bits;
  } quantization;

  // Cdef parameters
  struct
  {
    union
    {
      struct
      {
        UCHAR damping:2;
        UCHAR bits:2;
        UCHAR Reserved:4;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;

    union
    {
      struct
      {
        UCHAR primary:6;
        UCHAR secondary:2;
      };
      UCHAR combined;
    } y_strengths[8];

    union
    {
      struct
      {
        UCHAR primary:6;
        UCHAR secondary:2;
      };
      UCHAR combined;
    } uv_strengths[8];

  } cdef;

  UCHAR interp_filter;

  // Segmentation
  struct
  {
    union
    {
      struct
      {
        UCHAR enabled:1;
        UCHAR update_map:1;
        UCHAR update_data:1;
        UCHAR temporal_update:1;
        UCHAR Reserved:4;
      };
      UCHAR ControlFlags;
    } DUMMYUNIONNAME;
    UCHAR Reserved24Bits[3];

    union
    {
      struct
      {
        UCHAR alt_q:1;
        UCHAR alt_lf_y_v:1;
        UCHAR alt_lf_y_h:1;
        UCHAR alt_lf_u:1;
        UCHAR alt_lf_v:1;
        UCHAR ref_frame:1;
        UCHAR skip:1;
        UCHAR globalmv:1;
      };
      UCHAR mask;
    } feature_mask[8];

    SHORT feature_data[8][8];

  } segmentation;

  struct
  {
    union
    {
      struct
      {
        USHORT apply_grain:1;
        USHORT scaling_shift_minus8:2;
        USHORT chroma_scaling_from_luma:1;
        USHORT ar_coeff_lag:2;
        USHORT ar_coeff_shift_minus6:2;
        USHORT grain_scale_shift:2;
        USHORT overlap_flag:1;
        USHORT clip_to_restricted_range:1;
        USHORT matrix_coeff_is_identity:1;
        USHORT Reserved:3;
      };
      USHORT ControlFlags;
    } DUMMYUNIONNAME;

    USHORT grain_seed;
    UCHAR scaling_points_y[14][2];
    UCHAR num_y_points;
    UCHAR scaling_points_cb[10][2];
    UCHAR num_cb_points;
    UCHAR scaling_points_cr[10][2];
    UCHAR num_cr_points;
    UCHAR ar_coeffs_y[24];
    UCHAR ar_coeffs_cb[25];
    UCHAR ar_coeffs_cr[25];
    UCHAR cb_mult;
    UCHAR cb_luma_mult;
    UCHAR cr_mult;
    UCHAR cr_luma_mult;
    UCHAR Reserved8Bits;
    SHORT cb_offset;
    SHORT cr_offset;
  } film_grain;

  UINT Reserved32Bits;
  UINT StatusReportFeedbackNumber;
} GST_DXVA_PicParams_AV1;

/* AV1 tile structure */
typedef struct _GST_DXVA_Tile_AV1
{
  UINT DataOffset;
  UINT DataSize;
  USHORT row;
  USHORT column;
  UINT16 Reserved16Bits;
  UCHAR anchor_frame;
  UCHAR Reserved8Bits;
} GST_DXVA_Tile_AV1;

/* AV1 status reporting data structure */
typedef struct _GST_DXVA_Status_AV1
{
  UINT StatusReportFeedbackNumber;
  GST_DXVA_PicEntry_AV1 CurrPic;
  UCHAR BufType;
  UCHAR Status;
  UCHAR Reserved8Bits;
  USHORT NumMbsAffected;
} GST_DXVA_Status_AV1;

#pragma pack(pop)

G_END_DECLS

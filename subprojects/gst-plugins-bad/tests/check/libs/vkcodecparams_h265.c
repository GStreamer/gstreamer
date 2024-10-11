/* GStreamer
 *
 * Copyright (C) 2024 Igalia, S.L.
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

/* H.265: 1 frame 320x240 blue box  */
StdVideoH265HrdParameters h265_std_hrd = { 0, };

static StdVideoH265ProfileTierLevel h265_std_ptl = {
  .flags = {
        .general_progressive_source_flag = 1,
        .general_frame_only_constraint_flag = 1,
      },
  .general_profile_idc = STD_VIDEO_H265_PROFILE_IDC_MAIN,
  .general_level_idc = STD_VIDEO_H265_LEVEL_IDC_6_0,
};

static StdVideoH265DecPicBufMgr h265_std_pbm = {
  .max_latency_increase_plus1 = {5, 0,},
  .max_dec_pic_buffering_minus1 = {4, 0,},
  .max_num_reorder_pics = {2, 0,},
};

static StdVideoH265VideoParameterSet h265_std_vps = {
  .flags = {
        .vps_temporal_id_nesting_flag = 1,
        .vps_sub_layer_ordering_info_present_flag = 1,
      },
  .vps_video_parameter_set_id = 0,
  .pDecPicBufMgr = &h265_std_pbm,
  .pHrdParameters = &h265_std_hrd,
  .pProfileTierLevel = &h265_std_ptl,
};

static StdVideoH265SequenceParameterSetVui h265_std_sps_vui = {
  .flags = {
        .video_signal_type_present_flag = 1,
        .vui_timing_info_present_flag = 1,
      },
  .aspect_ratio_idc = STD_VIDEO_H265_ASPECT_RATIO_IDC_UNSPECIFIED,
  .video_format = 5,
  .colour_primaries = 2,
  .transfer_characteristics = 2,
  .matrix_coeffs = 2,
  .vui_num_units_in_tick = 1,
  .vui_time_scale = 25,
  .pHrdParameters = &h265_std_hrd,
};

static StdVideoH265SequenceParameterSet h265_std_sps = {
  .flags = {
        .sps_temporal_id_nesting_flag = 1,
        .sps_sub_layer_ordering_info_present_flag = 1,
        .sample_adaptive_offset_enabled_flag = 1,
        .sps_temporal_mvp_enabled_flag = 1,
        .strong_intra_smoothing_enabled_flag = 1,
        .vui_parameters_present_flag = 1,
        .sps_extension_present_flag = 1,
      },
  .chroma_format_idc = STD_VIDEO_H265_CHROMA_FORMAT_IDC_420,
  .pic_width_in_luma_samples = 320,
  .pic_height_in_luma_samples = 240,
  .sps_video_parameter_set_id = 0,
  .sps_seq_parameter_set_id = 0,
  .log2_max_pic_order_cnt_lsb_minus4 = 4,
  .log2_diff_max_min_luma_coding_block_size = 3,
  .log2_diff_max_min_luma_transform_block_size = 3,
  .pProfileTierLevel = &h265_std_ptl,
  .pDecPicBufMgr = &h265_std_pbm,
  .pSequenceParameterSetVui = &h265_std_sps_vui,
};

static StdVideoH265PictureParameterSet h265_std_pps = {
  .flags = {
        .sign_data_hiding_enabled_flag = 1,
        .cu_qp_delta_enabled_flag = 1,
        .weighted_pred_flag = 1,
        .entropy_coding_sync_enabled_flag = 1,
        .uniform_spacing_flag = 1,
        .loop_filter_across_tiles_enabled_flag = 1,
        .pps_loop_filter_across_slices_enabled_flag = 1,
      },
  .pps_pic_parameter_set_id = 0,
  .pps_seq_parameter_set_id = 0,
  .sps_video_parameter_set_id = 0,
  .diff_cu_qp_delta_depth = 1,
};

static const uint8_t h265_slice[] = {
  0x28, 0x01, 0xaf, 0x1d, 0x21, 0x6a, 0x83, 0x40, 0xf7, 0xcf, 0x80, 0xff, 0xf8,
  0x90, 0xfa, 0x3b, 0x77, 0x87, 0x96, 0x96, 0xba, 0xfa, 0xcd, 0x61, 0xb5, 0xe3,
  0xc1, 0x02, 0x2d, 0xe0, 0xa8, 0x17, 0x96, 0x03, 0x4c, 0x4e, 0x1a, 0x9e, 0xd0,
  0x93, 0x0b, 0x93, 0x40, 0x00, 0x05, 0xec, 0x87, 0x00, 0x00, 0x03, 0x00, 0x00,
  0x03, 0x00, 0x56, 0x40
};

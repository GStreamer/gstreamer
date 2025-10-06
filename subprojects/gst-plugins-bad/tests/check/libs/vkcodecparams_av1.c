/* GStreamer
 *
 * Copyright (C) 2026 Igalia, S.L.
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

/* 2 frames 320x240 blue box - AV1 OBU format */

#define AV1_FRAME_WIDTH 320
#define AV1_FRAME_HEIGHT 240

static StdVideoAV1TimingInfo av1_std_timing_info = {
  .flags = {
        .equal_picture_interval = 0,
      },
  .num_units_in_display_tick = 0,
  .time_scale = 0,
  .num_ticks_per_picture_minus_1 = 0,
};

static StdVideoAV1ColorConfig av1_std_color_config = {
  .flags = {
        .mono_chrome = 0,
        .color_range = 0,
        .separate_uv_delta_q = 0,
      },
  .BitDepth = 8,
  .subsampling_x = 1,
  .subsampling_y = 1,
  .color_primaries = STD_VIDEO_AV1_COLOR_PRIMARIES_UNSPECIFIED,
  .transfer_characteristics =
      STD_VIDEO_AV1_TRANSFER_CHARACTERISTICS_UNSPECIFIED,
  .matrix_coefficients = STD_VIDEO_AV1_MATRIX_COEFFICIENTS_UNSPECIFIED,
};

static StdVideoAV1SequenceHeader av1_std_sequence = {
  .flags = {
        .still_picture = 0,
        .reduced_still_picture_header = 0,
        .use_128x128_superblock = 1,
        .enable_filter_intra = 1,
        .enable_intra_edge_filter = 1,
        .enable_interintra_compound = 1,
        .enable_masked_compound = 1,
        .enable_warped_motion = 1,
        .enable_dual_filter = 1,
        .enable_order_hint = 1,
        .enable_jnt_comp = 1,
        .enable_ref_frame_mvs = 1,
        .frame_id_numbers_present_flag = 1,
        .enable_superres = 0,
        .enable_cdef = 1,
        .enable_restoration = 1,
        .film_grain_params_present = 0,
        .timing_info_present_flag = 0,
        .initial_display_delay_present_flag = 0,
      },
  .seq_profile = STD_VIDEO_AV1_PROFILE_MAIN,
  .frame_width_bits_minus_1 = 8,
  .frame_height_bits_minus_1 = 7,
  .max_frame_width_minus_1 = AV1_FRAME_WIDTH - 1,
  .max_frame_height_minus_1 = AV1_FRAME_HEIGHT - 1,
  .delta_frame_id_length_minus_2 = 12,
  .additional_frame_id_length_minus_1 = 0,
  .order_hint_bits_minus_1 = 6,
  .seq_force_integer_mv = STD_VIDEO_AV1_SELECT_INTEGER_MV,
  .seq_force_screen_content_tools = STD_VIDEO_AV1_SELECT_SCREEN_CONTENT_TOOLS,
  .pTimingInfo = &av1_std_timing_info,
  .pColorConfig = &av1_std_color_config,
};

/* Frame 1: Keyframe */
static const uint8_t av1_obu[] = {
  0x12, 0x00, 0x0a, 0x0c, 0x00, 0x00, 0x00, 0x04, 0x3c, 0xff, 0xbf, 0x83,
  0xff, 0xf3, 0x00, 0x80, 0x32, 0x21, 0x11, 0x49, 0x48, 0x01, 0x00, 0x00,
  0x00, 0x64, 0xe4, 0xf5, 0xfc, 0xb3, 0xb1, 0x6a, 0x70, 0x7f, 0x1c, 0xc1,
  0x2f, 0x98, 0xfb, 0x55, 0x45, 0xb6, 0xbf, 0xba, 0x9c, 0x89, 0x58, 0xf2,
  0x08, 0xb1, 0x80
};

/* Frame 2: Inter-frame */
static const uint8_t av1_obu_2[] = {
  0x12, 0x00, 0x32, 0x25, 0x38, 0xa4, 0xa8, 0x04, 0x08, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x14, 0x00, 0x00, 0x00, 0xf1,
  0x6f, 0x9f, 0x78, 0x9e, 0xcc
};

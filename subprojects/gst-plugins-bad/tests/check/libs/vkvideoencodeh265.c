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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/codecparsers/gsth265parser.h>
#include <math.h>

#include "vkvideoencodebase.c"

// Include h265 std session params
#include "vkcodecparams_h265.c"

typedef struct
{
  GstVulkanEncoderPicture picture;

  gboolean is_ref;
  gint pic_num;

  VkVideoEncodeH265PictureInfoKHR enc_pic_info;
  VkVideoEncodeH265NaluSliceSegmentInfoKHR slice_info;
  VkVideoEncodeH265DpbSlotInfoKHR dpb_slot_info;
  VkVideoEncodeH265RateControlInfoKHR rc_info;

  StdVideoEncodeH265WeightTable slice_wt;
  StdVideoEncodeH265SliceSegmentHeader slice_hdr;
  StdVideoEncodeH265PictureInfo pic_info;
  StdVideoEncodeH265ReferenceInfo ref_info;
  StdVideoEncodeH265ReferenceListsInfo ref_list_info;
  StdVideoH265ShortTermRefPicSet short_term_ref_pic_set;
} GstVulkanH265EncodeFrame;


static GstVulkanH265EncodeFrame *
_h265_encode_frame_new (GstVulkanEncoder * enc, GstBuffer * img_buffer,
    gsize size, gboolean is_ref)
{
  GstVulkanH265EncodeFrame *frame;

  frame = g_new (GstVulkanH265EncodeFrame, 1);
  fail_unless (gst_vulkan_encoder_picture_init (&frame->picture, enc,
          img_buffer, size));
  frame->is_ref = is_ref;

  return frame;
}

static void
_h265_encode_frame_free (GstVulkanEncoder * enc, gpointer pframe)
{
  GstVulkanH265EncodeFrame *frame = (GstVulkanH265EncodeFrame *) pframe;

  gst_vulkan_encoder_picture_clear (&frame->picture, enc);
  g_free (frame);
}

/* allocate a frame to be encoded from given buffer pools */
static GstVulkanH265EncodeFrame *
allocate_h265_frame (GstVulkanEncoder * enc, int width,
    int height, gboolean is_ref)
{
  GstVulkanH265EncodeFrame *frame;
  GstBuffer *in_buffer, *img_buffer;

  /* generate the input buffer */
  in_buffer = generate_input_buffer (buffer_pool, width, height);

  /* get a Vulkan image buffer out of the input buffer */
  upload_buffer_to_image (img_pool, in_buffer, &img_buffer);

  frame = _h265_encode_frame_new (enc, img_buffer, width * height * 3, is_ref);
  fail_unless (frame);
  gst_buffer_unref (in_buffer);
  gst_buffer_unref (img_buffer);

  return frame;
}

#define PICTURE_TYPE(slice_type, is_ref)                                \
    (slice_type == STD_VIDEO_H265_SLICE_TYPE_I && is_ref) ?    \
    STD_VIDEO_H265_PICTURE_TYPE_IDR : \
    slice_type == STD_VIDEO_H265_SLICE_TYPE_P ? STD_VIDEO_H265_PICTURE_TYPE_P: \
    slice_type == STD_VIDEO_H265_SLICE_TYPE_B ? STD_VIDEO_H265_PICTURE_TYPE_B: \
    (StdVideoH265PictureType) slice_type

static void
setup_codec_pic (GstVulkanEncoderPicture * pic, VkVideoEncodeInfoKHR * info,
    gpointer data)
{
  GstVulkanH265EncodeFrame *frame = (GstVulkanH265EncodeFrame *) pic;

  info->pNext = &frame->enc_pic_info;
  pic->dpb_slot.pNext = &frame->dpb_slot_info;

  {
    /* *INDENT-OFF* */
    frame->enc_pic_info = (VkVideoEncodeH265PictureInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR,
      .pNext = NULL,
      .naluSliceSegmentEntryCount = 1,
      .pNaluSliceSegmentEntries = &frame->slice_info,
      .pStdPictureInfo = &frame->pic_info,
    };
    frame->dpb_slot_info = (VkVideoEncodeH265DpbSlotInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR,
      .pNext = NULL,
      .pStdReferenceInfo = &frame->ref_info,
    };
    /* *INDENT-ON* */
  }
}

static void
setup_rc_codec (GstVulkanEncoderPicture * pic,
    VkVideoEncodeRateControlInfoKHR * rc_info,
    VkVideoEncodeRateControlLayerInfoKHR * rc_layer, gpointer data)
{
  GstVulkanH265EncodeFrame *frame = (GstVulkanH265EncodeFrame *) pic;

  /* *INDENT-OFF* */
  frame->rc_info = (VkVideoEncodeH265RateControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR,
    .flags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
        VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .pNext = NULL,
    .gopFrameCount = 1,
    .idrPeriod = 1,
    .consecutiveBFrameCount = 0,
  };
  /* *INDENT-ON* */

  rc_info->pNext = &frame->rc_info;
}

static void
encode_frame (GstVulkanEncoder * enc, GstVulkanH265EncodeFrame * frame,
    StdVideoH265SliceType slice_type, guint frame_num,
    GstVulkanH265EncodeFrame ** list0, gint list0_num,
    GstVulkanH265EncodeFrame ** list1, gint list1_num, gint vps_id, gint sps_id,
    gint pps_id)
{
  GstVulkanVideoCapabilities enc_caps;
  int i, ref_pics_num = 0;
  GstVulkanEncoderPicture *ref_pics[16] = { NULL, };
  gint16 delta_poc_s0_minus1 = 0, delta_poc_s1_minus1 = 0;
  GstVulkanEncoderPicture *picture = &frame->picture;
  gint picture_type = PICTURE_TYPE (slice_type, frame->is_ref);
  GstVulkanEncoderCallbacks cb = { setup_codec_pic, setup_rc_codec };

  GST_DEBUG ("Encoding frame num: %d", frame_num);

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  gst_vulkan_encoder_set_callbacks (enc, &cb, &enc_caps, NULL);

  ref_pics_num = list0_num + list1_num;

  frame->slice_wt = (StdVideoEncodeH265WeightTable) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265WeightTableFlags) {
        .luma_weight_l0_flag = 0,
        .chroma_weight_l0_flag = 0,
        .luma_weight_l1_flag = 0,
        .chroma_weight_l1_flag = 0,
    },
    .luma_log2_weight_denom = 0,
    .delta_chroma_log2_weight_denom = 0,
    .delta_luma_weight_l0 = { 0 },
    .luma_offset_l0 = { 0 },
    .delta_chroma_weight_l0 = { { 0 } },
    .delta_chroma_offset_l0 = { { 0 } },
    .delta_luma_weight_l1 = { 0 },
    .luma_offset_l1 = { 0 },
    .delta_chroma_weight_l1 = { { 0 } },
    .delta_chroma_offset_l1 = { { 0 } },
    /* *INDENT-ON* */
  };

  frame->slice_hdr = (StdVideoEncodeH265SliceSegmentHeader) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265SliceSegmentHeaderFlags) {
      .first_slice_segment_in_pic_flag = 1,
      .dependent_slice_segment_flag = 0,
      .slice_sao_luma_flag = 1,
      .slice_sao_chroma_flag = 1,
      .num_ref_idx_active_override_flag = 0,
      .mvd_l1_zero_flag = 0,
      .cabac_init_flag = 0,
      .cu_chroma_qp_offset_enabled_flag = 1,
      .deblocking_filter_override_flag = 1,
      .slice_deblocking_filter_disabled_flag = 0,
      .collocated_from_l0_flag = 0,
      .slice_loop_filter_across_slices_enabled_flag = 0,
    },
    .slice_type = slice_type,
    .slice_segment_address = 0,
    .collocated_ref_idx = 0,
    .MaxNumMergeCand = 5,
    .slice_cb_qp_offset = 0,
    .slice_cr_qp_offset = 0,
    .slice_beta_offset_div2 = 0,
    .slice_tc_offset_div2 = 0,
    .slice_act_y_qp_offset = 0,
    .slice_act_cb_qp_offset = 0,
    .slice_act_cr_qp_offset = 0,
    .slice_qp_delta = 0,
    .pWeightTable = &frame->slice_wt,
    /* *INDENT-ON* */
  };

  if (list0_num)
    delta_poc_s0_minus1 = frame->pic_num - list0[0]->pic_num - 1;
  if (list1_num)
    delta_poc_s1_minus1 = list1[0]->pic_num - frame->pic_num - 1;

  frame->short_term_ref_pic_set = (StdVideoH265ShortTermRefPicSet) {
    /* *INDENT-OFF* */
    .flags = (StdVideoH265ShortTermRefPicSetFlags) {
      .inter_ref_pic_set_prediction_flag = 0u,
      .delta_rps_sign = 0u,
    },
    .delta_idx_minus1 = 0,
    .use_delta_flag = 0,
    .abs_delta_rps_minus1 = 0,
    .used_by_curr_pic_flag = 0,
    .used_by_curr_pic_s0_flag  = list0_num ? 1 : 0,
    .used_by_curr_pic_s1_flag = list1_num ? 1 : 0,
    .num_negative_pics = list0_num,
    .num_positive_pics = list1_num,
    .delta_poc_s0_minus1 = {delta_poc_s0_minus1},
    .delta_poc_s1_minus1 = {delta_poc_s1_minus1},
    /* *INDENT-ON* */
  };

  frame->pic_info = (StdVideoEncodeH265PictureInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265PictureInfoFlags) {
          .is_reference = frame->is_ref,
          .IrapPicFlag = (picture_type == STD_VIDEO_H265_PICTURE_TYPE_IDR),
          .used_for_long_term_reference = 0,
          .discardable_flag = 0,
          .cross_layer_bla_flag = 0,
          .pic_output_flag = (picture_type == STD_VIDEO_H265_PICTURE_TYPE_IDR),
          .no_output_of_prior_pics_flag = (picture_type == STD_VIDEO_H265_PICTURE_TYPE_IDR),
          .short_term_ref_pic_set_sps_flag = (picture_type != STD_VIDEO_H265_PICTURE_TYPE_IDR),
          .slice_temporal_mvp_enabled_flag = 0,
    },
    .pic_type = picture_type,
    .sps_video_parameter_set_id = vps_id,
    .pps_seq_parameter_set_id = sps_id,
    .pps_pic_parameter_set_id = pps_id,
    .PicOrderCntVal = frame->pic_num,
    .pShortTermRefPicSet = &frame->short_term_ref_pic_set,
    .pLongTermRefPics = NULL,
    /* *INDENT-ON* */
  };

  if (ref_pics_num > 0) {
    frame->ref_list_info = (StdVideoEncodeH265ReferenceListsInfo) {
      /* *INDENT-OFF* */
      .flags = (StdVideoEncodeH265ReferenceListsInfoFlags) {
        .ref_pic_list_modification_flag_l0 = 0,
        .ref_pic_list_modification_flag_l1 = 0,
      },
      .num_ref_idx_l0_active_minus1 = 0,
      .num_ref_idx_l1_active_minus1 = 0,
      .RefPicList0 = {0, },
      .RefPicList1 = {0, },
      .list_entry_l0 = {0, },
      .list_entry_l1 = {0, },
      /* *INDENT-ON* */
    };
    frame->pic_info.pRefLists = &frame->ref_list_info;
  }

  memset (frame->ref_list_info.RefPicList0, STD_VIDEO_H265_NO_REFERENCE_PICTURE,
      STD_VIDEO_H265_MAX_NUM_LIST_REF);
  memset (frame->ref_list_info.RefPicList1, STD_VIDEO_H265_NO_REFERENCE_PICTURE,
      STD_VIDEO_H265_MAX_NUM_LIST_REF);

  /* *INDENT-OFF* */
  frame->slice_info = (VkVideoEncodeH265NaluSliceSegmentInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR,
    .pNext = NULL,
    .pStdSliceSegmentHeader = &frame->slice_hdr,
    .constantQp = 26,
  };

  fail_unless(frame->slice_info.constantQp >= enc_caps.encoder.codec.h265.minQp);

  frame->rc_info = (VkVideoEncodeH265RateControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_INFO_KHR,
  };

  frame->ref_info = (StdVideoEncodeH265ReferenceInfo) {
    .flags = (StdVideoEncodeH265ReferenceInfoFlags) {
      .used_for_long_term_reference = 0,
      .unused_for_reference = 0,
    },
    .pic_type = picture_type,
    .PicOrderCntVal = frame->pic_num,
    .TemporalId = 0,
  };
  /* *INDENT-ON* */

  picture->codec_rc_info = &frame->rc_info;

  for (i = 0; i < list0_num; i++) {
    ref_pics[i] = &list0[i]->picture;
    frame->ref_list_info.RefPicList0[0] = list0[i]->picture.dpb_slot.slotIndex;
  }
  for (i = 0; i < list1_num; i++) {
    ref_pics[i + list0_num] = &list1[i]->picture;
    frame->ref_list_info.RefPicList1[i] = list1[i]->picture.dpb_slot.slotIndex;
  }

  fail_unless (gst_vulkan_encoder_encode (enc, &in_info, picture, ref_pics_num,
          ref_pics));
}

static void
check_h265_nalu (guint8 * bitstream, gsize size, GstH265NalUnitType nal_type)
{
  GstH265ParserResult res;
  GstH265NalUnit nalu;
  GstH265Parser *const parser = gst_h265_parser_new ();
  res = gst_h265_parser_identify_nalu (parser, bitstream, 0, size, &nalu);
  assert_equals_int (res, GST_H265_PARSER_NO_NAL_END);
  assert_equals_int (nalu.type, nal_type);
  switch (nal_type) {
    case GST_H265_NAL_VPS:
    {
      GstH265VPS vps;
      res = gst_h265_parser_parse_vps (parser, &nalu, &vps);
      break;
    }
    case GST_H265_NAL_SPS:
    {
      GstH265SPS sps;
      res = gst_h265_parser_parse_sps (parser, &nalu, &sps, FALSE);
      break;
    }
    case GST_H265_NAL_PPS:
    {
      GstH265PPS pps;
      res = gst_h265_parser_parse_pps (parser, &nalu, &pps);

      break;
    }
    default:
      res = gst_h265_parser_parse_nal (parser, &nalu);
      break;
  }
  assert_equals_int (res, GST_H265_PARSER_OK);

  gst_h265_parser_free (parser);
}

static void
check_h265_session_params (GstVulkanEncoder * enc, gint vps_id, gint sps_id,
    gint pps_id)
{
  GError *err = NULL;
  GstVulkanEncoderParametersFeedback feedback = { 0, };
  guint8 *bitstream = NULL;
  gsize bitstream_size = 0;
  // Check VPS
  GstVulkanEncoderParametersOverrides override_params = {
    .h265 = {
          .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR,
          .writeStdVPS = VK_TRUE,
          .writeStdSPS = VK_FALSE,
          .writeStdPPS = VK_FALSE,
          .stdVPSId = 0,
          .stdSPSId = 0,
          .stdPPSId = 0,
        }
  };
  fail_unless (gst_vulkan_encoder_video_session_parameters_overrides (enc,
          &override_params, &feedback, &bitstream_size,
          (gpointer *) & bitstream, &err));
  check_h265_nalu (bitstream, bitstream_size, GST_H265_NAL_VPS);
  g_free (bitstream);

  // Check SPS
  override_params = (GstVulkanEncoderParametersOverrides) {
    .h265 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR,
      .writeStdVPS = VK_FALSE,
      .writeStdSPS = VK_TRUE,
      .writeStdPPS = VK_FALSE,
      .stdVPSId = 0,
      .stdSPSId = 0,
      .stdPPSId = 0,
    }
  };
  fail_unless (gst_vulkan_encoder_video_session_parameters_overrides (enc,
          &override_params, &feedback, &bitstream_size,
          (gpointer *) & bitstream, &err));
  check_h265_nalu (bitstream, bitstream_size, GST_H265_NAL_SPS);
  g_free (bitstream);

  // Check PPS
  override_params = (GstVulkanEncoderParametersOverrides) {
    .h265 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR,
      .writeStdVPS = VK_FALSE,
      .writeStdSPS = VK_FALSE,
      .writeStdPPS = VK_TRUE,
      .stdVPSId = 0,
      .stdSPSId = 0,
      .stdPPSId = 0,
    }
  };
  fail_unless (gst_vulkan_encoder_video_session_parameters_overrides (enc,
          &override_params, &feedback, &bitstream_size,
          (gpointer *) & bitstream, &err));
  check_h265_nalu (bitstream, bitstream_size, GST_H265_NAL_PPS);
  g_free (bitstream);
}

static GstVulkanEncoder *
setup_h265_encoder (uint32_t width, uint32_t height, gint vps_id,
    gint sps_id, gint pps_id)
{
  GstVulkanEncoder *enc;
  GError *err = NULL;
  uint32_t mbAlignedWidth, mbAlignedHeight;
  StdVideoH265ProfileIdc profile_idc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
  GstVulkanVideoProfile profile;
  GstVulkanEncoderParameters enc_params;
  VkVideoEncodeH265SessionParametersAddInfoKHR params_add;
  GstVulkanVideoCapabilities enc_caps;
  gint min_ctb_size = 64, max_ctb_size = 16;
  gint max_tb_size = 0, min_tb_size = 0;
  gint max_transform_hierarchy;
  GstVulkanEncoderQualityProperties quality_props;

  /* *INDENT-OFF* */
  profile = (GstVulkanVideoProfile) {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.codec,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    },
    .usage.encode = {
      .pNext = &profile.codec,
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
      .videoUsageHints = VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
      .videoContentHints = VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
      .tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
    },
    .codec.h265enc = (VkVideoEncodeH265ProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR,
      .stdProfileIdc = profile_idc,
    }
  };
  quality_props = (GstVulkanEncoderQualityProperties) {
    .quality_level = -1,
    .codec.h265 = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR,
    },
  };
  /* *INDENT-ON* */

  setup_queue (VK_QUEUE_VIDEO_ENCODE_BIT_KHR,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR);

  if (!video_queue) {
    GST_WARNING ("Unable to find encoding queue");
    return NULL;
  }

  if (!graphics_queue) {
    GST_WARNING ("Unable to find graphics queue");
    return NULL;
  }

  enc = gst_vulkan_encoder_create_from_queue (video_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR);

  if (!enc) {
    GST_WARNING ("Unable to create a vulkan encoder, queue=%p", video_queue);
    return NULL;
  }

  fail_unless (gst_vulkan_encoder_quality_level (enc) == -1);

  fail_unless (gst_vulkan_encoder_start (enc, &profile, &quality_props, &err));

  fail_unless (gst_vulkan_encoder_quality_level (enc) > -1);

  fail_unless (gst_vulkan_encoder_is_started (enc));

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  if (enc_caps.encoder.codec.h265.ctbSizes
      & VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR)
    max_ctb_size = 64;
  else if (enc_caps.encoder.codec.h265.ctbSizes
      & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR)
    max_ctb_size = 32;
  else if (enc_caps.encoder.codec.h265.ctbSizes
      & VK_VIDEO_ENCODE_H265_CTB_SIZE_16_BIT_KHR)
    min_ctb_size = 16;

  if (enc_caps.encoder.codec.h265.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
    min_tb_size = 4;
  else if (enc_caps.encoder.codec.h265.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
    min_tb_size = 8;
  else if (enc_caps.encoder.codec.h265.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
    min_tb_size = 16;
  else if (enc_caps.encoder.codec.h265.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
    min_tb_size = 32;

  if (enc_caps.encoder.codec.h265.transformBlockSizes
      & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
    max_tb_size = 32;
  else if (enc_caps.encoder.codec.h265.transformBlockSizes
      & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
    max_tb_size = 16;
  else if (enc_caps.encoder.codec.h265.transformBlockSizes
      & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
    max_tb_size = 8;
  else if (enc_caps.encoder.codec.h265.transformBlockSizes
      & VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
    max_tb_size = 4;

  max_transform_hierarchy =
      gst_util_ceil_log2 (max_ctb_size) - gst_util_ceil_log2 (min_tb_size);

  h265_std_ptl.general_profile_idc = profile_idc;
  h265_std_vps.vps_video_parameter_set_id = vps_id;

  mbAlignedWidth = (width + min_ctb_size - 1) & ~(min_ctb_size - 1);
  mbAlignedHeight = (height + min_ctb_size - 1) & ~(min_ctb_size - 1);

  h265_std_sps.pic_width_in_luma_samples = mbAlignedWidth;
  h265_std_sps.pic_height_in_luma_samples = mbAlignedHeight;
  h265_std_sps.sps_video_parameter_set_id = vps_id;
  h265_std_sps.sps_seq_parameter_set_id = sps_id;
  h265_std_sps.bit_depth_luma_minus8 = 0;       //gst_vulkan_video_get_component_bit_depth (bit_depth_luma) - 8,
  h265_std_sps.bit_depth_chroma_minus8 = 0;     //gst_vulkan_video_get_component_bit_depth (bit_depth_chroma) - 8,
  h265_std_sps.log2_diff_max_min_luma_coding_block_size =
      gst_util_ceil_log2 (max_ctb_size) - 3;
  h265_std_sps.log2_min_luma_transform_block_size_minus2 =
      gst_util_ceil_log2 (min_tb_size) - 2;
  h265_std_sps.log2_diff_max_min_luma_transform_block_size =
      gst_util_ceil_log2 (max_tb_size) - gst_util_ceil_log2 (min_tb_size);
  h265_std_sps.max_transform_hierarchy_depth_inter = max_transform_hierarchy;
  h265_std_sps.max_transform_hierarchy_depth_intra = max_transform_hierarchy;
  h265_std_sps.conf_win_left_offset = 0;
  h265_std_sps.conf_win_right_offset = (mbAlignedWidth - width) / 2;
  h265_std_sps.conf_win_top_offset = (mbAlignedHeight - height) / 2;
  h265_std_sps.conf_win_bottom_offset = 0;

  h265_std_pps.flags.transform_skip_enabled_flag =
      enc_caps.encoder.codec.h265.stdSyntaxFlags
      & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR
      ? 1 : 0;
  h265_std_pps.flags.weighted_pred_flag =
      enc_caps.encoder.codec.h265.stdSyntaxFlags
      & VK_VIDEO_ENCODE_H265_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ? 1 : 0;
  h265_std_pps.flags.entropy_coding_sync_enabled_flag =
      (enc_caps.encoder.codec.h265.maxTiles.width > 1
      || enc_caps.encoder.codec.h265.maxTiles.height > 1)
      ? 1 : 0;
  h265_std_pps.sps_video_parameter_set_id = vps_id;
  h265_std_pps.pps_seq_parameter_set_id = sps_id;
  h265_std_pps.pps_pic_parameter_set_id = pps_id;

  /* *INDENT-OFF* */
  params_add = (VkVideoEncodeH265SessionParametersAddInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
    .pStdVPSs = &h265_std_vps,
    .stdVPSCount = 1,
    .pStdSPSs = &h265_std_sps,
    .stdSPSCount = 1,
    .pStdPPSs = &h265_std_pps,
    .stdPPSCount = 1,
  };
  enc_params.h265 = (VkVideoEncodeH265SessionParametersCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdVPSCount = 1,
    .maxStdSPSCount = 1,
    .maxStdPPSCount = 1,
    .pParametersAddInfo = &params_add
  };
  /* *INDENT-ON* */

  fail_unless (gst_vulkan_encoder_update_video_session_parameters (enc,
          &enc_params, &err));

  /* retrieve the VPS/SPS/PPS from the device */
  check_h265_session_params (enc, vps_id, sps_id, pps_id);

  return enc;
}

static void
tear_down_encoder (GstVulkanEncoder * enc)
{
  if (enc) {
    fail_unless (gst_vulkan_encoder_stop (enc));
    gst_object_unref (enc);
  }
  if (exec) {
    if (!gst_vulkan_operation_wait (exec)) {
      GST_WARNING
          ("Failed to wait for all fences to complete before shutting down");
    }
    gst_object_unref (exec);
    exec = NULL;
  }
}

static void
check_encoded_frame (GstVulkanH265EncodeFrame * frame,
    GstH265NalUnitType nal_type)
{
  GstMapInfo info;
  fail_unless (frame->picture.out_buffer != NULL);
  gst_buffer_map (frame->picture.out_buffer, &info, GST_MAP_READ);
  fail_unless (info.size);
  GST_MEMDUMP ("out buffer", info.data, info.size);
  check_h265_nalu (info.data, info.size, nal_type);
  gst_buffer_unmap (frame->picture.out_buffer, &info);
}

/* Greater than the maxDpbSlots == 16*/
#define N_BUFFERS 17
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240

GST_START_TEST (test_encoder_h265_i)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  gint vps_id = 0;
  gint sps_id = 0;
  gint pps_id = 0;
  GstVulkanH265EncodeFrame *frame;
  int frame_num = 0;
  int i;
  /* Create and setup a H.265 encoder with its initial session parameters */
  enc = setup_h265_encoder (width, height, vps_id, sps_id, pps_id);
  if (!enc) {
    GST_WARNING ("Unable to initialize H265 encoder");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode N_BUFFERS I-Frames */
  for (i = 0; i < N_BUFFERS; i++) {
    frame = allocate_h265_frame (enc, width, height, TRUE);
    encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_I,
        frame_num, NULL, 0, NULL, 0, vps_id, sps_id, pps_id);
    check_encoded_frame (frame, GST_H265_NAL_SLICE_IDR_W_RADL);

    frame_num++;
    _h265_encode_frame_free (enc, frame);
  }

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

beach:
  tear_down_encoder (enc);
}

GST_END_TEST;

GST_START_TEST (test_encoder_h265_i_p)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  gint vps_id = 0;
  gint sps_id = 0;
  gint pps_id = 0;
  GstVulkanH265EncodeFrame *frame;
  GstVulkanH265EncodeFrame *list0[16] = { NULL, };
  gint list0_num = 1;
  int frame_num = 0;
  int i = 0;

  enc = setup_h265_encoder (width, height, vps_id, sps_id, pps_id);
  if (!enc) {
    GST_WARNING ("Unable to initialize H265 encoder");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  frame = allocate_h265_frame (enc, width, height, TRUE);
  frame->pic_num = frame_num;
  /* Encode first picture as an IDR-Frame */
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_IDR_W_RADL);
  list0[0] = frame;
  frame_num++;

  /* Encode following pictures as a P-Frames */
  for (i = 1; i < N_BUFFERS; i++) {
    frame = allocate_h265_frame (enc, width, height, TRUE);
    frame->pic_num = frame_num;
    encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_P,
        frame_num, list0, list0_num, NULL, 0, vps_id, sps_id, pps_id);
    check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_R);
    _h265_encode_frame_free (enc, list0[0]);
    list0[0] = frame;
    frame_num++;
  }
  _h265_encode_frame_free (enc, list0[0]);
  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

beach:
  tear_down_encoder (enc);
}

GST_END_TEST;

GST_START_TEST (test_encoder_h265_i_p_b)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  gint vps_id = 0;
  gint sps_id = 0;
  gint pps_id = 0;
  GstVulkanH265EncodeFrame *frame;
  GstVulkanH265EncodeFrame *list0[16] = { NULL, };
  GstVulkanH265EncodeFrame *list1[16] = { NULL, };
  gint list0_num = 1;
  gint list1_num = 1;
  int frame_num = 0;
  GstVulkanVideoCapabilities enc_caps;

  enc = setup_h265_encoder (width, height, vps_id, sps_id, pps_id);
  if (!enc) {
    GST_WARNING ("Unable to initialize H265 encoder");
    goto beach;
  }

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  if (!enc_caps.encoder.codec.h265.maxL1ReferenceCount) {
    GST_WARNING ("Driver does not support B frames");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode first picture as an IDR-Frame */
  frame = allocate_h265_frame (enc, width, height, TRUE);
  frame->pic_num = frame_num;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_IDR_W_RADL);
  list0[0] = frame;
  frame_num++;

  /* Encode 4th picture as a P-Frame */
  frame = allocate_h265_frame (enc, width, height, TRUE);
  frame->pic_num = frame_num + 2;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_P,
      frame_num, list0, list0_num, NULL, 0, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_R);
  list1[0] = frame;
  frame_num++;

  /* Encode 2nd picture as a B-Frame */
  frame = allocate_h265_frame (enc, width, height, FALSE);
  frame->pic_num = frame_num - 1;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_N);
  frame_num++;
  _h265_encode_frame_free (enc, frame);

  /* Encode 3rd picture as a B-Frame */
  frame = allocate_h265_frame (enc, width, height, FALSE);
  frame->pic_num = frame_num - 1;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_N);
  frame_num++;
  _h265_encode_frame_free (enc, frame);

  _h265_encode_frame_free (enc, list0[0]);
  _h265_encode_frame_free (enc, list1[0]);

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

beach:
  tear_down_encoder (enc);
}

GST_END_TEST;


static Suite *
vkvideo_suite (void)
{
  Suite *s = suite_create ("vkvideo");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  /* FIXME: CI doesn't have a software vulkan renderer (and none exists currently) */
  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_encoder_h265_i);
    tcase_add_test (tc_basic, test_encoder_h265_i_p);
    tcase_add_test (tc_basic, test_encoder_h265_i_p_b);
  }

  return s;
}

GST_CHECK_MAIN (vkvideo);

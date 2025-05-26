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

#include <gst/codecparsers/gsth264parser.h>

#include "vkvideoencodebase.c"

// Include h264 std session params
#include "vkcodecparams_h264.c"

typedef struct
{
  GstVulkanEncoderPicture picture;

  gboolean is_ref;
  gint pic_num;
  gint pic_order_cnt;

  VkVideoEncodeH264NaluSliceInfoKHR slice_info;
  VkVideoEncodeH264PictureInfoKHR enc_pic_info;
  VkVideoEncodeH264DpbSlotInfoKHR dpb_slot_info;
  VkVideoEncodeH264RateControlInfoKHR rc_info;

  StdVideoEncodeH264SliceHeader slice_hdr;
  StdVideoEncodeH264PictureInfo pic_info;
  StdVideoEncodeH264ReferenceInfo ref_info;
  StdVideoEncodeH264ReferenceListsInfo ref_list_info;
} GstVulkanH264EncodeFrame;

static GstVulkanH264EncodeFrame *
_h264_encode_frame_new (GstVulkanEncoder * enc, GstBuffer * img_buffer,
    gsize size, gboolean is_ref)
{
  GstVulkanH264EncodeFrame *frame;

  frame = g_new (GstVulkanH264EncodeFrame, 1);
  fail_unless (gst_vulkan_encoder_picture_init (&frame->picture, enc,
          img_buffer, size));
  frame->is_ref = is_ref;

  return frame;
}

static void
_h264_encode_frame_free (GstVulkanEncoder * enc, gpointer pframe)
{
  GstVulkanH264EncodeFrame *frame = (GstVulkanH264EncodeFrame *) pframe;

  gst_vulkan_encoder_picture_clear (&frame->picture, enc);
  g_free (frame);
}


#define H264_MB_SIZE_ALIGNMENT 16


static GstVulkanH264EncodeFrame *
allocate_h264_frame (GstVulkanEncoder * enc, int width,
    int height, gboolean is_ref)
{
  GstVulkanH264EncodeFrame *frame;
  GstBuffer *in_buffer, *img_buffer;

  in_buffer = generate_input_buffer (buffer_pool, width, height);

  upload_buffer_to_image (img_pool, in_buffer, &img_buffer);

  frame = _h264_encode_frame_new (enc, img_buffer, width * height * 3, is_ref);
  fail_unless (frame);
  gst_buffer_unref (in_buffer);
  gst_buffer_unref (img_buffer);

  return frame;
}

#define PICTURE_TYPE(slice_type, is_ref)                                       \
  (slice_type == STD_VIDEO_H264_SLICE_TYPE_I && is_ref)                        \
      ? STD_VIDEO_H264_PICTURE_TYPE_IDR                                        \
      : (StdVideoH264PictureType)slice_type

static void
setup_codec_pic (GstVulkanEncoderPicture * pic, VkVideoEncodeInfoKHR * info,
    gpointer data)
{
  GstVulkanH264EncodeFrame *frame = (GstVulkanH264EncodeFrame *) pic;
  GstVulkanVideoCapabilities *enc_caps = (GstVulkanVideoCapabilities *) data;

  info->pNext = &frame->enc_pic_info;
  pic->dpb_slot.pNext = &frame->dpb_slot_info;

  {
    /* *INDENT-OFF* */
    frame->enc_pic_info = (VkVideoEncodeH264PictureInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR,
      .pNext = NULL,
      .naluSliceEntryCount = 1,
      .pNaluSliceEntries = &frame->slice_info,
      .pStdPictureInfo = &frame->pic_info,
      .generatePrefixNalu =
          (enc_caps->encoder.codec.h264.flags
           & VK_VIDEO_ENCODE_H264_CAPABILITY_GENERATE_PREFIX_NALU_BIT_KHR),
    };
    frame->dpb_slot_info = (VkVideoEncodeH264DpbSlotInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR,
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
  GstVulkanH264EncodeFrame *frame = (GstVulkanH264EncodeFrame *) pic;

  /* *INDENT-OFF* */
  frame->rc_info = (VkVideoEncodeH264RateControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR,
    .pNext = NULL,
    .flags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
        VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .gopFrameCount = 1,
    .idrPeriod = 1,
    .consecutiveBFrameCount = 0,
    .temporalLayerCount = 0,
  };
  /* *INDENT-ON* */

  rc_info->pNext = &frame->rc_info;
}

static void
encode_frame (GstVulkanEncoder * enc, GstVulkanH264EncodeFrame * frame,
    StdVideoH264SliceType slice_type, guint frame_num,
    GstVulkanH264EncodeFrame ** list0, gint list0_num,
    GstVulkanH264EncodeFrame ** list1, gint list1_num, gint sps_id, gint pps_id)
{
  GstVulkanVideoCapabilities enc_caps;
  int i, ref_pics_num = 0;
  GstVulkanEncoderPicture *ref_pics[16] = { NULL, };
  GstVulkanEncoderPicture *picture = &frame->picture;
  GstVulkanEncoderCallbacks cb = { setup_codec_pic, setup_rc_codec };

  GST_DEBUG ("Encoding frame num:%d", frame_num);

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  gst_vulkan_encoder_set_callbacks (enc, &cb, &enc_caps, NULL);

  frame->slice_hdr = (StdVideoEncodeH264SliceHeader) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH264SliceHeaderFlags) {
      .direct_spatial_mv_pred_flag = 0,
      .num_ref_idx_active_override_flag = (slice_type != STD_VIDEO_H264_SLICE_TYPE_I && (list0_num > 0 || list1_num > 0)),
    },
    .first_mb_in_slice = 0,
    .slice_type = slice_type,
    .slice_alpha_c0_offset_div2 = 0,
    .slice_beta_offset_div2 = 0,
    .slice_qp_delta = 0,
    .cabac_init_idc = STD_VIDEO_H264_CABAC_INIT_IDC_0,
    .disable_deblocking_filter_idc = STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED,
    .pWeightTable = NULL,
    /* *INDENT-ON* */
  };

  frame->pic_info = (StdVideoEncodeH264PictureInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH264PictureInfoFlags) {
        .IdrPicFlag = (slice_type == STD_VIDEO_H264_SLICE_TYPE_I && frame->is_ref),
        .is_reference = frame->is_ref,   /* TODO: Check why it creates a deadlock in query result when TRUE  */
        .no_output_of_prior_pics_flag = 0,
        .long_term_reference_flag = 0,
        .adaptive_ref_pic_marking_mode_flag = 0,
    },
    .seq_parameter_set_id = sps_id,
    .pic_parameter_set_id = pps_id,
    .primary_pic_type = PICTURE_TYPE (slice_type, frame->is_ref),
    .frame_num = frame_num,
    .PicOrderCnt = frame->pic_order_cnt,
    /* *INDENT-ON* */
  };

  ref_pics_num = list0_num + list1_num;

  if (ref_pics_num > 0) {
    /* *INDENT-OFF* */
    frame->ref_list_info = (StdVideoEncodeH264ReferenceListsInfo) {
      .flags = {
        .ref_pic_list_modification_flag_l0 = 0,
        .ref_pic_list_modification_flag_l1 = 0,
      },
      .num_ref_idx_l0_active_minus1 = 0,
      .num_ref_idx_l1_active_minus1 = 0,
      .RefPicList0 = {0, },
      .RefPicList1 = {0, },
      .refList0ModOpCount = 0,
      .refList1ModOpCount = 0,
      .refPicMarkingOpCount = 0,
      .reserved1 = {0, },
      .pRefList0ModOperations = NULL,
      .pRefList1ModOperations = NULL,
      .pRefPicMarkingOperations = NULL,
    };
    /* *INDENT-ON* */
    frame->pic_info.pRefLists = &frame->ref_list_info;
  }

  memset (frame->ref_list_info.RefPicList0, STD_VIDEO_H264_NO_REFERENCE_PICTURE,
      STD_VIDEO_H264_MAX_NUM_LIST_REF);
  memset (frame->ref_list_info.RefPicList1, STD_VIDEO_H264_NO_REFERENCE_PICTURE,
      STD_VIDEO_H264_MAX_NUM_LIST_REF);

  /* *INDENT-OFF* */
  frame->slice_info = (VkVideoEncodeH264NaluSliceInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR,
    .pNext = NULL,
    .constantQp = 26,
    .pStdSliceHeader = &frame->slice_hdr,
  };

  fail_unless (frame->slice_info.constantQp >= enc_caps.encoder.codec.h264.minQp);

  frame->rc_info = (VkVideoEncodeH264RateControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR,
  };

  frame->ref_info = (StdVideoEncodeH264ReferenceInfo) {
    .flags = {
      .used_for_long_term_reference = 0,
    },
    .primary_pic_type = PICTURE_TYPE (slice_type, frame->is_ref),
    .FrameNum = frame_num,
    .PicOrderCnt = frame->pic_order_cnt,
    .long_term_pic_num = 0,
    .long_term_frame_idx = 0,
    .temporal_id = 0,
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
check_h264_nalu (guint8 * bitstream, gsize size, GstH264NalUnitType nal_type)
{
  GstH264ParserResult res;
  GstH264NalUnit nalu;
  GstH264NalParser *const parser = gst_h264_nal_parser_new ();
  res = gst_h264_parser_identify_nalu (parser, bitstream, 0, size, &nalu);
  assert_equals_int (res, GST_H264_PARSER_NO_NAL_END);
  assert_equals_int (nalu.type, nal_type);

  switch (nal_type) {
    case GST_H264_NAL_SPS:
    {
      GstH264SPS sps;
      res = gst_h264_parser_parse_sps (parser, &nalu, &sps);
      assert_equals_int (res, GST_H264_PARSER_OK);
      break;
    }
    case GST_H264_NAL_PPS:
    {
      GstH264PPS pps;
      res = gst_h264_parser_parse_pps (parser, &nalu, &pps);
      assert_equals_int (res, GST_H264_PARSER_BROKEN_LINK);
      break;
    }
    default:
      res = gst_h264_parser_parse_nal (parser, &nalu);
      assert_equals_int (res, GST_H264_PARSER_OK);
      break;
  }

  gst_h264_nal_parser_free (parser);
}

static void
check_h264_session_params (GstVulkanEncoder * enc, gint sps_id, gint pps_id)
{
  GError *err = NULL;
  GstVulkanEncoderParametersFeedback feedback = { 0, };
  guint8 *bitstream = NULL;
  gsize bitstream_size = 0;
  GstVulkanEncoderParametersOverrides override_params = {
    .h264 = {
          .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR,
          .writeStdSPS = VK_TRUE,
          .writeStdPPS = VK_FALSE,
          .stdSPSId = 0,
          .stdPPSId = 0,
        }
  };
  fail_unless (gst_vulkan_encoder_video_session_parameters_overrides (enc,
          &override_params, &feedback, &bitstream_size,
          (gpointer *) & bitstream, &err));

  check_h264_nalu (bitstream, bitstream_size, GST_H264_NAL_SPS);

  override_params = (GstVulkanEncoderParametersOverrides) {
    .h264 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR,
      .writeStdSPS = VK_FALSE,
      .writeStdPPS = VK_TRUE,
      .stdSPSId = 0,
      .stdPPSId = 0,
    }
  };
  g_free (bitstream);
  fail_unless (gst_vulkan_encoder_video_session_parameters_overrides (enc,
          &override_params, &feedback, &bitstream_size,
          (gpointer *) & bitstream, &err));
  check_h264_nalu (bitstream, bitstream_size, GST_H264_NAL_PPS);
  g_free (bitstream);

}

static GstVulkanEncoder *
setup_h264_encoder (guint32 width, gint32 height, gint sps_id, gint pps_id)
{
  GstVulkanEncoder *enc = NULL;
  GError *err = NULL;
  uint32_t mbAlignedWidth, mbAlignedHeight;
  GstVulkanVideoProfile profile;
  StdVideoH264ProfileIdc profile_idc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
  GstVulkanEncoderParameters enc_params;
  VkVideoEncodeH264SessionParametersAddInfoKHR params_add;
  GstVulkanEncoderQualityProperties quality_props;

  /* *INDENT-OFF* */
  profile = (GstVulkanVideoProfile) {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.usage.encode,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    },
    .usage.encode = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR,
      .pNext = &profile.codec,
      .videoUsageHints = VK_VIDEO_ENCODE_USAGE_DEFAULT_KHR,
      .videoContentHints = VK_VIDEO_ENCODE_CONTENT_DEFAULT_KHR,
      .tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_DEFAULT_KHR,
    },
    .codec.h264enc = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
      .stdProfileIdc = profile_idc,
    }
  };
  quality_props = (GstVulkanEncoderQualityProperties) {
    .quality_level = -1,
    .codec.h264 = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_QUALITY_LEVEL_PROPERTIES_KHR,
    },
  };
  /* *INDENT-ON* */

  setup_queue (VK_QUEUE_VIDEO_DECODE_BIT_KHR,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);

  if (!video_queue) {
    GST_WARNING ("Unable to find encoding queue");
    return NULL;
  }

  if (!graphics_queue) {
    GST_WARNING ("Unable to find graphics queue");
    return NULL;
  }

  enc = gst_vulkan_encoder_create_from_queue (video_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);

  if (!enc) {
    GST_WARNING ("Unable to create a vulkan encoder, queue=%p", video_queue);
    return NULL;
  }

  fail_unless (gst_vulkan_encoder_quality_level (enc) == -1);

  fail_unless (gst_vulkan_encoder_start (enc, &profile, &quality_props, &err));

  fail_unless (gst_vulkan_encoder_quality_level (enc) > -1);

  fail_unless (gst_vulkan_encoder_is_started (enc));

  mbAlignedWidth = GST_ROUND_UP_16 (width);
  mbAlignedHeight = GST_ROUND_UP_16 (height);

  h264_std_sps.profile_idc = profile_idc;
  h264_std_sps.seq_parameter_set_id = sps_id;
  h264_std_sps.pic_width_in_mbs_minus1 =
      mbAlignedWidth / H264_MB_SIZE_ALIGNMENT - 1;
  h264_std_sps.pic_height_in_map_units_minus1 =
      mbAlignedHeight / H264_MB_SIZE_ALIGNMENT - 1;
  h264_std_sps.frame_crop_right_offset = mbAlignedWidth - width;
  h264_std_sps.frame_crop_bottom_offset = mbAlignedHeight - height;

  /* *INDENT-OFF* */
  params_add = (VkVideoEncodeH264SessionParametersAddInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
    .stdSPSCount = 1,
    .pStdSPSs = &h264_std_sps,
    .stdPPSCount = 1,
    .pStdPPSs = &h264_std_pps,
  };
  enc_params.h264 = (VkVideoEncodeH264SessionParametersCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdSPSCount = 1,
    .maxStdPPSCount = 1,
    .pParametersAddInfo = &params_add
  };
  /* *INDENT-ON* */

  fail_unless (gst_vulkan_encoder_update_video_session_parameters (enc,
          &enc_params, &err));

  /* retrieve the SPS/PPS from the device */
  check_h264_session_params (enc, sps_id, pps_id);
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
  gst_clear_object (&video_queue);
  gst_clear_object (&graphics_queue);
}

static void
check_encoded_frame (GstVulkanH264EncodeFrame * frame,
    GstH264NalUnitType nal_type)
{
  GstMapInfo info;
  fail_unless (frame->picture.out_buffer != NULL);
  gst_buffer_map (frame->picture.out_buffer, &info, GST_MAP_READ);
  fail_unless (info.size);
  GST_MEMDUMP ("out buffer", info.data, info.size);
  check_h264_nalu (info.data, info.size, nal_type);
  gst_buffer_unmap (frame->picture.out_buffer, &info);
}

/* Greater than the maxDpbSlots == 16*/
#define N_BUFFERS 17
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240

GST_START_TEST (test_encoder_h264_i)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  gint sps_id = 0;
  gint pps_id = 0;
  GstVulkanH264EncodeFrame *frame;
  int frame_num = 0;
  int i;
  /* Create and setup a H.264 encoder with its initial session parameters */
  enc = setup_h264_encoder (width, height, sps_id, pps_id);
  if (!enc) {
    GST_WARNING ("Unable to initialize H264 encoder");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode N_BUFFERS of I-Frames */
  for (i = 0; i < N_BUFFERS; i++) {
    frame = allocate_h264_frame (enc, width, height, TRUE);
    encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_I,
        frame_num, NULL, 0, NULL, 0, sps_id, pps_id);
    check_encoded_frame (frame, GST_H264_NAL_SLICE_IDR);

    frame_num++;
    _h264_encode_frame_free (enc, frame);
  }

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

beach:
  tear_down_encoder (enc);
}

GST_END_TEST;

GST_START_TEST (test_encoder_h264_i_p)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  gint sps_id = 0;
  gint pps_id = 0;
  GstVulkanH264EncodeFrame *frame;
  GstVulkanH264EncodeFrame *list0[16] = { NULL, };
  gint list0_num = 1;
  int frame_num = 0;
  int i = 0;

  enc = setup_h264_encoder (width, height, sps_id, pps_id);
  if (!enc) {
    GST_WARNING ("Unable to initialize H264 encoder");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode first picture as an IDR-Frame */
  frame = allocate_h264_frame (enc, width, height, TRUE);
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE_IDR);
  list0[0] = frame;
  frame_num++;

  /* Encode following pictures as P-Frames */
  for (i = 1; i < N_BUFFERS; i++) {
    frame = allocate_h264_frame (enc, width, height, TRUE);
    frame->pic_num = frame_num;
    frame->pic_order_cnt = frame_num;

    encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_P,
        frame_num, list0, list0_num, NULL, 0, sps_id, pps_id);
    check_encoded_frame (frame, GST_H264_NAL_SLICE);
    _h264_encode_frame_free (enc, list0[0]);
    list0[0] = frame;
    frame_num++;
  }

  _h264_encode_frame_free (enc, list0[0]);

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

beach:
  tear_down_encoder (enc);
}

GST_END_TEST;

GST_START_TEST (test_encoder_h264_i_p_b)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  gint sps_id = 0;
  gint pps_id = 0;
  GstVulkanH264EncodeFrame *frame;
  GstVulkanH264EncodeFrame *list0[16] = { NULL, };
  GstVulkanH264EncodeFrame *list1[16] = { NULL, };
  gint list0_num = 0;
  gint list1_num = 0;
  int frame_num = 0;
  GstVulkanVideoCapabilities enc_caps;

  enc = setup_h264_encoder (width, height, sps_id, pps_id);
  if (!enc) {
    GST_WARNING ("Unable to initialize H264 encoder");
    goto beach;
  }

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  if (!enc_caps.encoder.codec.h264.maxL1ReferenceCount) {
    GST_WARNING ("Driver does not support B frames");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode 1st picture as an IDR-Frame */
  frame = allocate_h264_frame (enc, width, height, TRUE);
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE_IDR);
  list0[0] = frame;
  list0_num++;
  frame_num++;

  /* Encode 4th picture as a P-Frame */
  frame = allocate_h264_frame (enc, width, height, TRUE);
  frame->pic_num = 3;
  frame->pic_order_cnt = frame->pic_num * 2;
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_P,
      frame_num, list0, list0_num, list1, list1_num, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE);
  list1[0] = frame;
  list1_num++;
  frame_num++;

  /* Encode second picture as a B-Frame */
  frame = allocate_h264_frame (enc, width, height, FALSE);
  frame->pic_num = 1;
  frame->pic_order_cnt = frame->pic_num * 2;
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE);
  frame_num++;
  _h264_encode_frame_free (enc, frame);

  /* Encode third picture as a B-Frame */
  frame = allocate_h264_frame (enc, width, height, FALSE);
  frame->pic_num = 2;
  frame->pic_order_cnt = frame->pic_num * 2;

  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE);
  frame_num++;
  _h264_encode_frame_free (enc, frame);

  _h264_encode_frame_free (enc, list0[0]);
  _h264_encode_frame_free (enc, list1[0]);

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
    tcase_add_test (tc_basic, test_encoder_h264_i);
    tcase_add_test (tc_basic, test_encoder_h264_i_p);
    tcase_add_test (tc_basic, test_encoder_h264_i_p_b);
  }

  return s;
}

GST_CHECK_MAIN (vkvideo);

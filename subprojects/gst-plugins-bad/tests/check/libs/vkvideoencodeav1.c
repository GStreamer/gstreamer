/* GStreamer
 *
 * Copyright (C) 2025 Igalia, S.L.
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

#include <gst/codecparsers/gstav1parser.h>

#include "vkvideoencodebase.c"

GstAV1Parser *parser = NULL;

#define MAX_ORDER_HINT 7
#define FRAME_ID_BITS 15
#define DELTA_FRAME_ID_BITS 14

typedef struct
{
  GstVulkanEncoderPicture picture;

  gboolean is_ref;
  gint pic_num;
  gint pic_order_cnt;

  VkVideoEncodeAV1PictureInfoKHR enc_pic_info;

  StdVideoEncodeAV1PictureInfo pic_info;
  StdVideoEncodeAV1ReferenceInfo ref_info;
  VkVideoEncodeAV1DpbSlotInfoKHR dpb_slot_info;
  VkVideoEncodeAV1RateControlInfoKHR rc_info;

} GstVulkanAV1EncoderFrame;


static GstAV1OBUType
check_av1_obu (guint8 * bitstream, gsize size, GstAV1OBU * obu)
{
  GstAV1ParserResult res = GST_AV1_PARSER_OK;
  guint32 consumed;
  guint32 offset = 0;

  if (!parser) {
    parser = gst_av1_parser_new ();
  }

  while (offset < size) {

    res =
        gst_av1_parser_identify_one_obu (parser, bitstream + offset, size, obu,
        &consumed);
    assert_equals_int (res, GST_AV1_PARSER_OK);

    switch (obu->obu_type) {
      case GST_AV1_OBU_TEMPORAL_DELIMITER:
      {
        res = gst_av1_parser_parse_temporal_delimiter_obu (parser, obu);
        assert_equals_int (res, GST_AV1_PARSER_OK);
        break;
      }
      case GST_AV1_OBU_SEQUENCE_HEADER:
      {
        GstAV1SequenceHeaderOBU seq_header;
        res =
            gst_av1_parser_parse_sequence_header_obu (parser, obu, &seq_header);
        assert_equals_int (res, GST_AV1_PARSER_OK);
        break;
      }
      case GST_AV1_OBU_FRAME_HEADER:
      {
        GstAV1FrameHeaderOBU frame_header;
        res =
            gst_av1_parser_parse_frame_header_obu (parser, obu, &frame_header);
        assert_equals_int (res, GST_AV1_PARSER_OK);
        break;
      }
      case GST_AV1_OBU_FRAME:
      {
        GstAV1FrameOBU frame;
        res = gst_av1_parser_parse_frame_obu (parser, obu, &frame);
        assert_equals_int (res, GST_AV1_PARSER_OK);
        break;
      }
      case GST_AV1_OBU_TILE_GROUP:
      {
        GstAV1TileGroupOBU tile_group;
        res = gst_av1_parser_parse_tile_group_obu (parser, obu, &tile_group);
        assert_equals_int (res, GST_AV1_PARSER_OK);
        fail_unless (tile_group.num_tiles > 0);
        break;
      }

      default:
        GST_ERROR ("Unknown OBU type: %d", obu->obu_type);
        fail_unless (0);
        break;
    }
    offset += consumed;
  }

  return obu->obu_type;
}

static void
check_av1_obu_frame (GstAV1OBU * obu, GstAV1FrameType frame_type)
{
  GstAV1FrameOBU frame;
  GstAV1ParserResult res = GST_AV1_PARSER_OK;

  res = gst_av1_parser_parse_frame_obu (parser, obu, &frame);
  assert_equals_int (res, GST_AV1_PARSER_OK);
  assert_equals_int (frame.frame_header.frame_type, frame_type);
}

static gint
_av1_helper_msb (guint n)
{
  int log = 0;
  guint value = n;
  int i;

  g_assert_cmpuint (n, !=, 0);

  for (i = 4; i >= 0; --i) {
    const gint shift = (1 << i);
    const guint x = value >> shift;
    if (x != 0) {
      value = x;
      log += shift;
    }
  }

  return log;
}

static void
check_av1_session_params (GstVulkanEncoder * enc)
{
  GError *err = NULL;
  guint8 *bitstream = NULL;
  gsize bitstream_size = 0;
  GstAV1OBU obu;

  fail_unless (gst_vulkan_encoder_video_session_parameters_overrides (enc,
          NULL, NULL, &bitstream_size, (gpointer *) & bitstream, &err));

  assert_equals_int (check_av1_obu (bitstream, bitstream_size, &obu),
      GST_AV1_OBU_SEQUENCE_HEADER);

  g_free (bitstream);
}

static GstVulkanAV1EncoderFrame *
_av1_encode_frame_new (GstVulkanEncoder * enc, GstBuffer * img_buffer,
    gsize size, gboolean is_ref)
{
  GstVulkanAV1EncoderFrame *frame;

  frame = g_new (GstVulkanAV1EncoderFrame, 1);
  fail_unless (gst_vulkan_encoder_picture_init (&frame->picture, enc,
          img_buffer, size));

  frame->is_ref = is_ref;

  return frame;
}

static void
_av1_encode_frame_free (GstVulkanEncoder * enc, gpointer pframe)
{
  GstVulkanAV1EncoderFrame *frame = (GstVulkanAV1EncoderFrame *) pframe;
  gst_vulkan_encoder_picture_clear (&frame->picture, enc);
  g_free (frame);
}

static GstVulkanAV1EncoderFrame *
allocate_av1_frame (GstVulkanEncoder * enc, int width, int height,
    gboolean is_ref)
{
  GstVulkanAV1EncoderFrame *frame;
  GstBuffer *in_buffer, *img_buffer;

  in_buffer = generate_input_buffer (buffer_pool, width, height);
  fail_unless (in_buffer);

  fail_unless (upload_buffer_to_image (img_pool, in_buffer,
          &img_buffer) == GST_FLOW_OK);

  frame = _av1_encode_frame_new (enc, img_buffer, width * height * 3, is_ref);
  fail_unless (frame);

  gst_buffer_unref (in_buffer);
  gst_buffer_unref (img_buffer);

  return frame;
}

static void
setup_codec_pic (GstVulkanEncoderPicture * pic, VkVideoEncodeInfoKHR * info,
    gpointer data)
{
  GstVulkanAV1EncoderFrame *frame = (GstVulkanAV1EncoderFrame *) pic;

  info->pNext = &frame->enc_pic_info;
  pic->dpb_slot.pNext = &frame->dpb_slot_info;


  /* *INDENT-OFF* */
  frame->dpb_slot_info = (VkVideoEncodeAV1DpbSlotInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_DPB_SLOT_INFO_KHR,
    .pNext = NULL,
    .pStdReferenceInfo = &frame->ref_info,
  };
  /* *INDENT-ON* */

  if (frame->pic_info.frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY) {
    frame->pic_info.refresh_frame_flags = 0xff;
  } else {
    frame->pic_info.refresh_frame_flags =
        1 << frame->picture.dpb_slot.slotIndex;
  }
}

static void
setup_rc_codec (GstVulkanEncoderPicture * pic,
    VkVideoEncodeRateControlInfoKHR * rc_info,
    VkVideoEncodeRateControlLayerInfoKHR * rc_layer, gpointer data)
{
  GstVulkanAV1EncoderFrame *frame = (GstVulkanAV1EncoderFrame *) pic;

  /* *INDENT-OFF* */
  frame->rc_info = (VkVideoEncodeAV1RateControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_RATE_CONTROL_INFO_KHR,
    .pNext = NULL,
    .flags = VK_VIDEO_ENCODE_AV1_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
        VK_VIDEO_ENCODE_AV1_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .gopFrameCount = 1,
    .keyFramePeriod = 1,
    .consecutiveBipredictiveFrameCount = 0,
    .temporalLayerCount = 0,
  };
  /* *INDENT-ON* */

  rc_info->pNext = &frame->rc_info;
}

static GstVulkanEncoder *
setup_av1_encoder (guint32 width, gint32 height, int gop_size)
{
  GstVulkanEncoder *enc = NULL;
  GError *err = NULL;
  GstVulkanVideoProfile profile;
  GstVulkanEncoderParameters enc_params;
  StdVideoAV1SequenceHeader av1_seq_header;
  StdVideoAV1Profile av1_profile = STD_VIDEO_AV1_PROFILE_MAIN;
  StdVideoAV1ColorConfig av1_color_config;
  StdVideoEncodeAV1DecoderModelInfo av1_model_info;
  StdVideoEncodeAV1OperatingPointInfo av1_operating_point_info;
  GstVulkanEncoderQualityProperties quality_props;

  /* *INDENT-OFF* */
  profile = (GstVulkanVideoProfile) {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.usage.encode,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR,
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
    .codec.av1enc = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR,
      .stdProfile = av1_profile,
    }
  };

  quality_props = (GstVulkanEncoderQualityProperties) {
    .quality_level = -1,
    .codec.av1 = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_QUALITY_LEVEL_PROPERTIES_KHR,
    },
  };
  /* *INDENT-ON* */

  setup_queue (VK_QUEUE_VIDEO_ENCODE_BIT_KHR,
      VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR);

  if (!video_queue) {
    GST_WARNING ("Unable to find encoding queue");
    return NULL;
  }

  if (!graphics_queue) {
    GST_WARNING ("Unable to find graphics queue");
    return NULL;
  }

  enc = gst_vulkan_encoder_create_from_queue (video_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR);

  if (!enc) {
    GST_WARNING ("Unable to create a vulkan encoder, queue=%p", video_queue);
    return NULL;
  }

  fail_unless (gst_vulkan_encoder_quality_level (enc) == -1);

  fail_unless (gst_vulkan_encoder_start (enc, &profile, &quality_props, &err));

  fail_unless (gst_vulkan_encoder_quality_level (enc) > -1);

  fail_unless (gst_vulkan_encoder_is_started (enc));

  /* *INDENT-OFF* */
  av1_color_config = (StdVideoAV1ColorConfig) {
    .flags = (StdVideoAV1ColorConfigFlags) {
      .mono_chrome = 0,
      .color_range = 0,
      .separate_uv_delta_q = 0,
      .color_description_present_flag = 0,
    },
    .BitDepth = 8, /* VK_FORMAT_G8_B8R8_2PLANE_420_UNORM */
    .subsampling_x = 1,
    .subsampling_y = 1,
    .color_primaries = STD_VIDEO_AV1_COLOR_PRIMARIES_BT_UNSPECIFIED,
    .transfer_characteristics = STD_VIDEO_AV1_TRANSFER_CHARACTERISTICS_UNSPECIFIED,
    .matrix_coefficients = STD_VIDEO_AV1_MATRIX_COEFFICIENTS_UNSPECIFIED,
    .chroma_sample_position = STD_VIDEO_AV1_CHROMA_SAMPLE_POSITION_UNKNOWN,
  };

  av1_seq_header = (StdVideoAV1SequenceHeader) {
    .flags = (StdVideoAV1SequenceHeaderFlags) {
      .still_picture = 0,
      .reduced_still_picture_header = 0,
      .use_128x128_superblock = 0,
      .enable_filter_intra = 0,
      .enable_intra_edge_filter = 0,
      .enable_interintra_compound = 0,
      .enable_masked_compound = 0,
      .enable_warped_motion = 0,
      .enable_dual_filter = 0,
      .enable_order_hint = 1,
      .enable_jnt_comp = 0,
      .enable_ref_frame_mvs = 0,
      .frame_id_numbers_present_flag = 0,
      .enable_superres = 0,
      .enable_cdef = 0,
      .enable_restoration = 0,
      .film_grain_params_present = 0,
      .timing_info_present_flag = 0,
      .initial_display_delay_present_flag = 0,
    },
    .seq_profile = av1_profile,
    .frame_width_bits_minus_1 = _av1_helper_msb (width),
    .frame_height_bits_minus_1 = _av1_helper_msb (height),
    .max_frame_width_minus_1 = width - 1,
    .max_frame_height_minus_1 = height - 1,
    .delta_frame_id_length_minus_2 = DELTA_FRAME_ID_BITS - 2, /* Comes from vk_video_samples */
    .additional_frame_id_length_minus_1 = FRAME_ID_BITS - DELTA_FRAME_ID_BITS - 1, /* Comes from vk_video_samples */
    .order_hint_bits_minus_1 = MAX (_av1_helper_msb(gop_size), MAX_ORDER_HINT - 1), /* Should be ceil log2 of the gop size with MAX_ORDER_HINT as max value */
    .seq_force_integer_mv = 0,
    .seq_force_screen_content_tools = 0,
    .pColorConfig = &av1_color_config,
    .pTimingInfo = NULL,
  };

  av1_model_info = (StdVideoEncodeAV1DecoderModelInfo) {
    .buffer_delay_length_minus_1 = 0,
    .buffer_removal_time_length_minus_1 = 0,
    .frame_presentation_time_length_minus_1 = 0,
    .num_units_in_decoding_tick = 0,
  };

  av1_operating_point_info = (StdVideoEncodeAV1OperatingPointInfo) {
    .flags = (StdVideoEncodeAV1OperatingPointInfoFlags) {
      .decoder_model_present_for_this_op = 0,
      .low_delay_mode_flag = 0,
      .initial_display_delay_present_for_this_op = 0,
    },
    .operating_point_idc = 0,
    .seq_level_idx = 0,
    .seq_tier = 0,
    .decoder_buffer_delay = 0,
    .encoder_buffer_delay = 0,
    .initial_display_delay_minus_1 = 0,
  };

  enc_params.av1 = (VkVideoEncodeAV1SessionParametersCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .pNext = NULL,
    .pStdSequenceHeader = &av1_seq_header,
    .pStdDecoderModelInfo = &av1_model_info,
    .stdOperatingPointCount = 1,
    .pStdOperatingPoints = &av1_operating_point_info,
  };
  /* *INDENT-ON* */

  fail_unless (gst_vulkan_encoder_update_video_session_parameters (enc,
          &enc_params, &err));

  check_av1_session_params (enc);

  return enc;
}

static void
encode_frame (GstVulkanEncoder * enc, GstVulkanAV1EncoderFrame * frame,
    StdVideoAV1FrameType frame_type, guint frame_num,
    GstVulkanAV1EncoderFrame ** list0, gint list0_num,
    GstVulkanAV1EncoderFrame ** list1, gint list1_num)
{
  GstVulkanVideoCapabilities enc_caps;
  int i, ref_pics_num = 0;
  GstVulkanEncoderPicture *ref_pics[16] = { NULL, };
  GstVulkanEncoderPicture *picture = &frame->picture;
  GstVulkanEncoderCallbacks cb = { setup_codec_pic, setup_rc_codec };

  GST_DEBUG ("Encoding frame num:%d", frame_num);

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  gst_vulkan_encoder_set_callbacks (enc, &cb, &enc_caps, NULL);

  /* *INDENT-OFF* */
  frame->pic_info = (StdVideoEncodeAV1PictureInfo) {
    .flags = (StdVideoEncodeAV1PictureInfoFlags) {
      .error_resilient_mode = (frame_type == STD_VIDEO_AV1_FRAME_TYPE_KEY),
      .disable_cdf_update = 0,
      .use_superres = 0,
      .render_and_frame_size_different = 0,
      .allow_screen_content_tools = 0,
      .is_filter_switchable = 0,
      .force_integer_mv = 0,
      .frame_size_override_flag = 0,
      .buffer_removal_time_present_flag = 0,
      .allow_intrabc = 0,
      .frame_refs_short_signaling = 0,
      .allow_high_precision_mv = 0,
      .is_motion_mode_switchable = 0,
      .use_ref_frame_mvs = 0,
      .disable_frame_end_update_cdf = 0,
      .allow_warped_motion = 0,
      .reduced_tx_set = 0,
      .skip_mode_present = 0,
      .delta_q_present = 0,
      .delta_lf_present = 0,
      .delta_lf_multi	= 0,
      .segmentation_enabled	= 0,
      .segmentation_update_map = 0,
      .segmentation_temporal_update = 0,
      .segmentation_update_data = 0,
      .UsesLr = 0,
      .usesChromaLr = 0,
      .show_frame = (frame->pic_order_cnt <= frame->pic_num),
      .showable_frame = (frame_type != STD_VIDEO_AV1_FRAME_TYPE_KEY),
    },
    .frame_type	= frame_type,
    .frame_presentation_time = 0,
    .current_frame_id	= frame_num,
    .order_hint	= frame->pic_order_cnt % (1 << MAX_ORDER_HINT),
    .primary_ref_frame = STD_VIDEO_AV1_PRIMARY_REF_NONE,
    .refresh_frame_flags = 0xff, /* set during `setup_codec_pic` callback */
    .coded_denom = 0,
    .render_width_minus_1 = GST_VIDEO_INFO_WIDTH (&out_info) - 1,
    .render_height_minus_1 = GST_VIDEO_INFO_HEIGHT (&out_info) - 1,
    .interpolation_filter	= STD_VIDEO_AV1_INTERPOLATION_FILTER_EIGHTTAP,
    .TxMode	= STD_VIDEO_AV1_TX_MODE_ONLY_4X4,
    .delta_q_res = 0,
    .delta_lf_res = 0,
    .pTileInfo = NULL,
    .pQuantization = NULL,
    .pSegmentation = NULL,
    .pLoopFilter = NULL,
    .pCDEF = NULL,
    .pLoopRestoration = NULL,
    .pGlobalMotion  = NULL,
    .pExtensionHeader = NULL,
    .pBufferRemovalTimes = NULL,
  };

  frame->enc_pic_info = (VkVideoEncodeAV1PictureInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PICTURE_INFO_KHR,
    .pNext = NULL,
    .predictionMode = VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR,
    .rateControlGroup = VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_INTRA_KHR,
    .constantQIndex = 64,
    .pStdPictureInfo = &frame->pic_info,
    .primaryReferenceCdfOnly	= VK_FALSE,
    .generateObuExtensionHeader = VK_FALSE,
  };
  /* *INDENT-ON* */

  memset (frame->pic_info.ref_order_hint, 0, STD_VIDEO_AV1_NUM_REF_FRAMES);
  memset (frame->pic_info.ref_frame_idx, 0, STD_VIDEO_AV1_REFS_PER_FRAME);
  memset (frame->pic_info.delta_frame_id_minus_1, 0,
      STD_VIDEO_AV1_REFS_PER_FRAME * sizeof (uint32_t));

  if (frame_type != STD_VIDEO_AV1_FRAME_TYPE_KEY) {
    if (list1_num) {            /* Bi-directional frame */
      frame->enc_pic_info.predictionMode =
          VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_BIDIRECTIONAL_COMPOUND_KHR;
      frame->enc_pic_info.rateControlGroup =
          VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_BIPREDICTIVE_KHR;
      frame->pic_info.refresh_frame_flags = 0;
    } else {
      if (enc_caps.encoder.codec.av1.maxUnidirectionalCompoundReferenceCount
          && list0_num > 1) {
        frame->enc_pic_info.predictionMode =
            VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_UNIDIRECTIONAL_COMPOUND_KHR;
      } else {
        frame->enc_pic_info.predictionMode =
            VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;
      }
      frame->enc_pic_info.rateControlGroup =
          VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_PREDICTIVE_KHR;
    }
  }


  if (frame_type != STD_VIDEO_AV1_FRAME_TYPE_KEY) {
    if (list1_num != 0) {
      frame->enc_pic_info.predictionMode =
          VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_BIDIRECTIONAL_COMPOUND_KHR;
      frame->enc_pic_info.rateControlGroup =
          VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_BIPREDICTIVE_KHR;
    } else {
      frame->enc_pic_info.predictionMode =
          VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_SINGLE_REFERENCE_KHR;
      frame->enc_pic_info.rateControlGroup =
          VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_PREDICTIVE_KHR;
    }
  } else {
    frame->enc_pic_info.predictionMode =
        VK_VIDEO_ENCODE_AV1_PREDICTION_MODE_INTRA_ONLY_KHR;
    frame->enc_pic_info.rateControlGroup =
        VK_VIDEO_ENCODE_AV1_RATE_CONTROL_GROUP_INTRA_KHR;
  }

  /* Cause a crash in NVIDIA driver if the referenceNameSlotIndices are not all
   * -1 by default. */
  memset (frame->enc_pic_info.referenceNameSlotIndices, -1,
      STD_VIDEO_AV1_REFS_PER_FRAME * sizeof (int32_t));

  /* *INDENT-OFF* */
  frame->ref_info = (StdVideoEncodeAV1ReferenceInfo) {
    .flags = (StdVideoEncodeAV1ReferenceInfoFlags) {
      .disable_frame_end_update_cdf = 0,
      .segmentation_enabled = 0,
    },
    .RefFrameId = 0, /* FIXME Vulkan Video Samples value is 0 too */
    .frame_type = frame_type,
    .OrderHint = frame->pic_order_cnt % (1 << MAX_ORDER_HINT),
    .pExtensionHeader = NULL,
  };
  /* *INDENT-ON* */

  for (i = 0; i < list0_num; i++) {
    ref_pics[i] = &list0[i]->picture;
    frame->enc_pic_info.referenceNameSlotIndices[i] =
        list0[i]->picture.dpb_slot.slotIndex;
    ref_pics_num++;
  }

  for (i = 0; i < list1_num; i++) {
    ref_pics[i + list0_num] = &list1[i]->picture;
    frame->enc_pic_info.referenceNameSlotIndices[STD_VIDEO_AV1_REFS_PER_FRAME -
        1] = list1[i]->picture.dpb_slot.slotIndex;
    ref_pics_num++;
  }

  fail_unless (gst_vulkan_encoder_encode (enc, &in_info, picture, ref_pics_num,
          ref_pics));
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
  gst_av1_parser_free (parser);
  parser = NULL;
}

static void
check_encoded_frame (GstVulkanAV1EncoderFrame * frame,
    GstAV1FrameType frame_type)
{
  GstMapInfo info;
  GstAV1OBU obu;
  GstAV1OBUType obu_type;
  fail_unless (frame->picture.out_buffer != NULL);
  gst_buffer_map (frame->picture.out_buffer, &info, GST_MAP_READ);
  fail_unless (info.size);
  GST_MEMDUMP ("out buffer", info.data, info.size);

  obu_type = check_av1_obu (info.data, info.size, &obu);
  if (obu_type == GST_AV1_OBU_FRAME) {
    check_av1_obu_frame (&obu, frame_type);
  }
  gst_buffer_unmap (frame->picture.out_buffer, &info);
}

#define N_BUFFERS STD_VIDEO_AV1_NUM_REF_FRAMES + 1
#define FRAME_WIDTH 720
#define FRAME_HEIGHT 480

GST_START_TEST (test_encoder_av1_key)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  GstVulkanAV1EncoderFrame *frame;
  int frame_num = 0;
  int i;
  /* Create and setup an AV1 encoder with its initial session parameters */
  enc = setup_av1_encoder (width, height, N_BUFFERS);
  if (!enc) {
    GST_WARNING ("Unable to initialize AV1 encoder");
    return;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode N_BUFFERS of I-Frames */
  for (i = 0; i < N_BUFFERS; i++) {
    frame = allocate_av1_frame (enc, width, height, TRUE);
    encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_KEY,
        frame_num, NULL, 0, NULL, 0);
    check_encoded_frame (frame, GST_AV1_KEY_FRAME);

    frame_num++;
    _av1_encode_frame_free (enc, frame);
  }

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

  tear_down_encoder (enc);
}

GST_END_TEST;

GST_START_TEST (test_encoder_av1_inter)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  GstVulkanAV1EncoderFrame *frame;
  GstVulkanAV1EncoderFrame *list0[2] = { NULL, };
  int frame_num = 0;
  int i;
  /* Create and setup an AV1 encoder with its initial session parameters */
  enc = setup_av1_encoder (width, height, N_BUFFERS);
  if (!enc) {
    GST_WARNING ("Unable to initialize AV1 encoder");
    return;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  frame = allocate_av1_frame (enc, width, height, TRUE);
  encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_KEY,
      frame_num, NULL, 0, NULL, 0);
  check_encoded_frame (frame, GST_AV1_KEY_FRAME);
  list0[0] = frame;
  frame_num++;

  /* Encode N_BUFFERS of Inter-Frames */
  for (i = 1; i < N_BUFFERS; i++) {
    frame = allocate_av1_frame (enc, width, height, TRUE);
    frame->pic_num = frame_num;
    frame->pic_order_cnt = frame_num;
    encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_INTER,
        frame_num, list0, 1, NULL, 0);
    check_encoded_frame (frame, GST_AV1_INTER_FRAME);
    _av1_encode_frame_free (enc, list0[0]);
    list0[0] = frame;
    frame_num++;
  }

  _av1_encode_frame_free (enc, frame);

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

  tear_down_encoder (enc);
}

GST_END_TEST;

GST_START_TEST (test_encoder_av1_inter_bi)
{
  GstVulkanEncoder *enc;
  uint32_t width = FRAME_WIDTH;
  uint32_t height = FRAME_HEIGHT;
  GstVulkanAV1EncoderFrame *frame;
  GstVulkanAV1EncoderFrame *list0[STD_VIDEO_AV1_NUM_REF_FRAMES] = { NULL, };
  GstVulkanAV1EncoderFrame *list1[STD_VIDEO_AV1_NUM_REF_FRAMES] = { NULL, };
  gint list0_num = 0;
  gint list1_num = 0;
  int frame_num = 0;
  GstVulkanVideoCapabilities enc_caps;

  /* Create and setup an AV1 encoder with its initial session parameters */
  enc = setup_av1_encoder (width, height, 4);
  if (!enc) {
    GST_WARNING ("Unable to initialize AV1 encoder");
    return;
  }

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  if (!enc_caps.encoder.codec.av1.maxBidirectionalCompoundReferenceCount) {
    GST_WARNING ("Driver does not support bi-directional frames");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode 1st picture as an IDR-Frame */
  frame = allocate_av1_frame (enc, width, height, TRUE);
  encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_KEY,
      frame_num, NULL, 0, NULL, 0);
  check_encoded_frame (frame, GST_AV1_KEY_FRAME);
  list0[0] = frame;
  list0_num++;
  frame_num++;

  /* Encode 4th picture as a P-Frame */
  frame = allocate_av1_frame (enc, width, height, TRUE);
  frame->pic_num = frame_num;   /* Encode order */
  frame->pic_order_cnt = 3;     /* Display order */
  encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_INTER,
      frame_num, list0, list0_num, list1, list1_num);
  check_encoded_frame (frame, GST_AV1_INTER_FRAME);
  list1[0] = frame;
  list1_num++;
  frame_num++;

  /* Encode 2nd picture as a B-Frame */
  frame = allocate_av1_frame (enc, width, height, FALSE);
  frame->pic_num = frame_num;
  frame->pic_order_cnt = 1;
  encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_INTER,
      frame_num, list0, list0_num, list1, list1_num);
  check_encoded_frame (frame, GST_AV1_INTER_FRAME);
  frame_num++;
  _av1_encode_frame_free (enc, frame);

  /* Encode 3rd picture as a B-Frame */
  frame = allocate_av1_frame (enc, width, height, FALSE);
  frame->pic_num = frame_num;
  frame->pic_order_cnt = 2;

  encode_frame (enc, frame, STD_VIDEO_AV1_FRAME_TYPE_INTER,
      frame_num, list0, list0_num, list1, list1_num);
  check_encoded_frame (frame, GST_AV1_INTER_FRAME);
  frame_num++;
  _av1_encode_frame_free (enc, frame);

  _av1_encode_frame_free (enc, list0[0]);
  _av1_encode_frame_free (enc, list1[0]);

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
    tcase_add_test (tc_basic, test_encoder_av1_key);
    tcase_add_test (tc_basic, test_encoder_av1_inter);
    tcase_add_test (tc_basic, test_encoder_av1_inter_bi);
  }

  return s;
}

GST_CHECK_MAIN (vkvideo);

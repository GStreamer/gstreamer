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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/vulkan/vulkan.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/vulkan/gstvkencoder-private.h>

// Include h264 std session params
#include "vkcodecparams_h264.c"

static GstVulkanInstance *instance;

static GstVulkanQueue *encode_queue = NULL;
static GstVulkanQueue *gfx_queue = NULL;
static GstBufferPool *img_pool;
static GstBufferPool *buffer_pool;

static GstVulkanOperation *exec = NULL;

static GstVideoInfo in_info;
static GstVideoInfo out_info;

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
  GstVulkanH264EncodeFrame *frame = pframe;

  gst_vulkan_encoder_picture_clear (&frame->picture, enc);
  g_free (frame);
}

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
}

static void
teardown (void)
{
  gst_clear_object (&encode_queue);
  gst_clear_object (&gfx_queue);
  gst_object_unref (instance);
}

#define H264_MB_SIZE_ALIGNMENT 16

/* initialize the vulkan image buffer pool */
static GstBufferPool *
allocate_image_buffer_pool (GstVulkanEncoder * enc, uint32_t width,
    uint32_t height)
{
  GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
  GstCaps *profile_caps, *caps = gst_caps_new_simple ("video/x-raw", "format",
      G_TYPE_STRING, gst_video_format_to_string (format), "width", G_TYPE_INT,
      width, "height", G_TYPE_INT, height, NULL);
  GstBufferPool *pool = gst_vulkan_image_buffer_pool_new (encode_queue->device);
  GstStructure *config = gst_buffer_pool_get_config (pool);
  gsize frame_size = width * height * 2;        //NV12

  gst_caps_set_features_simple (caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
          NULL));
  fail_unless (gst_vulkan_encoder_create_dpb_pool (enc, caps));

  gst_video_info_from_caps (&out_info, caps);

  gst_buffer_pool_config_set_params (config, caps, frame_size, 1, 0);
  gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

  profile_caps = gst_vulkan_encoder_profile_caps (enc);
  gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);

  gst_caps_unref (caps);
  gst_caps_unref (profile_caps);

  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));
  return pool;
}

static GstBufferPool *
allocate_buffer_pool (GstVulkanEncoder * enc, uint32_t width, uint32_t height)
{
  GstVideoFormat format = GST_VIDEO_FORMAT_NV12;
  GstCaps *profile_caps, *caps = gst_caps_new_simple ("video/x-raw", "format",
      G_TYPE_STRING, gst_video_format_to_string (format), "width", G_TYPE_INT,
      width, "height", G_TYPE_INT, height, NULL);
  gsize frame_size = width * height * 2;        //NV12
  GstBufferPool *pool = gst_vulkan_buffer_pool_new (encode_queue->device);
  GstStructure *config = gst_buffer_pool_get_config (pool);

  gst_caps_set_features_simple (caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER,
          NULL));

  gst_video_info_from_caps (&in_info, caps);

  gst_buffer_pool_config_set_params (config, caps, frame_size, 1, 0);


  profile_caps = gst_vulkan_encoder_profile_caps (enc);
  gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);

  gst_caps_unref (caps);
  gst_caps_unref (profile_caps);

  gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT);

  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));

  return pool;
}

static GstBuffer *
generate_input_buffer (GstBufferPool * pool, int width, int height)
{
  int i;
  GstBuffer *buffer;
  GstMapInfo info;
  GstMemory *mem;

  if ((gst_buffer_pool_acquire_buffer (pool, &buffer, NULL))
      != GST_FLOW_OK)
    goto out;

  // PLANE Y COLOR BLUE
  mem = gst_buffer_peek_memory (buffer, 0);
  gst_memory_map (mem, &info, GST_MAP_WRITE);
  for (i = 0; i < width * height; i++)
    info.data[i] = 0x29;
  gst_memory_unmap (mem, &info);

  // PLANE UV
  mem = gst_buffer_peek_memory (buffer, 1);
  gst_memory_map (mem, &info, GST_MAP_WRITE);
  for (i = 0; i < width * height / 2; i++) {
    info.data[i] = 0xf0;
    info.data[i++] = 0x6e;
  }

  gst_memory_unmap (mem, &info);

out:
  return buffer;
}

/* upload the raw input buffer pool into a vulkan image buffer */
static GstFlowReturn
upload_buffer_to_image (GstBufferPool * pool, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GError *error = NULL;
  GstVulkanCommandBuffer *cmd_buf;
  guint i, n_mems, n_planes;
  GArray *barriers = NULL;
  VkImageLayout dst_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  if ((ret = gst_buffer_pool_acquire_buffer (pool, outbuf, NULL))
      != GST_FLOW_OK)
    goto out;

  if (!exec) {
    GstVulkanCommandPool *cmd_pool =
        gst_vulkan_queue_create_command_pool (gfx_queue, &error);
    if (!cmd_pool)
      goto error;

    exec = gst_vulkan_operation_new (cmd_pool);
    gst_object_unref (cmd_pool);
  }

  if (!gst_vulkan_operation_add_dependency_frame (exec, *outbuf,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT))
    goto error;

  if (!gst_vulkan_operation_begin (exec, &error))
    goto error;

  cmd_buf = exec->cmd_buf;

  if (!gst_vulkan_operation_add_frame_barrier (exec, *outbuf,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          NULL))
    goto unlock_error;

  barriers = gst_vulkan_operation_retrieve_image_barriers (exec);
  if (barriers->len == 0) {
    ret = GST_FLOW_ERROR;
    goto unlock_error;
  }

  VkDependencyInfoKHR dependency_info = {
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
    .pImageMemoryBarriers = (gpointer) barriers->data,
    .imageMemoryBarrierCount = barriers->len,
  };

  gst_vulkan_operation_pipeline_barrier2 (exec, &dependency_info);
  dst_layout = g_array_index (barriers, VkImageMemoryBarrier2KHR, 0).newLayout;

  g_clear_pointer (&barriers, g_array_unref);

  n_mems = gst_buffer_n_memory (*outbuf);
  n_planes = GST_VIDEO_INFO_N_PLANES (&out_info);

  for (i = 0; i < n_planes; i++) {
    VkBufferImageCopy region;
    GstMemory *in_mem, *out_mem;
    GstVulkanBufferMemory *buf_mem;
    GstVulkanImageMemory *img_mem;
    const VkImageAspectFlags aspects[] = { VK_IMAGE_ASPECT_PLANE_0_BIT,
      VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT,
    };
    VkImageAspectFlags plane_aspect;
    guint idx;

    in_mem = gst_buffer_peek_memory (inbuf, i);

    buf_mem = (GstVulkanBufferMemory *) in_mem;

    if (n_planes == n_mems)
      plane_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    else
      plane_aspect = aspects[i];

    /* *INDENT-OFF* */
    region = (VkBufferImageCopy) {
        .bufferOffset = 0,
        .bufferRowLength = GST_VIDEO_INFO_COMP_WIDTH (&in_info, i),
        .bufferImageHeight = GST_VIDEO_INFO_COMP_HEIGHT (&in_info, i),
        .imageSubresource = {
            .aspectMask = plane_aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = { .x = 0, .y = 0, .z = 0, },
        .imageExtent = {
            .width = GST_VIDEO_INFO_COMP_WIDTH (&out_info, i),
            .height = GST_VIDEO_INFO_COMP_HEIGHT (&out_info, i),
            .depth = 1,
        }
    };

    idx = MIN (i, n_mems - 1);
    out_mem = gst_buffer_peek_memory (*outbuf, idx);
    if (!gst_is_vulkan_image_memory (out_mem)) {
      GST_WARNING ("Output is not a GstVulkanImageMemory");
      goto unlock_error;
    }
    img_mem = (GstVulkanImageMemory *) out_mem;

    gst_vulkan_command_buffer_lock (cmd_buf);
    vkCmdCopyBufferToImage (cmd_buf->cmd, buf_mem->buffer, img_mem->image,
        dst_layout, 1, &region);
    gst_vulkan_command_buffer_unlock (cmd_buf);
  }

  if (!gst_vulkan_operation_end (exec, &error))
    goto error;

  /*Hazard WRITE_AFTER_WRITE*/
  gst_vulkan_operation_wait (exec);


  ret = GST_FLOW_OK;

out:
  return ret;

unlock_error:
  gst_vulkan_operation_reset (exec);

error:
  if (error) {
    GST_WARNING ("Error: %s", error->message);
    g_clear_error (&error);
  }
  gst_clear_buffer (outbuf);
  ret = GST_FLOW_ERROR;
  goto out;
}

static GstVulkanH264EncodeFrame *
allocate_frame (GstVulkanEncoder * enc, int width,
    int height, gboolean is_ref)
{
  GstVulkanH264EncodeFrame *frame;
  GstBuffer *in_buffer, *img_buffer;

  in_buffer = generate_input_buffer (buffer_pool, width, height);

  upload_buffer_to_image(img_pool, in_buffer, &img_buffer);

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
  GstVulkanVideoCapabilities *enc_caps = data;

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
    .flags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR |
        VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .pNext = NULL,
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
    .cabac_init_idc = STD_VIDEO_H264_CABAC_INIT_IDC_0,
    .disable_deblocking_filter_idc = STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED,
    .slice_alpha_c0_offset_div2 = 0,
    .slice_beta_offset_div2 = 0,
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
    .pStdSliceHeader = &frame->slice_hdr,
    .constantQp = 26,
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
  int i;
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

  for (i = 0; i < instance->n_physical_devices; i++) {
    GstVulkanDevice *device = gst_vulkan_device_new_with_index (instance, i);
    encode_queue =
        gst_vulkan_device_select_queue (device, VK_QUEUE_VIDEO_ENCODE_BIT_KHR);
    gfx_queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
    gst_object_unref (device);

    if (encode_queue && gfx_queue)
      break;
  }

  if (!encode_queue) {
    GST_WARNING ("Unable to find encoding queue");
    return NULL;
  }

  if (!gfx_queue) {
    GST_WARNING ("Unable to find graphics queue");
    return NULL;
  }

  enc = gst_vulkan_encoder_create_from_queue (encode_queue,
      VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);

  if (!enc) {
    GST_WARNING ("Unable to create a vulkan encoder, queue=%p", encode_queue);
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
    .pStdSPSs = &h264_std_sps,
    .stdSPSCount = 1,
    .pStdPPSs = &h264_std_pps,
    .stdPPSCount = 1,
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
  gst_clear_object (&encode_queue);
  gst_clear_object (&gfx_queue);
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
    frame = allocate_frame (enc, width, height, TRUE);
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
  frame = allocate_frame (enc, width, height, TRUE);
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE_IDR);
  list0[0] = frame;
  frame_num++;

  /* Encode following pictures as P-Frames */
  for (i = 1; i < N_BUFFERS; i++) {
    frame = allocate_frame (enc, width, height, TRUE);
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
  frame = allocate_frame (enc, width, height, TRUE);
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE_IDR);
  list0[0] = frame;
  list0_num++;
  frame_num++;

  /* Encode 4th picture as a P-Frame */
  frame = allocate_frame (enc, width, height, TRUE);
  frame->pic_num = 3;
  frame->pic_order_cnt = frame->pic_num * 2;
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_P,
      frame_num, list0, list0_num, list1, list1_num, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE);
  list1[0] = frame;
  list1_num++;
  frame_num++;

  /* Encode second picture as a B-Frame */
  frame = allocate_frame (enc, width, height, FALSE);
  frame->pic_num = 1;
  frame->pic_order_cnt = frame->pic_num * 2;
  encode_frame (enc, frame, STD_VIDEO_H264_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, sps_id, pps_id);
  check_encoded_frame (frame, GST_H264_NAL_SLICE);
  frame_num++;
  _h264_encode_frame_free (enc, frame);

  /* Encode third picture as a B-Frame */
  frame = allocate_frame (enc, width, height, FALSE);
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

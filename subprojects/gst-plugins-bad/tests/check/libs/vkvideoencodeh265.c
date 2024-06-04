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
#include <gst/codecparsers/gsth265parser.h>
#include <gst/vulkan/vulkan.h>
#include <gst/vulkan/gstvkencoder-private.h>


#include <math.h>

// Include h265 std session params
#include "vkcodecparams_h265.c"

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
  GstVulkanEncodePicture *picture;

  VkVideoEncodeH265PictureInfoKHR enc_pic_info;
  VkVideoEncodeH265NaluSliceSegmentInfoKHR slice_info;
  VkVideoEncodeH265DpbSlotInfoKHR dpb_slot_info;
  VkVideoEncodeH265RateControlInfoKHR rc_info;
  VkVideoEncodeH265RateControlLayerInfoKHR rc_layer_info;
  VkVideoEncodeH265QualityLevelPropertiesKHR quality_level;

  StdVideoEncodeH265WeightTable slice_wt;
  StdVideoEncodeH265SliceSegmentHeader slice_hdr;
  StdVideoEncodeH265PictureInfo pic_info;
  StdVideoEncodeH265ReferenceInfo ref_info;
  StdVideoEncodeH265ReferenceListsInfo ref_list_info;
  StdVideoH265ShortTermRefPicSet short_term_ref_pic_set;
} GstVulkanH265EncodeFrame;


static GstVulkanH265EncodeFrame *
_h265_encode_frame_new (GstVulkanEncodePicture * picture)
{
  GstVulkanH265EncodeFrame *frame;

  g_return_val_if_fail (picture, NULL);
  frame = g_new (GstVulkanH265EncodeFrame, 1);
  frame->picture = picture;

  return frame;
}

static void
_h265_encode_frame_free (gpointer pframe)
{
  GstVulkanH265EncodeFrame *frame = pframe;
  g_clear_pointer (&frame->picture, gst_vulkan_encode_picture_free);
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

/* initialize the input vulkan image buffer pool */
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
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

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

/* initialize the raw input buffer pool */
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
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_BUFFER, NULL));

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

/* generate a buffer representing a blue window in NV12 format */
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
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT))
    goto error;

  if (!gst_vulkan_operation_begin (exec, &error))
    goto error;

  cmd_buf = exec->cmd_buf;

  if (!gst_vulkan_operation_add_frame_barrier (exec, *outbuf,
          VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, NULL))
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

/* allocate a frame to be encoded from given buffer pools */
static GstVulkanH265EncodeFrame *
allocate_frame (GstVulkanEncoder * enc, int width,
    int height, gboolean is_ref, gint nb_refs)
{
  GstVulkanH265EncodeFrame *frame;
  GstBuffer *in_buffer, *img_buffer;

  /* generate the input buffer */
  in_buffer = generate_input_buffer (buffer_pool, width, height);

  /* get a Vulkan image buffer out of the input buffer */
  upload_buffer_to_image(img_pool, in_buffer, &img_buffer);


  frame = _h265_encode_frame_new (gst_vulkan_encode_picture_new (enc, img_buffer, width, height, is_ref,
      nb_refs));
  fail_unless (frame);
  fail_unless (frame->picture);
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
encode_frame (GstVulkanEncoder * enc, GstVulkanH265EncodeFrame * frame,
    StdVideoH265SliceType slice_type, guint frame_num,
    GstVulkanH265EncodeFrame ** list0, gint list0_num,
    GstVulkanH265EncodeFrame ** list1, gint list1_num, gint vps_id, gint sps_id,
    gint pps_id)
{
  GstVulkanVideoCapabilities enc_caps;
  int i, ref_pics_num = 0;
  GstVulkanEncodePicture *ref_pics[16] = { NULL, };
  gint16 delta_poc_s0_minus1 = 0, delta_poc_s1_minus1 = 0;
  guint qp_i = 26;
  guint qp_p = 26;
  guint qp_b = 26;
  GstVulkanEncodePicture *picture = frame->picture;
  gint picture_type = PICTURE_TYPE(slice_type, picture->is_ref);

  GST_DEBUG ("Encoding frame num: %d", frame_num);

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

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

  if (list0_num) {
    delta_poc_s0_minus1 =
        picture->pic_order_cnt - list0[0]->picture->pic_order_cnt - 1;
  }
  if (list1_num) {
    delta_poc_s1_minus1 =
        list1[0]->picture->pic_order_cnt - picture->pic_order_cnt - 1;
  }

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
          .is_reference = picture->is_ref,
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
    .PicOrderCntVal = picture->pic_order_cnt,
    .pShortTermRefPicSet = &frame->short_term_ref_pic_set,
    .pLongTermRefPics = NULL,
    /* *INDENT-ON* */
  };

  if (picture->nb_refs) {
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

  frame->slice_info = (VkVideoEncodeH265NaluSliceSegmentInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR,
    .pNext = NULL,
    .pStdSliceSegmentHeader = &frame->slice_hdr,
    /* *INDENT-ON* */
  };

  frame->rc_layer_info = (VkVideoEncodeH265RateControlLayerInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_LAYER_INFO_KHR,
    .pNext = NULL,
    .useMinQp = TRUE,
    .minQp = (VkVideoEncodeH265QpKHR){ qp_i, qp_p, qp_b },
    .useMaxQp = TRUE,
    .maxQp = (VkVideoEncodeH265QpKHR){ qp_i, qp_p, qp_b },
    .useMaxFrameSize = 0,
    .maxFrameSize = (VkVideoEncodeH265FrameSizeKHR) {0, 0, 0},
    /* *INDENT-ON* */
  };

  frame->rc_info = (VkVideoEncodeH265RateControlInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_RATE_CONTROL_INFO_KHR,
    .pNext = &frame->rc_layer_info,
    .gopFrameCount = 0,
    .idrPeriod = 0,
    .consecutiveBFrameCount = 0,
    /* *INDENT-ON* */
  };

  frame->quality_level = (VkVideoEncodeH265QualityLevelPropertiesKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_QUALITY_LEVEL_PROPERTIES_KHR,
    .pNext = NULL,
    .preferredRateControlFlags = VK_VIDEO_ENCODE_H265_RATE_CONTROL_REGULAR_GOP_BIT_KHR,
    .preferredGopFrameCount = 0,
    .preferredIdrPeriod = 0,
    .preferredConsecutiveBFrameCount = 0,
    .preferredConstantQp = (VkVideoEncodeH265QpKHR){ qp_i, qp_p, qp_b },
    .preferredMaxL0ReferenceCount = 0,
    .preferredMaxL1ReferenceCount = 0,
    /* *INDENT-ON* */
  };

  frame->enc_pic_info = (VkVideoEncodeH265PictureInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR,
    .pNext = NULL,
    .naluSliceSegmentEntryCount = 1,
    .pNaluSliceSegmentEntries = &frame->slice_info,
    .pStdPictureInfo = &frame->pic_info,
    /* *INDENT-ON* */
  };

  frame->ref_info = (StdVideoEncodeH265ReferenceInfo) {
    /* *INDENT-OFF* */
    .flags = (StdVideoEncodeH265ReferenceInfoFlags) {
      .used_for_long_term_reference = 0,
      .unused_for_reference = 0,
    },
    .pic_type = picture_type,
    .PicOrderCntVal = picture->pic_order_cnt,
    .TemporalId = 0,
    /* *INDENT-ON* */
  };

  frame->dpb_slot_info = (VkVideoEncodeH265DpbSlotInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_DPB_SLOT_INFO_KHR,
    .pNext = NULL,
    .pStdReferenceInfo = &frame->ref_info,
    /* *INDENT-ON* */
  };

  picture->codec_pic_info = &frame->enc_pic_info;
  picture->codec_rc_layer_info = &frame->rc_layer_info;
  picture->codec_rc_info = &frame->rc_info;
  picture->codec_quality_level = &frame->quality_level;
  picture->codec_dpb_slot_info = &frame->dpb_slot_info;

  for (i = 0; i < list0_num; i++) {
    ref_pics[i] = list0[i]->picture;
    frame->ref_list_info.RefPicList0[0] = list0[i]->picture->slotIndex;
    ref_pics_num++;
  }
  for (i = 0; i < list1_num; i++) {
    ref_pics[i + list0_num] = list1[i]->picture;
    ref_pics_num++;
    frame->ref_list_info.RefPicList1[i] = list1[i]->picture->slotIndex;
  }

  picture->nb_refs = ref_pics_num;

  fail_unless (gst_vulkan_encoder_encode (enc, picture, ref_pics));
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
  int i;
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

  profile = (GstVulkanVideoProfile) {
    /* *INDENT-OFF* */
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.codec,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    },
    .codec.h265enc = (VkVideoEncodeH265ProfileInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR,
      .stdProfileIdc = profile_idc,
    }
    /* *INDENT-ON* */
  };

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
      VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR);

  if (!enc) {
    GST_WARNING ("Unable to create a vulkan encoder, queue=%p", encode_queue);
    return NULL;
  }

  fail_unless (gst_vulkan_encoder_start (enc, &profile, width * height * 3,
          &err));

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));


  if (enc_caps.codec.h265enc.
      ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_64_BIT_KHR) {
    max_ctb_size = 64;
  } else if (enc_caps.codec.h265enc.
      ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR) {
    max_ctb_size = 32;
  }

  if (enc_caps.codec.h265enc.
      ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_16_BIT_KHR) {
    min_ctb_size = 16;
  } else if (enc_caps.codec.h265enc.
      ctbSizes & VK_VIDEO_ENCODE_H265_CTB_SIZE_32_BIT_KHR) {
    min_ctb_size = 32;
  }

  if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
    min_tb_size = 4;
  else if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
    min_tb_size = 8;
  else if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
    min_tb_size = 16;
  else if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
    min_tb_size = 32;

  if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_32_BIT_KHR)
    max_tb_size = 32;
  else if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_16_BIT_KHR)
    max_tb_size = 16;
  else if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_8_BIT_KHR)
    max_tb_size = 8;
  else if (enc_caps.codec.h265enc.transformBlockSizes &
      VK_VIDEO_ENCODE_H265_TRANSFORM_BLOCK_SIZE_4_BIT_KHR)
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
      enc_caps.codec.h265enc.stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR ? 1 : 0;
  h265_std_pps.flags.weighted_pred_flag =
      enc_caps.codec.h265enc.stdSyntaxFlags &
      VK_VIDEO_ENCODE_H265_STD_WEIGHTED_PRED_FLAG_SET_BIT_KHR ? 1 : 0;
  h265_std_pps.flags.entropy_coding_sync_enabled_flag =
      (enc_caps.codec.h265enc.maxTiles.width > 1
      || enc_caps.codec.h265enc.maxTiles.height > 1) ? 1 : 0;
  h265_std_pps.sps_video_parameter_set_id = vps_id;
  h265_std_pps.pps_seq_parameter_set_id = sps_id;
  h265_std_pps.pps_pic_parameter_set_id = pps_id;

  params_add = (VkVideoEncodeH265SessionParametersAddInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
    .pStdVPSs = &h265_std_vps,
    .stdVPSCount = 1,
    .pStdSPSs = &h265_std_sps,
    .stdSPSCount = 1,
    .pStdPPSs = &h265_std_pps,
    .stdPPSCount = 1,
    /* *INDENT-ON* */
  };

  enc_params.h265 = (VkVideoEncodeH265SessionParametersCreateInfoKHR) {
    /* *INDENT-OFF* */
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdVPSCount = 1,
    .maxStdSPSCount = 1,
    .maxStdPPSCount = 1,
    .pParametersAddInfo = &params_add
    /* *INDENT-ON* */
  };

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
  fail_unless (frame->picture->out_buffer != NULL);
  gst_buffer_map (frame->picture->out_buffer, &info, GST_MAP_READ);
  fail_unless (info.size);
  GST_MEMDUMP ("out buffer", info.data, info.size);
  check_h265_nalu (info.data, info.size, nal_type);
  gst_buffer_unmap (frame->picture->out_buffer, &info);
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
    return;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode N_BUFFERS I-Frames */
  for (i = 0; i < N_BUFFERS; i++) {
    frame = allocate_frame (enc, width, height, TRUE, 0);
    encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_I,
        frame_num, NULL, 0, NULL, 0, vps_id, sps_id, pps_id);
    check_encoded_frame (frame, GST_H265_NAL_SLICE_IDR_W_RADL);

    frame_num++;
    _h265_encode_frame_free (frame);
  }

  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

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
    return;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  frame = allocate_frame (enc, width, height, TRUE, 0);
  /* Encode first picture as an IDR-Frame */
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_IDR_W_RADL);
  list0[0] = frame;
  frame_num++;

  /* Encode following pictures as a P-Frames */
  for (i = 1; i < N_BUFFERS; i++) {
    frame = allocate_frame (enc, width, height, TRUE, list0_num);
    frame->picture->pic_num = frame_num;
    encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_P,
        frame_num, list0, list0_num, NULL, 0, vps_id, sps_id, pps_id);
    check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_R);
    _h265_encode_frame_free (list0[0]);
    list0[0] = frame;
    frame_num++;
  }
  _h265_encode_frame_free (list0[0]);
  fail_unless (gst_buffer_pool_set_active (buffer_pool, FALSE));
  gst_object_unref (buffer_pool);
  fail_unless (gst_buffer_pool_set_active (img_pool, FALSE));
  gst_object_unref (img_pool);

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
    return;
  }

  fail_unless (gst_vulkan_encoder_caps (enc, &enc_caps));

  if (!enc_caps.codec.h265enc.maxL1ReferenceCount) {
    GST_WARNING ("Driver does not support B frames");
    goto beach;
  }

  buffer_pool = allocate_buffer_pool (enc, width, height);
  img_pool = allocate_image_buffer_pool (enc, width, height);

  /* Encode first picture as an IDR-Frame */
  frame = allocate_frame (enc, width, height, TRUE, 0);
  frame->picture->pic_num = frame_num;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_I,
      frame_num, NULL, 0, NULL, 0, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_IDR_W_RADL);
  list0[0] = frame;
  frame_num++;

  /* Encode 4th picture as a P-Frame */
  frame = allocate_frame (enc, width, height, TRUE, list0_num);
  frame->picture->pic_num = frame_num + 2;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_P,
      frame_num, list0, list0_num, NULL, 0, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_R);
  list1[0] = frame;
  frame_num++;

  /* Encode 2nd picture as a B-Frame */
  frame = allocate_frame (enc, width, height, FALSE, list0_num + list1_num);
  frame->picture->pic_num = frame_num - 1;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_N);
  frame_num++;
  _h265_encode_frame_free (frame);

  /* Encode 3rd picture as a B-Frame */
  frame = allocate_frame (enc, width, height, FALSE, list0_num + list1_num);
  frame->picture->pic_num = frame_num - 1;
  encode_frame (enc, frame, STD_VIDEO_H265_SLICE_TYPE_B,
      frame_num, list0, list0_num, list1, list1_num, vps_id, sps_id, pps_id);
  check_encoded_frame (frame, GST_H265_NAL_SLICE_TRAIL_N);
  frame_num++;
  _h265_encode_frame_free (frame);

  _h265_encode_frame_free (list0[0]);
  _h265_encode_frame_free (list1[0]);

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

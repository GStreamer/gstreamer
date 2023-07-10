/* GStreamer
 *
 * Copyright (C) 2023 Igalia, S.L.
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

#include "gst/vulkan/gstvkdecoder-private.h"

static GstVulkanInstance *instance;
static GstVulkanDevice *device;
static GstVulkanQueue *video_queue = NULL;
static GstVulkanQueue *graphics_queue = NULL;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
}

static void
teardown (void)
{
  gst_clear_object (&video_queue);
  gst_clear_object (&graphics_queue);
  gst_clear_object (&device);
  gst_object_unref (instance);
}

struct QueueProps
{
  guint expected_flags;
  guint codec;
};

static gboolean
_choose_queue (GstVulkanDevice * device, GstVulkanQueue * _queue, gpointer data)
{
  guint flags =
      device->physical_device->queue_family_props[_queue->family].queueFlags;
  guint32 codec =
      device->physical_device->queue_family_ops[_queue->family].video;
  struct QueueProps *qprops = data;

  if ((flags & VK_QUEUE_TRANSFER_BIT) == VK_QUEUE_TRANSFER_BIT) {
    gst_object_replace ((GstObject **) & graphics_queue,
        GST_OBJECT_CAST (_queue));
  }

  if (((flags & qprops->expected_flags) == qprops->expected_flags)
      && ((codec & qprops->codec) == qprops->codec))
    gst_object_replace ((GstObject **) & video_queue, GST_OBJECT_CAST (_queue));


  return !(graphics_queue && video_queue);
}

static void
setup_queue (guint expected_flags, guint codec)
{
  int i;
  struct QueueProps qprops = { expected_flags, codec };

  for (i = 0; i < instance->n_physical_devices; i++) {
    device = gst_vulkan_device_new_with_index (instance, i);
    fail_unless (gst_vulkan_device_open (device, NULL));
    gst_vulkan_device_foreach_queue (device, _choose_queue, &qprops);
    if (video_queue && GST_IS_VULKAN_QUEUE (video_queue)
        && graphics_queue && GST_IS_VULKAN_QUEUE (graphics_queue))
      break;
    gst_clear_object (&device);
    gst_clear_object (&video_queue);
    gst_clear_object (&graphics_queue);
  }
}

static void
get_output_buffer (GstVulkanDecoder * dec, VkFormat vk_format,
    GstVulkanDecoderPicture * pic)
{
  GstBuffer *outbuf;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  GstVideoFormat format = gst_vulkan_format_to_video_format (vk_format);
  GstCaps *profile_caps, *caps = gst_caps_new_simple ("video/x-raw", "format",
      G_TYPE_STRING, gst_video_format_to_string (format), "width", G_TYPE_INT,
      320, "height", G_TYPE_INT, 240, NULL);
  GstBufferPool *pool;
  GstStructure *config;

  gst_caps_set_features_simple (caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL));

  profile_caps = gst_vulkan_decoder_profile_caps (dec);
  fail_unless (profile_caps);
  fail_unless (gst_vulkan_decoder_create_dpb_pool (dec, caps));

  if (!dec->dedicated_dpb)
    usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

  pool = gst_vulkan_image_buffer_pool_new (device);

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, 1024, 1, 0);

  gst_caps_unref (caps);

  gst_vulkan_image_buffer_pool_config_set_allocation_params (config, usage,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR, VK_ACCESS_TRANSFER_WRITE_BIT);
  gst_vulkan_image_buffer_pool_config_set_decode_caps (config, profile_caps);

  gst_caps_unref (profile_caps);

  fail_unless (gst_buffer_pool_set_config (pool, config));
  fail_unless (gst_buffer_pool_set_active (pool, TRUE));

  fail_unless (gst_buffer_pool_acquire_buffer (pool, &outbuf, NULL) ==
      GST_FLOW_OK);

  fail_unless (gst_vulkan_decoder_picture_init (dec, pic, outbuf));

  gst_buffer_unref (outbuf);
  fail_unless (gst_buffer_pool_set_active (pool, FALSE));
  gst_object_unref (pool);
}

static void
download_and_check_output_buffer (GstVulkanDecoder * dec, VkFormat vk_format,
    GstVulkanDecoderPicture * pic)
{
  GstVulkanOperation *exec;
  GstVulkanCommandPool *cmd_pool;
  GstBufferPool *out_pool = gst_vulkan_buffer_pool_new (dec->queue->device);
  GstStructure *config = gst_buffer_pool_get_config (out_pool);
  GstVideoFormat format = gst_vulkan_format_to_video_format (vk_format);
  GstCaps *caps = gst_caps_new_simple ("video/x-raw", "format",
      G_TYPE_STRING, gst_video_format_to_string (format), "width", G_TYPE_INT,
      320, "height", G_TYPE_INT, 240, NULL);
  GstBuffer *rawbuf;
  GError *error = NULL;
  GArray *barriers;
  GstVideoInfo info;
  GstMapInfo mapinfo;
  guint i, n_mems, n_planes;

  gst_buffer_pool_config_set_params (config, caps, 1, 0, 0);
  gst_buffer_pool_set_config (out_pool, config);

  gst_video_info_from_caps (&info, caps);
  gst_caps_unref (caps);

  gst_buffer_pool_set_active (out_pool, TRUE);
  fail_unless (gst_buffer_pool_acquire_buffer (out_pool, &rawbuf, NULL)
      == GST_FLOW_OK);

  cmd_pool = gst_vulkan_queue_create_command_pool (graphics_queue, &error);
  fail_unless (cmd_pool);
  exec = gst_vulkan_operation_new (cmd_pool);
  gst_object_unref (cmd_pool);

  fail_unless (gst_vulkan_operation_begin (exec, &error));
  gst_vulkan_operation_add_dependency_frame (exec, pic->out,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  gst_vulkan_operation_add_frame_barrier (exec, pic->out,
      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, NULL);

  barriers = gst_vulkan_operation_retrieve_image_barriers (exec);
  /* *INDENT-OFF* */
  vkCmdPipelineBarrier2 (exec->cmd_buf->cmd, &(VkDependencyInfo) {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .pImageMemoryBarriers = (gpointer) barriers->data,
      .imageMemoryBarrierCount = barriers->len,
    });
  /* *INDENT-ON* */

  n_planes = GST_VIDEO_INFO_N_PLANES (&info);
  n_mems = gst_buffer_n_memory (pic->out);

  for (i = 0; i < n_planes; i++) {
    VkBufferImageCopy region;
    GstMemory *out_mem;
    GstVulkanBufferMemory *buf_mem;
    GstVulkanImageMemory *img_mem;
    gint idx;
    const VkImageAspectFlags aspects[] = { VK_IMAGE_ASPECT_PLANE_0_BIT,
      VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT,
    };
    VkImageAspectFlags plane_aspect;

    idx = MIN (i, n_mems - 1);
    img_mem = (GstVulkanImageMemory *) gst_buffer_peek_memory (pic->out, idx);

    out_mem = gst_buffer_peek_memory (rawbuf, i);
    buf_mem = (GstVulkanBufferMemory *) out_mem;

    if (n_planes == n_mems)
      plane_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    else
      plane_aspect = aspects[i];

    /* *INDENT-OFF* */
    region = (VkBufferImageCopy) {
      .bufferOffset = 0,
      .bufferRowLength = GST_VIDEO_INFO_COMP_WIDTH (&info, i),
      .bufferImageHeight = GST_VIDEO_INFO_COMP_HEIGHT (&info, i),
      .imageSubresource = {
        /* XXX: each plane is a buffer */
        .aspectMask = plane_aspect,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
      .imageOffset = { .x = 0, .y = 0, .z = 0, },
      .imageExtent = {
        .width = GST_VIDEO_INFO_COMP_WIDTH (&info, i),
        .height = GST_VIDEO_INFO_COMP_HEIGHT (&info, i),
        .depth = 1,
      }
    };
    /* *INDENT-ON* */

    vkCmdCopyImageToBuffer (exec->cmd_buf->cmd, img_mem->image,
        g_array_index (barriers, VkImageMemoryBarrier2, 0).newLayout,
        buf_mem->buffer, 1, &region);
  }

  g_array_unref (barriers);

  fail_unless (gst_vulkan_operation_end (exec, &error));

  gst_vulkan_operation_wait (exec);
  gst_object_unref (exec);

  fail_unless (gst_buffer_map (rawbuf, &mapinfo, GST_MAP_READ));

  /* Check for a blue square */
  /* Y */
  for (i = 0; i < 0x12c00; i++)
    fail_unless (mapinfo.data[i] == 0x29);
  /* UV */
  for (i = 0x12c00; i < 0x1c1f0; i++)
    fail_unless (mapinfo.data[i] == 0xf0 && mapinfo.data[++i] == 0x6e);
  gst_buffer_unmap (rawbuf, &mapinfo);

  gst_buffer_unref (rawbuf);
  fail_unless (gst_buffer_pool_set_active (out_pool, FALSE));
  gst_object_unref (out_pool);
}

#include "vkcodecparams_h264.c"

static VkVideoDecodeH264SessionParametersAddInfoKHR h264_params = {
  .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR,
  .stdSPSCount = 1,
  .pStdSPSs = &h264_std_sps,
  .stdPPSCount = 1,
  .pStdPPSs = &h264_std_pps,
};

GST_START_TEST (test_h264_decoder)
{
  GstVulkanDecoder *dec;
  GError *err = NULL;
  VkVideoFormatPropertiesKHR format_prop;
  /* *INDENT-OFF* */
  GstVulkanVideoProfile profile = {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.usage,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    },
    .usage.decode = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR,
      .videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR,
      .pNext = &profile.codec,
    },
    .codec.h264dec = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
      .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN,
      .pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR,
    }
  };
  GstVulkanDecoderParameters create_params = { {
    .sType =
        VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .maxStdSPSCount = h264_params.stdSPSCount,
    .maxStdPPSCount = h264_params.stdPPSCount,
    .pParametersAddInfo = &h264_params,
    } };
  /* *INDENT-ON* */
  GstVulkanVideoCapabilities video_caps;
  GstVulkanDecoderPicture pic = { NULL, };

  setup_queue (VK_QUEUE_VIDEO_DECODE_BIT_KHR,
      VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR);
  if (!video_queue) {
    GST_WARNING ("Unable to find decoding queue");
    return;
  }

  dec = gst_vulkan_decoder_new_from_queue (video_queue,
      VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR);
  if (!dec) {
    GST_WARNING ("Unable to create a vulkan decoder");
    return;
  }

  fail_unless (gst_vulkan_decoder_start (dec, &profile, &err));

  fail_unless (gst_vulkan_decoder_update_ycbcr_sampler (dec,
          VK_SAMPLER_YCBCR_RANGE_ITU_FULL, VK_CHROMA_LOCATION_COSITED_EVEN,
          VK_CHROMA_LOCATION_MIDPOINT, &err));

  fail_unless (gst_vulkan_decoder_update_video_session_parameters (dec,
          &create_params, &err));

  fail_unless (gst_vulkan_decoder_out_format (dec, &format_prop));
  fail_unless (gst_vulkan_decoder_caps (dec, &video_caps));

  get_output_buffer (dec, format_prop.format, &pic);

  /* get input buffer */
  fail_unless (gst_vulkan_decoder_append_slice (dec, &pic, h264_slice,
          sizeof (h264_slice), TRUE));

  /* decode */
  {
    StdVideoDecodeH264PictureInfo std_pic = {
      .flags = {
            .field_pic_flag = 0,
            .is_intra = 1,
            .IdrPicFlag = 1,
            .bottom_field_flag = 0,
            .is_reference = 1,
            .complementary_field_pair = 0,
          },
      .seq_parameter_set_id = 0,
      .pic_parameter_set_id = 0,
      .reserved1 = 0,
      .reserved2 = 0,
      .frame_num = 0,
      .idr_pic_id = 0,
      .PicOrderCnt = {0,},
    };
    StdVideoDecodeH264ReferenceInfo std_h264_ref = {
      .flags = {
            .top_field_flag = 0,
            .bottom_field_flag = 0,
            .used_for_long_term_reference = 0,
            .is_non_existing = 0,
          },
      .FrameNum = 0,
      .reserved = 0,
      .PicOrderCnt = {0,},
    };
    VkVideoDecodeH264DpbSlotInfoKHR h264_dpb_slot = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR,
      .pStdReferenceInfo = &std_h264_ref,
    };
    VkVideoDecodeH264PictureInfoKHR vk_pic = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR,
      .pStdPictureInfo = &std_pic,
      .sliceCount = pic.slice_offs->len - 1,
      .pSliceOffsets = (guint32 *) pic.slice_offs->data,
    };

    /* *INDENT-OFF* */
    pic.pic_res = (VkVideoPictureResourceInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
      .codedOffset = (VkOffset2D) {0, 0},
      .codedExtent = (VkExtent2D) {320, 240},
      .baseArrayLayer = 0,
      .imageViewBinding = pic.img_view_ref->view,
    };
    pic.slot = (VkVideoReferenceSlotInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
      .pNext = &h264_dpb_slot,
      .slotIndex = 0,
      .pPictureResource = &pic.pic_res,
    };
    pic.decode_info = (VkVideoDecodeInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
      .pNext = &vk_pic,
      .flags = 0,
      .srcBufferOffset = 0,
      .dstPictureResource = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D) {0, 0},
        .codedExtent = (VkExtent2D) {320, 240},
        .baseArrayLayer = 0,
        .imageViewBinding = pic.img_view_out->view,
      },
      .pSetupReferenceSlot = &pic.slot,
      .referenceSlotCount = 0,
      .pReferenceSlots = pic.slots,
    };
    /* *INDENT-ON* */

    fail_unless (gst_vulkan_decoder_decode (dec, &pic, &err));
  }

  download_and_check_output_buffer (dec, format_prop.format, &pic);

  fail_unless (gst_vulkan_decoder_stop (dec));

  gst_vulkan_decoder_picture_release (&pic);

  gst_object_unref (dec);
}

GST_END_TEST;

#include "vkcodecparams_h265.c"

static VkVideoDecodeH265SessionParametersAddInfoKHR h265_params = {
  .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_ADD_INFO_KHR,
  .stdVPSCount = 1,
  .pStdVPSs = &h265_std_vps,
  .stdSPSCount = 1,
  .pStdSPSs = &h265_std_sps,
  .stdPPSCount = 1,
  .pStdPPSs = &h265_std_pps,
};

GST_START_TEST (test_h265_decoder)
{
  GstVulkanDecoder *dec;
  GError *err = NULL;
  VkVideoFormatPropertiesKHR format_prop;
  /* *INDENT-OFF* */
  GstVulkanVideoProfile profile = {
    .profile = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
      .pNext = &profile.usage,
      .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR,
      .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
      .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
      .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
    },
    .usage.decode = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_USAGE_INFO_KHR,
      .videoUsageHints = VK_VIDEO_DECODE_USAGE_DEFAULT_KHR,
      .pNext = &profile.codec,
    },
    .codec.h265dec = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR,
      .stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN,
    }
  };
  GstVulkanDecoderParameters create_params = {
    .h265 = {
      .sType =
          VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR,
      .maxStdVPSCount = h265_params.stdVPSCount,
      .maxStdSPSCount = h265_params.stdSPSCount,
      .maxStdPPSCount = h265_params.stdPPSCount,
      .pParametersAddInfo = &h265_params,
    }
  };
  /* *INDENT-ON* */
  GstVulkanVideoCapabilities video_caps;
  GstVulkanDecoderPicture pic = { NULL, };

  setup_queue (VK_QUEUE_VIDEO_DECODE_BIT_KHR,
      VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR);
  if (!video_queue) {
    GST_WARNING ("Unable to find decoding queue");
    return;
  }

  dec = gst_vulkan_decoder_new_from_queue (video_queue,
      VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR);
  if (!dec) {
    GST_WARNING ("Unable to create a vulkan decoder");
    return;
  }

  fail_unless (gst_vulkan_decoder_start (dec, &profile, &err));

  fail_unless (gst_vulkan_decoder_update_ycbcr_sampler (dec,
          VK_SAMPLER_YCBCR_RANGE_ITU_FULL, VK_CHROMA_LOCATION_COSITED_EVEN,
          VK_CHROMA_LOCATION_MIDPOINT, &err));

  fail_unless (gst_vulkan_decoder_update_video_session_parameters (dec,
          &create_params, &err));

  fail_unless (gst_vulkan_decoder_out_format (dec, &format_prop));
  fail_unless (gst_vulkan_decoder_caps (dec, &video_caps));

  get_output_buffer (dec, format_prop.format, &pic);

  /* get input buffer */
  fail_unless (gst_vulkan_decoder_append_slice (dec, &pic, h265_slice,
          sizeof (h265_slice), TRUE));

  /* decode */
  {
    StdVideoDecodeH265PictureInfo std_pic = {
      .flags = {
            .IrapPicFlag = 1,
            .IdrPicFlag = 1,
            .IsReference = 1,
            .short_term_ref_pic_set_sps_flag = 0,
          },
      .sps_video_parameter_set_id = 0,
      .pps_seq_parameter_set_id = 0,
      .pps_pic_parameter_set_id = 0,
      .NumDeltaPocsOfRefRpsIdx = 0,
      .PicOrderCntVal = 0,
      .NumBitsForSTRefPicSetInSlice = 0,
      .RefPicSetStCurrBefore =
          {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,},
      .RefPicSetStCurrAfter = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,},
      .RefPicSetLtCurr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,},
    };
    StdVideoDecodeH265ReferenceInfo std_h265_ref = {
      .flags = {
            .used_for_long_term_reference = 0,
            .unused_for_reference = 0,
          },
      .PicOrderCntVal = 0,
    };
    VkVideoDecodeH265DpbSlotInfoKHR h265_dpb_slot = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_DPB_SLOT_INFO_KHR,
      .pStdReferenceInfo = &std_h265_ref,
    };
    VkVideoDecodeH265PictureInfoKHR vk_pic = {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR,
      .pStdPictureInfo = &std_pic,
      .sliceSegmentCount = pic.slice_offs->len - 1,
      .pSliceSegmentOffsets = (guint32 *) pic.slice_offs->data,
    };

    /* *INDENT-OFF* */
    pic.pic_res = (VkVideoPictureResourceInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
      .codedOffset = (VkOffset2D) {0, 0},
      .codedExtent = (VkExtent2D) {320, 240},
      .baseArrayLayer = 0,
      .imageViewBinding = pic.img_view_ref->view,
    };
    pic.slot = (VkVideoReferenceSlotInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
      .pNext = &h265_dpb_slot,
      .slotIndex = 0,
      .pPictureResource = &pic.pic_res,
    };
    pic.decode_info = (VkVideoDecodeInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR,
      .pNext = &vk_pic,
      .flags = 0,
      .srcBufferOffset = 0,
      .dstPictureResource = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .codedOffset = (VkOffset2D) {0, 0},
        .codedExtent = (VkExtent2D) {320, 240},
        .baseArrayLayer = 0,
        .imageViewBinding = pic.img_view_out->view,
      },
      .pSetupReferenceSlot = &pic.slot,
      .referenceSlotCount = 0,
      .pReferenceSlots = pic.slots,
    };
    /* *INDENT-ON* */

    fail_unless (gst_vulkan_decoder_decode (dec, &pic, &err));
  }

  download_and_check_output_buffer (dec, format_prop.format, &pic);

  fail_unless (gst_vulkan_decoder_stop (dec));

  gst_vulkan_decoder_picture_release (&pic);

  gst_object_unref (dec);
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
    tcase_add_test (tc_basic, test_h264_decoder);
    tcase_add_test (tc_basic, test_h265_decoder);
  }

  return s;
}

GST_CHECK_MAIN (vkvideo);

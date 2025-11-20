/*
 * GStreamer
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvkvideo-private.h"
#include "gstvkphysicaldevice-private.h"
#include "gstvkinstance.h"

#include <vk_video/vulkan_video_codecs_common.h>

/* *INDENT-OFF* */
const VkExtensionProperties _vk_codec_extensions[] = {
  [GST_VK_VIDEO_EXTENSION_DECODE_H264] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION,
  },
  [GST_VK_VIDEO_EXTENSION_DECODE_H265] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION,
  },
  [GST_VK_VIDEO_EXTENSION_DECODE_VP9] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_VP9_DECODE_SPEC_VERSION,
  },
  [GST_VK_VIDEO_EXTENSION_DECODE_AV1] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_DECODE_SPEC_VERSION,
  },
  [GST_VK_VIDEO_EXTENSION_ENCODE_H264] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION,
  },
  [GST_VK_VIDEO_EXTENSION_ENCODE_H265] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION,
  },
  [GST_VK_VIDEO_EXTENSION_ENCODE_AV1] = {
    .extensionName = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_EXTENSION_NAME,
    .specVersion = VK_STD_VULKAN_VIDEO_CODEC_AV1_ENCODE_SPEC_VERSION,
  },
};

const VkComponentMapping _vk_identity_component_map = {
    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
};
/* *INDENT-ON* */

gboolean
gst_vulkan_video_get_vk_functions (GstVulkanDevice * device,
    GstVulkanVideoFunctions * vk_funcs)
{
  gboolean ret = FALSE;
  GstVulkanInstance *instance;

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);
  g_return_val_if_fail (vk_funcs, FALSE);

  instance = gst_vulkan_device_get_instance (device);

#define GET_PROC_ADDRESS_REQUIRED(name, type)                           \
  G_STMT_START {                                                        \
    const char *fname = "vk" G_STRINGIFY (name) "KHR";                  \
    vk_funcs->G_PASTE (, name) = G_PASTE(G_PASTE(gst_vulkan_, type), _get_proc_address) (type, fname); \
    if (!vk_funcs->G_PASTE(, name)) {                                   \
      GST_ERROR_OBJECT (device, "Failed to find required function %s", fname); \
      goto bail;                                                        \
    }                                                                   \
  } G_STMT_END;
#define GET_DEVICE_PROC_ADDRESS_REQUIRED(name) GET_PROC_ADDRESS_REQUIRED(name, device)
#define GET_INSTANCE_PROC_ADDRESS_REQUIRED(name) GET_PROC_ADDRESS_REQUIRED(name, instance)
  GST_VULKAN_DEVICE_VIDEO_FN_LIST (GET_DEVICE_PROC_ADDRESS_REQUIRED);
  GST_VULKAN_INSTANCE_VIDEO_FN_LIST (GET_INSTANCE_PROC_ADDRESS_REQUIRED);
#undef GET_DEVICE_PROC_ADDRESS_REQUIRED
#undef GET_INSTANCE_PROC_ADDRESS_REQUIRED
#undef GET_PROC_ADDRESS_REQUIRED
  ret = TRUE;

bail:
  gst_object_unref (instance);
  return ret;
}

static void
gst_vulkan_handle_free_video_session (GstVulkanHandle * handle, gpointer data)
{
  PFN_vkDestroyVideoSessionKHR vkDestroyVideoSession;

  g_return_if_fail (handle != NULL);
  g_return_if_fail (handle->handle != VK_NULL_HANDLE);
  g_return_if_fail (handle->type == GST_VULKAN_HANDLE_TYPE_VIDEO_SESSION);
  g_return_if_fail (handle->user_data);

  vkDestroyVideoSession = handle->user_data;
  vkDestroyVideoSession (handle->device->device,
      (VkVideoSessionKHR) handle->handle, NULL);
}

gboolean
gst_vulkan_video_session_create (GstVulkanVideoSession * session,
    GstVulkanDevice * device, GstVulkanVideoFunctions * vk,
    VkVideoSessionCreateInfoKHR * session_create, GError ** error)
{
  VkVideoSessionKHR vk_session;
  VkMemoryRequirements2 *mem_req = NULL;
  VkVideoSessionMemoryRequirementsKHR *mem = NULL;
  VkBindVideoSessionMemoryInfoKHR *bind_mem = NULL;
  VkResult res;
  guint32 i, n_mems;
  gboolean ret = FALSE;

  g_return_val_if_fail (session && !session->session, FALSE);
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);
  g_return_val_if_fail (vk, FALSE);
  g_return_val_if_fail (session_create, FALSE);

  if (gst_vulkan_physical_device_has_feature_video_maintenance1
      (device->physical_device)) {
    session_create->flags |= VK_VIDEO_SESSION_CREATE_INLINE_QUERIES_BIT_KHR;
  }

  res = vk->CreateVideoSession (device->device, session_create, NULL,
      &vk_session);
  if (gst_vulkan_error_to_g_error (res, error, "vkCreateVideoSessionKHR")
      != VK_SUCCESS)
    return FALSE;

  session->session = gst_vulkan_handle_new_wrapped (device,
      GST_VULKAN_HANDLE_TYPE_VIDEO_SESSION, (GstVulkanHandleTypedef) vk_session,
      gst_vulkan_handle_free_video_session, vk->DestroyVideoSession);

  res = vk->GetVideoSessionMemoryRequirements (device->device, vk_session,
      &n_mems, NULL);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkGetVideoSessionMemoryRequirementsKHR") != VK_SUCCESS)
    goto beach;

  session->buffer = gst_buffer_new ();
  mem_req = g_new (VkMemoryRequirements2, n_mems);
  mem = g_new (VkVideoSessionMemoryRequirementsKHR, n_mems);
  bind_mem = g_new (VkBindVideoSessionMemoryInfoKHR, n_mems);

  for (i = 0; i < n_mems; i++) {
    /* *INDENT-OFF* */
    mem_req[i] = (VkMemoryRequirements2) {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    };
    mem[i] = (VkVideoSessionMemoryRequirementsKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR,
      .memoryRequirements = mem_req[i].memoryRequirements,
    };
    /* *INDENT-ON* */
  }
  res = vk->GetVideoSessionMemoryRequirements (device->device, vk_session,
      &n_mems, mem);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkGetVideoSessionMemoryRequirementsKHR") != VK_SUCCESS)
    goto beach;

  for (i = 0; i < n_mems; i++) {
    GstMemory *vk_mem;
    VkPhysicalDeviceMemoryProperties *props;
    VkMemoryPropertyFlags prop_flags;
    guint index;

    if (!gst_vulkan_memory_find_memory_type_index_with_requirements (device,
            &mem[i].memoryRequirements, G_MAXUINT32, &index)) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "Cannot find memory type for video session");
      goto beach;
    }

    props = &device->physical_device->memory_properties;
    prop_flags = props->memoryTypes[index].propertyFlags;

    vk_mem = gst_vulkan_memory_alloc (device, index, NULL,
        mem[i].memoryRequirements.size, prop_flags);
    if (!vk_mem) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "Cannot allocate memory for video session");
      goto beach;
    }
    gst_buffer_append_memory (session->buffer, vk_mem);

    /* *INDENT-OFF* */
    bind_mem[i] = (VkBindVideoSessionMemoryInfoKHR) {
      .sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR,
      .memory = ((GstVulkanMemory *) vk_mem)->mem_ptr,
      .memoryBindIndex = mem[i].memoryBindIndex,
      .memorySize = vk_mem->size,
    };
    /* *INDENT-ON* */
  }

  res =
      vk->BindVideoSessionMemory (device->device, vk_session, n_mems, bind_mem);
  if (gst_vulkan_error_to_g_error (res, error, "vkBindVideoSessionMemoryKHR")
      != VK_SUCCESS)
    goto beach;

  ret = TRUE;

beach:
  g_free (mem_req);
  g_free (mem);
  g_free (bind_mem);

  return ret;
}

void
gst_vulkan_video_session_destroy (GstVulkanVideoSession * session)
{
  g_return_if_fail (session);

  gst_clear_vulkan_handle (&session->session);
  gst_clear_buffer (&session->buffer);
}

GstBuffer *
gst_vulkan_video_codec_buffer_new (GstVulkanDevice * device,
    const GstVulkanVideoProfile * profile, VkBufferUsageFlags usage, gsize size)
{
  GstMemory *mem;
  GstBuffer *buf;
  VkVideoProfileListInfoKHR profile_list = {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
    .profileCount = 1,
    .pProfiles = &profile->profile,
  };
  VkBufferCreateInfo buf_info = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = &profile_list,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .size = size,
  };

  buf_info.size = MAX (buf_info.size, 1024 * 1024);

  mem = gst_vulkan_buffer_memory_alloc_with_buffer_info (device, &buf_info,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

  if (!mem)
    return NULL;

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);
  return buf;
}

/**
 * gst_vulkan_video_image_create_view:
 * @buf: a #GstBuffer
 * @layered_dpb: if DPB is layered
 * @is_out: if @buf is for output or for DPB
 * @sampler: (optional): sampler #GstVulkanHandle
 *
 * Creates a #GstVulkanImageView for @buf for decoding, with the internal Ycbcr
 * sampler, if available.
 *
 * Returns: (transfer full) (nullable): the #GstVulkanImageView.
 */
GstVulkanImageView *
gst_vulkan_video_image_create_view (GstBuffer * buf, gboolean layered_dpb,
    gboolean is_out, GstVulkanHandle * sampler)
{
  VkSamplerYcbcrConversionInfo yuv_sampler_info;
  VkImageViewCreateInfo view_create_info;
  GstVulkanImageMemory *vkmem;
  GstMemory *mem;
  gpointer pnext;
  guint n_mems;

  g_return_val_if_fail (GST_IS_BUFFER (buf), NULL);

  n_mems = gst_buffer_n_memory (buf);
  if (n_mems != 1)
    return NULL;

  mem = gst_buffer_peek_memory (buf, 0);
  if (!gst_is_vulkan_image_memory (mem))
    return NULL;

  pnext = NULL;
  if (sampler
      && sampler->type == GST_VULKAN_HANDLE_TYPE_SAMPLER_YCBCR_CONVERSION) {
    yuv_sampler_info = (VkSamplerYcbcrConversionInfo) {
      /* *INDENT-OFF* */
      .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
      .conversion = sampler->handle,
      /* *INDENT-ON* */
    };

    pnext = &yuv_sampler_info;
  }

  vkmem = (GstVulkanImageMemory *) mem;

  /* *INDENT-OFF* */
  view_create_info = (VkImageViewCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = pnext,
    .viewType = layered_dpb && !is_out ?
        VK_IMAGE_VIEW_TYPE_2D_ARRAY: VK_IMAGE_VIEW_TYPE_2D,
    .format = vkmem->create_info.format,
    .image = vkmem->image,
    .components = _vk_identity_component_map,
    .subresourceRange = (VkImageSubresourceRange) {
      .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseArrayLayer = 0,
      .layerCount     = layered_dpb && !is_out ?
          VK_REMAINING_ARRAY_LAYERS : 1,
      .levelCount     = 1,
    },
  };
  /* *INDENT-ON* */

  return gst_vulkan_get_or_create_image_view_with_info (vkmem,
      &view_create_info);
}

/**
 * gst_vulkan_video_try_configuration:
 * @device: a #GstVulkanPhysicalDevice
 * @profile: the #GstVulkanVideoProfile to configure
 * @out_vkcaps: (out caller-allocates): the capabilities given @profile
 * @out_caps: (out) (optional) (transfer full): the codec #GstCaps given
 *   @profile
 * @out_formats: (out) (optional) (transfer full): a #GArray with all possible
 *   raw video formats
 * @error: (out) (optional) (transfer full): the resulting error
 *
 * This function will try @profile, as a configuration in @device, by getting
 * its Vulkan capabilities and the output formats that @profile can produce by
 * the driver.
 *
 * If the capabilities are fetched correctly, then @out_caps is generated. If
 * the output formats are fetched correctly, then @out_formats is generated.
 *
 * Return: whether @profile configuration is possible in @device
 */
gboolean
gst_vulkan_video_try_configuration (GstVulkanPhysicalDevice * device,
    GstVulkanVideoProfile * profile, GstVulkanVideoCapabilities * out_vkcaps,
    GstCaps ** out_caps, GArray ** out_formats, GError ** error)
{
  VkVideoCodecOperationFlagBitsKHR codec_op;
  VkImageUsageFlags image_usage;
  GstVulkanVideoCapabilities vkcaps = {
    .caps = {.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,},
  };
  GArray *fmts;
  gboolean decode, encode;

  g_return_val_if_fail (GST_IS_VULKAN_PHYSICAL_DEVICE (device), FALSE);
  g_return_val_if_fail (profile && profile->profile.videoCodecOperation, FALSE);

  codec_op = profile->profile.videoCodecOperation;

  /* VkVideoCodecOperationFlagBitsKHR distinguish decoding and encoding
   * operations by the bit position with the following masks */
  decode = GST_VULKAN_VIDEO_CODEC_OPERATION_IS_DECODE (codec_op);
  encode = GST_VULKAN_VIDEO_CODEC_OPERATION_IS_ENCODE (codec_op);
  g_assert (decode ^ encode);

  /* fill vkcaps & output format usage */
  if (decode) {
    gboolean dedicated_dpb;

    vkcaps.caps.pNext = &vkcaps.decoder;
    /* *INDENT-OFF* */
    vkcaps.decoder.caps = (VkVideoDecodeCapabilitiesKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR,
      .pNext = &vkcaps.decoder.codec,
    };
    /* *INDENT-ON* */

    dedicated_dpb = ((vkcaps.decoder.caps.flags &
            VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR) == 0);

    image_usage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!dedicated_dpb)
      image_usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
  } else if (encode) {
    vkcaps.caps.pNext = &vkcaps.encoder;
    /* *INDENT-OFF* */
    vkcaps.encoder.caps = (VkVideoEncodeCapabilitiesKHR) {
      .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR,
      .pNext = &vkcaps.encoder.codec,
    };
    /* *INDENT-ON* */

    image_usage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR
        | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;
  } else {
    g_assert_not_reached ();
  }

  switch (codec_op) {
    case VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.decoder.codec.h264 = (VkVideoDecodeH264CapabilitiesKHR) {
          .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    case VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.decoder.codec.h265 = (VkVideoDecodeH265CapabilitiesKHR) {
          .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    case VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.decoder.codec.vp9 = (VkVideoDecodeVP9CapabilitiesKHR) {
          .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_VP9_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.encoder.codec.h264 = (VkVideoEncodeH264CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    case VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.decoder.codec.av1 = (VkVideoDecodeAV1CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_AV1_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.encoder.codec.h265 = (VkVideoEncodeH265CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR:
      /* *INDENT-OFF* */
      vkcaps.encoder.codec.av1 = (VkVideoEncodeAV1CapabilitiesKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR,
      };
      /* *INDENT-ON* */
      break;
    default:
      g_assert_not_reached ();
  }

  if (!gst_vulkan_physical_device_get_video_capabilities (device,
          &profile->profile, &vkcaps.caps, error))
    return FALSE;

  fmts =
      gst_vulkan_physical_device_get_video_formats (device, image_usage,
      &profile->profile, error);
  if (!fmts || (error && *error)) {
    g_clear_pointer (&fmts, g_array_unref);
    return FALSE;
  }

  if (out_vkcaps) {
    *out_vkcaps = vkcaps;
    out_vkcaps->caps.pNext = NULL;
  }

  if (out_formats)
    *out_formats = fmts;
  else
    g_array_unref (fmts);

  if (out_caps)
    *out_caps = gst_vulkan_video_profile_to_caps (profile);

  return TRUE;
}

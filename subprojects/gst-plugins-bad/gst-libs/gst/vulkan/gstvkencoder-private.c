/*
 * GStreamer
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvkencoder-private.h"

#include "gstvkvideo-private.h"

extern const VkExtensionProperties vk_codec_extensions[3];
extern const uint32_t _vk_codec_supported_extensions[4];

typedef struct _GstVulkanEncoderPrivate GstVulkanEncoderPrivate;

struct _GstVulkanEncoderPrivate
{
  GstVulkanHandle *session_params;

  GstCaps *profile_caps;

  GstVulkanOperation *exec;

  GstVulkanVideoSession session;
  GstVulkanVideoCapabilities caps;
  VkVideoFormatPropertiesKHR format;

  GstVulkanVideoProfile profile;

  gboolean vk_loaded;
  GstVulkanVideoFunctions vk;

  GstVulkanEncoderPicture *slots[32];

  gboolean started;
  gboolean session_reset;

  gboolean layered_dpb;
  GstBufferPool *dpb_pool;
  GstBuffer *layered_buffer;
  GstVulkanImageView *layered_view;
};

/**
 * SECTION:vkencoder
 * @title: GstVulkanEncoder
 * @short_description: Generic Vulkan Video Encoder
 */

#define GST_CAT_DEFAULT gst_vulkan_encoder_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define gst_vulkan_encoder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanEncoder, gst_vulkan_encoder,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstVulkanEncoder)
    GST_DEBUG_CATEGORY_INIT (gst_vulkan_encoder_debug,
        "vulkanencoder", 0, "Vulkan device encoder"));

const uint32_t _vk_codec_supported_extensions[] = {
  [GST_VK_VIDEO_EXTENSION_ENCODE_H264] = VK_MAKE_VIDEO_STD_VERSION (0, 9, 11),
  [GST_VK_VIDEO_EXTENSION_ENCODE_H265] = VK_MAKE_VIDEO_STD_VERSION (0, 9, 12),
};

static gboolean
_populate_function_table (GstVulkanEncoder * self)
{
  GstVulkanEncoderPrivate *priv =
      gst_vulkan_encoder_get_instance_private (self);
  GstVulkanInstance *instance;

  if (priv->vk_loaded)
    return TRUE;

  instance = gst_vulkan_device_get_instance (self->queue->device);
  if (!instance) {
    GST_ERROR_OBJECT (self, "Failed to get instance from the device");
    return FALSE;
  }

  priv->vk_loaded = gst_vulkan_video_get_vk_functions (instance, &priv->vk);
  gst_object_unref (instance);
  return priv->vk_loaded;
}

static void
gst_vulkan_encoder_finalize (GObject * object)
{
  GstVulkanEncoder *self = GST_VULKAN_ENCODER (object);

  gst_clear_object (&self->queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_encoder_init (GstVulkanEncoder * self)
{
}

static void
gst_vulkan_encoder_class_init (GstVulkanEncoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_vulkan_encoder_finalize;
}

static VkFormat
gst_vulkan_video_encoder_get_format (GstVulkanEncoder * self,
    VkImageUsageFlagBits imageUsage, GError ** error)
{
  VkResult res;
  VkVideoFormatPropertiesKHR *fmts = NULL;
  guint i, n_fmts;
  VkPhysicalDevice gpu =
      gst_vulkan_device_get_physical_device (self->queue->device);
  GstVulkanEncoderPrivate *priv =
      gst_vulkan_encoder_get_instance_private (self);
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  VkFormat vk_format = VK_FORMAT_UNDEFINED;
  VkVideoProfileListInfoKHR profile_list = {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
    .profileCount = 1,
    .pProfiles = &priv->profile.profile,
  };
  VkPhysicalDeviceVideoFormatInfoKHR fmt_info = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
    .pNext = &profile_list,
    .imageUsage = imageUsage,
  };

  res = priv->vk.GetPhysicalDeviceVideoFormatProperties (gpu, &fmt_info,
      &n_fmts, NULL);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkGetPhysicalDeviceVideoFormatPropertiesKHR") != VK_SUCCESS)
    goto beach;

  if (n_fmts == 0) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Profile doesn't have an output format");
    return vk_format;
  }

  fmts = g_new0 (VkVideoFormatPropertiesKHR, n_fmts);
  for (i = 0; i < n_fmts; i++)
    fmts[i].sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;

  res = priv->vk.GetPhysicalDeviceVideoFormatProperties (gpu, &fmt_info,
      &n_fmts, fmts);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkGetPhysicalDeviceVideoFormatPropertiesKHR") != VK_SUCCESS) {
    goto beach;
  }

  if (n_fmts == 0) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Profile doesn't have an output format");
    goto beach;
  }

  /* find the best output format */
  for (i = 0; i < n_fmts; i++) {
    format = gst_vulkan_format_to_video_format (fmts[i].format);
    if (format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_WARNING_OBJECT (self, "Unknown Vulkan format %i", fmts[i].format);
      continue;
    } else {
      vk_format = fmts[i].format;
      priv->format = fmts[i];
      break;
    }
  }

  if (vk_format == VK_FORMAT_UNDEFINED) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "No valid output format found");
  }

beach:
  g_clear_pointer (&fmts, g_free);
  return vk_format;
}

static void
gst_vulkan_handle_free_video_session_parameters (GstVulkanHandle * handle,
    gpointer data)
{
  PFN_vkDestroyVideoSessionParametersKHR vkDestroyVideoSessionParameters;

  g_return_if_fail (handle != NULL);
  g_return_if_fail (handle->handle != VK_NULL_HANDLE);
  g_return_if_fail (handle->type ==
      GST_VULKAN_HANDLE_TYPE_VIDEO_SESSION_PARAMETERS);
  g_return_if_fail (handle->user_data);

  vkDestroyVideoSessionParameters = handle->user_data;
  vkDestroyVideoSessionParameters (handle->device->device,
      (VkVideoSessionKHR) handle->handle, NULL);
}

static GstVulkanHandle *
gst_vulkan_encoder_new_video_session_parameters (GstVulkanEncoder * self,
    GstVulkanEncoderParameters * params, GError ** error)
{
  GstVulkanEncoderPrivate *priv;
  VkVideoSessionParametersCreateInfoKHR session_params_info;
  VkResult res;
  VkVideoSessionParametersKHR session_params;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), NULL);
  g_return_val_if_fail (params != NULL, NULL);

  priv = gst_vulkan_encoder_get_instance_private (self);

  if (!priv->session.session)
    return NULL;

  /* *INDENT-OFF* */
  session_params_info = (VkVideoSessionParametersCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR,
    .pNext = params,
    .videoSession = priv->session.session->handle,
  };
  /* *INDENT-ON* */

  res = priv->vk.CreateVideoSessionParameters (self->queue->device->device,
      &session_params_info, NULL, &session_params);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkCreateVideoSessionParametersKHR") != VK_SUCCESS)
    return NULL;

  return gst_vulkan_handle_new_wrapped (self->queue->device,
      GST_VULKAN_HANDLE_TYPE_VIDEO_SESSION_PARAMETERS,
      (GstVulkanHandleTypedef) session_params,
      gst_vulkan_handle_free_video_session_parameters,
      priv->vk.DestroyVideoSessionParameters);
}

/**
 * gst_vulkan_encode_picture_init:
 * @pic: the #GstVulkanEncoderPicture to initialize
 * @self: the #GstVulkanEncoder with the pool's configuration.
 * @in_buffer: (transfer none): the input #GstBuffer.
 * @size: size of the output buffer
 *
 * Initialize @pic structure.
 *
 * Returns: %TRUE if @pic was initialized correctly; otherwise %FALSE
 */
gboolean
gst_vulkan_encoder_picture_init (GstVulkanEncoderPicture * pic,
    GstVulkanEncoder * self, GstBuffer * in_buffer, gsize size)
{
  GstVulkanEncoderPrivate *priv;
  gsize size_aligned;

  g_return_val_if_fail (pic != NULL, FALSE);
  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (in_buffer), FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);

  size_aligned = GST_ROUND_UP_N (size,
      priv->caps.caps.minBitstreamBufferSizeAlignment);

  if (priv->layered_dpb) {
    g_assert (priv->layered_buffer);
    pic->dpb_buffer = gst_buffer_ref (priv->layered_buffer);
  } else {
    GstFlowReturn ret;

    g_assert (GST_IS_BUFFER_POOL (priv->dpb_pool));
    ret =
        gst_buffer_pool_acquire_buffer (priv->dpb_pool, &pic->dpb_buffer, NULL);
    if (ret != GST_FLOW_OK)
      return FALSE;
  }
  pic->in_buffer = gst_buffer_ref (in_buffer);
  pic->out_buffer =
      gst_vulkan_video_codec_buffer_new (self->queue->device, &priv->profile,
      VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR, size_aligned);
  if (!pic->out_buffer) {
    gst_clear_buffer (&pic->dpb_buffer);
    return FALSE;
  }
  pic->offset = 0;

  pic->img_view = gst_vulkan_video_image_create_view (pic->in_buffer,
      priv->layered_dpb, TRUE, NULL);

  if (priv->layered_dpb) {
    pic->dpb_view = gst_vulkan_image_view_ref (priv->layered_view);
  } else {
    pic->dpb_view = gst_vulkan_video_image_create_view (pic->dpb_buffer,
        priv->layered_dpb, FALSE, NULL);
  }

  return TRUE;
}

/**
 * gst_vulkan_encoder_picture_clear:
 * @pic: the #GstVulkanEncoderPicture to free.
 * @self: the #GstVulkanEncoder instance.
 *
 * Release data of @pic.
 */
void
gst_vulkan_encoder_picture_clear (GstVulkanEncoderPicture * pic,
    GstVulkanEncoder * self)
{
  GstVulkanEncoderPrivate *priv;

  g_return_if_fail (GST_IS_VULKAN_ENCODER (self));
  g_return_if_fail (pic != NULL);

  priv = gst_vulkan_encoder_get_instance_private (self);

  if (pic->dpb_slot.slotIndex > 0) {
    priv->slots[pic->dpb_slot.slotIndex] = NULL;
    pic->dpb_slot.slotIndex = -1;
  }

  gst_clear_buffer (&pic->in_buffer);
  gst_clear_buffer (&pic->dpb_buffer);
  gst_clear_buffer (&pic->out_buffer);

  gst_vulkan_image_view_unref (pic->img_view);
  pic->img_view = NULL;

  gst_vulkan_image_view_unref (pic->dpb_view);
  pic->dpb_view = NULL;
}

/**
 * gst_vulkan_encoder_caps:
 * @self: a #GstVulkanEncoder
 * @caps: (out): a #GstVulkanVideoCapabilities
 *
 * Get the #GstVulkanVideoCapabilities of the encoder if available
 *
 * Returns: whether the encoder has vulkan encoder caps.
 *
 */
gboolean
gst_vulkan_encoder_caps (GstVulkanEncoder * self,
    GstVulkanVideoCapabilities * caps)
{
  GstVulkanEncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);

  if (!priv->started)
    return FALSE;

  if (caps) {
    *caps = priv->caps;
    caps->caps.pNext = &caps->encoder.caps;
    caps->encoder.caps.pNext = &caps->encoder.codec;
  }

  return TRUE;
}

/**
 * gst_vulkan_encoder_profile_caps:
 * @self: a #GstVulkanEncoder
 *
 * Get the #GstCaps according to the encoder video profile
 *
 * Returns: (transfer full): #GstCaps of the profile defined at gst_vulkan_encoder_start()
 *
 */
GstCaps *
gst_vulkan_encoder_profile_caps (GstVulkanEncoder * self)
{
  GstVulkanEncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), NULL);

  priv = gst_vulkan_encoder_get_instance_private (self);

  if (!priv->started)
    return NULL;

  return gst_caps_ref (priv->profile_caps);
}

/**
 * gst_vulkan_encoder_stop:
 * @self: a #GstVulkanEncoder
 *
 * Stop the encoder.
 *
 * Returns: whether the encoder stopped correctly.
 *
 */
gboolean
gst_vulkan_encoder_stop (GstVulkanEncoder * self)
{
  GstVulkanEncoderPrivate *priv;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);
  if (!priv->started)
    return TRUE;

  gst_vulkan_video_session_destroy (&priv->session);

  gst_clear_caps (&priv->profile_caps);

  gst_clear_vulkan_handle (&priv->session_params);

  if (priv->layered_view)
    gst_vulkan_image_view_unref (priv->layered_view);
  gst_clear_buffer (&priv->layered_buffer);
  gst_clear_object (&priv->dpb_pool);

  gst_clear_object (&priv->exec);

  priv->started = FALSE;

  return TRUE;
}

/**
 * gst_vulkan_encoder_start:
 * @self: a #GstVulkanEncoder
 * @profile: a #GstVulkanVideoProfile
 * @error: (out) : an error result in case of failure or %NULL
 *
 * Start the encoding session according to a valid Vulkan profile
 *
 * Returns: whether the encoder started correctly.
 *
 */
gboolean
gst_vulkan_encoder_start (GstVulkanEncoder * self,
    GstVulkanVideoProfile * profile, GError ** error)
{
  GstVulkanEncoderPrivate *priv;
  VkResult res;
  VkVideoSessionCreateInfoKHR session_create;
  VkPhysicalDevice gpu;
  VkFormat pic_format = VK_FORMAT_UNDEFINED;
  int codec_idx;
  GstVulkanCommandPool *cmd_pool;
  VkQueryPoolVideoEncodeFeedbackCreateInfoKHR query_create;
  GError *query_err = NULL;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);
  g_return_val_if_fail (profile != NULL, FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);

  if (priv->started)
    return TRUE;

  if (!_populate_function_table (self)) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Couldn't load Vulkan Video functions");
    return FALSE;
  }

  switch (self->codec) {
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      if (!gst_vulkan_video_profile_is_valid (profile, self->codec)) {
        g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
            "Invalid profile");
        return FALSE;
      }
      priv->caps.encoder.codec.h264 = (VkVideoEncodeH264CapabilitiesKHR) {
        /* *INDENT-OFF* */
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR,
        /* *INDENT-ON* */
      };
      codec_idx = GST_VK_VIDEO_EXTENSION_ENCODE_H264;
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      if (!gst_vulkan_video_profile_is_valid (profile, self->codec)) {
        g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
            "Invalid profile");
        return FALSE;
      }
      priv->caps.encoder.codec.h265 = (VkVideoEncodeH265CapabilitiesKHR) {
        /* *INDENT-OFF* */
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_KHR,
        /* *INDENT-ON* */
      };
      codec_idx = GST_VK_VIDEO_EXTENSION_ENCODE_H265;
      break;
    default:
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "Invalid codec");
      return FALSE;
  }

  priv->profile = *profile;

  /* ensure the chain up of structure */
  priv->profile.usage.encode.pNext = &priv->profile.codec;
  priv->profile.profile.pNext = &priv->profile.usage.encode;

  /* *INDENT-OFF* */
  priv->caps.encoder.caps = (VkVideoEncodeCapabilitiesKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR,
    .pNext = &priv->caps.encoder.codec,
  };
  priv->caps.caps = (VkVideoCapabilitiesKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
    .pNext = &priv->caps.encoder.caps,
  };
  /* *INDENT-ON* */

  gpu = gst_vulkan_device_get_physical_device (self->queue->device);
  res = priv->vk.GetPhysicalDeviceVideoCapabilities (gpu,
      &priv->profile.profile, &priv->caps.caps);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkGetPhysicalDeviceVideoCapabilitiesKHR") != VK_SUCCESS)
    return FALSE;

  if (_vk_codec_extensions[codec_idx].specVersion <
      _vk_codec_supported_extensions[codec_idx]) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "STD version headers [%i.%i.%i] not supported, need at least [%i.%i.%i], check your SDK path.",
        VK_CODEC_VERSION (_vk_codec_extensions[codec_idx].specVersion),
        VK_CODEC_VERSION (_vk_codec_supported_extensions[codec_idx]));
    return FALSE;
  }

  if (_vk_codec_extensions[codec_idx].specVersion <
      priv->caps.caps.stdHeaderVersion.specVersion) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "The driver needs a newer version [%i.%i.%i] of the current headers %d.%d.%d, please update the code to support this driver.",
        VK_CODEC_VERSION (priv->caps.caps.stdHeaderVersion.specVersion),
        VK_CODEC_VERSION (_vk_codec_extensions[codec_idx].specVersion));
    return FALSE;
  }

  /* Get output format */
  pic_format = gst_vulkan_video_encoder_get_format (self,
      VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR |
      VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR, error);
  if (pic_format == VK_FORMAT_UNDEFINED)
    return FALSE;

  cmd_pool = gst_vulkan_queue_create_command_pool (self->queue, error);
  if (!cmd_pool)
    return FALSE;
  priv->exec = gst_vulkan_operation_new (cmd_pool);
  gst_object_unref (cmd_pool);

  /* we don't want overridden parameters in queries */
  /* *INDENT-OFF* */
  query_create = (VkQueryPoolVideoEncodeFeedbackCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR,
    .pNext = &profile->profile,
    .encodeFeedbackFlags = priv->caps.encoder.caps.supportedEncodeFeedbackFlags &
        (~VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_HAS_OVERRIDES_BIT_KHR),
  };
  /* *INDENT-ON* */

  if (!gst_vulkan_operation_enable_query (priv->exec,
          VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR, 1, &query_create,
          &query_err)) {
    if (query_err->code != VK_ERROR_FEATURE_NOT_PRESENT) {
      g_propagate_error (error, query_err);
      goto failed;
    }
    g_clear_error (&query_err);
  }

  priv->profile_caps = gst_vulkan_video_profile_to_caps (&priv->profile);

  GST_LOG_OBJECT (self, "Encoder capabilities for %" GST_PTR_FORMAT ":\n"
      "    Codec header version: %i.%i.%i (driver), %i.%i.%i (compiled)\n"
      "    Width from %i to %i\n"
      "    Height from %i to %i\n"
      "    Width granularity: %i\n"
      "    Height granularity: %i\n"
      "    Bitstream offset alignment: %" G_GUINT64_FORMAT "\n"
      "    Bitstream size alignment: %" G_GUINT64_FORMAT "\n"
      "    Maximum reference slots: %u\n"
      "    Maximum active references: %u\n"
      "    encode maximum bitrate: %" G_GUINT64_FORMAT "\n"
      "    encode quality levels: %i\n"
      "    encode image width granularity: %i\n"
      "    encode image height granularity: %i\n"
      "    encode pool feedback bitstream:%s%s%s%s\n"
      "    encode rate-control modes:%s%s\n"
      "    Capability flags:%s%s%s\n",
      priv->profile_caps,
      VK_CODEC_VERSION (priv->caps.caps.stdHeaderVersion.specVersion),
      VK_CODEC_VERSION (_vk_codec_extensions[codec_idx].specVersion),
      priv->caps.caps.minCodedExtent.width,
      priv->caps.caps.maxCodedExtent.width,
      priv->caps.caps.minCodedExtent.height,
      priv->caps.caps.maxCodedExtent.height,
      priv->caps.caps.pictureAccessGranularity.width,
      priv->caps.caps.pictureAccessGranularity.height,
      priv->caps.caps.minBitstreamBufferOffsetAlignment,
      priv->caps.caps.minBitstreamBufferSizeAlignment,
      priv->caps.caps.maxDpbSlots,
      priv->caps.caps.maxActiveReferencePictures,
      priv->caps.encoder.caps.maxBitrate,
      priv->caps.encoder.caps.maxQualityLevels,
      priv->caps.encoder.caps.encodeInputPictureGranularity.width,
      priv->caps.encoder.caps.encodeInputPictureGranularity.height,
      priv->caps.encoder.caps.supportedEncodeFeedbackFlags ? "" : " none",
      priv->caps.encoder.caps.supportedEncodeFeedbackFlags &
      VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR ?
      " buffer_offset" : "",
      priv->caps.encoder.caps.supportedEncodeFeedbackFlags &
      VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR ?
      " bytes_written" : "",
      priv->caps.encoder.caps.supportedEncodeFeedbackFlags &
      VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_HAS_OVERRIDES_BIT_KHR ?
      " has_overrides" : "",
      priv->caps.encoder.caps.rateControlModes &
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR ? " cbr" : "",
      priv->caps.encoder.caps.rateControlModes &
      VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR ? " vbr" : "",
      priv->caps.caps.flags ? "" : " none",
      priv->caps.caps.flags &
      VK_VIDEO_CAPABILITY_PROTECTED_CONTENT_BIT_KHR ? " protected" : "",
      priv->caps.caps.flags &
      VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR ?
      " separate_references" : "");

  priv->layered_dpb =
      !(priv->caps.
      caps.flags & VK_VIDEO_CAPABILITY_SEPARATE_REFERENCE_IMAGES_BIT_KHR);


  /* *INDENT-OFF* */
  session_create = (VkVideoSessionCreateInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR,
    .queueFamilyIndex = self->queue->family,
    .pVideoProfile = &profile->profile,
    .pictureFormat = pic_format,
    .maxCodedExtent = priv->caps.caps.maxCodedExtent,
    .referencePictureFormat = pic_format,
    .maxDpbSlots = priv->caps.caps.maxDpbSlots,
    .maxActiveReferencePictures = priv->caps.caps.maxActiveReferencePictures,
    .pStdHeaderVersion = &_vk_codec_extensions[codec_idx],
  };
  /* *INDENT-ON* */

  if (!gst_vulkan_video_session_create (&priv->session, self->queue->device,
          &priv->vk, &session_create, error))
    goto failed;

  priv->session_reset = TRUE;
  priv->started = TRUE;

  return TRUE;

failed:
  gst_clear_object (&priv->exec);
  gst_clear_caps (&priv->profile_caps);
  return FALSE;
}

/**
 * gst_vulkan_encoder_update_video_session_parameters:
 * @self: a #GstVulkanEncoder
 * @params: a #GstVulkanEncoderParameters
 * @error: (out) (optional): an error result in case of failure
 *
 * Set the sessions parameters to be used by the encoder
 *
 * Returns: whether the encoder updated the session parameters correctly.
 *
 */
gboolean
gst_vulkan_encoder_update_video_session_parameters (GstVulkanEncoder * self,
    GstVulkanEncoderParameters * params, GError ** error)
{
  GstVulkanEncoderPrivate *priv;
  GstVulkanHandle *handle;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);
  g_return_val_if_fail (params != NULL, FALSE);

  handle =
      gst_vulkan_encoder_new_video_session_parameters (self, params, error);
  if (!handle)
    return FALSE;

  priv = gst_vulkan_encoder_get_instance_private (self);

  gst_clear_vulkan_handle (&priv->session_params);
  priv->session_params = handle;

  return TRUE;
}

/**
 * gst_vulkan_encoder_video_session_parameters_overrides:
 * @self: a #GstVulkanEncoder
 * @params: a #GstVulkanEncoderParametersOverrides
 * @feedback: (out) (optional): #GstVulkanEncoderParametersFeedback or %NULL
 * @data_size: (out) (optional): the allocated size of @data
 * @data: (out) (optional): location to store the requested overrides, use
 *     g_free() to free after use
 *
 * 42.15.1. Codec-Specific Semantics
 *
 * Implementations supporting video encode operations for any particular video
 * codec operation often support only a subset of the available encoding tools
 * defined by the corresponding video compression standards.
 *
 * … this specification allows implementations to override the value of any of
 * the codec-specific parameters,
 *
 * Returns: whether the encoder has bew sessions parameters.
 *
 */
gboolean
gst_vulkan_encoder_video_session_parameters_overrides (GstVulkanEncoder * self,
    GstVulkanEncoderParametersOverrides * params,
    GstVulkanEncoderParametersFeedback * feedback, gsize * data_size,
    gpointer * data, GError ** error)
{
  VkVideoEncodeSessionParametersGetInfoKHR video_params_info;
  VkVideoEncodeSessionParametersFeedbackInfoKHR feedback_info;
  VkResult res;
  GstVulkanEncoderPrivate *priv;
  gsize size;
  gpointer param_data;
  gboolean write;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);
  g_return_val_if_fail (params != NULL && feedback != NULL, FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);
  if (!priv->started)
    return FALSE;

  switch (self->codec) {
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      if (params->h264.sType !=
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR) {
        gst_vulkan_error_to_g_error (GST_VULKAN_ERROR, error,
            "Invalid parameter for H.264");
        return FALSE;
      }
      write = params->h264.writeStdPPS || params->h264.writeStdSPS;
      if (feedback) {
        feedback->h264.sType =
            VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_FEEDBACK_INFO_KHR;
      }
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      if (params->h265.sType !=
          VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_GET_INFO_KHR) {
        gst_vulkan_error_to_g_error (GST_VULKAN_ERROR, error,
            "Invalid parameter for H.265");
        return FALSE;
      }
      write = params->h265.writeStdPPS || params->h265.writeStdSPS
          || params->h265.writeStdVPS;
      if (feedback) {
        feedback->h265.sType =
            VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_FEEDBACK_INFO_KHR;
      }
      break;
    default:
      return FALSE;
  }

  /* *INDENT-OFF* */
  video_params_info = (VkVideoEncodeSessionParametersGetInfoKHR) {
	  .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR,
		.pNext = params,
		.videoSessionParameters = priv->session_params->handle,
  };

  feedback_info = (VkVideoEncodeSessionParametersFeedbackInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR,
    .pNext = feedback,
  };
  /* *INDENT-ON* */

  res = priv->vk.GetEncodedVideoSessionParameters (self->queue->device->device,
      &video_params_info, &feedback_info, &size, NULL);
  if (gst_vulkan_error_to_g_error (res, error,
          "vGetEncodedVideoSessionParametersKHR") != VK_SUCCESS)
    return FALSE;

  /* FIXME: forcing because a bug in NVIDIA driver */
  feedback_info.hasOverrides = 1;
  if (!feedback_info.hasOverrides || !data || !write)
    return TRUE;

  GST_DEBUG_OBJECT (self, "allocating for bitstream parameters %"
      G_GSIZE_FORMAT, size);
  param_data = g_malloc (size);

  res = priv->vk.GetEncodedVideoSessionParameters (self->queue->device->device,
      &video_params_info, &feedback_info, &size, param_data);
  if (gst_vulkan_error_to_g_error (res, error,
          "vGetEncodedVideoSessionParametersKHR") != VK_SUCCESS)
    return FALSE;

  if (data_size)
    *data_size = size;
  *data = param_data;

  return TRUE;
}

/**
 * gst_vulkan_encoder_create_dpb_pool:
 * @self: a #GstVulkanEncoder
 * @caps: the #GstCaps of the DPB pool
 *
 * Instantiates an internal Vulkan image pool for driver encoders whose output
 * buffers can be used as DPB buffers.
 *
 * Returns: whether the pool was created.
 */
gboolean
gst_vulkan_encoder_create_dpb_pool (GstVulkanEncoder * self, GstCaps * caps)
{
  GstVulkanEncoderPrivate *priv;
  GstCaps *profile_caps;
  GstStructure *config;
  guint min_buffers, max_buffers;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);

  if (!priv->started)
    return FALSE;

  if ((!priv->layered_dpb && priv->dpb_pool)
      || (priv->layered_dpb && priv->layered_buffer))
    return TRUE;

  if (priv->layered_dpb) {
    min_buffers = max_buffers = 1;
  } else {
    min_buffers = priv->caps.caps.maxDpbSlots;
    max_buffers = 0;
  }

  priv->dpb_pool = gst_vulkan_image_buffer_pool_new (self->queue->device);

  config = gst_buffer_pool_get_config (priv->dpb_pool);
  gst_buffer_pool_config_set_params (config, caps, 1024, min_buffers,
      max_buffers);
  gst_vulkan_image_buffer_pool_config_set_allocation_params (config,
      VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR,
      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

  if (priv->layered_dpb) {
    gst_structure_set (config, "num-layers", G_TYPE_UINT,
        priv->caps.caps.maxDpbSlots, NULL);
  }
  profile_caps = gst_vulkan_encoder_profile_caps (self);
  gst_vulkan_image_buffer_pool_config_set_encode_caps (config, profile_caps);
  gst_caps_unref (profile_caps);

  if (!gst_buffer_pool_set_config (priv->dpb_pool, config))
    goto bail;
  if (!gst_buffer_pool_set_active (priv->dpb_pool, TRUE))
    goto bail;

  if (priv->layered_dpb) {
    ret = gst_buffer_pool_acquire_buffer (priv->dpb_pool, &priv->layered_buffer,
        NULL);
    if (ret != GST_FLOW_OK)
      goto bail;

    priv->layered_view =
        gst_vulkan_video_image_create_view (priv->layered_buffer,
        priv->layered_dpb, FALSE, NULL);

    gst_clear_object (&priv->dpb_pool);
  }

  return TRUE;

bail:
  gst_clear_object (&priv->dpb_pool);
  return FALSE;
}

/**
 * gst_vulkan_encoder_encode:
 * @self: a #GstVulkanEncoder
 * @info: the #GstVideoInfo of the @pic to process
 * @pic: a #GstVulkanEncoderPicture
 * @nb_refs: number of @ref_pics
 * @ref_pics: an array of #GstVulkanEncoderPicture
 *
 * Encode a picture according to its reference pictures.
 *
 * Returns: whether the encode process completed successfully.
 *
 */
gboolean
gst_vulkan_encoder_encode (GstVulkanEncoder * self, GstVideoInfo * info,
    GstVulkanEncoderPicture * pic, guint nb_refs,
    GstVulkanEncoderPicture ** ref_pics)
{
  GstVulkanEncoderPrivate *priv;
  GError *err = NULL;
  gboolean ret = TRUE;
  GstMemory *mem;
  int i, slot_index = -1;
  GstVulkanEncodeQueryResult *encode_res;
  VkVideoCodingControlInfoKHR coding_ctrl;
  VkVideoBeginCodingInfoKHR begin_coding;
  VkVideoEncodeInfoKHR encode_info;
  VkVideoEndCodingInfoKHR end_coding;
  VkVideoReferenceSlotInfoKHR ref_slots[37];
  GstVulkanCommandBuffer *cmd_buf;
  GArray *barriers;

  g_return_val_if_fail (GST_IS_VULKAN_ENCODER (self), FALSE);
  g_return_val_if_fail (info != NULL && pic != NULL, FALSE);

  priv = gst_vulkan_encoder_get_instance_private (self);

  /* initialize the vulkan operation */
  if (!gst_vulkan_operation_begin (priv->exec, &err))
    goto bail;

  /* *INDENT-OFF* */
  coding_ctrl = (VkVideoCodingControlInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR,
    .flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR,
  };
  /* *INDENT-ON* */

  /* some information such as rate_control must be initialized. */
  if (!priv->session_reset) {
    /* begin_coding.pNext = &rate_control_info; */
  }

  g_assert (pic->dpb_buffer && pic->dpb_view);
  g_assert (pic->in_buffer && pic->img_view);
  g_assert (pic->out_buffer);

  /* Attribute a free slot index to the picture to be used later as a reference.
   * The picture is kept until it remains useful to the encoding process.*/
  for (i = 0; i < priv->caps.caps.maxDpbSlots; i++) {
    if (!priv->slots[i]) {
      priv->slots[i] = pic;
      slot_index = i;
      break;
    }
  }

  /* Set the ref slots according to the pic refs to bound the video
     session encoding. It should contain all the references + 1 to book
     a new slotIndex (-1) for the current picture. */
  /* *INDENT-OFF* */
  pic->dpb = (VkVideoPictureResourceInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
    .pNext = NULL,
    .codedOffset = { 0, 0 },
    .codedExtent = {
      .width = GST_VIDEO_INFO_WIDTH (info),
      .height = GST_VIDEO_INFO_HEIGHT (info),
    },
    .baseArrayLayer = 0,
    .imageViewBinding = pic->dpb_view->view,
  };
  pic->dpb_slot = (VkVideoReferenceSlotInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR,
    .pNext = pic->codec_dpb_slot_info,
    .slotIndex = slot_index,
    .pPictureResource = &pic->dpb,
  };
  /* *INDENT-ON* */

  for (i = 0; i < nb_refs; i++)
    ref_slots[i] = ref_pics[i]->dpb_slot;

  ref_slots[nb_refs] = pic->dpb_slot;
  ref_slots[nb_refs].slotIndex = -1;

  /* Setup the begin coding structure using the reference slots */
  /* *INDENT-OFF* */
  begin_coding = (VkVideoBeginCodingInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR,
    .pNext = NULL,
    .videoSession = priv->session.session->handle,
    .videoSessionParameters = priv->session_params->handle,
    .referenceSlotCount = nb_refs + 1,
    .pReferenceSlots = ref_slots,
  };
  /* *INDENT-ON* */

  cmd_buf = priv->exec->cmd_buf;
  priv->vk.CmdBeginVideoCoding (cmd_buf->cmd, &begin_coding);

  /* 42.9. Video Coding Control
     To apply dynamic controls to the currently bound video session object such as
     quality information. This should be done when requesting a new coding contol ie
     first attempt of encoding.
   */
  if (priv->session_reset) {
    priv->vk.CmdControlVideoCoding (cmd_buf->cmd, &coding_ctrl);
    priv->session_reset = FALSE;
  }

  /* Peek the output memory to be used by VkVideoEncodeInfoKHR.dstBuffer */
  mem = gst_buffer_peek_memory (pic->out_buffer, 0);

  /* Setup the encode info */
  /* *INDENT-OFF* */
  encode_info = (VkVideoEncodeInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR,
    .pNext = pic->codec_pic_info,
    .flags = 0x0,
    .dstBuffer = ((GstVulkanBufferMemory *) mem)->buffer,
    .dstBufferOffset = pic->offset,
    .dstBufferRange = gst_memory_get_sizes (mem, NULL, NULL),
    .srcPictureResource = (VkVideoPictureResourceInfoKHR) {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR,
        .pNext = NULL,
        .codedOffset = { 0, 0 },
        .codedExtent = {
          .width = GST_VIDEO_INFO_WIDTH (info),
          .height = GST_VIDEO_INFO_HEIGHT (info),
        },
        .baseArrayLayer = 0,
        .imageViewBinding = pic->img_view->view,
    },
    .pSetupReferenceSlot = &pic->dpb_slot,
    .referenceSlotCount = nb_refs,
    .pReferenceSlots = ref_slots,
    .precedingExternallyEncodedBytes = 0,
  };
  /* *INDENT-ON* */

  encode_info.dstBufferRange -= encode_info.dstBufferOffset;
  encode_info.dstBufferRange = GST_ROUND_DOWN_N (encode_info.dstBufferRange,
      priv->caps.caps.minBitstreamBufferSizeAlignment);

  gst_vulkan_operation_add_dependency_frame (priv->exec, pic->in_buffer,
      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
  gst_vulkan_operation_add_frame_barrier (priv->exec, pic->in_buffer,
      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
      VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR,
      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR, NULL);

  gst_vulkan_operation_add_dependency_frame (priv->exec, pic->dpb_buffer,
      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR);
  gst_vulkan_operation_add_frame_barrier (priv->exec, pic->dpb_buffer,
      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
      VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR,
      VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR, NULL);

  barriers = gst_vulkan_operation_retrieve_image_barriers (priv->exec);

  /* *INDENT-OFF* */
  vkCmdPipelineBarrier2 (cmd_buf->cmd, &(VkDependencyInfo) {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
      .pImageMemoryBarriers = (VkImageMemoryBarrier2 *) barriers->data,
      .imageMemoryBarrierCount = barriers->len,
      }
  );
  /* *INDENT-ON* */
  g_array_unref (barriers);

  gst_vulkan_operation_begin_query (priv->exec, 0);
  priv->vk.CmdEncodeVideo (cmd_buf->cmd, &encode_info);
  gst_vulkan_operation_end_query (priv->exec, 0);

  /* *INDENT-OFF* */
  end_coding = (VkVideoEndCodingInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR,
  };
  /* *INDENT-ON* */

  /* 41.5 4. vkCmdEndVideoCodingKHR signals the end of the recording of the
   * Vulkan Video Context, as established by vkCmdBeginVideoCodingKHR. */
  priv->vk.CmdEndVideoCoding (cmd_buf->cmd, &end_coding);

  if (!gst_vulkan_operation_end (priv->exec, &err)) {
    GST_ERROR_OBJECT (self, "The operation did not complete properly");
    goto bail;
  }
  /* Wait the operation to complete or we might have a failing query */
  gst_vulkan_operation_wait (priv->exec);

  gst_vulkan_operation_get_query (priv->exec, (gpointer *) & encode_res, &err);
  if (encode_res->status == VK_QUERY_RESULT_STATUS_COMPLETE_KHR) {
    GST_INFO_OBJECT (self, "The frame %p has been encoded with size %"
        G_GUINT64_FORMAT, pic, encode_res->data_size + pic->offset);
    gst_buffer_set_size (pic->out_buffer, encode_res->data_size + pic->offset);
  } else {
    GST_ERROR_OBJECT (self,
        "The operation did not complete properly, query status = %d",
        encode_res->status);
    goto bail;
  }

  return ret;
bail:
  {
    return FALSE;
  }
}

/**
 * gst_vulkan_create_encoder_from_queue:
 * @queue: a #GstVulkanQueue
 * @codec: (type guint): the VkVideoCodecOperationFlagBitsKHR to encode
 *
 * Creates a #GstVulkanEncoder object if @codec encoding is supported by @queue
 *
 * Returns: (transfer full) (nullable): the #GstVulkanEncoder object
 *
 */
GstVulkanEncoder *
gst_vulkan_encoder_create_from_queue (GstVulkanQueue * queue, guint codec)
{
  GstVulkanPhysicalDevice *device;
  GstVulkanEncoder *encoder;
  guint flags, expected_flag, supported_video_ops;
  const char *extension;

  g_return_val_if_fail (GST_IS_VULKAN_QUEUE (queue), NULL);

  device = queue->device->physical_device;
  expected_flag = VK_QUEUE_VIDEO_ENCODE_BIT_KHR;
  flags = device->queue_family_props[queue->family].queueFlags;
  supported_video_ops = device->queue_family_ops[queue->family].video;

  if (device->properties.apiVersion < VK_MAKE_VERSION (1, 3, 275)) {
    GST_WARNING_OBJECT (queue,
        "API version %d.%d.%d doesn't support video encode extensions",
        VK_VERSION_MAJOR (device->properties.apiVersion),
        VK_VERSION_MINOR (device->properties.apiVersion),
        VK_VERSION_PATCH (device->properties.apiVersion));
    return NULL;
  }

  switch (codec) {
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR:
      extension = VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME;
      break;
    case VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR:
      extension = VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME;
      break;
    default:
      GST_WARNING_OBJECT (queue, "Unsupported codec");
      return NULL;
  }

  if ((flags & expected_flag) != expected_flag) {
    GST_WARNING_OBJECT (queue, "Queue doesn't support encoding");
    return NULL;
  }
  if ((supported_video_ops & codec) != codec) {
    GST_WARNING_OBJECT (queue, "Queue doesn't support codec encoding");
    return NULL;
  }

  if (!(gst_vulkan_device_is_extension_enabled (queue->device,
              VK_KHR_VIDEO_QUEUE_EXTENSION_NAME)
          && gst_vulkan_device_is_extension_enabled (queue->device,
              VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME)
          && gst_vulkan_device_is_extension_enabled (queue->device, extension)))
    return NULL;

  encoder = g_object_new (GST_TYPE_VULKAN_ENCODER, NULL);
  gst_object_ref_sink (encoder);
  encoder->queue = gst_object_ref (queue);
  encoder->codec = codec;

  return encoder;
}

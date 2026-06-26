/*
 * GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#include "vkupload-ahb.h"

#include <string.h>
#include "gstvkutils.h"

#if GST_VULKAN_UPLOAD_HAVE_AHB
#include <android/hardware_buffer.h>
#include <gst/allocators/gstahardwarebuffer.h>
#ifdef HAVE_GLSLC
#include "shaders/identity.vert.h"
#include "shaders/identity.frag.h"
#endif

GST_DEBUG_CATEGORY_EXTERN (gst_debug_vulkan_upload);
#define GST_CAT_DEFAULT gst_debug_vulkan_upload

void
gst_vulkan_upload_ahb_request_device_extensions (GstVulkanUpload * upload)
{
  GstContext *context;
  gboolean needs_vulkan_1_0_extensions;

  g_return_if_fail (upload->instance != NULL);

  needs_vulkan_1_0_extensions =
      !gst_vulkan_instance_check_api_version (upload->instance, 1, 1, 0);

  context = gst_vulkan_requested_device_extensions_context_new ();
  gst_vulkan_requested_extensions_context_set_vulkan_instance (context,
      upload->instance);
  gst_vulkan_requested_extensions_context_add (context,
      VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
  if (needs_vulkan_1_0_extensions) {
#if defined(VK_KHR_external_memory)
    gst_vulkan_requested_extensions_context_add (context,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
#endif
#if defined(VK_KHR_dedicated_allocation)
    gst_vulkan_requested_extensions_context_add (context,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
#endif
    gst_vulkan_requested_extensions_context_add (context,
        VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
  }
  gst_element_set_context (GST_ELEMENT (upload), context);
  gst_context_unref (context);
}

struct AhbImportData
{
  GstVulkanDevice *device;
  GstMemory *in_mem;
  VkImage image;
  VkDeviceMemory memory;
};

struct AhbYcbcrSamplerKey
{
  VkFormat format;
  guint64 external_format;
  VkSamplerYcbcrModelConversion ycbcr_model;
  VkSamplerYcbcrRange ycbcr_range;
  VkComponentMapping components;
  VkChromaLocation x_chroma_offset;
  VkChromaLocation y_chroma_offset;
  VkFilter chroma_filter;
  VkFilter reconstruction_filter;
  gboolean yv12_chroma_order;
};

struct AhbVertex
{
  gfloat x, y, z;
  gfloat s, t;
};

struct AhbUpload
{
  GstVulkanUpload *upload;

  GstVideoInfo in_info;
  GstVideoInfo out_info;
  GstVideoChromaSite chroma_site;
  guint32 ahb_format;

  GstVulkanDevice *cached_device;
  PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb_properties;

  GstVulkanFullScreenQuad *quad;
  GstVulkanHandle *simple_sampler;
  GstVulkanHandle *ycbcr_sampler;
  struct AhbYcbcrSamplerKey ycbcr_sampler_key;
  gboolean have_ycbcr_sampler_key;
  gboolean quad_info_set;
  GstVulkanHandle *vert;
  GstVulkanHandle *frag;
  VkImageUsageFlags output_usage;
  gboolean output_usage_known;
  gboolean have_quad_vertices;
#ifndef GST_DISABLE_GST_DEBUG
  gboolean have_logged_import_info;
#endif
  gfloat quad_u_max;
  gfloat quad_v_max;
};

static void
_ahb_import_data_free (struct AhbImportData *data)
{
  if (data->image)
    vkDestroyImage (data->device->device, data->image, NULL);
  if (data->memory)
    vkFreeMemory (data->device->device, data->memory, NULL);
  gst_memory_unref (data->in_mem);
  gst_clear_object (&data->device);
  g_free (data);
}

static void
_ahb_free_sampler (GstVulkanHandle * handle, gpointer user_data)
{
  gst_vulkan_handle_free_sampler (handle, NULL);
  gst_vulkan_handle_unref (user_data);
}

static gboolean
_ahb_query_properties (struct AhbUpload *ahb_upload, AHardwareBuffer * ahb,
    VkAndroidHardwareBufferPropertiesANDROID * props,
    VkAndroidHardwareBufferFormatPropertiesANDROID * format_props,
    GError ** error)
{
  GstVulkanDevice *device = ahb_upload->upload->device;
  VkResult res;

  if (!gst_vulkan_device_is_extension_enabled (device,
          VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME)) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_EXTENSION_NOT_PRESENT,
        "VK_ANDROID_external_memory_android_hardware_buffer is not enabled");
    return FALSE;
  }

  if (ahb_upload->cached_device != device) {
    gst_clear_object (&ahb_upload->cached_device);
    ahb_upload->cached_device = gst_object_ref (device);
    ahb_upload->get_ahb_properties = NULL;
    gst_clear_vulkan_handle (&ahb_upload->simple_sampler);
    gst_clear_vulkan_handle (&ahb_upload->ycbcr_sampler);
    ahb_upload->have_ycbcr_sampler_key = FALSE;
#ifndef GST_DISABLE_GST_DEBUG
    ahb_upload->have_logged_import_info = FALSE;
#endif
  }

  if (!ahb_upload->get_ahb_properties) {
    ahb_upload->get_ahb_properties =
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)
        gst_vulkan_device_get_proc_address (device,
        "vkGetAndroidHardwareBufferPropertiesANDROID");
  }

  if (!ahb_upload->get_ahb_properties) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_EXTENSION_NOT_PRESENT,
        "vkGetAndroidHardwareBufferPropertiesANDROID is unavailable");
    return FALSE;
  }

  *format_props = (VkAndroidHardwareBufferFormatPropertiesANDROID) {
  .sType =
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,};
  *props = (VkAndroidHardwareBufferPropertiesANDROID) {
  .sType =
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,.pNext =
        format_props,};

  res = ahb_upload->get_ahb_properties (device->device, ahb, props);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkGetAndroidHardwareBufferPropertiesANDROID") < 0)
    return FALSE;

  return TRUE;
}

static gboolean
_ahb_vk_format_is_renderable (VkFormat format)
{
  const GstVulkanFormatInfo *info = gst_vulkan_format_get_info (format);

  return info && (info->flags & GST_VULKAN_FORMAT_FLAG_RGB) &&
      (info->scaling == GST_VULKAN_FORMAT_SCALING_UNORM ||
      info->scaling == GST_VULKAN_FORMAT_SCALING_SRGB);
}

enum AhbFormatClass
{
  /* Numeric vendor format: allow caps provisionally, validate per buffer. */
  AHB_FORMAT_CLASS_PROVISIONAL_VENDOR,
  /* AHardwareBuffer format maps to a concrete renderable VkFormat. */
  AHB_FORMAT_CLASS_CONCRETE,
  /* AHardwareBuffer format must be imported through external-format YCbCr. */
  AHB_FORMAT_CLASS_EXTERNAL_YUV,
  /* Known format that this upload path cannot import or convert. */
  AHB_FORMAT_CLASS_UNSUPPORTED,
};

static gboolean
_ahb_format_to_vk_format (guint32 format, VkFormat * vk_format)
{
  /* These equivalences are documented alongside each format in Android's
   * hardware_buffer.h header. */
  switch (format) {
    case GST_AHARDWARE_BUFFER_FORMAT_R8G8B8A8_UNORM:
    case GST_AHARDWARE_BUFFER_FORMAT_R8G8B8X8_UNORM:
      *vk_format = VK_FORMAT_R8G8B8A8_UNORM;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R8G8B8_UNORM:
      *vk_format = VK_FORMAT_R8G8B8_UNORM;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R5G6B5_UNORM:
      *vk_format = VK_FORMAT_R5G6B5_UNORM_PACK16;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R16G16B16A16_FLOAT:
      *vk_format = VK_FORMAT_R16G16B16A16_SFLOAT;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R10G10B10A2_UNORM:
      *vk_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R8_UNORM:
      *vk_format = VK_FORMAT_R8_UNORM;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R16_UINT:
      *vk_format = VK_FORMAT_R16_UINT;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R16G16_UINT:
      *vk_format = VK_FORMAT_R16G16_UINT;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_R10G10B10A10_UNORM:
      *vk_format = VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16;
      return TRUE;
    case GST_AHARDWARE_BUFFER_FORMAT_B10G10R10A2_UNORM:
    case GST_AHARDWARE_BUFFER_FORMAT_B10G10R10X2_UNORM:
      *vk_format = VK_FORMAT_A2R10G10B10_UNORM_PACK32;
      return TRUE;
    default:
      return FALSE;
  }
}

static GstVideoFormat
_ahb_format_to_video_format (guint32 format)
{
  switch (format) {
    case GST_AHARDWARE_BUFFER_FORMAT_R8G8B8A8_UNORM:
      return GST_VIDEO_FORMAT_RGBA;
    case GST_AHARDWARE_BUFFER_FORMAT_R8G8B8X8_UNORM:
      return GST_VIDEO_FORMAT_RGBx;
    case GST_AHARDWARE_BUFFER_FORMAT_R8G8B8_UNORM:
      return GST_VIDEO_FORMAT_RGB;
    case GST_AHARDWARE_BUFFER_FORMAT_R8_UNORM:
      return GST_VIDEO_FORMAT_GRAY8;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

static enum AhbFormatClass
_ahb_classify_format (const gchar * format_str, guint32 format)
{
  VkFormat vk_format;

  if (format == GST_AHARDWARE_BUFFER_FORMAT_Y8Cb8Cr8_420 ||
      format == GST_AHARDWARE_BUFFER_FORMAT_YCbCr_P010 ||
      format == GST_AHARDWARE_BUFFER_FORMAT_YCbCr_P210 ||
      format == gst_video_format_to_fourcc (GST_VIDEO_FORMAT_YV12))
    return AHB_FORMAT_CLASS_EXTERNAL_YUV;

  if (_ahb_format_to_vk_format (format, &vk_format))
    return _ahb_vk_format_is_renderable (vk_format) ?
        AHB_FORMAT_CLASS_CONCRETE : AHB_FORMAT_CLASS_UNSUPPORTED;

  if (format_str && !g_str_has_prefix (format_str, "0x"))
    return AHB_FORMAT_CLASS_UNSUPPORTED;

  /* Vendor-defined numeric formats cannot be classified from caps. Accept
   * them provisionally and use the per-buffer Vulkan properties to decide
   * whether they are concrete, external, or unsupported. */
  return AHB_FORMAT_CLASS_PROVISIONAL_VENDOR;
}

static gboolean
_ahb_have_ycbcr_support (GstVulkanDevice * device)
{
  PFN_vkCreateSamplerYcbcrConversion create_conversion;

  if (!gst_vulkan_physical_device_check_api_version (device->physical_device,
          1, 1, 0) &&
      !gst_vulkan_device_is_extension_enabled (device,
          VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME))
    return FALSE;

  create_conversion = (PFN_vkCreateSamplerYcbcrConversion)
      gst_vulkan_device_get_proc_address (device,
      "vkCreateSamplerYcbcrConversion");
  if (!create_conversion)
    create_conversion = (PFN_vkCreateSamplerYcbcrConversion)
        gst_vulkan_device_get_proc_address (device,
        "vkCreateSamplerYcbcrConversionKHR");

  return create_conversion != NULL;
}

static gboolean
_ahb_have_base_support (struct AhbUpload *ahb_upload)
{
  GstVulkanDevice *device = ahb_upload->upload->device;

  /* Caps queries may happen before a Vulkan device is selected. Keep the AHB
   * path visible then; set_caps() and per-buffer validation remain
   * authoritative once a device exists. */
  if (!device)
    return TRUE;

  return gst_vulkan_device_is_extension_enabled (device,
      VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME) &&
      gst_vulkan_device_get_proc_address (device,
      "vkGetAndroidHardwareBufferPropertiesANDROID") != NULL;
}

static gboolean
_ahb_format_is_available (struct AhbUpload *ahb_upload,
    const gchar * format_str)
{
  guint32 format;
  enum AhbFormatClass format_class;

  if (!gst_ahardware_buffer_format_from_caps_string (format_str, &format))
    return FALSE;

  format_class = _ahb_classify_format (format_str, format);
  if (format_class == AHB_FORMAT_CLASS_UNSUPPORTED)
    return FALSE;
  if (format_class == AHB_FORMAT_CLASS_EXTERNAL_YUV) {
#ifndef HAVE_GLSLC
    /* External YUV must be rendered through the embedded identity shaders,
     * which are only available when glslc was found at build time. */
    return FALSE;
#else
    /* GstVulkanFullScreenQuad submits the YCbCr conversion draw on this
     * graphics-capable queue. */
    if (!ahb_upload->upload->queue)
      return FALSE;
    return _ahb_have_ycbcr_support (ahb_upload->upload->device);
#endif
  }

  return TRUE;
}

static gboolean
_ahb_structure_is_available (struct AhbUpload *ahb_upload,
    const GstStructure * structure)
{
  const GValue *value;

  /* Without a Vulkan device, transform_caps() cannot filter formats by
   * device capability yet. Keep the caps provisional. */
  if (!ahb_upload->upload->device)
    return TRUE;

  value = gst_structure_get_value (structure, "ahb-format");
  if (!value)
    return TRUE;
  if (G_VALUE_HOLDS_STRING (value))
    return _ahb_format_is_available (ahb_upload, g_value_get_string (value));
  if (GST_VALUE_HOLDS_LIST (value)) {
    guint n = gst_value_list_get_size (value);

    for (guint i = 0; i < n; i++) {
      const GValue *item = gst_value_list_get_value (value, i);

      if (G_VALUE_HOLDS_STRING (item) &&
          _ahb_format_is_available (ahb_upload, g_value_get_string (item)))
        return TRUE;
    }
  }

  return FALSE;
}

static GstStructure *
_ahb_copy_available_structure (struct AhbUpload *ahb_upload,
    const GstStructure * structure)
{
  const GValue *value;
  GstStructure *copy;

  /* Without a Vulkan device, transform_caps() cannot filter formats by
   * device capability yet. Keep the caps provisional. */
  if (!ahb_upload->upload->device)
    return gst_structure_copy (structure);

  value = gst_structure_get_value (structure, "ahb-format");
  if (!value || G_VALUE_HOLDS_STRING (value))
    return _ahb_structure_is_available (ahb_upload, structure) ?
        gst_structure_copy (structure) : NULL;

  if (GST_VALUE_HOLDS_LIST (value)) {
    GValue filtered = G_VALUE_INIT;
    guint n = gst_value_list_get_size (value);

    g_value_init (&filtered, GST_TYPE_LIST);
    for (guint i = 0; i < n; i++) {
      const GValue *item = gst_value_list_get_value (value, i);

      if (G_VALUE_HOLDS_STRING (item) &&
          _ahb_format_is_available (ahb_upload, g_value_get_string (item)))
        gst_value_list_append_value (&filtered, item);
    }
    if (gst_value_list_get_size (&filtered) == 0) {
      g_value_unset (&filtered);
      return NULL;
    }

    copy = gst_structure_copy (structure);
    gst_structure_set_value (copy, "ahb-format", &filtered);
    g_value_unset (&filtered);
    return copy;
  }

  return NULL;
}

static gboolean
_ahb_can_direct_import (const struct AhbUpload *ahb_upload)
{
  const VkImageUsageFlags direct_usage = VK_IMAGE_USAGE_SAMPLED_BIT;

  return ahb_upload->output_usage_known &&
      (ahb_upload->output_usage & ~direct_usage) == 0;
}

static gboolean
_ahb_desc_has_yv12_chroma_order (const AHardwareBuffer_Desc * desc)
{
  return desc->format == gst_video_format_to_fourcc (GST_VIDEO_FORMAT_YV12);
}

static VkComponentSwizzle
_ahb_resolve_identity_swizzle (VkComponentSwizzle swizzle,
    VkComponentSwizzle identity)
{
  return swizzle == VK_COMPONENT_SWIZZLE_IDENTITY ? identity : swizzle;
}

static void
_ahb_select_ycbcr_filters (VkFormatFeatureFlags format_features,
    VkFilter * chroma_filter, VkFilter * reconstruction_filter)
{
  gboolean have_chroma_linear =
      format_features &
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;
  gboolean have_reconstruction_linear =
      format_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  gboolean have_separate_reconstruction =
      format_features &
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT;

  if (!have_separate_reconstruction) {
    *chroma_filter = *reconstruction_filter =
        have_chroma_linear &&
        have_reconstruction_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    return;
  }

  *chroma_filter = have_chroma_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  *reconstruction_filter =
      have_reconstruction_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

static void
_ahb_resolve_ycbcr_properties (const struct AhbUpload *ahb_upload,
    const VkAndroidHardwareBufferFormatPropertiesANDROID * format_props,
    VkSamplerYcbcrModelConversion * model, VkSamplerYcbcrRange * range,
    VkChromaLocation * x_offset, VkChromaLocation * y_offset)
{
  *model = format_props->suggestedYcbcrModel;
  *range = format_props->suggestedYcbcrRange;
  *x_offset = format_props->suggestedXChromaOffset;
  *y_offset = format_props->suggestedYChromaOffset;

  switch (ahb_upload->in_info.colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT601:
      *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020;
      break;
    default:
      break;
  }

  switch (ahb_upload->in_info.colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      *range = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
      break;
    case GST_VIDEO_COLOR_RANGE_16_235:
      *range = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
      break;
    default:
      break;
  }

  switch (ahb_upload->chroma_site & ~GST_VIDEO_CHROMA_SITE_ALT_LINE) {
    case GST_VIDEO_CHROMA_SITE_NONE:
      *x_offset = VK_CHROMA_LOCATION_MIDPOINT;
      *y_offset = VK_CHROMA_LOCATION_MIDPOINT;
      break;
    case GST_VIDEO_CHROMA_SITE_H_COSITED:
      *x_offset = VK_CHROMA_LOCATION_COSITED_EVEN;
      *y_offset = VK_CHROMA_LOCATION_MIDPOINT;
      break;
    case GST_VIDEO_CHROMA_SITE_V_COSITED:
      *x_offset = VK_CHROMA_LOCATION_MIDPOINT;
      *y_offset = VK_CHROMA_LOCATION_COSITED_EVEN;
      break;
    case GST_VIDEO_CHROMA_SITE_COSITED:
      *x_offset = VK_CHROMA_LOCATION_COSITED_EVEN;
      *y_offset = VK_CHROMA_LOCATION_COSITED_EVEN;
      break;
    default:
      break;
  }
}

static GstVulkanHandle *
_ahb_create_simple_sampler (GstVulkanDevice * device, GError ** error)
{
  VkSamplerCreateInfo sampler_info = {
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_NEAREST,
    .minFilter = VK_FILTER_NEAREST,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .maxLod = 1.0f,
  };
  VkSampler sampler = VK_NULL_HANDLE;
  VkResult res = vkCreateSampler (device->device, &sampler_info, NULL,
      &sampler);
  if (gst_vulkan_error_to_g_error (res, error, "vkCreateSampler") < 0)
    return NULL;

  return gst_vulkan_handle_new_wrapped (device, GST_VULKAN_HANDLE_TYPE_SAMPLER,
      (GstVulkanHandleTypedef) sampler, gst_vulkan_handle_free_sampler, NULL);
}

static GstVulkanHandle *
_ahb_create_ycbcr_sampler (GstVulkanDevice * device,
    const VkAndroidHardwareBufferFormatPropertiesANDROID * format_props,
    VkFormat format, guint64 external_format, gboolean yv12_chroma_order,
    VkSamplerYcbcrModelConversion ycbcr_model,
    VkSamplerYcbcrRange ycbcr_range, VkChromaLocation x_chroma_offset,
    VkChromaLocation y_chroma_offset, GError ** error)
{
  VkFilter chroma_filter;
  VkFilter reconstruction_filter;
  VkExternalFormatANDROID external_info = {
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
    .externalFormat = external_format,
  };
  VkComponentMapping components =
      format_props->samplerYcbcrConversionComponents;
  VkSamplerYcbcrConversionCreateInfo conversion_info = {
    .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
    .pNext = &external_info,
    .format = format,
    .ycbcrModel = ycbcr_model,
    .ycbcrRange = ycbcr_range,
    .components = components,
    .xChromaOffset = x_chroma_offset,
    .yChromaOffset = y_chroma_offset,
    .chromaFilter = VK_FILTER_NEAREST,
  };
  VkSamplerYcbcrConversionInfo sampler_conversion_info = {
    .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
  };
  VkSamplerCreateInfo sampler_info = {
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .pNext = &sampler_conversion_info,
    .magFilter = VK_FILTER_NEAREST,
    .minFilter = VK_FILTER_NEAREST,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .maxLod = 1.0f,
  };
  VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;
  GstVulkanHandle *conversion_handle;
  VkSampler sampler = VK_NULL_HANDLE;
  PFN_vkCreateSamplerYcbcrConversion create_conversion;
  VkResult res;

  _ahb_select_ycbcr_filters (format_props->formatFeatures, &chroma_filter,
      &reconstruction_filter);
  conversion_info.chromaFilter = chroma_filter;
  sampler_info.magFilter = reconstruction_filter;
  sampler_info.minFilter = reconstruction_filter;

  if (external_format == 0) {
    g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
        "External-format YCbCr sampler requires non-zero externalFormat");
    return NULL;
  }

  if (yv12_chroma_order) {
    VkComponentMapping original_components = components;

    /* YV12 stores Cr before Cb; compensate when sampling as YCbCr while
     * preserving any driver-provided component mapping. IDENTITY is
     * contextual to each component and must be resolved before swapping. */
    components.r =
        _ahb_resolve_identity_swizzle (original_components.b,
        VK_COMPONENT_SWIZZLE_B);
    components.b =
        _ahb_resolve_identity_swizzle (original_components.r,
        VK_COMPONENT_SWIZZLE_R);
    conversion_info.components = components;
  }

  GST_TRACE_OBJECT (device, "AHardwareBuffer YCbCr sampler: format %u, "
      "externalFormat 0x%" G_GINT64_MODIFIER "x, formatFeatures 0x%x, "
      "model %u, range %u, xChromaOffset %u, yChromaOffset %u, components "
      "{%u,%u,%u,%u}, chromaFilter %u, minFilter %u, magFilter %u, "
      "YV12 correction %d", format, (guint64) external_format,
      format_props->formatFeatures, conversion_info.ycbcrModel,
      conversion_info.ycbcrRange, conversion_info.xChromaOffset,
      conversion_info.yChromaOffset, conversion_info.components.r,
      conversion_info.components.g, conversion_info.components.b,
      conversion_info.components.a, conversion_info.chromaFilter,
      sampler_info.minFilter, sampler_info.magFilter, yv12_chroma_order);

  /* Resolve the Vulkan 1.1 core entry point dynamically, with the Vulkan 1.0
   * VK_KHR_sampler_ycbcr_conversion entry point as fallback. */
  create_conversion = (PFN_vkCreateSamplerYcbcrConversion)
      gst_vulkan_device_get_proc_address (device,
      "vkCreateSamplerYcbcrConversion");
  if (!create_conversion) {
    create_conversion = (PFN_vkCreateSamplerYcbcrConversion)
        gst_vulkan_device_get_proc_address (device,
        "vkCreateSamplerYcbcrConversionKHR");
  }
  if (!create_conversion) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_EXTENSION_NOT_PRESENT,
        "vkCreateSamplerYcbcrConversion is unavailable");
    return NULL;
  }

  res = create_conversion (device->device, &conversion_info, NULL, &conversion);
  if (gst_vulkan_error_to_g_error (res, error,
          "vkCreateSamplerYcbcrConversion") < 0)
    return NULL;

  conversion_handle = gst_vulkan_handle_new_wrapped (device,
      GST_VULKAN_HANDLE_TYPE_SAMPLER_YCBCR_CONVERSION,
      (GstVulkanHandleTypedef) conversion,
      gst_vulkan_handle_free_sampler_ycbcr_conversion, NULL);

  sampler_conversion_info.conversion = conversion;
  res = vkCreateSampler (device->device, &sampler_info, NULL, &sampler);
  if (gst_vulkan_error_to_g_error (res, error, "vkCreateSampler") < 0) {
    gst_vulkan_handle_unref (conversion_handle);
    return NULL;
  }

  return gst_vulkan_handle_new_wrapped (device, GST_VULKAN_HANDLE_TYPE_SAMPLER,
      (GstVulkanHandleTypedef) sampler, _ahb_free_sampler, conversion_handle);
}

static gboolean
_ahb_ycbcr_sampler_key_equal (const struct AhbYcbcrSamplerKey *a,
    const struct AhbYcbcrSamplerKey *b)
{
  return memcmp (a, b, sizeof (*a)) == 0;
}

static GstVulkanHandle *
_ahb_ensure_ycbcr_sampler (struct AhbUpload *ahb_upload,
    const VkAndroidHardwareBufferFormatPropertiesANDROID * format_props,
    VkFormat format, guint64 external_format, gboolean yv12_chroma_order,
    GError ** error)
{
  GstVulkanUpload *upload = ahb_upload->upload;
  struct AhbYcbcrSamplerKey key;
  VkSamplerYcbcrModelConversion ycbcr_model;
  VkSamplerYcbcrRange ycbcr_range;
  VkChromaLocation x_chroma_offset;
  VkChromaLocation y_chroma_offset;

  _ahb_resolve_ycbcr_properties (ahb_upload, format_props, &ycbcr_model,
      &ycbcr_range, &x_chroma_offset, &y_chroma_offset);

  memset (&key, 0, sizeof (key));
  key.format = format;
  key.external_format = external_format;
  key.ycbcr_model = ycbcr_model;
  key.ycbcr_range = ycbcr_range;
  key.components = format_props->samplerYcbcrConversionComponents;
  key.x_chroma_offset = x_chroma_offset;
  key.y_chroma_offset = y_chroma_offset;
  _ahb_select_ycbcr_filters (format_props->formatFeatures, &key.chroma_filter,
      &key.reconstruction_filter);
  key.yv12_chroma_order = yv12_chroma_order;

  if (ahb_upload->ycbcr_sampler && ahb_upload->have_ycbcr_sampler_key &&
      _ahb_ycbcr_sampler_key_equal (&ahb_upload->ycbcr_sampler_key, &key)) {
    return gst_vulkan_handle_ref (ahb_upload->ycbcr_sampler);
  }

  gst_clear_vulkan_handle (&ahb_upload->ycbcr_sampler);
  ahb_upload->have_ycbcr_sampler_key = FALSE;

  ahb_upload->ycbcr_sampler = _ahb_create_ycbcr_sampler (upload->device,
      format_props, format, external_format, yv12_chroma_order, ycbcr_model,
      ycbcr_range, x_chroma_offset, y_chroma_offset, error);
  if (!ahb_upload->ycbcr_sampler)
    return NULL;

  ahb_upload->ycbcr_sampler_key = key;
  ahb_upload->have_ycbcr_sampler_key = TRUE;

  return gst_vulkan_handle_ref (ahb_upload->ycbcr_sampler);
}

static GstMemory *
_ahb_import_as_vulkan_image (struct AhbUpload *ahb_upload,
    AHardwareBuffer * ahb, GstMemory * in_mem,
    const VkAndroidHardwareBufferPropertiesANDROID * props,
    const VkAndroidHardwareBufferFormatPropertiesANDROID * format_props,
    gboolean external, GstVulkanHandle ** sampler, GError ** error)
{
  GstVulkanUpload *upload = ahb_upload->upload;
  AHardwareBuffer_Desc desc;
  VkFormat format = external ? VK_FORMAT_UNDEFINED : format_props->format;
  guint64 external_format = external ? format_props->externalFormat : 0;
  VkExternalMemoryImageCreateInfo external_memory_info = {
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
  };
  VkExternalFormatANDROID external_format_info = {
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
    .externalFormat = external_format,
  };
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = &external_memory_info,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  VkMemoryDedicatedAllocateInfo dedicated_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
  };
  VkImportAndroidHardwareBufferInfoANDROID import_info = {
    .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
    .pNext = &dedicated_info,
    .buffer = ahb,
  };
  VkMemoryAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext = &import_info,
    .allocationSize = props->allocationSize,
  };
  VkMemoryRequirements req;
#ifndef GST_DISABLE_GST_DEBUG
  guint32 image_memory_type_bits;
  gboolean log_import_info = !ahb_upload->have_logged_import_info;
#endif
  GstMemory *mem = NULL;
  struct AhbImportData *data = NULL;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkResult res;

  if (sampler)
    *sampler = NULL;

  AHardwareBuffer_describe (ahb, &desc);

#ifndef GST_DISABLE_GST_DEBUG
  if (log_import_info) {
    GST_TRACE_OBJECT (upload, "AHardwareBuffer descriptor: format 0x%08x, "
        "width %u, height %u, layers %u, stride %u, usage 0x%016"
        G_GINT64_MODIFIER "x",
        desc.format, desc.width, desc.height, desc.layers, desc.stride,
        (guint64) desc.usage);
    GST_TRACE_OBJECT (upload, "AHardwareBuffer Vulkan properties: "
        "allocationSize %" G_GUINT64_FORMAT ", memoryTypeBits 0x%x, "
        "format %u, externalFormat 0x%" G_GINT64_MODIFIER "x, "
        "formatFeatures 0x%x", (guint64) props->allocationSize,
        props->memoryTypeBits, format_props->format,
        (guint64) format_props->externalFormat, format_props->formatFeatures);
    GST_TRACE_OBJECT (upload, "AHardwareBuffer import setup: mode %s, "
        "image format %u, extent %ux%u, usage 0x%x, model %u, range %u, "
        "xChromaOffset %u, yChromaOffset %u, components {%u,%u,%u,%u}",
        external ? "external" : "concrete", image_info.format, desc.width,
        desc.height, image_info.usage, format_props->suggestedYcbcrModel,
        format_props->suggestedYcbcrRange,
        format_props->suggestedXChromaOffset,
        format_props->suggestedYChromaOffset,
        format_props->samplerYcbcrConversionComponents.r,
        format_props->samplerYcbcrConversionComponents.g,
        format_props->samplerYcbcrConversionComponents.b,
        format_props->samplerYcbcrConversionComponents.a);
  }
#endif

  if (desc.width == 0 || desc.height == 0 || desc.layers == 0) {
    g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
        "Invalid AHardwareBuffer dimensions");
    return NULL;
  }

  if ((desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) == 0) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
        "AHardwareBuffer usage 0x%" G_GINT64_MODIFIER "x does not include "
        "GPU sampled-image usage", (guint64) desc.usage);
    return NULL;
  }

  if ((format_props->formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
        "AHardwareBuffer Vulkan format features 0x%x do not support sampled "
        "images", format_props->formatFeatures);
    return NULL;
  }

  if (desc.width < GST_VIDEO_INFO_WIDTH (&ahb_upload->in_info) ||
      desc.height < GST_VIDEO_INFO_HEIGHT (&ahb_upload->in_info) ||
      desc.layers != 1) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
        "AHardwareBuffer dimensions %ux%u with %u layer(s) are smaller than "
        "negotiated caps %dx%d with 1 layer",
        (guint) desc.width, (guint) desc.height, (guint) desc.layers,
        GST_VIDEO_INFO_WIDTH (&ahb_upload->in_info),
        GST_VIDEO_INFO_HEIGHT (&ahb_upload->in_info));
    return NULL;
  }

  image_info.extent = (VkExtent3D) {
  desc.width, desc.height, 1};

  if (external)
    external_memory_info.pNext = &external_format_info;

  res = vkCreateImage (upload->device->device, &image_info, NULL, &image);
  if (gst_vulkan_error_to_g_error (res, error, "vkCreateImage") < 0)
    return NULL;

  vkGetImageMemoryRequirements (upload->device->device, image, &req);
#ifndef GST_DISABLE_GST_DEBUG
  image_memory_type_bits = req.memoryTypeBits;
#endif
  req.memoryTypeBits &= props->memoryTypeBits;
  if (!gst_vulkan_memory_find_memory_type_index_with_requirements
      (upload->device, &req, G_MAXUINT32, &alloc_info.memoryTypeIndex)) {
    g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_MEMORY_MAP_FAILED,
        "No compatible Vulkan memory type for AHardwareBuffer");
    goto error;
  }
#ifndef GST_DISABLE_GST_DEBUG
  if (log_import_info) {
    GST_TRACE_OBJECT (upload, "AHardwareBuffer image memory: size %"
        G_GUINT64_FORMAT ", alignment %" G_GUINT64_FORMAT ", "
        "image memoryTypeBits 0x%x, compatible bits 0x%x, selected memory "
        "type %u", (guint64) req.size, (guint64) req.alignment,
        image_memory_type_bits, req.memoryTypeBits, alloc_info.memoryTypeIndex);
    ahb_upload->have_logged_import_info = TRUE;
  }
#endif

  dedicated_info.image = image;
  res = vkAllocateMemory (upload->device->device, &alloc_info, NULL, &memory);
  if (gst_vulkan_error_to_g_error (res, error, "vkAllocateMemory") < 0)
    goto error;

  res = vkBindImageMemory (upload->device->device, image, memory, 0);
  if (gst_vulkan_error_to_g_error (res, error, "vkBindImageMemory") < 0)
    goto error;

  data = g_new0 (struct AhbImportData, 1);
  data->device = gst_object_ref (upload->device);
  /* Keep the source memory alive while the imported image exists. Retaining
   * the AHardwareBuffer alone might not preserve producer-side state that
   * prevents its storage from being recycled. */
  data->in_mem = gst_memory_ref (in_mem);
  data->image = image;
  data->memory = memory;

  mem = gst_vulkan_image_memory_wrapped_with_image_info (upload->device,
      image, &image_info, data, (GDestroyNotify) _ahb_import_data_free);
  if (!mem) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_INITIALIZATION_FAILED,
        "Failed to initialize Vulkan image memory");
    return NULL;
  }
  if (external) {
    GstVulkanHandle *local_sampler;
    VkSamplerYcbcrConversionInfo conversion_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
    };
    VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = &conversion_info,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      /* Match gst_vulkan_get_or_create_image_view() so fullscreenquad reuses
       * this YCbCr-conversion view instead of creating a plain generic view. */
      .components = {
            .r = VK_COMPONENT_SWIZZLE_R,
            .g = VK_COMPONENT_SWIZZLE_G,
            .b = VK_COMPONENT_SWIZZLE_B,
            .a = VK_COMPONENT_SWIZZLE_A,
          },
      .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
          },
    };
    GstVulkanImageView *view;

    local_sampler = _ahb_ensure_ycbcr_sampler (ahb_upload, format_props,
        format, external_format, _ahb_desc_has_yv12_chroma_order (&desc),
        error);
    if (!local_sampler) {
      gst_memory_unref (mem);
      return NULL;
    }

    conversion_info.conversion = (VkSamplerYcbcrConversion) ((GstVulkanHandle *)
        local_sampler->user_data)->handle;
    view = gst_vulkan_image_view_new ((GstVulkanImageMemory *) mem, &view_info);
    if (!view) {
      gst_vulkan_handle_unref (local_sampler);
      gst_memory_unref (mem);
      return NULL;
    }
    gst_vulkan_image_memory_add_view ((GstVulkanImageMemory *) mem, view);
    gst_vulkan_image_view_unref (view);

    *sampler = local_sampler;
  }

  return mem;

error:
  if (memory)
    vkFreeMemory (upload->device->device, memory, NULL);
  if (image)
    vkDestroyImage (upload->device->device, image, NULL);
  return NULL;

}

static gboolean
_ahb_ensure_quad (struct AhbUpload *ahb_upload, GError ** error)
{
#ifdef HAVE_GLSLC
  GstVulkanUpload *upload = ahb_upload->upload;

  if (!ahb_upload->quad) {
    ahb_upload->quad = gst_vulkan_full_screen_quad_new (upload->queue);
    if (!ahb_upload->quad)
      return FALSE;
  }

  if (!ahb_upload->quad_info_set) {
    if (!gst_vulkan_full_screen_quad_set_info (ahb_upload->quad,
            &ahb_upload->in_info, &ahb_upload->out_info))
      return FALSE;
    ahb_upload->quad_info_set = TRUE;
  }

  if (!ahb_upload->vert) {
    ahb_upload->vert = gst_vulkan_create_shader (upload->device, identity_vert,
        identity_vert_size, error);
    if (!ahb_upload->vert)
      return FALSE;
  }

  if (!ahb_upload->frag) {
    ahb_upload->frag = gst_vulkan_create_shader (upload->device, identity_frag,
        identity_frag_size, error);
    if (!ahb_upload->frag)
      return FALSE;
  }

  return gst_vulkan_full_screen_quad_set_shaders (ahb_upload->quad,
      ahb_upload->vert, ahb_upload->frag);
#else
  g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_FEATURE_NOT_PRESENT,
      "Vulkan shader support is unavailable for AHardwareBuffer conversion");
  return FALSE;
#endif
}

static gboolean
_ahb_ensure_sampler (struct AhbUpload *ahb_upload, GError ** error)
{
  GstVulkanUpload *upload = ahb_upload->upload;

  if (ahb_upload->simple_sampler)
    return TRUE;

  ahb_upload->simple_sampler = _ahb_create_simple_sampler (upload->device,
      error);

  return ahb_upload->simple_sampler != NULL;
}

static gboolean
_ahb_image_has_padding (struct AhbUpload *ahb_upload, GstMemory * vk_mem)
{
  GstVulkanImageMemory *image = (GstVulkanImageMemory *) vk_mem;

  return gst_vulkan_image_memory_get_width (image) >
      GST_VIDEO_INFO_WIDTH (&ahb_upload->in_info) ||
      gst_vulkan_image_memory_get_height (image) >
      GST_VIDEO_INFO_HEIGHT (&ahb_upload->in_info);
}

static gboolean
_ahb_update_quad_vertices (struct AhbUpload *ahb_upload, GstMemory * vk_mem,
    GError ** error)
{
  GstVulkanUpload *upload = ahb_upload->upload;
  GstVulkanImageMemory *image = (GstVulkanImageMemory *) vk_mem;
  guint32 storage_width = gst_vulkan_image_memory_get_width (image);
  guint32 storage_height = gst_vulkan_image_memory_get_height (image);
  gint visible_width = GST_VIDEO_INFO_WIDTH (&ahb_upload->in_info);
  gint visible_height = GST_VIDEO_INFO_HEIGHT (&ahb_upload->in_info);
  GstMemory *vertices_mem;
  GstMapInfo map_info;
  gfloat u_max;
  gfloat v_max;
  struct AhbVertex vertices[] = {
    {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
    {-1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
  };

  g_return_val_if_fail (storage_width >= visible_width, FALSE);
  g_return_val_if_fail (storage_height >= visible_height, FALSE);

  u_max = (gfloat) visible_width / storage_width;
  v_max = (gfloat) visible_height / storage_height;

  if (ahb_upload->have_quad_vertices &&
      ahb_upload->quad_u_max == u_max && ahb_upload->quad_v_max == v_max)
    return TRUE;

  vertices[1].s = u_max;
  vertices[2].s = u_max;
  vertices[2].t = v_max;
  vertices[3].t = v_max;

  vertices_mem = gst_vulkan_buffer_memory_alloc (upload->device,
      sizeof (vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!vertices_mem) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_OUT_OF_DEVICE_MEMORY,
        "Failed to allocate AHardwareBuffer quad vertex buffer");
    return FALSE;
  }

  if (!gst_memory_map (vertices_mem, &map_info, GST_MAP_WRITE)) {
    gst_memory_unref (vertices_mem);
    g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_MEMORY_MAP_FAILED,
        "Failed to map AHardwareBuffer quad vertex buffer");
    return FALSE;
  }

  memcpy (map_info.data, vertices, sizeof (vertices));
  gst_memory_unmap (vertices_mem, &map_info);

  if (!gst_vulkan_full_screen_quad_set_vertex_buffer (ahb_upload->quad,
          vertices_mem, error)) {
    gst_memory_unref (vertices_mem);
    return FALSE;
  }

  gst_memory_unref (vertices_mem);
  ahb_upload->quad_u_max = u_max;
  ahb_upload->quad_v_max = v_max;
  ahb_upload->have_quad_vertices = TRUE;

  return TRUE;
}

static GstFlowReturn
_ahb_render_sampled_image (struct AhbUpload *ahb_upload, GstMemory * vk_mem,
    GstBuffer * parent, GstVulkanHandle * sampler, gboolean immutable_sampler,
    GstBuffer ** outbuf, GError ** error)
{
  GstVulkanUpload *upload = ahb_upload->upload;
  GstBaseTransformClass *bclass =
      GST_BASE_TRANSFORM_GET_CLASS (GST_BASE_TRANSFORM_CAST (upload));
  GstBuffer *vk_inbuf = NULL;
  GstBufferPool *pool = NULL;
  GstFlowReturn ret;

  vk_inbuf = gst_buffer_new ();
  gst_buffer_append_memory (vk_inbuf, vk_mem);
  gst_buffer_add_parent_buffer_meta (vk_inbuf, parent);

  pool = gst_base_transform_get_buffer_pool (GST_BASE_TRANSFORM_CAST (upload));
  if (!pool) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_INITIALIZATION_FAILED,
        "No output buffer pool configured for AHardwareBuffer conversion");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  gst_buffer_pool_set_active (pool, TRUE);
  ret = gst_buffer_pool_acquire_buffer (pool, outbuf, NULL);
  gst_object_unref (pool);
  pool = NULL;
  if (ret != GST_FLOW_OK)
    goto out;

  if (!bclass->copy_metadata (GST_BASE_TRANSFORM_CAST (upload), parent,
          *outbuf)) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_INITIALIZATION_FAILED,
        "Failed to copy AHardwareBuffer input metadata");
    goto error;
  }

  if (!_ahb_ensure_quad (ahb_upload, error))
    goto error;

  if (!_ahb_update_quad_vertices (ahb_upload, vk_mem, error))
    goto error;

  if (immutable_sampler) {
    if (!gst_vulkan_full_screen_quad_set_immutable_sampler (ahb_upload->quad,
            sampler))
      goto error;
  } else {
    if (!gst_vulkan_full_screen_quad_set_sampler (ahb_upload->quad, sampler))
      goto error;
  }

  if (!gst_vulkan_full_screen_quad_set_input_buffer (ahb_upload->quad,
          vk_inbuf, error))
    goto error;

  if (!gst_vulkan_full_screen_quad_set_output_buffer (ahb_upload->quad,
          *outbuf, error))
    goto error;

  if (!gst_vulkan_full_screen_quad_draw (ahb_upload->quad, error))
    goto error;

  gst_vulkan_full_screen_quad_set_input_buffer (ahb_upload->quad, NULL, NULL);
  gst_vulkan_full_screen_quad_set_output_buffer (ahb_upload->quad, NULL, NULL);

  ret = GST_FLOW_OK;

out:
  gst_clear_object (&pool);
  gst_clear_buffer (&vk_inbuf);
  return ret;

error:
  gst_clear_buffer (outbuf);
  ret = GST_FLOW_ERROR;
  goto out;
}

static GstStaticCaps _ahb_in_templ =
GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER
    "), format = (string) AHARDWARE_BUFFER");
static GstStaticCaps _ahb_out_templ =
GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE ")");

static gpointer
_ahb_new_impl (GstVulkanUpload * upload)
{
  struct AhbUpload *ahb_upload = g_new0 (struct AhbUpload, 1);

  ahb_upload->upload = upload;

  return ahb_upload;
}

static void
_ahb_append_video_format (GValue * formats, GstVideoFormat format)
{
  const gchar *format_str = gst_video_format_to_string (format);
  GValue value = G_VALUE_INIT;
  guint n = gst_value_list_get_size (formats);

  for (guint i = 0; i < n; i++) {
    const GValue *existing = gst_value_list_get_value (formats, i);

    if (g_str_equal (g_value_get_string (existing), format_str))
      return;
  }

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_static_string (&value, format_str);
  gst_value_list_append_value (formats, &value);
  g_value_unset (&value);
}

static GstVideoFormat
_ahb_caps_format_to_output_video_format (const gchar * ahb_format_str,
    guint32 ahb_format)
{
  GstVideoFormat video_format = GST_VIDEO_FORMAT_UNKNOWN;

  if (_ahb_classify_format (ahb_format_str,
          ahb_format) == AHB_FORMAT_CLASS_CONCRETE)
    video_format = _ahb_format_to_video_format (ahb_format);

  /* External and vendor formats are rendered into RGBA by this upload path;
   * expose the converted output format when caps cannot name a concrete
   * directly-imported video format. */
  if (video_format == GST_VIDEO_FORMAT_UNKNOWN)
    video_format = GST_VIDEO_FORMAT_RGBA;

  return video_format;
}

static gboolean
_ahb_set_transformed_format (GstStructure * structure)
{
  const GValue *ahb_formats = gst_structure_get_value (structure, "ahb-format");

  if (!ahb_formats) {
    GValue video_formats = G_VALUE_INIT;

    g_value_init (&video_formats, GST_TYPE_LIST);
    _ahb_append_video_format (&video_formats, GST_VIDEO_FORMAT_RGBA);
    _ahb_append_video_format (&video_formats, GST_VIDEO_FORMAT_RGBx);
    _ahb_append_video_format (&video_formats, GST_VIDEO_FORMAT_RGB);
    _ahb_append_video_format (&video_formats, GST_VIDEO_FORMAT_GRAY8);
    gst_structure_set_value (structure, "format", &video_formats);
    g_value_unset (&video_formats);
    return TRUE;
  }

  if (G_VALUE_HOLDS_STRING (ahb_formats)) {
    const gchar *ahb_format_str = g_value_get_string (ahb_formats);
    GstVideoFormat video_format;
    guint32 ahb_format;

    if (!gst_ahardware_buffer_format_from_caps_string (ahb_format_str,
            &ahb_format))
      return FALSE;

    video_format =
        _ahb_caps_format_to_output_video_format (ahb_format_str, ahb_format);

    gst_structure_set (structure, "format", G_TYPE_STRING,
        gst_video_format_to_string (video_format), NULL);
    return TRUE;
  }

  if (GST_VALUE_HOLDS_LIST (ahb_formats)) {
    GValue video_formats = G_VALUE_INIT;
    guint n = gst_value_list_get_size (ahb_formats);

    g_value_init (&video_formats, GST_TYPE_LIST);
    for (guint i = 0; i < n; i++) {
      const GValue *item = gst_value_list_get_value (ahb_formats, i);
      const gchar *ahb_format_str;
      GstVideoFormat video_format;
      guint32 ahb_format;

      if (!G_VALUE_HOLDS_STRING (item))
        continue;

      ahb_format_str = g_value_get_string (item);
      if (!gst_ahardware_buffer_format_from_caps_string (ahb_format_str,
              &ahb_format))
        continue;
      video_format =
          _ahb_caps_format_to_output_video_format (ahb_format_str, ahb_format);
      _ahb_append_video_format (&video_formats, video_format);
    }

    if (gst_value_list_get_size (&video_formats) == 0) {
      g_value_unset (&video_formats);
      return FALSE;
    }

    gst_structure_set_value (structure, "format", &video_formats);
    g_value_unset (&video_formats);
    return TRUE;
  }

  return FALSE;
}

static GstCaps *
_ahb_transform_caps (gpointer impl, GstPadDirection direction, GstCaps * caps)
{
  struct AhbUpload *ahb_upload = impl;
  const gchar *feature =
      direction == GST_PAD_SINK ?
      GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER :
      GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE;
  GstCaps *filtered;
  GstCaps *ret;

  if (!_ahb_have_base_support (ahb_upload))
    return NULL;

  filtered = gst_caps_new_empty ();
  for (guint i = 0, n = gst_caps_get_size (caps); i < n; i++) {
    const GstCapsFeatures *features = gst_caps_get_features (caps, i);
    GstStructure *structure;

    if (!gst_caps_features_is_any (features) &&
        !gst_caps_features_contains (features, feature))
      continue;

    if (direction == GST_PAD_SINK)
      structure = _ahb_copy_available_structure (ahb_upload,
          gst_caps_get_structure (caps, i));
    else
      structure = gst_structure_copy (gst_caps_get_structure (caps, i));
    if (!structure)
      continue;

    gst_caps_append_structure_full (filtered, structure,
        gst_caps_features_copy (features));
  }

  if (direction == GST_PAD_SINK) {
    ret = gst_vulkan_upload_set_caps_features_with_passthrough (filtered,
        GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, NULL);
    for (guint i = 0, n = gst_caps_get_size (ret); i < n;) {
      GstStructure *structure = gst_caps_get_structure (ret, i);

      if (!_ahb_set_transformed_format (structure)) {
        gst_caps_remove_structure (ret, i);
        n--;
        continue;
      }
      gst_structure_remove_fields (structure, "ahb-format", "chroma-site",
          "colorimetry", NULL);
      i++;
    }
  } else {
    ret = gst_vulkan_upload_set_caps_features_with_passthrough (filtered,
        GST_CAPS_FEATURE_MEMORY_AHARDWAREBUFFER, NULL);
    for (guint i = 0, n = gst_caps_get_size (ret); i < n; i++) {
      GstStructure *structure = gst_caps_get_structure (ret, i);

      gst_structure_set (structure, "format", G_TYPE_STRING, "AHARDWARE_BUFFER",
          NULL);
      gst_structure_remove_fields (structure, "ahb-format", "chroma-site",
          "colorimetry", NULL);
    }
  }
  gst_caps_unref (filtered);

  return ret;
}

static gboolean
_ahb_set_caps (gpointer impl, GstCaps * in_caps, GstCaps * out_caps)
{
  struct AhbUpload *ahb_upload = impl;
  const GstStructure *structure;
  const gchar *ahb_format_str;
  const gchar *chroma_site_str;
  GstCaps *parse_caps;
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  if (!_ahb_have_base_support (ahb_upload))
    return FALSE;

  parse_caps = gst_caps_copy (in_caps);
  gst_structure_remove_field (gst_caps_get_structure (parse_caps, 0),
      "chroma-site");
  if (!gst_video_info_from_caps (&in_info, parse_caps)) {
    gst_caps_unref (parse_caps);
    return FALSE;
  }
  gst_caps_unref (parse_caps);

  if (!gst_video_info_from_caps (&out_info, out_caps))
    return FALSE;
  if (GST_VIDEO_INFO_FORMAT (&in_info) != GST_VIDEO_FORMAT_AHARDWARE_BUFFER)
    return FALSE;

  structure = gst_caps_get_structure (in_caps, 0);
  ahb_format_str = gst_structure_get_string (structure, "ahb-format");
  ahb_upload->ahb_format = 0;
  if (gst_structure_has_field (structure, "ahb-format")) {
    if (!ahb_format_str)
      return FALSE;
    if (!gst_ahardware_buffer_format_from_caps_string (ahb_format_str,
            &ahb_upload->ahb_format))
      return FALSE;
    if (ahb_upload->ahb_format == 0)
      return FALSE;
  }
  if (!_ahb_structure_is_available (ahb_upload, structure))
    return FALSE;

  if (ahb_upload->ahb_format != 0) {
    GstVideoFormat expected_format = GST_VIDEO_FORMAT_RGBA;

    if (_ahb_classify_format (ahb_format_str,
            ahb_upload->ahb_format) == AHB_FORMAT_CLASS_CONCRETE)
      expected_format = _ahb_format_to_video_format (ahb_upload->ahb_format);
    if (expected_format == GST_VIDEO_FORMAT_UNKNOWN ||
        GST_VIDEO_INFO_FORMAT (&out_info) != expected_format)
      return FALSE;
  } else {
    switch (GST_VIDEO_INFO_FORMAT (&out_info)) {
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_RGBx:
      case GST_VIDEO_FORMAT_RGB:
      case GST_VIDEO_FORMAT_GRAY8:
        break;
      default:
        return FALSE;
    }
  }

  ahb_upload->chroma_site = GST_VIDEO_CHROMA_SITE_UNKNOWN;
  chroma_site_str = gst_structure_get_string (structure, "chroma-site");
  if (chroma_site_str)
    ahb_upload->chroma_site =
        gst_video_chroma_site_from_string (chroma_site_str);

  ahb_upload->in_info = in_info;
  ahb_upload->out_info = out_info;
  ahb_upload->output_usage = 0;
  ahb_upload->output_usage_known = FALSE;
  ahb_upload->quad_info_set = FALSE;
  ahb_upload->have_quad_vertices = FALSE;
#ifndef GST_DISABLE_GST_DEBUG
  ahb_upload->have_logged_import_info = FALSE;
#endif

  return TRUE;
}

static gboolean
_ahb_update_output_usage (gpointer impl, VkImageUsageFlags downstream_usage,
    VkImageUsageFlags * usage)
{
  struct AhbUpload *ahb_impl = impl;

  g_return_val_if_fail (usage != NULL, FALSE);

  *usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

  ahb_impl->output_usage = downstream_usage;
  ahb_impl->output_usage_known = TRUE;

  return TRUE;
}

static gboolean
_ahb_copy_metadata (gpointer impl, GstVulkanUpload * upload,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  /* Metadata is copied earlier in the AHardwareBuffer output paths. */
  (void) impl;
  (void) upload;
  (void) inbuf;
  (void) outbuf;

  return TRUE;
}

static GstFlowReturn
_ahb_perform (gpointer impl, GstBuffer * inbuf, GstBuffer ** outbuf)
{
  struct AhbUpload *ahb_upload = impl;
  GstVulkanUpload *upload = ahb_upload->upload;
  VkAndroidHardwareBufferPropertiesANDROID props;
  VkAndroidHardwareBufferFormatPropertiesANDROID format_props;
  AHardwareBuffer_Desc desc;
  AHardwareBuffer *ahb = NULL;
  GstMemory *in_mem;
  GstMemory *vk_mem;
  GstVulkanHandle *sampler = NULL;
  GError *error = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;

  if (gst_buffer_n_memory (inbuf) != 1)
    return GST_FLOW_ERROR;

  in_mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_ahardware_buffer_memory_peek_buffer (in_mem, &ahb) || !ahb)
    return GST_FLOW_ERROR;

  AHardwareBuffer_describe (ahb, &desc);

  if (ahb_upload->ahb_format != 0 && ahb_upload->ahb_format != desc.format) {
    gchar *actual = gst_ahardware_buffer_format_to_caps_string (desc.format);
    gchar *expected =
        gst_ahardware_buffer_format_to_caps_string (ahb_upload->ahb_format);

    g_set_error (&error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
        "AHardwareBuffer format %s does not match negotiated format %s",
        actual, expected);
    g_free (actual);
    g_free (expected);
    goto error;
  }

  if (!_ahb_query_properties (ahb_upload, ahb, &props, &format_props, &error))
    goto error;

  GST_LOG_OBJECT (upload, "AHardwareBuffer Vulkan properties: format %u, "
      "externalFormat 0x%" G_GINT64_MODIFIER "x, memoryTypeBits 0x%x",
      format_props.format, (guint64) format_props.externalFormat,
      props.memoryTypeBits);

  {
    VkFormat expected_format;

    if (_ahb_format_to_vk_format (desc.format, &expected_format) &&
        format_props.format != expected_format) {
      g_set_error (&error, GST_VULKAN_ERROR, VK_ERROR_FORMAT_NOT_SUPPORTED,
          "AHardwareBuffer format 0x%x maps to VkFormat %u but reports %u",
          desc.format, expected_format, format_props.format);
      goto error;
    }
  }

  if (_ahb_vk_format_is_renderable (format_props.format)) {
    GstVideoFormat imported_format = _ahb_format_to_video_format (desc.format);

    if (imported_format == GST_VIDEO_FORMAT_UNKNOWN)
      imported_format = gst_vulkan_format_to_video_format (format_props.format);

    vk_mem = _ahb_import_as_vulkan_image (ahb_upload, ahb, in_mem, &props,
        &format_props, FALSE, NULL, &error);
    if (!vk_mem)
      goto error;

    if (imported_format == GST_VIDEO_INFO_FORMAT (&ahb_upload->out_info) &&
        _ahb_can_direct_import (ahb_upload) &&
        !_ahb_image_has_padding (ahb_upload, vk_mem)) {
      GstBaseTransformClass *bclass =
          GST_BASE_TRANSFORM_GET_CLASS (GST_BASE_TRANSFORM_CAST (upload));

      *outbuf = gst_buffer_new ();
      gst_buffer_append_memory (*outbuf, vk_mem);
      /* The rendering path must copy metadata before the fullscreen quad
       * retains the output buffer. Do the same here so _ahb_copy_metadata()
       * can remain a no-op for both AHardwareBuffer output paths. */
      if (!bclass->copy_metadata (GST_BASE_TRANSFORM_CAST (upload), inbuf,
              *outbuf)) {
        g_set_error_literal (&error, GST_VULKAN_ERROR,
            VK_ERROR_INITIALIZATION_FAILED,
            "Failed to copy AHardwareBuffer input metadata");
        goto error;
      }
      gst_buffer_add_parent_buffer_meta (*outbuf, inbuf);
      GST_LOG_OBJECT (upload, "Imported AHardwareBuffer as concrete VkFormat");
      return GST_FLOW_OK;
    }

    /* Copy through the quad when the imported image cannot satisfy the
     * negotiated output format or downstream image-usage requirements. */
    if (!_ahb_ensure_sampler (ahb_upload, &error)) {
      gst_memory_unref (vk_mem);
      goto error;
    }

    ret = _ahb_render_sampled_image (ahb_upload, vk_mem, inbuf,
        ahb_upload->simple_sampler, FALSE, outbuf, &error);
    if (ret != GST_FLOW_OK)
      goto error;

    GST_LOG_OBJECT (upload, "Rendered concrete-format AHardwareBuffer to "
        "Vulkan image");
    return ret;
  } else if (format_props.externalFormat == 0) {
    GST_WARNING_OBJECT (upload,
        "AHardwareBuffer has unsupported Vulkan format %u and no external "
        "format", format_props.format);
    return GST_FLOW_ERROR;
  }

  if (!_ahb_have_ycbcr_support (upload->device)) {
    g_set_error_literal (&error, GST_VULKAN_ERROR,
        VK_ERROR_FEATURE_NOT_PRESENT,
        "External-format AHardwareBuffer requires sampler YCbCr conversion");
    goto error;
  }

  vk_mem = _ahb_import_as_vulkan_image (ahb_upload, ahb, in_mem, &props,
      &format_props, TRUE, &sampler, &error);
  if (!vk_mem)
    goto error;

  ret = _ahb_render_sampled_image (ahb_upload, vk_mem, inbuf, sampler, TRUE,
      outbuf, &error);
  if (ret != GST_FLOW_OK)
    goto error;

  GST_LOG_OBJECT (upload, "Converted external-format AHardwareBuffer to "
      "Vulkan image");
  ret = GST_FLOW_OK;

out:
  gst_clear_vulkan_handle (&sampler);
  return ret;

error:
  if (error) {
    GST_WARNING_OBJECT (upload, "AHardwareBuffer upload failed: %s",
        error->message);
    g_clear_error (&error);
  }
  gst_clear_buffer (outbuf);
  ret = GST_FLOW_ERROR;
  goto out;
}

static void
_ahb_free (gpointer impl)
{
  struct AhbUpload *ahb_upload = impl;

  gst_clear_vulkan_handle (&ahb_upload->ycbcr_sampler);
  gst_clear_vulkan_handle (&ahb_upload->simple_sampler);
  gst_clear_vulkan_handle (&ahb_upload->frag);
  gst_clear_vulkan_handle (&ahb_upload->vert);
  gst_clear_object (&ahb_upload->quad);
  gst_clear_object (&ahb_upload->cached_device);
  g_free (ahb_upload);
}

const struct UploadMethod gst_vulkan_upload_ahb_method = {
  "AHardwareBufferToVulkanImage",
  &_ahb_in_templ,
  &_ahb_out_templ,
  _ahb_new_impl,
  _ahb_transform_caps,
  _ahb_set_caps,
  gst_vulkan_upload_buffer_propose_allocation,
  _ahb_update_output_usage,
  _ahb_copy_metadata,
  _ahb_perform,
  _ahb_free,
};

#endif

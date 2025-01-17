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

#pragma once

#include <gst/gst.h>
#include <gst/vulkan/gstvkapi.h>

G_BEGIN_DECLS

typedef struct _GstVulkanVideoProfile GstVulkanVideoProfile;
typedef struct _GstVulkanVideoCapabilities GstVulkanVideoCapabilities;

/**
 * GstVulkanVideoProfile:
 * @profile: the generic vulkan video profile
 * @codec: the specific codec profile
 *
 * Since: 1.24
 */
struct _GstVulkanVideoProfile
{
  /*< private >*/
  VkVideoProfileInfoKHR profile;
  union {
    VkVideoDecodeUsageInfoKHR decode;
    /**
     * GstVulkanVideoProfile.usage.encode:
     *
     * Since: 1.26
     **/
    VkVideoEncodeUsageInfoKHR encode;
  } usage;

  union {
    VkBaseInStructure base;
    VkVideoDecodeH264ProfileInfoKHR h264dec;
    VkVideoDecodeH265ProfileInfoKHR h265dec;
    VkVideoDecodeAV1ProfileInfoKHR av1dec;
    /**
     * GstVulkanVideoProfile.usage.codec.vp9dec:
     *
     * Since: 1.28
     **/
    VkVideoDecodeVP9ProfileInfoKHR vp9dec;
    /**
     * GstVulkanVideoProfile.usage.codec.h264enc:
     *
     * Since: 1.26
     **/
    VkVideoEncodeH264ProfileInfoKHR h264enc;
    /**
     * GstVulkanVideoProfile.usage.codec.h265enc:
     *
     * Since: 1.26
     **/
    VkVideoEncodeH265ProfileInfoKHR h265enc;
    /**
     * GstVulkanVideoProfile.usage.codec.av1enc:
     *
     * Since: 1.28
     **/
    VkVideoEncodeAV1ProfileInfoKHR av1enc;
  } codec;
  gpointer _reserved[GST_PADDING];
};

/**
 * GstVulkanVideoCapabilities:
 *
 * Since: 1.24
 */
struct _GstVulkanVideoCapabilities
{
  /*< private >*/
  VkVideoCapabilitiesKHR caps;
  union
  {
    struct
    {
      /*< private >*/
      VkVideoDecodeCapabilitiesKHR caps;
      union
      {
        /*< private >*/
        VkVideoDecodeH264CapabilitiesKHR h264;
        VkVideoDecodeH265CapabilitiesKHR h265;
        /**
         * GstVulkanVideoCapabilities.caps.codec.vp9:
         *
         * Since: 1.28
         **/
        VkVideoDecodeVP9CapabilitiesKHR vp9;
        /**
         * GstVulkanVideoCapabilities.caps.codec.av1:
         *
         * Since: 1.28
         **/
        VkVideoDecodeAV1CapabilitiesKHR av1;
      } codec;
    } decoder;
    struct
    {
      /*< private >*/
      VkVideoEncodeCapabilitiesKHR caps;
      union
      {
        /*< private >*/
        VkVideoEncodeH264CapabilitiesKHR h264;
        VkVideoEncodeH265CapabilitiesKHR h265;
        /**
         * _GstVulkanVideoCapabilities.encoder.codec.av1:
         *
         * Since: 1.28
         **/
        VkVideoEncodeAV1CapabilitiesKHR av1;

      } codec;
    } encoder;
  };
  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

/**
 * GstVulkanVideoOperation:
 * @GST_VULKAN_VIDEO_OPERATION_DECODE: decode operation
 * @GST_VULKAN_VIDEO_OPERATION_ENCODE: encode operation
 * @GST_VULKAN_VIDEO_OPERATION_UNKNOWN: unknown
 *
 * The type of video operation.
 *
 * Since: 1.24
 */
typedef enum  {
  GST_VULKAN_VIDEO_OPERATION_DECODE = 0,
  GST_VULKAN_VIDEO_OPERATION_ENCODE,
  GST_VULKAN_VIDEO_OPERATION_UNKNOWN,
} GstVulkanVideoOperation;

GST_VULKAN_API
GstCaps *               gst_vulkan_video_profile_to_caps        (const GstVulkanVideoProfile * profile);
GST_VULKAN_API
gboolean                gst_vulkan_video_profile_from_caps      (GstVulkanVideoProfile * profile,
                                                                 GstCaps * caps,
                                                                 GstVulkanVideoOperation video_operation);
GST_VULKAN_API
gboolean                gst_vulkan_video_profile_is_valid       (GstVulkanVideoProfile * profile,
                                                                 guint codec);
GST_VULKAN_API
gboolean                gst_vulkan_video_profile_is_equal       (const GstVulkanVideoProfile * a,
                                                                 const GstVulkanVideoProfile * b);

G_END_DECLS

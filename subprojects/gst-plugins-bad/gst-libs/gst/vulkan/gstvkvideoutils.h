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
#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

/**
 * GstVulkanVideoProfile:
 * @profile: the generic vulkan video profile
 * @codec: the specific codec profile
 *
 * Since: 1.24
 */
struct _GstVulkanVideoProfile
{
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
  VkVideoProfileInfoKHR profile;
  union {
    VkVideoDecodeH264ProfileInfoKHR h264;
    VkVideoDecodeH265ProfileInfoKHR h265;
  } codec;
#endif
  /* <private> */
  gpointer _reserved[GST_PADDING];
};

GST_VULKAN_API
GstCaps *               gst_vulkan_video_profile_to_caps        (const GstVulkanVideoProfile * profile);
GST_VULKAN_API
gboolean                gst_vulkan_video_profile_from_caps      (GstVulkanVideoProfile * profile,
                                                                 GstCaps * caps);

G_END_DECLS

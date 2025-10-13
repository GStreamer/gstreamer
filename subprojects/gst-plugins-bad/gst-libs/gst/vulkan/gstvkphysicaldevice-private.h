/*
 * GStreamer
 * Copyright (C) 2022 Igalia, S.L.
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

#ifndef __GST_VULKAN_PHYSICAL_DEVICE_PRIVATE_H__
#define __GST_VULKAN_PHYSICAL_DEVICE_PRIVATE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstVulkanFormatProperties GstVulkanFormatProperties;

/**
 * GstVulkanFormatProperties: (skip):
 * @linear_tiling_feat: linear tiling features
 * @optimal_tiling_feat: optimal tiling features
 * @buffer_feat: buffer features
 *
 * Common structure for Vulkan color format properties.
 */
struct _GstVulkanFormatProperties
{
  guint64 linear_tiling_feat;
  guint64 optimal_tiling_feat;
  guint64 buffer_feat;
};

const VkPhysicalDeviceFeatures2 *
                            gst_vulkan_physical_device_get_features         (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_sampler_ycbrc_conversion
                                                                            (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_synchronization2
                                                                            (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_timeline_sempahore
                                                                            (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_video_maintenance1
                                                                            (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_video_maintenance2
                                                                            (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_video_decode_vp9
                                                                            (GstVulkanPhysicalDevice * device);

gboolean                    gst_vulkan_physical_device_has_feature_video_encode_av1
                                                                            (GstVulkanPhysicalDevice * device);

void                        gst_vulkan_physical_device_get_format_properties
                                                                            (GstVulkanPhysicalDevice * device,
                                                                             guint vk_format,
                                                                             GstVulkanFormatProperties * props);


static inline void
vk_link_struct (gpointer chain, gconstpointer in)
{
  VkBaseOutStructure *out = chain;

  while (out->pNext)
    out = out->pNext;

  out->pNext = (void *) in;
}

static inline gconstpointer
vk_find_struct (gconstpointer chain, VkStructureType stype)
{
  const VkBaseInStructure *in = chain;

  while (in) {
    if (in->sType == stype)
      return in;
    in = in->pNext;
  }

  return NULL;
}

G_END_DECLS

#endif /* __GST_VULKAN_PHYSICAL_DEVICE_PRIVATE_H__ */

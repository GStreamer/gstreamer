/*
 * GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

#include "vkelementutils.h"

static void
fill_vulkan_image_view_info (VkImage image, VkFormat format,
    VkImageViewCreateInfo * info)
{
  /* *INDENT-OFF* */
  *info = (VkImageViewCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = NULL,
      .image = image,
      .format = format,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .flags = 0,
      .components = (VkComponentMapping) {
          VK_COMPONENT_SWIZZLE_R,
          VK_COMPONENT_SWIZZLE_G,
          VK_COMPONENT_SWIZZLE_B,
          VK_COMPONENT_SWIZZLE_A
      },
      .subresourceRange = (VkImageSubresourceRange) {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
      }
  };
  /* *INDENT-ON* */
}

static gboolean
find_compatible_view (GstVulkanImageView * view, VkImageViewCreateInfo * info)
{
  return view->create_info.image == info->image
      && view->create_info.format == info->format
      && view->create_info.viewType == info->viewType
      && view->create_info.flags == info->flags
      && view->create_info.components.r == info->components.r
      && view->create_info.components.g == info->components.g
      && view->create_info.components.b == info->components.b
      && view->create_info.components.a == info->components.a
      && view->create_info.subresourceRange.aspectMask ==
      info->subresourceRange.aspectMask
      && view->create_info.subresourceRange.baseMipLevel ==
      info->subresourceRange.baseMipLevel
      && view->create_info.subresourceRange.levelCount ==
      info->subresourceRange.levelCount
      && view->create_info.subresourceRange.levelCount ==
      info->subresourceRange.levelCount
      && view->create_info.subresourceRange.baseArrayLayer ==
      info->subresourceRange.baseArrayLayer
      && view->create_info.subresourceRange.layerCount ==
      info->subresourceRange.layerCount;
}

GstVulkanImageView *
get_or_create_image_view (GstVulkanImageMemory * image)
{
  VkImageViewCreateInfo create_info;
  GstVulkanImageView *ret = NULL;

  fill_vulkan_image_view_info (image->image, image->create_info.format,
      &create_info);

  ret = gst_vulkan_image_memory_find_view (image,
      (GstVulkanImageMemoryFindViewFunc) find_compatible_view, &create_info);
  if (!ret) {
    ret = gst_vulkan_image_view_new (image, &create_info);
    gst_vulkan_image_memory_add_view (image, ret);
  }

  return ret;
}

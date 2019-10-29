/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_VULKAN_IMAGE_MEMORY_H__
#define __GST_VULKAN_IMAGE_MEMORY_H__

#include <gst/vulkan/gstvkbarrier.h>
#include <gst/vulkan/gstvkdevice.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_IMAGE_MEMORY_ALLOCATOR (gst_vulkan_image_memory_allocator_get_type())
GST_VULKAN_API
GType gst_vulkan_image_memory_allocator_get_type(void);

#define GST_IS_VULKAN_IMAGE_MEMORY_ALLOCATOR(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VULKAN_IMAGE_MEMORY_ALLOCATOR))
#define GST_IS_VULKAN_IMAGE_MEMORY_ALLOCATOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VULKAN_IMAGE_MEMORY_ALLOCATOR))
#define GST_VULKAN_IMAGE_MEMORY_ALLOCATOR_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VULKAN_MEMORY_ALLOCATOR, GstVulkanImageMemoryAllocatorClass))
#define GST_VULKAN_IMAGE_MEMORY_ALLOCATOR(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VULKAN_MEMORY_ALLOCATOR, GstVulkanImageMemoryAllocator))
#define GST_VULKAN_IMAGE_MEMORY_ALLOCATOR_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VULKAN_MEMORY_ALLOCATOR, GstVulkanImageMemoryAllocatorClass))
#define GST_VULKAN_IMAGE_MEMORY_ALLOCATOR_CAST(obj)            ((GstVulkanImageMemoryAllocator *)(obj))

#define GST_VULKAN_IMAGE_MEMORY_ALLOCATOR_NAME "VulkanImage"
#define GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE "memory:VulkanImage"

struct _GstVulkanBarrierImageInfo
{
  GstVulkanBarrierMemoryInfo parent;

  VkImageLayout image_layout;
  /* FIXME: multiple layers or mipmap levels may require multiple barriers */
  VkImageSubresourceRange subresource_range;
};

struct _GstVulkanImageMemory
{
  GstMemory parent;

  GstVulkanDevice * device;

  VkImage image;
  GstVulkanMemory *vk_mem;

  VkImageCreateInfo create_info;
  VkMemoryRequirements requirements;
  VkImageFormatProperties format_properties;
  VkImageUsageFlags usage;

  GstVulkanBarrierImageInfo barrier;

  GMutex lock;
  gboolean wrapped;
  GDestroyNotify notify;
  gpointer user_data;

  GPtrArray *views;
};

/**
 * GstVulkanImageMemoryFindViewFunc:
 *
 * Function definition used to find views.  Return %TRUE if @view matches the
 * criteria.
 */
typedef gboolean (*GstVulkanImageMemoryFindViewFunc) (GstVulkanImageView * view, gpointer user_data);

/**
 * GstVulkanImageMemoryAllocator
 *
 * Opaque #GstVulkanImageMemoryAllocator struct
 */
struct _GstVulkanImageMemoryAllocator
{
  GstAllocator parent;
};

/**
 * GstVulkanImageMemoryAllocatorClass:
 *
 * The #GstVulkanImageMemoryAllocatorClass only contains private data
 */
struct _GstVulkanImageMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

GST_VULKAN_API
void            gst_vulkan_image_memory_init_once       (void);
GST_VULKAN_API
gboolean        gst_is_vulkan_image_memory              (GstMemory * mem);

GST_VULKAN_API
GstMemory *     gst_vulkan_image_memory_alloc           (GstVulkanDevice * device,
                                                         VkFormat format,
                                                         gsize width,
                                                         gsize height,
                                                         VkImageTiling tiling,
                                                         VkImageUsageFlags usage,
                                                         VkMemoryPropertyFlags mem_prop_flags);

GST_VULKAN_API
GstMemory *     gst_vulkan_image_memory_wrapped         (GstVulkanDevice * device,
                                                         VkImage image,
                                                         VkFormat format,
                                                         gsize width,
                                                         gsize height,
                                                         VkImageTiling tiling,
                                                         VkImageUsageFlags usage,
                                                         gpointer user_data,
                                                         GDestroyNotify notify);

GST_VULKAN_API
guint32         gst_vulkan_image_memory_get_width       (GstVulkanImageMemory * image);
GST_VULKAN_API
guint32         gst_vulkan_image_memory_get_height      (GstVulkanImageMemory * image);

GST_VULKAN_API
GstVulkanImageView *gst_vulkan_image_memory_find_view   (GstVulkanImageMemory * image,
                                                         GstVulkanImageMemoryFindViewFunc find_func,
                                                         gpointer user_data);
GST_VULKAN_API
void            gst_vulkan_image_memory_add_view        (GstVulkanImageMemory * image,
                                                         GstVulkanImageView * view);

GST_VULKAN_API
VkFormat gst_vulkan_format_from_video_info   (GstVideoInfo * v_info,
                                              guint plane);

G_END_DECLS

#endif /* __GST_VULKAN_IMAGE_MEMORY_H__ */

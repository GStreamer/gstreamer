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

#ifndef __GST_VULKAN_IMAGE_VIEW_H__
#define __GST_VULKAN_IMAGE_VIEW_H__

#include <gst/vulkan/gstvkbarrier.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_IMAGE_VIEW (gst_vulkan_image_view_get_type())
GST_VULKAN_API
GType gst_vulkan_image_view_get_type(void);

struct _GstVulkanImageView
{
  GstMiniObject parent;

  GstVulkanDevice * device;

  GstVulkanImageMemory *image;
  VkImageView view;

  VkImageViewCreateInfo create_info;
};

/**
 * gst_vulkan_image_view_ref: (skip)
 * @trash: a #GstVulkanImageView.
 *
 * Increases the refcount of the given trash object by one.
 *
 * Returns: (transfer full): @trash
 */
static inline GstVulkanImageView* gst_vulkan_image_view_ref(GstVulkanImageView* trash);
static inline GstVulkanImageView *
gst_vulkan_image_view_ref (GstVulkanImageView * trash)
{
  return (GstVulkanImageView *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (trash));
}

/**
 * gst_vulkan_image_view_unref: (skip)
 * @trash: (transfer full): a #GstVulkanImageView.
 *
 * Decreases the refcount of the trash object. If the refcount reaches 0, the
 * trash will be freed.
 */
static inline void gst_vulkan_image_view_unref(GstVulkanImageView* trash);
static inline void
gst_vulkan_image_view_unref (GstVulkanImageView * trash)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (trash));
}

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVulkanImageView, gst_vulkan_image_view_unref)
#endif

GST_VULKAN_API
GstVulkanImageView *    gst_vulkan_image_view_new           (GstVulkanImageMemory * image,
                                                             VkImageViewCreateInfo * create_info);

G_END_DECLS

#endif /* __GST_VULKAN_IMAGE_MEMORY_H__ */

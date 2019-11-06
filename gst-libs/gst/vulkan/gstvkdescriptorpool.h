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

#ifndef __GST_VULKAN_DESCRIPTOR_POOL_H__
#define __GST_VULKAN_DESCRIPTOR_POOL_H__

#include <gst/vulkan/gstvkqueue.h>

#define GST_TYPE_VULKAN_DESCRIPTOR_POOL         (gst_vulkan_descriptor_pool_get_type())
#define GST_VULKAN_DESCRIPTOR_POOL(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_DESCRIPTOR_POOL, GstVulkanDescriptorPool))
#define GST_VULKAN_DESCRIPTOR_POOL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_DESCRIPTOR_POOL, GstVulkanDescriptorPoolClass))
#define GST_IS_VULKAN_DESCRIPTOR_POOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_DESCRIPTOR_POOL))
#define GST_IS_VULKAN_DESCRIPTOR_POOL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_DESCRIPTOR_POOL))
#define GST_VULKAN_DESCRIPTOR_POOL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_DESCRIPTOR_POOL, GstVulkanDescriptorPoolClass))
GST_VULKAN_API
GType gst_vulkan_descriptor_pool_get_type       (void);

struct _GstVulkanDescriptorPool
{
  GstObject parent;

  GstVulkanDevice *device;

  VkDescriptorPool pool; /* hides a pointer */
};

struct _GstVulkanDescriptorPoolClass
{
  GstObjectClass parent_class;
};

GST_VULKAN_API
GstVulkanDescriptorPool *   gst_vulkan_descriptor_pool_new_wrapped      (GstVulkanDevice * device,
                                                                         VkDescriptorPool pool,
                                                                         gsize max_sets);

GST_VULKAN_API
GstVulkanDevice *           gst_vulkan_descriptor_pool_get_device       (GstVulkanDescriptorPool * pool);

GST_VULKAN_API
GstVulkanDescriptorSet *    gst_vulkan_descriptor_pool_create           (GstVulkanDescriptorPool * pool,
                                                                         guint n_layouts,
                                                                         GstVulkanHandle **layouts,
                                                                         GError ** error);
GST_VULKAN_API
gsize                       gst_vulkan_descriptor_pool_get_max_sets     (GstVulkanDescriptorPool * pool);

#endif /* __GST_VULKAN_DESCRIPTOR_POOL_H__ */

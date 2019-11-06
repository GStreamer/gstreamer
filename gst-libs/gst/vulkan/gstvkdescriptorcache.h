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

#ifndef __GST_VULKAN_DESCRIPTOR_CACHE_H__
#define __GST_VULKAN_DESCRIPTOR_CACHE_H__

#include <gst/vulkan/gstvkqueue.h>

#define GST_TYPE_VULKAN_DESCRIPTOR_CACHE         (gst_vulkan_descriptor_cache_get_type())
#define GST_VULKAN_DESCRIPTOR_CACHE(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_DESCRIPTOR_CACHE, GstVulkanDescriptorCache))
#define GST_VULKAN_DESCRIPTOR_CACHE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_DESCRIPTOR_CACHE, GstVulkanDescriptorCacheClass))
#define GST_IS_VULKAN_DESCRIPTOR_CACHE(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_DESCRIPTOR_CACHE))
#define GST_IS_VULKAN_DESCRIPTOR_CACHE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_DESCRIPTOR_CACHE))
#define GST_VULKAN_DESCRIPTOR_CACHE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_DESCRIPTOR_CACHE, GstVulkanDescriptorCacheClass))
GST_VULKAN_API
GType gst_vulkan_descriptor_cache_get_type       (void);

struct _GstVulkanDescriptorCache
{
  GstObject                     parent;

  GstVulkanDescriptorPool      *pool;
};

struct _GstVulkanDescriptorCacheClass
{
  GstObjectClass parent_class;
};

GST_VULKAN_API
GstVulkanDescriptorCache *   gst_vulkan_descriptor_cache_new            (GstVulkanDescriptorPool * pool,
                                                                         guint n_layouts,
                                                                         GstVulkanHandle ** layouts);

GST_VULKAN_API
GstVulkanDescriptorSet *    gst_vulkan_descriptor_cache_acquire         (GstVulkanDescriptorCache * cache,
                                                                         GError ** error);

#endif /* __GST_VULKAN_DESCRIPTOR_CACHE_H__ */

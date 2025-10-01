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

#ifndef __GST_VULKAN_COMMAND_POOL_H__
#define __GST_VULKAN_COMMAND_POOL_H__

#include <gst/vulkan/gstvkqueue.h>

#define GST_TYPE_VULKAN_COMMAND_POOL         (gst_vulkan_command_pool_get_type())
#define GST_VULKAN_COMMAND_POOL(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_COMMAND_POOL, GstVulkanCommandPool))
#define GST_VULKAN_COMMAND_POOL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_COMMAND_POOL, GstVulkanCommandPoolClass))
#define GST_IS_VULKAN_COMMAND_POOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_COMMAND_POOL))
#define GST_IS_VULKAN_COMMAND_POOL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_COMMAND_POOL))
#define GST_VULKAN_COMMAND_POOL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_COMMAND_POOL, GstVulkanCommandPoolClass))
GST_VULKAN_API
GType gst_vulkan_command_pool_get_type       (void);

/**
 * GstVulkanCommandPool:
 * @parent: the parent #GstObject
 * @queue: the #GstVulkanQueue to command buffers will be allocated from
 * @pool: the vulkan command pool handle
 *
 * Since: 1.18
 */
struct _GstVulkanCommandPool
{
  GstObject parent;

  GstVulkanQueue *queue;

  VkCommandPool pool; /* hides a pointer */

  /* <private> */
  gpointer _reserved        [GST_PADDING];
};

/**
 * GstVulkanCommandPoolClass:
 * @parent_class: the parent #GstObjectClass
 *
 * Since: 1.18
 */
struct _GstVulkanCommandPoolClass
{
  GstObjectClass parent_class;

  /* <private> */
  gpointer _reserved        [GST_PADDING];
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstVulkanCommandPool, gst_object_unref)

GST_VULKAN_API
GstVulkanQueue *        gst_vulkan_command_pool_get_queue           (GstVulkanCommandPool * pool) G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
GstVulkanCommandBuffer * gst_vulkan_command_pool_create             (GstVulkanCommandPool * pool,
                                                                     GError ** error) G_GNUC_WARN_UNUSED_RESULT;

GST_VULKAN_API
void                    gst_vulkan_command_pool_lock                (GstVulkanCommandPool * pool);
GST_VULKAN_API
void                    gst_vulkan_command_pool_unlock              (GstVulkanCommandPool * pool);

#endif /* __GST_VULKAN_COMMAND_POOL_H__ */

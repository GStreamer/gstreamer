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

#include "gstvkcommandpool.h"

/**
 * SECTION:vkcommandpool
 * @title: GstVulkanCommandPool
 * @short_description: Vulkan command pool
 * @see_also: #GstVulkanDevice
 */

#define GST_CAT_DEFAULT gst_vulkan_command_pool_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define parent_class gst_vulkan_command_pool_parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanCommandPool, gst_vulkan_command_pool,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "vulkancommandpool", 0, "Vulkan Command Pool"));

static void gst_vulkan_command_pool_dispose (GObject * object);

static void
gst_vulkan_command_pool_init (GstVulkanCommandPool * device)
{
}

static void
gst_vulkan_command_pool_class_init (GstVulkanCommandPoolClass * device_class)
{
  GObjectClass *gobject_class = (GObjectClass *) device_class;

  gobject_class->dispose = gst_vulkan_command_pool_dispose;
}

static void
gst_vulkan_command_pool_dispose (GObject * object)
{
  GstVulkanCommandPool *pool = GST_VULKAN_COMMAND_POOL (object);

  if (pool->pool)
    vkDestroyCommandPool (pool->queue->device->device, pool->pool, NULL);
  pool->pool = VK_NULL_HANDLE;

  if (pool->queue)
    gst_object_unref (pool->queue);
  pool->queue = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * gst_vulkan_command_pool_get_queue
 * @pool: a #GstVulkanCommandPool
 *
 * Returns: (transfer full): the parent #GstVulkanQueue for this command pool
 *
 * Since: 1.18
 */
GstVulkanQueue *
gst_vulkan_command_pool_get_queue (GstVulkanCommandPool * pool)
{
  g_return_val_if_fail (GST_IS_VULKAN_COMMAND_POOL (pool), NULL);

  return pool->queue ? gst_object_ref (pool->queue) : NULL;
}

/**
 * gst_vulkan_command_pool_create: (skip)
 * @pool: a #GstVulkanCommandPool
 * @error: a #GError
 *
 * Returns: a new primary VkCommandBuffer
 *
 * Since: 1.18
 */
VkCommandBuffer
gst_vulkan_command_pool_create (GstVulkanCommandPool * pool, GError ** error)
{
  VkResult err;
  VkCommandBufferAllocateInfo cmd_info = { 0, };
  VkCommandBuffer cmd;

  g_return_val_if_fail (GST_IS_VULKAN_COMMAND_POOL (pool), NULL);

  cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_info.pNext = NULL;
  cmd_info.commandPool = pool->pool;
  cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_info.commandBufferCount = 1;

  err = vkAllocateCommandBuffers (pool->queue->device->device, &cmd_info, &cmd);
  if (gst_vulkan_error_to_g_error (err, error, "vkCreateCommandBuffer") < 0)
    return NULL;

  GST_LOG_OBJECT (pool, "created cmd buffer %p", cmd);

  return cmd;
}

/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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

#include "gstvkfence.h"

/**
 * SECTION:vkfence
 * @title: GstVulkanFence
 * @short_description: Vulkan fences
 * @see_also: #GstVulkanDevice
 *
 * A #GstVulkanFence encapsulates a VkFence
 */

GST_DEBUG_CATEGORY (gst_debug_vulkan_fence);
#define GST_CAT_DEFAULT gst_debug_vulkan_fence

static void
_init_debug (void)
{
  static volatile gsize init;

  if (g_once_init_enter (&init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_fence,
        "vulkanfence", 0, "Vulkan Fence");
    g_once_init_leave (&init, 1);
  }
}

static void
gst_vulkan_fence_free (GstVulkanFence * fence)
{
  if (!fence)
    return;

  GST_TRACE ("Freeing fence %p", fence);

  if (fence->fence)
    vkDestroyFence (fence->device->device, fence->fence, NULL);

  gst_object_unref (fence->device);

  g_free (fence);
}

/**
 * gst_vulkan_fence_new:
 * @device: the parent #GstVulkanDevice
 * @flags: set of flags to create the fence with
 * @error: a #GError for the failure condition
 *
 * Returns: whether a new #GstVulkanFence or %NULL on error
 *
 * Since: 1.18
 */
GstVulkanFence *
gst_vulkan_fence_new (GstVulkanDevice * device, VkFenceCreateFlags flags,
    GError ** error)
{
  VkFenceCreateInfo fence_info = { 0, };
  GstVulkanFence *fence;
  VkResult err;

  _init_debug ();

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);

  fence = g_new0 (GstVulkanFence, 1);
  GST_TRACE ("Creating fence %p with device %" GST_PTR_FORMAT, fence, device);
  fence->device = gst_object_ref (device);

  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.pNext = NULL;
  fence_info.flags = flags;

  err = vkCreateFence (device->device, &fence_info, NULL, &fence->fence);
  if (gst_vulkan_error_to_g_error (err, error, "vkCreateFence") < 0) {
    gst_clear_object (&fence->device);
    g_free (fence);
    return NULL;
  }

  gst_mini_object_init (GST_MINI_OBJECT_CAST (fence), 0, GST_TYPE_VULKAN_FENCE,
      NULL, NULL, (GstMiniObjectFreeFunction) gst_vulkan_fence_free);

  return fence;
}

/**
 * gst_vulkan_fence_new_always_signalled:
 *
 * Returns: a new #GstVulkanFence that is always in the signalled state
 */
GstVulkanFence *
gst_vulkan_fence_new_always_signalled (GstVulkanDevice * device)
{
  GstVulkanFence *fence;

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);

  _init_debug ();

  fence = g_new0 (GstVulkanFence, 1);
  GST_TRACE ("Creating always-signalled fence %p with device %" GST_PTR_FORMAT,
      fence, device);
  fence->device = gst_object_ref (device);
  fence->fence = VK_NULL_HANDLE;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (fence), 0, GST_TYPE_VULKAN_FENCE,
      NULL, NULL, (GstMiniObjectFreeFunction) gst_vulkan_fence_free);

  return fence;
}

/**
 * gst_vulkan_fence_is_signaled:
 * @fence: a #GstVulkanFence
 *
 * Returns: whether @fence has been signalled
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_fence_is_signaled (GstVulkanFence * fence)
{
  g_return_val_if_fail (fence != NULL, FALSE);

  if (!fence->fence)
    return TRUE;

  return vkGetFenceStatus (fence->device->device, fence->fence) == VK_SUCCESS;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVulkanFence, gst_vulkan_fence);

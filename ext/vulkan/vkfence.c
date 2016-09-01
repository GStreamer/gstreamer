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

#include "vkfence.h"
#include "vkutils_private.h"

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

  vkDestroyFence (fence->device->device, fence->fence, NULL);

  gst_object_unref (fence->device);

  g_free (fence);
}

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
    g_free (fence);
    return NULL;
  }

  gst_mini_object_init (GST_MINI_OBJECT_CAST (fence), 0, GST_TYPE_VULKAN_FENCE,
      NULL, NULL, (GstMiniObjectFreeFunction) gst_vulkan_fence_free);

  return fence;
}

gboolean
gst_vulkan_fence_is_signaled (GstVulkanFence * fence)
{
  g_return_val_if_fail (fence != NULL, FALSE);

  return vkGetFenceStatus (fence->device->device, fence->fence) == VK_SUCCESS;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVulkanFence, gst_vulkan_fence);

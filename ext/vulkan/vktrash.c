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

#include "vktrash.h"
#include "vkutils_private.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_trash);
#define GST_CAT_DEFAULT gst_debug_vulkan_trash

static void
_init_debug (void)
{
  static volatile gsize init;

  if (g_once_init_enter (&init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_trash,
        "vulkantrash", 0, "Vulkan Trash");
    g_once_init_leave (&init, 1);
  }
}

void
gst_vulkan_trash_free (GstVulkanTrash * trash)
{
  if (!trash)
    return;

  GST_TRACE ("Freeing trash object %p with fence %" GST_PTR_FORMAT, trash,
      trash->fence);

  gst_vulkan_fence_unref (trash->fence);

  g_free (trash);
}

GstVulkanTrash *
gst_vulkan_trash_new (GstVulkanFence * fence, GstVulkanTrashNotify notify,
    gpointer user_data)
{
  GstVulkanTrash *ret = NULL;

  g_return_val_if_fail (fence != NULL, NULL);
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (fence->device), NULL);
  g_return_val_if_fail (notify != NULL, NULL);

  _init_debug ();

  ret = g_new0 (GstVulkanTrash, 1);
  GST_TRACE ("Creating new trash object %p with fence %" GST_PTR_FORMAT
      " on device %" GST_PTR_FORMAT, ret, fence, fence->device);
  ret->fence = fence;
  ret->notify = notify;
  ret->user_data = user_data;

  return ret;
}

GList *
gst_vulkan_trash_list_gc (GList * trash_list)
{
  GList *l = trash_list;

  while (l) {
    GstVulkanTrash *trash = l->data;

    if (gst_vulkan_fence_is_signaled (trash->fence)) {
      GList *next = g_list_next (l);
      GST_TRACE ("fence %" GST_PTR_FORMAT " has been signalled, notifying",
          trash->fence);
      trash->notify (trash->fence->device, trash->user_data);
      gst_vulkan_trash_free (trash);
      trash_list = g_list_delete_link (trash_list, l);
      l = next;
    } else {
      l = g_list_next (l);
    }
  }

  return trash_list;
}

gboolean
gst_vulkan_trash_list_wait (GList * trash_list, guint64 timeout)
{
  VkResult err = VK_SUCCESS;
  guint i, n;

  /* remove all the previously signaled fences */
  trash_list = gst_vulkan_trash_list_gc (trash_list);

  n = g_list_length (trash_list);
  if (n > 0) {
    VkFence *fences;
    GstVulkanDevice *device = NULL;
    GList *l = NULL;

    fences = g_new0 (VkFence, n);
    for (i = 0, l = trash_list; i < n; i++, l = g_list_next (l)) {
      GstVulkanTrash *trash = l->data;

      if (device == NULL)
        device = trash->fence->device;

      fences[i] = trash->fence->fence;

      /* only support waiting on fences from the same device */
      g_assert (device == trash->fence->device);
    }

    GST_TRACE ("Waiting on %d fences with timeout %" GST_TIME_FORMAT, n,
        GST_TIME_ARGS (timeout));
    err = vkWaitForFences (device->device, n, fences, TRUE, timeout);
    g_free (fences);

    trash_list = gst_vulkan_trash_list_gc (trash_list);
  }

  return err == VK_SUCCESS;
}

static void
_free_command_buffer (GstVulkanDevice * device, VkCommandBuffer * cmd)
{
  g_assert (cmd);
  vkFreeCommandBuffers (device->device, device->cmd_pool, 1, cmd);

  g_free (cmd);
}

GstVulkanTrash *
gst_vulkan_trash_new_free_command_buffer (GstVulkanFence * fence,
    VkCommandBuffer cmd)
{
  VkCommandBuffer *data;
  GstVulkanTrash *trash;

  data = g_new0 (VkCommandBuffer, 1);
  *data = cmd;
  trash = gst_vulkan_trash_new (fence,
      (GstVulkanTrashNotify) _free_command_buffer, data);

  return trash;
}

static void
_free_semaphore (GstVulkanDevice * device, VkSemaphore * semaphore)
{
  if (semaphore)
    vkDestroySemaphore (device->device, *semaphore, NULL);

  g_free (semaphore);
}

GstVulkanTrash *
gst_vulkan_trash_new_free_semaphore (GstVulkanFence * fence,
    VkSemaphore semaphore)
{
  VkSemaphore *data;
  GstVulkanTrash *trash;

  data = g_new0 (VkSemaphore, 1);
  *data = semaphore;
  trash = gst_vulkan_trash_new (fence,
      (GstVulkanTrashNotify) _free_semaphore, data);

  return trash;
}

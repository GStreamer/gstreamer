/*
 * GStreamer
 * Copyright (C) 2026 Matthew Waters <matthew@centricular.com>
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

#include "gstvktimelinesemaphore.h"
#include "gstvkphysicaldevice.h"

#define GST_CAT_DEFAULT gst_debug_vulkan_timeline_semaphore
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

static void
init_debug (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkantimelinesemaphore", 0,
        "Vulkan timeline semaphore");
    g_once_init_leave (&_init, 1);
  }
}

static void
gst_vulkan_timeline_semaphore_free (GstVulkanTimelineSemaphore * timeline)
{
  gst_clear_vulkan_handle (&timeline->semaphore);
  g_mutex_clear (&timeline->lock);

  g_free (timeline);
}

static void
gst_vulkan_timeline_semaphore_init (GstVulkanTimelineSemaphore * timeline,
    GstVulkanDevice * device, VkSemaphore semaphore)
{
  init_debug ();

  GST_TRACE ("new %p", timeline);

  gst_mini_object_init (&timeline->parent, 0,
      GST_TYPE_VULKAN_TIMELINE_SEMAPHORE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vulkan_timeline_semaphore_free);
  timeline->semaphore =
      gst_vulkan_handle_new_wrapped (device, GST_VULKAN_HANDLE_TYPE_SEMAPHORE,
      semaphore, gst_vulkan_handle_free_semaphore, NULL);
  g_mutex_init (&timeline->lock);
  timeline->value = 0;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVulkanTimelineSemaphore,
    gst_vulkan_timeline_semaphore);

GstVulkanTimelineSemaphore *
gst_vulkan_timeline_semaphore_new (GstVulkanDevice * device)
{
#if defined(VK_KHR_timeline_semaphore)
  if (gst_vulkan_physical_device_check_api_version (device->physical_device, 1,
          2, 0) || gst_vulkan_device_is_extension_enabled (device,
          VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkSemaphoreTypeCreateInfo semaphore_type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0,
    };
    VkSemaphoreCreateInfo semaphore_create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info,
    };
    VkResult err =
        vkCreateSemaphore (device->device, &semaphore_create_info, NULL,
        &semaphore);
    GError *error = NULL;

    if (gst_vulkan_error_to_g_error (err, &error, "vkCreateSemaphore") < 0) {
      GST_WARNING_OBJECT (device, "Failed to create timeline semaphore: %s",
          error->message);
      return NULL;
    }

    GstVulkanTimelineSemaphore *ret = g_new0 (GstVulkanTimelineSemaphore, 1);
    gst_vulkan_timeline_semaphore_init (ret, device, semaphore);
    return ret;
  }
#endif

  return NULL;
}

/**
 * gst_vulkan_timeline_semaphore_lock:
 * @timeline: a #GstVulkanTimelineSemaphore to lock
 *
 * Exclusively lock the timeline semaphore.
 *
 * Since: 1.30
 */
void
gst_vulkan_timeline_semaphore_lock (GstVulkanTimelineSemaphore * timeline)
{
  g_mutex_lock (&timeline->lock);
  GST_TRACE ("locked timeline semaphore %p", timeline);
}

/**
 * gst_vulkan_timeline_semaphore_unlock:
 * @timeline: a #GstVulkanTimelineSemaphore to unlock
 *
 * Exclusively unlock the timeline semaphore.
 *
 * Since: 1.30
 */
void
gst_vulkan_timeline_semaphore_unlock (GstVulkanTimelineSemaphore * timeline)
{
  g_mutex_unlock (&timeline->lock);
  GST_TRACE ("unlocked timeline semaphore %p", timeline);
}

/**
 * gst_vulkan_timeline_semaphore_peek_unlocked:
 * @timeline: a #GstVulkanTimelineSemaphore to retrieve the value of.
 *
 * Since: 1.30
 */
guint64
gst_vulkan_timeline_semaphore_peek_unlocked (GstVulkanTimelineSemaphore *
    timeline)
{
  GST_TRACE ("timeline semaphore %p peek %" G_GUINT64_FORMAT, timeline,
      timeline->value);
  return timeline->value;
}

/**
 * gst_vulkan_timeline_semaphore_compare_exchange_unlocked:
 * @timeline: a #GstVulkanTimelineSemaphore to retrieve the value of.
 * @old_value: the expected value of the timeline semaphore.
 * @new_value: the new value to set on the timeline semaphore.
 *
 * Since: 1.30
 */
gboolean
    gst_vulkan_timeline_semaphore_compare_exchange_unlocked
    (GstVulkanTimelineSemaphore * timeline, guint64 old_value,
    guint64 new_value) {
  if (timeline->value != old_value)
    return FALSE;

  GST_TRACE ("timeline semaphore %p update from %" G_GUINT64_FORMAT " to %"
      G_GUINT64_FORMAT, timeline, timeline->value, new_value);
  timeline->value = new_value;
  return TRUE;
}

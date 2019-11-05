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

/**
 * SECTION:vulkancommandbuffer
 * @title: vulkancommandbuffer
 *
 * vulkancommandbuffer holds information about a command buffer.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvkhandle.h"
#include "gstvkdevice.h"

#define GST_CAT_DEFAULT gst_debug_vulkan_handle
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

static void
init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkanhandle", 0,
        "Vulkan handle");
    g_once_init_leave (&_init, 1);
  }
}

static void
gst_vulkan_handle_free (GstVulkanHandle * handle)
{
  GST_TRACE ("Freeing %p", handle);

  if (handle->notify)
    handle->notify (handle, handle->user_data);

  gst_clear_object (&handle->device);

  g_free (handle);
}

static void
gst_vulkan_handle_init (GstVulkanHandle * handle, GstVulkanDevice * device,
    GstVulkanHandleType type, GstVulkanHandleTypedef handle_val,
    GstVulkanHandleDestroyNotify notify, gpointer user_data)
{
  handle->device = gst_object_ref (device);
  handle->type = type;
  handle->handle = handle_val;
  handle->notify = notify;
  handle->user_data = user_data;

  init_debug ();

  GST_TRACE ("new %p", handle);

  gst_mini_object_init (&handle->parent, 0, GST_TYPE_VULKAN_HANDLE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vulkan_handle_free);
}

/**
 * gst_vulkan_handle_new_wrapped:
 * @handle: a Vulkan handle
 * @destroy: a #GDestroyNotify
 * @user_data: data to pass to @notify
 *
 * Returns: (transfer full): a new #GstVulkanHandle wrapping @handle
 */
GstVulkanHandle *
gst_vulkan_handle_new_wrapped (GstVulkanDevice * device,
    GstVulkanHandleType type, GstVulkanHandleTypedef handle,
    GstVulkanHandleDestroyNotify notify, gpointer user_data)
{
  GstVulkanHandle *ret;

  ret = g_new0 (GstVulkanHandle, 1);
  gst_vulkan_handle_init (ret, device, type, handle, notify, user_data);

  return ret;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVulkanHandle, gst_vulkan_handle);

void
gst_vulkan_handle_free_descriptor_set_layout (GstVulkanHandle * handle,
    gpointer user_data)
{
  vkDestroyDescriptorSetLayout (handle->device->device,
      (VkDescriptorSetLayout) handle->handle, NULL);
}

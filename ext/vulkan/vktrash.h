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

#ifndef _VK_TRASH_H_
#define _VK_TRASH_H_

#include <vk.h>

G_BEGIN_DECLS

typedef void (*GstVulkanTrashNotify) (GstVulkanDevice * device, gpointer user_data);

struct _GstVulkanTrash
{
  GstVulkanFence       *fence;

  GstVulkanTrashNotify  notify;
  gpointer              user_data;
};

GstVulkanTrash *    gst_vulkan_trash_new                        (GstVulkanFence * fence,
                                                                 GstVulkanTrashNotify notify,
                                                                 gpointer user_data);
GstVulkanTrash *    gst_vulkan_trash_new_free_command_buffer    (GstVulkanFence * fence,
                                                                 VkCommandBuffer cmd);
GstVulkanTrash *    gst_vulkan_trash_new_free_semaphore         (GstVulkanFence * fence,
                                                                 VkSemaphore cmd);
void                gst_vulkan_trash_free                       (GstVulkanTrash * trash);

GList *             gst_vulkan_trash_list_gc                    (GList * trash_list);
gboolean            gst_vulkan_trash_list_wait                  (GList * trash_list,
                                                                 guint64 timeout);

G_END_DECLS

#endif /* _VK_INSTANCE_H_ */

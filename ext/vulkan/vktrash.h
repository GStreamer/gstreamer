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

#ifndef __GST_VULKAN_TRASH_H__
#define __GST_VULKAN_TRASH_H__

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

typedef void (*GstVulkanTrashNotify) (GstVulkanDevice * device, gpointer user_data);

typedef struct _GstVulkanTrash GstVulkanTrash;

struct _GstVulkanTrash
{
  GstVulkanFence       *fence;

  GstVulkanTrashNotify  notify;
  gpointer              user_data;
};

GstVulkanTrash *    gst_vulkan_trash_new                            (GstVulkanFence * fence,
                                                                     GstVulkanTrashNotify notify,
                                                                     gpointer user_data);

GstVulkanTrash *    gst_vulkan_trash_new_free_descriptor_pool       (GstVulkanFence * fence,
                                                                     VkDescriptorPool descriptor_pool);
GstVulkanTrash *    gst_vulkan_trash_new_free_descriptor_set_layout (GstVulkanFence * fence,
                                                                     VkDescriptorSetLayout descriptor_set_layout);
GstVulkanTrash *    gst_vulkan_trash_new_free_framebuffer           (GstVulkanFence * fence,
                                                                     VkFramebuffer framebuffer);
GstVulkanTrash *    gst_vulkan_trash_new_free_pipeline              (GstVulkanFence * fence,
                                                                     VkPipeline pipeline);
GstVulkanTrash *    gst_vulkan_trash_new_free_pipeline_layout       (GstVulkanFence * fence,
                                                                     VkPipelineLayout pipeline_layout);
GstVulkanTrash *    gst_vulkan_trash_new_free_render_pass           (GstVulkanFence * fence,
                                                                     VkRenderPass render_pass);
GstVulkanTrash *    gst_vulkan_trash_new_free_sampler               (GstVulkanFence * fence,
                                                                     VkSampler sampler);
GstVulkanTrash *    gst_vulkan_trash_new_free_semaphore             (GstVulkanFence * fence,
                                                                     VkSemaphore semaphore);

GstVulkanTrash *    gst_vulkan_trash_new_free_command_buffer        (GstVulkanFence * fence,
                                                                     GstVulkanCommandPool * parent,
                                                                     VkCommandBuffer command_buffer);
GstVulkanTrash *    gst_vulkan_trash_new_free_descriptor_set        (GstVulkanFence * fence,
                                                                     VkDescriptorPool parent,
                                                                     VkDescriptorSet descriptor_set);

GstVulkanTrash *    gst_vulkan_trash_new_object_unref               (GstVulkanFence * fence,
                                                                     GstObject * object);

void                gst_vulkan_trash_free                           (GstVulkanTrash * trash);

GList *             gst_vulkan_trash_list_gc                        (GList * trash_list);
gboolean            gst_vulkan_trash_list_wait                      (GList * trash_list,
                                                                     guint64 timeout);

G_END_DECLS

#endif /* __GST_VULKAN_TRASH_H__ */

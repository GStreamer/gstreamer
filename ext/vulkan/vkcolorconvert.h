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

#ifndef _VK_COLOR_CONVERT_H_
#define _VK_COLOR_CONVERT_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>
#include "vkfullscreenrender.h"

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_COLOR_CONVERT            (gst_vulkan_color_convert_get_type())
#define GST_VULKAN_COLOR_CONVERT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VULKAN_COLOR_CONVERT,GstVulkanColorConvert))
#define GST_VULKAN_COLOR_CONVERT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VULKAN_COLOR_CONVERT,GstVulkanColorConvertClass))
#define GST_IS_VULKAN_COLOR_CONVERT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VULKAN_COLOR_CONVERT))
#define GST_IS_VULKAN_COLOR_CONVERT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VULKAN_COLOR_CONVERT))

typedef struct _GstVulkanColorConvert GstVulkanColorConvert;
typedef struct _GstVulkanColorConvertClass GstVulkanColorConvertClass;

#define MAX_PUSH_CONSTANTS 4

typedef struct _shader_info shader_info;

typedef gboolean (*CommandStateUpdate) (GstVulkanColorConvert * conv, VkCommandBuffer cmd, shader_info * sinfo, GstVulkanImageView ** src_views, GstVulkanImageView ** dest_views, GstVulkanFence * fence);

struct _shader_info
{
  GstVideoFormat from;
  GstVideoFormat to;
  CommandStateUpdate cmd_state_update;
  gchar *frag_code;
  gsize frag_size;
  VkPushConstantRange push_constant_ranges[MAX_PUSH_CONSTANTS];
  gsize uniform_size;
  GDestroyNotify notify;
  gpointer user_data;
};

struct _GstVulkanColorConvert
{
  GstVulkanFullScreenRender         parent;

  GstVulkanCommandPool             *cmd_pool;

  VkSampler                         sampler;
  GstVulkanDescriptorCache         *descriptor_pool;

  VkShaderModule                    vert_module;
  VkShaderModule                    frag_module;

  VkDescriptorSetLayoutBinding      sampler_layout_binding;
  VkDescriptorSetLayoutCreateInfo   layout_info;

  shader_info                      *current_shader;
  GstMemory                        *uniform;
};

struct _GstVulkanColorConvertClass
{
  GstVulkanFullScreenRenderClass parent_class;
};

GType gst_vulkan_color_convert_get_type(void);

G_END_DECLS

#endif

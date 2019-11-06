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

#ifndef _VK_FULL_SCREEN_RENDER_H_
#define _VK_FULL_SCREEN_RENDER_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_FULL_SCREEN_RENDER              (gst_vulkan_full_screen_render_get_type())
#define GST_VULKAN_FULL_SCREEN_RENDER(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VULKAN_FULL_SCREEN_RENDER,GstVulkanFullScreenRender))
#define GST_VULKAN_FULL_SCREEN_RENDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VULKAN_FULL_SCREEN_RENDER,GstVulkanFullScreenRenderClass))
#define GST_VULKAN_FULL_SCREEN_RENDER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VULKAN_FULL_SCREEN_RENDER,GstVulkanFullScreenRenderClass))
#define GST_IS_VULKAN_FULL_SCREEN_RENDER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VULKAN_FULL_SCREEN_RENDER))
#define GST_IS_VULKAN_FULL_SCREEN_RENDER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VULKAN_FULL_SCREEN_RENDER))

typedef struct _GstVulkanFullScreenRender GstVulkanFullScreenRender;
typedef struct _GstVulkanFullScreenRenderClass GstVulkanFullScreenRenderClass;

typedef void (*GstVulkanFullScreenRenderDestroyInfoNotify)
    (GstVulkanFullScreenRender * render, gpointer info);

struct Vertex
{
  gfloat x, y, z;
  gfloat s, t;
};

struct _GstVulkanFullScreenRender
{
  GstBaseTransform      parent;

  GstVulkanInstance    *instance;
  GstVulkanDevice      *device;
  GstVulkanQueue       *queue;

  GstCaps              *in_caps;
  GstVideoInfo          in_info;
  GstCaps              *out_caps;
  GstVideoInfo          out_info;

  guint                 n_shader_stages;
  VkPipelineShaderStageCreateInfo *shader_create_info;
  GstVulkanFullScreenRenderDestroyInfoNotify destroy_shader_create_info;

  VkRenderPass          render_pass;
  VkPipelineLayout      pipeline_layout;
  VkPipeline            graphics_pipeline;
  GstVulkanHandle      *descriptor_set_layout;

  GstMemory            *vertices;
  GstMemory            *indices;

  GstVulkanTrashList   *trash_list;
  GstVulkanFence       *last_fence;
};

struct _GstVulkanFullScreenRenderClass
{
  GstBaseTransformClass video_sink_class;

  void                                 (*shader_create_info)                    (GstVulkanFullScreenRender * render);
  VkDescriptorSetLayoutBinding *       (*descriptor_set_layout_bindings)        (GstVulkanFullScreenRender * render, guint * n_bindings);
  VkAttachmentReference *              (*render_pass_attachment_references)     (GstVulkanFullScreenRender * render, guint * n_refs);
  VkAttachmentDescription *            (*render_pass_attachment_descriptions)   (GstVulkanFullScreenRender * render, guint * n_descriptions);
  VkPushConstantRange *                (*push_constant_ranges)                  (GstVulkanFullScreenRender * render, guint * n_constants);
};

gboolean gst_vulkan_full_screen_render_fill_command_buffer (GstVulkanFullScreenRender * render, VkCommandBuffer cmd, VkFramebuffer framebuffer);
gboolean gst_vulkan_full_screen_render_submit (GstVulkanFullScreenRender * render, VkCommandBuffer cmd, GstVulkanFence *fence);

GType gst_vulkan_full_screen_render_get_type(void);

G_END_DECLS

#endif

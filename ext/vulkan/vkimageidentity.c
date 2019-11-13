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
 * SECTION:element-vulkanimageidentity
 * @title: vulkanimgeidentity
 *
 * vulkanimageidentity produces a vulkan image that is a copy of the input image.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "vkimageidentity.h"
#include "vkshader.h"
#include "vkelementutils.h"

#include "shaders/identity.vert.h"
#include "shaders/identity.frag.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_image_identity);
#define GST_CAT_DEFAULT gst_debug_vulkan_image_identity

static gboolean gst_vulkan_image_identity_start (GstBaseTransform * bt);
static gboolean gst_vulkan_image_identity_stop (GstBaseTransform * bt);

static GstCaps *gst_vulkan_image_identity_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstFlowReturn gst_vulkan_image_identity_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_vulkan_image_identity_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);

static VkAttachmentReference
    * gst_vulkan_image_identity_render_pass_attachment_references
    (GstVulkanFullScreenRender * render, guint * n_attachments);
static VkAttachmentDescription
    * gst_vulkan_image_identity_render_pass_attachment_descriptions
    (GstVulkanFullScreenRender * render, guint * n_descriptions);
static VkDescriptorSetLayoutBinding
    * gst_vulkan_image_identity_descriptor_set_layout_bindings
    (GstVulkanFullScreenRender * render, guint * n_bindings);
static void
gst_vulkan_image_identity_shader_create_info (GstVulkanFullScreenRender *
    render);

#define IMAGE_FORMATS " { BGRA }"

static GstStaticPadTemplate gst_vulkan_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
            IMAGE_FORMATS)));

static GstStaticPadTemplate gst_vulkan_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
            IMAGE_FORMATS)));

enum
{
  PROP_0,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* static guint gst_vulkan_image_identity_signals[LAST_SIGNAL] = { 0 }; */

#define gst_vulkan_image_identity_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanImageIdentity, gst_vulkan_image_identity,
    GST_TYPE_VULKAN_FULL_SCREEN_RENDER,
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_image_identity,
        "vulkanimageidentity", 0, "Vulkan Image identity"));

static void
gst_vulkan_image_identity_class_init (GstVulkanImageIdentityClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;
  GstVulkanFullScreenRenderClass *fullscreenrender_class;

  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;
  fullscreenrender_class = (GstVulkanFullScreenRenderClass *) klass;

  gst_element_class_set_metadata (gstelement_class, "Vulkan Uploader",
      "Filter/Video", "A Vulkan image copier",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_src_template);

  gstbasetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_vulkan_image_identity_start);
  gstbasetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_vulkan_image_identity_stop);
  gstbasetransform_class->transform_caps =
      gst_vulkan_image_identity_transform_caps;
  gstbasetransform_class->set_caps = gst_vulkan_image_identity_set_caps;
  gstbasetransform_class->transform = gst_vulkan_image_identity_transform;

  fullscreenrender_class->render_pass_attachment_references =
      gst_vulkan_image_identity_render_pass_attachment_references;
  fullscreenrender_class->render_pass_attachment_descriptions =
      gst_vulkan_image_identity_render_pass_attachment_descriptions;
  fullscreenrender_class->descriptor_set_layout_bindings =
      gst_vulkan_image_identity_descriptor_set_layout_bindings;
  fullscreenrender_class->shader_create_info =
      gst_vulkan_image_identity_shader_create_info;
}

static void
gst_vulkan_image_identity_init (GstVulkanImageIdentity * vk_identity)
{
}

static GstCaps *
gst_vulkan_image_identity_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *result, *tmp;

  tmp = gst_caps_copy (caps);

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  return result;
}

static void
destroy_shader_create_info (GstVulkanFullScreenRender * render, gpointer data)
{
  VkPipelineShaderStageCreateInfo *info = data;
  int i;

  for (i = 0; i < render->n_shader_stages; i++) {
    vkDestroyShaderModule (render->device->device, info[i].module, NULL);
  }

  g_free (info);
}

static void
gst_vulkan_image_identity_shader_create_info (GstVulkanFullScreenRender *
    render)
{
  VkShaderModule vert_module, frag_module;

  vert_module =
      _vk_create_shader (render->device, identity_vert, identity_vert_size,
      NULL);
  frag_module =
      _vk_create_shader (render->device, identity_frag, identity_frag_size,
      NULL);

  render->n_shader_stages = 2;
  render->shader_create_info =
      g_new0 (VkPipelineShaderStageCreateInfo, render->n_shader_stages);
  render->destroy_shader_create_info = destroy_shader_create_info;

  /* *INDENT-OFF* */
  render->shader_create_info[0] = (VkPipelineShaderStageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_module,
      .pName = "main"
  };

  render->shader_create_info[1] = (VkPipelineShaderStageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_module,
      .pName = "main"
  };
}

static VkDescriptorSetLayoutBinding *
gst_vulkan_image_identity_descriptor_set_layout_bindings (GstVulkanFullScreenRender * render, guint * n_bindings)
{
  VkDescriptorSetLayoutBinding *bindings;

  *n_bindings = 1;
  bindings = g_new0 (VkDescriptorSetLayoutBinding, *n_bindings);

  /* *INDENT-OFF* */
  bindings[0] = (VkDescriptorSetLayoutBinding) {
      .binding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImmutableSamplers = NULL,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
  };
  /* *INDENT-ON* */

  return bindings;
}

static VkAttachmentReference
    * gst_vulkan_image_identity_render_pass_attachment_references
    (GstVulkanFullScreenRender * render, guint * n_attachments)
{
  VkAttachmentReference *attachments;

  *n_attachments = 1;
  attachments = g_new0 (VkAttachmentReference, *n_attachments);
  /* *INDENT-OFF* */
  attachments[0] = (VkAttachmentReference) {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };
  /* *INDENT-ON* */

  return attachments;
}

static VkAttachmentDescription
    * gst_vulkan_image_identity_render_pass_attachment_descriptions
    (GstVulkanFullScreenRender * render, guint * n_descriptions)
{
  VkAttachmentDescription *color_attachments;

  *n_descriptions = 1;
  color_attachments = g_new0 (VkAttachmentDescription, *n_descriptions);
  /* *INDENT-OFF* */
  color_attachments[0] = (VkAttachmentDescription) {
      .format = gst_vulkan_format_from_video_info (&render->in_info, 0),
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      /* FIXME: share this between elements to avoid pipeline barriers */
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };
  /* *INDENT-ON* */

  return color_attachments;
}

static GstVulkanDescriptorCache *
_create_descriptor_pool (GstVulkanImageIdentity * vk_identity)
{
  GstVulkanFullScreenRender *render =
      GST_VULKAN_FULL_SCREEN_RENDER (vk_identity);
  guint max_sets = 32;          /* FIXME: Don't hardcode this! */

  /* *INDENT-OFF* */
  VkDescriptorPoolSize pool_sizes = {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = max_sets
  };

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = NULL,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_sizes,
      .maxSets = max_sets
  };
  /* *INDENT-ON* */
  VkDescriptorPool pool;
  GstVulkanDescriptorPool *ret;
  GstVulkanDescriptorCache *cache;
  GError *error = NULL;
  VkResult err;

  err =
      vkCreateDescriptorPool (render->device->device, &pool_info, NULL, &pool);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateDescriptorPool") < 0) {
    GST_ERROR_OBJECT (render, "Failed to create descriptor pool: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  ret = gst_vulkan_descriptor_pool_new_wrapped (render->device, pool, max_sets);
  cache =
      gst_vulkan_descriptor_cache_new (ret, 1, &render->descriptor_set_layout);
  gst_object_unref (ret);

  return cache;
}

static gboolean
gst_vulkan_image_identity_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);
  GstVulkanFence *last_fence;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->set_caps (bt, in_caps,
          out_caps))
    return FALSE;

  if (render->last_fence)
    last_fence = gst_vulkan_fence_ref (render->last_fence);
  else
    last_fence = gst_vulkan_fence_new_always_signalled (render->device);

  if (vk_identity->descriptor_pool)
    gst_vulkan_trash_list_add (render->trash_list,
        gst_vulkan_trash_new_object_unref (last_fence,
            (GstObject *) vk_identity->descriptor_pool));
  vk_identity->descriptor_pool = NULL;

  gst_vulkan_fence_unref (last_fence);

  if (!(vk_identity->descriptor_pool = _create_descriptor_pool (vk_identity)))
    return FALSE;

  return TRUE;
}

static VkSampler
_create_sampler (GstVulkanImageIdentity * vk_identity)
{
  GstVulkanFullScreenRender *render =
      GST_VULKAN_FULL_SCREEN_RENDER (vk_identity);

  /* *INDENT-OFF* */
  VkSamplerCreateInfo samplerInfo = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1,
      .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
      .compareEnable = VK_FALSE,
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .mipLodBias = 0.0f,
      .minLod = 0.0f,
      .maxLod = 0.0f
  };
  /* *INDENT-ON* */
  GError *error = NULL;
  VkSampler sampler;
  VkResult err;

  err = vkCreateSampler (render->device->device, &samplerInfo, NULL, &sampler);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateSampler") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create sampler: %s",
        error->message);
    g_clear_error (&error);
    return VK_NULL_HANDLE;
  }

  return sampler;
}

static gboolean
gst_vulkan_image_identity_start (GstBaseTransform * bt)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->start (bt))
    return FALSE;

  if (!(vk_identity->sampler = _create_sampler (vk_identity)))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vulkan_image_identity_stop (GstBaseTransform * bt)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);

  if (render->device) {
    GstVulkanFence *last_fence;

    if (render->last_fence)
      last_fence = gst_vulkan_fence_ref (render->last_fence);
    else
      last_fence = gst_vulkan_fence_new_always_signalled (render->device);

    if (vk_identity->descriptor_pool)
      gst_vulkan_trash_list_add (render->trash_list,
          gst_vulkan_trash_new_object_unref (last_fence,
              (GstObject *) vk_identity->descriptor_pool));
    vk_identity->descriptor_pool = NULL;
    if (vk_identity->sampler)
      gst_vulkan_trash_list_add (render->trash_list,
          gst_vulkan_trash_new_free_sampler (last_fence, vk_identity->sampler));
    vk_identity->sampler = VK_NULL_HANDLE;

    gst_vulkan_fence_unref (last_fence);
  }

  if (vk_identity->cmd_pool)
    gst_object_unref (vk_identity->cmd_pool);
  vk_identity->cmd_pool = VK_NULL_HANDLE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (bt);
}

static void
update_descriptor_set (GstVulkanImageIdentity * vk_identity,
    VkDescriptorSet set, VkImageView view)
{
  GstVulkanFullScreenRender *render =
      GST_VULKAN_FULL_SCREEN_RENDER (vk_identity);

  /* *INDENT-OFF* */
  VkDescriptorImageInfo image_info = {
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .imageView = view,
      .sampler = vk_identity->sampler
  };

  VkWriteDescriptorSet writes = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = NULL,
      .dstSet = set,
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .pImageInfo = &image_info
  };
  /* *INDENT-ON* */
  vkUpdateDescriptorSets (render->device->device, 1, &writes, 0, NULL);
}

static VkFramebuffer
_create_framebuffer (GstVulkanImageIdentity * vk_identity, VkImageView view)
{
  GstVulkanFullScreenRender *render =
      GST_VULKAN_FULL_SCREEN_RENDER (vk_identity);

  /* *INDENT-OFF* */
  VkImageView attachments[] = {
    view,
  };
  VkFramebufferCreateInfo framebuffer_info = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = render->render_pass,
      .attachmentCount = 1,
      .pAttachments = attachments,
      .width = GST_VIDEO_INFO_WIDTH (&render->in_info),
      .height = GST_VIDEO_INFO_HEIGHT (&render->in_info),
      .layers = 1
  };
  /* *INDENT-ON* */
  VkFramebuffer framebuffer;
  GError *error = NULL;
  VkResult err;

  err =
      vkCreateFramebuffer (render->device->device, &framebuffer_info, NULL,
      &framebuffer);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateFramebuffer") < 0) {
    GST_ERROR_OBJECT (render, "Failed to create framebuffer: %s",
        error->message);
    g_clear_error (&error);
    return VK_NULL_HANDLE;
  }

  return framebuffer;
}

static GstFlowReturn
gst_vulkan_image_identity_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);
  GstVulkanImageMemory *in_img_mem, *out_img_mem;
  GstVulkanImageView *in_img_view, *out_img_view;
  GstVulkanFence *fence = NULL;
  GstVulkanDescriptorSet *set;
  GstMemory *in_mem, *out_mem;
  VkFramebuffer framebuffer;
  GstVulkanCommandBuffer *cmd_buf;
  GError *error = NULL;
  VkResult err;

  fence = gst_vulkan_fence_new (render->device, 0, &error);
  if (!fence)
    goto error;

  in_mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_vulkan_image_memory (in_mem)) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Input memory must be a GstVulkanImageMemory");
    goto error;
  }
  in_img_mem = (GstVulkanImageMemory *) in_mem;
  in_img_view = get_or_create_image_view (in_img_mem);
  gst_vulkan_trash_list_add (render->trash_list,
      gst_vulkan_trash_new_mini_object_unref (fence,
          GST_MINI_OBJECT_CAST (in_img_view)));

  out_mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_vulkan_image_memory (out_mem)) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Input memory must be a GstVulkanImageMemory");
    goto error;
  }
  out_img_mem = (GstVulkanImageMemory *) out_mem;
  out_img_view = get_or_create_image_view (out_img_mem);
  gst_vulkan_trash_list_add (render->trash_list,
      gst_vulkan_trash_new_mini_object_unref (fence,
          GST_MINI_OBJECT_CAST (out_img_view)));

  if (!vk_identity->cmd_pool) {
    if (!(vk_identity->cmd_pool =
            gst_vulkan_queue_create_command_pool (render->queue, &error)))
      goto error;
  }
  if (!(set =
          gst_vulkan_descriptor_cache_acquire (vk_identity->descriptor_pool,
              &error)))
    goto error;
  update_descriptor_set (vk_identity, set->set, in_img_view->view);

  if (!(cmd_buf =
          gst_vulkan_command_pool_create (vk_identity->cmd_pool, &error)))
    goto error;

  if (!(framebuffer = _create_framebuffer (vk_identity, out_img_view->view))) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Failed to create framebuffer");
    goto error;
  }


  {
    VkCommandBufferBeginInfo cmd_buf_info = { 0, };

    /* *INDENT-OFF* */
    cmd_buf_info = (VkCommandBufferBeginInfo) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL
    };
    /* *INDENT-ON* */

    gst_vulkan_command_buffer_lock (cmd_buf);
    err = vkBeginCommandBuffer (cmd_buf->cmd, &cmd_buf_info);
    if (gst_vulkan_error_to_g_error (err, &error, "vkBeginCommandBuffer") < 0)
      goto unlock_error;
  }

  {
    /* *INDENT-OFF* */
    VkImageMemoryBarrier in_image_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = in_img_mem->barrier.parent.access_flags,
        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .oldLayout = in_img_mem->barrier.image_layout,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        /* FIXME: implement exclusive transfers */
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .image = in_img_mem->image,
        .subresourceRange = in_img_mem->barrier.subresource_range
    };

    VkImageMemoryBarrier out_image_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = out_img_mem->barrier.parent.access_flags,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = out_img_mem->barrier.image_layout,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        /* FIXME: implement exclusive transfers */
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .image = out_img_mem->image,
        .subresourceRange = out_img_mem->barrier.subresource_range
    };
    /* *INDENT-ON* */

    vkCmdPipelineBarrier (cmd_buf->cmd,
        in_img_mem->barrier.parent.pipeline_stages,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &in_image_memory_barrier);

    in_img_mem->barrier.parent.pipeline_stages =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    in_img_mem->barrier.parent.access_flags =
        in_image_memory_barrier.dstAccessMask;
    in_img_mem->barrier.image_layout = in_image_memory_barrier.newLayout;

    vkCmdPipelineBarrier (cmd_buf->cmd,
        out_img_mem->barrier.parent.pipeline_stages,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
        &out_image_memory_barrier);

    out_img_mem->barrier.parent.pipeline_stages =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    out_img_mem->barrier.parent.access_flags =
        out_image_memory_barrier.dstAccessMask;
    out_img_mem->barrier.image_layout = out_image_memory_barrier.newLayout;
  }

  vkCmdBindDescriptorSets (cmd_buf->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
      render->pipeline_layout, 0, 1, &set->set, 0, NULL);
  if (!gst_vulkan_full_screen_render_fill_command_buffer (render, cmd_buf->cmd,
          framebuffer))
    goto unlock_error;

  err = vkEndCommandBuffer (cmd_buf->cmd);
  gst_vulkan_command_buffer_unlock (cmd_buf);
  if (gst_vulkan_error_to_g_error (err, &error, "vkEndCommandBuffer") < 0)
    goto error;

  gst_vulkan_trash_list_add (render->trash_list,
      gst_vulkan_trash_new_mini_object_unref (fence,
          GST_MINI_OBJECT_CAST (set)));
  gst_vulkan_trash_list_add (render->trash_list,
      gst_vulkan_trash_new_free_framebuffer (fence, framebuffer));
  gst_vulkan_trash_list_add (render->trash_list,
      gst_vulkan_trash_new_mini_object_unref (fence,
          GST_MINI_OBJECT_CAST (cmd_buf)));

  if (!gst_vulkan_full_screen_render_submit (render, cmd_buf->cmd, fence))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;

unlock_error:
  if (cmd_buf) {
    gst_vulkan_command_buffer_unlock (cmd_buf);
    gst_vulkan_command_buffer_unref (cmd_buf);
  }
error:
  GST_ELEMENT_ERROR (bt, LIBRARY, FAILED, ("%s", error->message), (NULL));
  g_clear_error (&error);
  return GST_FLOW_ERROR;
}

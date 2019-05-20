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
#include "vktrash.h"
#include "vkshader.h"
#include "shaders/identity.vert.h"
#include "shaders/identity.frag.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_image_identity);
#define GST_CAT_DEFAULT gst_debug_vulkan_image_identity

struct Vertex
{
  gfloat x, y, z;
  gfloat s, t;
};

struct Vertex vertices[] = {
  {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
  {1.0f, -1.0f, 0.0f, 1.0f, 0.0f},
  {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
  {-1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
};

gushort indices[] = {
  0, 1, 2, 0, 2, 3,
};

static void gst_vulkan_image_identity_finalize (GObject * object);
static void gst_vulkan_image_identity_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * param_spec);
static void gst_vulkan_image_identity_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * param_spec);

static gboolean gst_vulkan_image_identity_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query);
static void gst_vulkan_image_identity_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_vulkan_image_identity_start (GstBaseTransform * bt);
static gboolean gst_vulkan_image_identity_stop (GstBaseTransform * bt);

static gboolean gst_vulkan_image_identity_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstCaps *gst_vulkan_image_identity_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_vulkan_image_identity_propose_allocation (GstBaseTransform *
    bt, GstQuery * decide_query, GstQuery * query);
static gboolean gst_vulkan_image_identity_decide_allocation (GstBaseTransform *
    bt, GstQuery * query);
static GstFlowReturn gst_vulkan_image_identity_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);

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
    GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_image_identity,
        "vulkanimageidentity", 0, "Vulkan Image identity"));

static void
gst_vulkan_image_identity_class_init (GstVulkanImageIdentityClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_vulkan_image_identity_set_property;
  gobject_class->get_property = gst_vulkan_image_identity_get_property;

  gst_element_class_set_metadata (gstelement_class, "Vulkan Uploader",
      "Filter/Video", "A Vulkan image copier",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_src_template);

  gobject_class->finalize = gst_vulkan_image_identity_finalize;

  gstelement_class->set_context = gst_vulkan_image_identity_set_context;
  gstbasetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_vulkan_image_identity_start);
  gstbasetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_vulkan_image_identity_stop);
  gstbasetransform_class->query =
      GST_DEBUG_FUNCPTR (gst_vulkan_image_identity_query);
  gstbasetransform_class->set_caps = gst_vulkan_image_identity_set_caps;
  gstbasetransform_class->transform_caps =
      gst_vulkan_image_identity_transform_caps;
  gstbasetransform_class->propose_allocation =
      gst_vulkan_image_identity_propose_allocation;
  gstbasetransform_class->decide_allocation =
      gst_vulkan_image_identity_decide_allocation;
  gstbasetransform_class->transform = gst_vulkan_image_identity_transform;
}

static void
gst_vulkan_image_identity_init (GstVulkanImageIdentity * vk_identity)
{
}

static void
gst_vulkan_image_identity_finalize (GObject * object)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (object);

  gst_caps_replace (&vk_identity->caps, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_image_identity_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
//  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_image_identity_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
//  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vulkan_image_identity_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      if (gst_vulkan_handle_context_query (GST_ELEMENT (vk_identity), query,
              NULL, &vk_identity->instance, &vk_identity->device))
        return TRUE;

      if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (vk_identity),
              query, &vk_identity->queue))
        return TRUE;

      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (bt, direction, query);
}

static void
gst_vulkan_image_identity_set_context (GstElement * element,
    GstContext * context)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (element);

  gst_vulkan_handle_set_context (element, context, NULL,
      &vk_identity->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

struct choose_data
{
  GstVulkanImageIdentity *upload;
  GstVulkanQueue *queue;
};

static gboolean
_choose_queue (GstVulkanDevice * device, GstVulkanQueue * queue,
    struct choose_data *data)
{
  guint flags = device->queue_family_props[queue->family].queueFlags;

  GST_ERROR ("flags 0x%x", flags);

  if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0) {
    if (data->queue)
      gst_object_unref (data->queue);
    data->queue = gst_object_ref (queue);
    return FALSE;
  }

  return TRUE;
}

static GstVulkanQueue *
_find_graphics_queue (GstVulkanImageIdentity * upload)
{
  struct choose_data data;

  data.upload = upload;
  data.queue = NULL;

  gst_vulkan_device_foreach_queue (upload->device,
      (GstVulkanDeviceForEachQueueFunc) _choose_queue, &data);

  return data.queue;
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

static VkPipeline
_create_pipeline (GstVulkanImageIdentity * vk_identity)
{
  VkShaderModule vert_module =
      _vk_create_shader (vk_identity->device, identity_vert, identity_vert_size,
      NULL);
  VkShaderModule frag_module =
      _vk_create_shader (vk_identity->device, identity_frag, identity_frag_size,
      NULL);

  /* *INDENT-OFF* */
  VkPipelineShaderStageCreateInfo vert_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_module,
      .pName = "main"
  };

  VkPipelineShaderStageCreateInfo frag_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = NULL,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_module,
      .pName = "main"
  };

  VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

  VkVertexInputBindingDescription vertex_binding_description = {
      .binding = 0,
      .stride = sizeof (struct Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
  };

  VkVertexInputAttributeDescription vertex_attribute_description[] = {
      {
          .binding = 0,
          .location = 0,
          .format = VK_FORMAT_R32G32B32_SFLOAT,
          .offset = G_STRUCT_OFFSET (struct Vertex, x)
      }, {
          .binding = 0,
          .location = 1,
          .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = G_STRUCT_OFFSET (struct Vertex, s)
      }
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = NULL,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertex_binding_description,
      .vertexAttributeDescriptionCount = G_N_ELEMENTS (vertex_attribute_description),
      .pVertexAttributeDescriptions = vertex_attribute_description
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .pNext = NULL,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE
  };

  VkViewport viewport = {
      .x = 0.0f,
      .y = 0.0f,
      .width = (float) GST_VIDEO_INFO_WIDTH (&vk_identity->v_info),
      .height = (float) GST_VIDEO_INFO_HEIGHT (&vk_identity->v_info),
      .minDepth = 0.0f,
      .maxDepth = 1.0f
  };

  VkRect2D scissor = {
      .offset = { 0, 0 },
      .extent = {
          GST_VIDEO_INFO_WIDTH (&vk_identity->v_info),
          GST_VIDEO_INFO_HEIGHT (&vk_identity->v_info)
      }
  };

  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .pNext = NULL,
      .viewportCount = 1,
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor
  };

  VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .pNext = NULL,
      .depthClampEnable = VK_FALSE,
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .lineWidth = 1.0f,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .depthBiasEnable = VK_FALSE
  };

  VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .pNext = NULL,
      .sampleShadingEnable = VK_FALSE,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
  };

  VkPipelineColorBlendAttachmentState color_blend_attachment = {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE
  };

  VkPipelineColorBlendStateCreateInfo color_blending = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .pNext = NULL,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
      .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }
  };

  VkGraphicsPipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = NULL,
      .stageCount = 2,
      .pStages = shader_stages,
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pColorBlendState = &color_blending,
      .layout = vk_identity->pipeline_layout,
      .renderPass = vk_identity->render_pass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE
  };
  /* *INDENT-ON* */
  VkResult err;
  GError *error = NULL;
  VkPipeline pipeline;

  err =
      vkCreateGraphicsPipelines (vk_identity->device->device, VK_NULL_HANDLE, 1,
      &pipeline_info, NULL, &pipeline);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkCreateGraphicsPipelines") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create pipeline layout: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  vkDestroyShaderModule (vk_identity->device->device, frag_module, NULL);
  vkDestroyShaderModule (vk_identity->device->device, vert_module, NULL);

  return pipeline;
}

static VkPipelineLayout
_create_pipeline_layout (GstVulkanImageIdentity * vk_identity)
{
  /* *INDENT-OFF* */
  VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .setLayoutCount = 1,
      .pSetLayouts = &vk_identity->descriptor_set_layout,
      .pushConstantRangeCount = 0
  };
  /* *INDENT-ON* */
  VkResult err;
  GError *error = NULL;
  VkPipelineLayout pipeline_layout;

  err =
      vkCreatePipelineLayout (vk_identity->device->device,
      &pipeline_layout_info, NULL, &pipeline_layout);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreatePipelineLayout") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create pipeline layout: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  return pipeline_layout;
}

static gboolean
gst_vulkan_image_identity_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);

  if (!gst_video_info_from_caps (&vk_identity->v_info, in_caps))
    return FALSE;

  if (vk_identity->graphics_pipeline) {
    vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
        gst_vulkan_trash_new_free_pipeline (vk_identity->last_fence,
            vk_identity->graphics_pipeline));
    vk_identity->graphics_pipeline = NULL;
  }
  if (!(vk_identity->graphics_pipeline = _create_pipeline (vk_identity)))
    return FALSE;

  gst_caps_replace (&vk_identity->caps, in_caps);

  GST_DEBUG_OBJECT (bt, "set caps: %" GST_PTR_FORMAT, in_caps);

  return TRUE;
}

static gboolean
gst_vulkan_image_identity_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query)
{
  /* FIXME: */
  return FALSE;
}

static gboolean
gst_vulkan_image_identity_decide_allocation (GstBaseTransform * bt,
    GstQuery * query)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool || !GST_IS_VULKAN_IMAGE_BUFFER_POOL (pool)) {
    if (pool)
      gst_object_unref (pool);
    pool = gst_vulkan_image_buffer_pool_new (vk_identity->device);
  }

  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static VkRenderPass
_create_render_pass (GstVulkanImageIdentity * vk_identity)
{
  /* *INDENT-OFF* */
  VkAttachmentDescription color_attachment = {
      /* FIXME: */
      .format = VK_FORMAT_B8G8R8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      /* FIXME: share this between elements to avoid pipeline barriers */
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };

  VkAttachmentReference color_attachment_ref = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  };

  VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_ref
  };

  VkRenderPassCreateInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = NULL,
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass
  };
  /* *INDENT-ON* */
  VkRenderPass render_pass = NULL;
  VkResult err;
  GError *error = NULL;

  err =
      vkCreateRenderPass (vk_identity->device->device, &render_pass_info, NULL,
      &render_pass);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateRenderPass") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create renderpass: %s",
        error->message);
    return NULL;
  }

  return render_pass;
}

static VkDescriptorSetLayout
_create_descriptor_set_layout (GstVulkanImageIdentity * vk_identity)
{
  /* *INDENT-OFF* */
  VkDescriptorSetLayoutBinding sampler_layout_binding = {
      .binding = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImmutableSamplers = NULL,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
  };
  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .bindingCount = 1,
      .pBindings = &sampler_layout_binding
  };
  /* *INDENT-ON* */
  VkDescriptorSetLayout descriptor_set_layout;
  VkResult err;
  GError *error = NULL;

  err =
      vkCreateDescriptorSetLayout (vk_identity->device->device, &layout_info,
      NULL, &descriptor_set_layout);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkCreateDescriptorSetLayout") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create renderpass: %s",
        error->message);
    return NULL;
  }

  return descriptor_set_layout;
}

static VkSampler
_create_sampler (GstVulkanImageIdentity * vk_identity)
{
  /* *INDENT-OFF* */
  VkSamplerCreateInfo samplerInfo = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
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

  err =
      vkCreateSampler (vk_identity->device->device, &samplerInfo, NULL,
      &sampler);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateSampler") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create sampler: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  return sampler;
}

static gboolean
_create_vertex_buffers (GstVulkanImageIdentity * vk_identity)
{
  GstMapInfo map_info;

  vk_identity->vertices =
      gst_vulkan_buffer_memory_alloc (vk_identity->device, VK_FORMAT_R8_UNORM,
      sizeof (vertices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (!gst_memory_map (vk_identity->vertices, &map_info, GST_MAP_WRITE)) {
    gst_memory_unref (vk_identity->vertices);
    vk_identity->vertices = NULL;
    return FALSE;
  }
  memcpy (map_info.data, vertices, sizeof (vertices));
  gst_memory_unmap (vk_identity->vertices, &map_info);

  vk_identity->indices =
      gst_vulkan_buffer_memory_alloc (vk_identity->device, VK_FORMAT_R8_UNORM,
      sizeof (indices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (!gst_memory_map (vk_identity->indices, &map_info, GST_MAP_WRITE)) {
    gst_memory_unref (vk_identity->vertices);
    vk_identity->vertices = NULL;
    gst_memory_unref (vk_identity->indices);
    vk_identity->indices = NULL;
    return FALSE;
  }
  memcpy (map_info.data, indices, sizeof (indices));
  gst_memory_unmap (vk_identity->indices, &map_info);

  return TRUE;
}

static VkFramebuffer
_create_framebuffer (GstVulkanImageIdentity * vk_identity, VkImageView view)
{
  /* *INDENT-OFF* */
  VkImageView attachments[] = {
    view,
  };
  VkFramebufferCreateInfo framebuffer_info = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = vk_identity->render_pass,
      .attachmentCount = 1,
      .pAttachments = attachments,
      .width = GST_VIDEO_INFO_WIDTH (&vk_identity->v_info),
      .height = GST_VIDEO_INFO_HEIGHT (&vk_identity->v_info),
      .layers = 1
  };
  /* *INDENT-ON* */
  VkFramebuffer framebuffer;
  GError *error = NULL;
  VkResult err;

  err =
      vkCreateFramebuffer (vk_identity->device->device, &framebuffer_info, NULL,
      &framebuffer);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateFramebuffer") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create framebuffer: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  return framebuffer;
}

static VkDescriptorPool
_create_descriptor_pool (GstVulkanImageIdentity * vk_identity)
{
  /* *INDENT-OFF* */
  VkDescriptorPoolSize pool_sizes = {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1
  };

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = NULL,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_sizes,
      .maxSets = 1
  };
  /* *INDENT-ON* */
  VkDescriptorPool pool;
  GError *error = NULL;
  VkResult err;

  err =
      vkCreateDescriptorPool (vk_identity->device->device, &pool_info, NULL,
      &pool);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateDescriptorPool") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to create descriptor pool: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  return pool;
}

static VkDescriptorSet
_create_descriptor_set (GstVulkanImageIdentity * vk_identity)
{
  /* *INDENT-OFF* */
  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = NULL,
      .descriptorPool = vk_identity->descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &vk_identity->descriptor_set_layout
  };
  /* *INDENT-ON* */
  VkDescriptorSet descriptor;
  GError *error = NULL;
  VkResult err;

  err =
      vkAllocateDescriptorSets (vk_identity->device->device, &alloc_info,
      &descriptor);
  if (gst_vulkan_error_to_g_error (err, &error, "vkAllocateDescriptorSets") < 0) {
    GST_ERROR_OBJECT (vk_identity, "Failed to allocate descriptor: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  return descriptor;
}

static gboolean
gst_vulkan_image_identity_start (GstBaseTransform * bt)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);

  if (!gst_vulkan_ensure_element_data (GST_ELEMENT (bt), NULL,
          &vk_identity->instance)) {
    GST_ELEMENT_ERROR (vk_identity, RESOURCE, NOT_FOUND,
        ("Failed to retreive vulkan instance"), (NULL));
    return FALSE;
  }
  if (!gst_vulkan_device_run_context_query (GST_ELEMENT (vk_identity),
          &vk_identity->device)) {
    GError *error = NULL;
    GST_DEBUG_OBJECT (vk_identity, "No device retrieved from peer elements");
    if (!(vk_identity->device =
            gst_vulkan_instance_create_device (vk_identity->instance,
                &error))) {
      GST_ELEMENT_ERROR (vk_identity, RESOURCE, NOT_FOUND,
          ("Failed to create vulkan device"), ("%s", error->message));
      g_clear_error (&error);
      return FALSE;
    }
  }

  if (!gst_vulkan_queue_run_context_query (GST_ELEMENT (vk_identity),
          &vk_identity->queue)) {
    GST_DEBUG_OBJECT (vk_identity, "No queue retrieved from peer elements");
    vk_identity->queue = _find_graphics_queue (vk_identity);
  }
  if (!vk_identity->queue)
    return FALSE;

  if (!(vk_identity->render_pass = _create_render_pass (vk_identity)))
    return FALSE;
  if (!(vk_identity->sampler = _create_sampler (vk_identity)))
    return FALSE;
  if (!(vk_identity->descriptor_set_layout =
          _create_descriptor_set_layout (vk_identity)))
    return FALSE;
  if (!(vk_identity->descriptor_pool = _create_descriptor_pool (vk_identity)))
    return FALSE;
  if (!(vk_identity->descriptor_set = _create_descriptor_set (vk_identity)))
    return FALSE;
  if (!(vk_identity->pipeline_layout = _create_pipeline_layout (vk_identity)))
    return FALSE;

  if (!_create_vertex_buffers (vk_identity))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vulkan_image_identity_stop (GstBaseTransform * bt)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);

  if (vk_identity->device) {
    if (vk_identity->last_fence) {
      vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
          gst_vulkan_trash_new_free_pipeline (gst_vulkan_fence_ref
              (vk_identity->last_fence), vk_identity->graphics_pipeline));
      vk_identity->graphics_pipeline = NULL;
      vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
          gst_vulkan_trash_new_free_pipeline_layout (gst_vulkan_fence_ref
              (vk_identity->last_fence), vk_identity->pipeline_layout));
      vk_identity->pipeline_layout = NULL;
      vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
          gst_vulkan_trash_new_free_render_pass (gst_vulkan_fence_ref
              (vk_identity->last_fence), vk_identity->render_pass));
      vk_identity->render_pass = NULL;
      vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
          gst_vulkan_trash_new_free_descriptor_pool (gst_vulkan_fence_ref
              (vk_identity->last_fence), vk_identity->descriptor_pool));
      vk_identity->descriptor_pool = NULL;
      vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
          gst_vulkan_trash_new_free_descriptor_set_layout (gst_vulkan_fence_ref
              (vk_identity->last_fence), vk_identity->descriptor_set_layout));
      vk_identity->descriptor_set_layout = NULL;
      vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
          gst_vulkan_trash_new_free_sampler (gst_vulkan_fence_ref
              (vk_identity->last_fence), vk_identity->sampler));
      vk_identity->sampler = NULL;

      gst_vulkan_fence_unref (vk_identity->last_fence);
      vk_identity->last_fence = NULL;
    } else {
      vkDestroyPipeline (vk_identity->device->device,
          vk_identity->graphics_pipeline, NULL);
      vk_identity->graphics_pipeline = NULL;

      vkDestroyPipelineLayout (vk_identity->device->device,
          vk_identity->pipeline_layout, NULL);
      vk_identity->pipeline_layout = NULL;

      vkDestroyRenderPass (vk_identity->device->device,
          vk_identity->render_pass, NULL);
      vk_identity->render_pass = NULL;

      vkFreeDescriptorSets (vk_identity->device->device,
          vk_identity->descriptor_pool, 1, &vk_identity->descriptor_set);
      vk_identity->descriptor_set = NULL;

      vkDestroyDescriptorPool (vk_identity->device->device,
          vk_identity->descriptor_pool, NULL);
      vk_identity->descriptor_pool = NULL;

      vkDestroyDescriptorSetLayout (vk_identity->device->device,
          vk_identity->descriptor_set_layout, NULL);
      vk_identity->descriptor_set_layout = NULL;

      vkDestroySampler (vk_identity->device->device, vk_identity->sampler,
          NULL);
      vk_identity->sampler = NULL;
    }

    if (!gst_vulkan_trash_list_wait (vk_identity->trash_list, -1))
      GST_WARNING_OBJECT (vk_identity,
          "Failed to wait for all resources to be freed");
    vk_identity->trash_list = NULL;

    if (vk_identity->vertices)
      gst_memory_unref (vk_identity->vertices);
    vk_identity->vertices = NULL;

    if (vk_identity->indices)
      gst_memory_unref (vk_identity->indices);
    vk_identity->indices = NULL;

    gst_object_unref (vk_identity->device);
  }
  vk_identity->device = NULL;

  if (vk_identity->cmd_pool)
    gst_object_unref (vk_identity->cmd_pool);
  vk_identity->cmd_pool = NULL;

  if (vk_identity->queue)
    gst_object_unref (vk_identity->queue);
  vk_identity->queue = NULL;

  if (vk_identity->instance)
    gst_object_unref (vk_identity->instance);
  vk_identity->instance = NULL;

  return TRUE;
}

static void
_update_descriptor (GstVulkanImageIdentity * vk_identity, VkImageView view)
{
  /* *INDENT-OFF* */
  VkDescriptorImageInfo image_info = {
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .imageView = view,
      .sampler = vk_identity->sampler
  };

  VkWriteDescriptorSet writes = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = NULL,
      .dstSet = vk_identity->descriptor_set,
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .pImageInfo = &image_info
  };
  /* *INDENT-ON* */

  vkUpdateDescriptorSets (vk_identity->device->device, 1, &writes, 0, NULL);
}

static GstFlowReturn
gst_vulkan_image_identity_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVulkanImageIdentity *vk_identity = GST_VULKAN_IMAGE_IDENTITY (bt);
  GstVulkanImageMemory *in_img_mem, *out_img_mem;
  GstVulkanFence *fence = NULL;
  GstMemory *in_mem, *out_mem;
  VkFramebuffer framebuffer;
  VkCommandBuffer cmd;
  GError *error = NULL;
  VkResult err;

  in_mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_vulkan_image_memory (in_mem)) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Input memory must be a GstVulkanImageMemory");
    goto error;
  }
  in_img_mem = (GstVulkanImageMemory *) in_mem;

  out_mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_vulkan_image_memory (out_mem)) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Input memory must be a GstVulkanImageMemory");
    goto error;
  }
  out_img_mem = (GstVulkanImageMemory *) out_mem;

  if (!vk_identity->cmd_pool) {
    if (!(vk_identity->cmd_pool =
            gst_vulkan_queue_create_command_pool (vk_identity->queue, &error)))
      goto error;
    _update_descriptor (vk_identity, in_img_mem->view);
  }

  if (!(cmd = gst_vulkan_command_pool_create (vk_identity->cmd_pool, &error)))
    goto error;

  if (!(framebuffer = _create_framebuffer (vk_identity, out_img_mem->view))) {
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

    err = vkBeginCommandBuffer (cmd, &cmd_buf_info);
    if (gst_vulkan_error_to_g_error (err, &error, "vkBeginCommandBuffer") < 0)
      goto error;
  }

  {
    /* *INDENT-OFF* */
    VkClearValue clearColor = {{{ 0.0f, 1.0f, 0.0f, 1.0f }}};
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk_identity->render_pass,
        .framebuffer = framebuffer,
        .renderArea.offset = { 0, 0 },
        .renderArea.extent = {
            GST_VIDEO_INFO_WIDTH (&vk_identity->v_info),
            GST_VIDEO_INFO_HEIGHT (&vk_identity->v_info)
        },
        .clearValueCount = 1,
        .pClearValues = &clearColor
    };

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
    VkDeviceSize offsets[] = { 0 };

    vkCmdPipelineBarrier (cmd, in_img_mem->barrier.parent.pipeline_stages,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &in_image_memory_barrier);

    in_img_mem->barrier.parent.pipeline_stages =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    in_img_mem->barrier.parent.access_flags =
        in_image_memory_barrier.dstAccessMask;
    in_img_mem->barrier.image_layout = in_image_memory_barrier.newLayout;

    vkCmdPipelineBarrier (cmd, out_img_mem->barrier.parent.pipeline_stages,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
        &out_image_memory_barrier);

    out_img_mem->barrier.parent.pipeline_stages =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    out_img_mem->barrier.parent.access_flags =
        out_image_memory_barrier.dstAccessMask;
    out_img_mem->barrier.image_layout = out_image_memory_barrier.newLayout;

    vkCmdBeginRenderPass (cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline (cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        vk_identity->graphics_pipeline);
    vkCmdBindVertexBuffers (cmd, 0, 1,
        &((GstVulkanBufferMemory *) vk_identity->vertices)->buffer, offsets);
    vkCmdBindIndexBuffer (cmd,
        ((GstVulkanBufferMemory *) vk_identity->indices)->buffer, 0,
        VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets (cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        vk_identity->pipeline_layout, 0, 1, &vk_identity->descriptor_set, 0,
        NULL);
    vkCmdDrawIndexed (cmd, G_N_ELEMENTS (indices), 1, 0, 0, 0);
    vkCmdEndRenderPass (cmd);
  }

  err = vkEndCommandBuffer (cmd);
  if (gst_vulkan_error_to_g_error (err, &error, "vkEndCommandBuffer") < 0)
    goto error;

  fence = gst_vulkan_fence_new (vk_identity->device, 0, &error);
  if (!fence)
    goto error;

  {
    VkSubmitInfo submit_info = { 0, };

    /* *INDENT-OFF* */
    submit_info = (VkSubmitInfo) {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL,
    };
    /* *INDENT-ON* */

    if (vk_identity->last_fence)
      gst_vulkan_fence_unref (vk_identity->last_fence);
    vk_identity->last_fence = gst_vulkan_fence_ref (fence);

    err =
        vkQueueSubmit (vk_identity->queue->queue, 1, &submit_info,
        GST_VULKAN_FENCE_FENCE (fence));
    if (gst_vulkan_error_to_g_error (err, &error, "vkQueueSubmit") < 0)
      goto error;
  }

  vk_identity->trash_list = g_list_prepend (vk_identity->trash_list,
      gst_vulkan_trash_new_free_command_buffer (gst_vulkan_fence_ref (fence),
          vk_identity->cmd_pool, cmd));
  vk_identity->trash_list =
      g_list_prepend (vk_identity->trash_list,
      gst_vulkan_trash_new_free_framebuffer (gst_vulkan_fence_ref (fence),
          framebuffer));

  vk_identity->trash_list = gst_vulkan_trash_list_gc (vk_identity->trash_list);

  gst_vulkan_fence_unref (fence);

  return GST_FLOW_OK;

error:
  GST_ELEMENT_ERROR (bt, LIBRARY, FAILED, ("%s", error->message), (NULL));
  g_clear_error (&error);
  return GST_FLOW_ERROR;
}

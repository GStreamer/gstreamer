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

GST_DEBUG_CATEGORY (gst_debug_vulkan_full_screen_render);
#define GST_CAT_DEFAULT gst_debug_vulkan_full_screen_render

struct Vertex vertices[] = {
  {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f},
  {1.0f, -1.0f, 0.0f, 1.0f, 0.0f},
  {1.0f, 1.0f, 0.0f, 1.0f, 1.0f},
  {-1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
};

gushort indices[] = {
  0, 1, 2, 0, 2, 3,
};

static void gst_vulkan_full_screen_render_finalize (GObject * object);

static gboolean gst_vulkan_full_screen_render_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query);
static void gst_vulkan_full_screen_render_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_vulkan_full_screen_render_start (GstBaseTransform * bt);
static gboolean gst_vulkan_full_screen_render_stop (GstBaseTransform * bt);

static gboolean gst_vulkan_full_screen_render_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);
static GstCaps *gst_vulkan_full_screen_render_transform_caps (GstBaseTransform *
    bt, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean
gst_vulkan_full_screen_render_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_vulkan_full_screen_render_decide_allocation (GstBaseTransform * bt,
    GstQuery * query);

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

/* static guint gst_vulkan_full_screen_render_signals[LAST_SIGNAL] = { 0 }; */

#define gst_vulkan_full_screen_render_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanFullScreenRender,
    gst_vulkan_full_screen_render, GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_full_screen_render,
        "vulkanimageidentity", 0, "Vulkan Image identity"));

static void
gst_vulkan_full_screen_render_class_init (GstVulkanFullScreenRenderClass *
    klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  gst_element_class_set_metadata (gstelement_class, "Vulkan Uploader",
      "Filter/Video", "A Vulkan image copier",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_src_template);

  gobject_class->finalize = gst_vulkan_full_screen_render_finalize;

  gstelement_class->set_context = gst_vulkan_full_screen_render_set_context;
  gstbasetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_vulkan_full_screen_render_start);
  gstbasetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_vulkan_full_screen_render_stop);
  gstbasetransform_class->query =
      GST_DEBUG_FUNCPTR (gst_vulkan_full_screen_render_query);
  gstbasetransform_class->set_caps = gst_vulkan_full_screen_render_set_caps;
  gstbasetransform_class->transform_caps =
      gst_vulkan_full_screen_render_transform_caps;
  gstbasetransform_class->propose_allocation =
      gst_vulkan_full_screen_render_propose_allocation;
  gstbasetransform_class->decide_allocation =
      gst_vulkan_full_screen_render_decide_allocation;
}

static void
gst_vulkan_full_screen_render_init (GstVulkanFullScreenRender * render)
{
}

static void
gst_vulkan_full_screen_render_finalize (GObject * object)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (object);

  gst_caps_replace (&render->in_caps, NULL);
  gst_caps_replace (&render->out_caps, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vulkan_full_screen_render_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      if (gst_vulkan_handle_context_query (GST_ELEMENT (render), query,
              NULL, render->instance, render->device))
        return TRUE;

      if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (render),
              query, render->queue))
        return TRUE;

      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (bt, direction, query);
}

static void
gst_vulkan_full_screen_render_set_context (GstElement * element,
    GstContext * context)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (element);

  gst_vulkan_handle_set_context (element, context, NULL, &render->instance);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

struct choose_data
{
  GstVulkanFullScreenRender *upload;
  GstVulkanQueue *queue;
};

static gboolean
_choose_queue (GstVulkanDevice * device, GstVulkanQueue * queue,
    struct choose_data *data)
{
  guint flags =
      device->physical_device->queue_family_props[queue->family].queueFlags;

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
_find_graphics_queue (GstVulkanFullScreenRender * upload)
{
  struct choose_data data;

  data.upload = upload;
  data.queue = NULL;

  gst_vulkan_device_foreach_queue (upload->device,
      (GstVulkanDeviceForEachQueueFunc) _choose_queue, &data);

  return data.queue;
}

static GstCaps *
gst_vulkan_full_screen_render_transform_caps (GstBaseTransform * bt,
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
clear_shader_create_info (GstVulkanFullScreenRender * render)
{
  if (render->shader_create_info) {
    if (render->destroy_shader_create_info)
      render->destroy_shader_create_info (render, render->shader_create_info);
  }
  render->n_shader_stages = 0;
  render->shader_create_info = NULL;
  render->destroy_shader_create_info = NULL;
}

static VkPipeline
_create_pipeline (GstVulkanFullScreenRender * render)
{
  GstVulkanFullScreenRenderClass *render_class =
      GST_VULKAN_FULL_SCREEN_RENDER_GET_CLASS (render);
  VkVertexInputBindingDescription vertex_binding_description;
  VkVertexInputAttributeDescription vertex_attribute_description[2];
  VkPipelineVertexInputStateCreateInfo vertex_input_info;
  VkPipelineInputAssemblyStateCreateInfo input_assembly;
  VkPipelineViewportStateCreateInfo viewport_state;
  VkPipelineRasterizationStateCreateInfo rasterizer;
  VkPipelineMultisampleStateCreateInfo multisampling;
  VkPipelineColorBlendAttachmentState
      color_blend_attachments[GST_VIDEO_MAX_PLANES];
  VkPipelineColorBlendStateCreateInfo color_blending;
  VkGraphicsPipelineCreateInfo pipeline_info;
  VkPipeline pipeline;
  GError *error = NULL;
  VkResult err;

  render_class->shader_create_info (render);

  /* *INDENT-OFF* */
  vertex_binding_description = (VkVertexInputBindingDescription) {
      .binding = 0,
      .stride = sizeof (struct Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
  };

  vertex_attribute_description[0] = (VkVertexInputAttributeDescription) {
      .binding = 0,
      .location = 0,
      .format = VK_FORMAT_R32G32B32_SFLOAT,
      .offset = G_STRUCT_OFFSET (struct Vertex, x)
  };
  vertex_attribute_description[1] = (VkVertexInputAttributeDescription) {
      .binding = 0,
      .location = 1,
      .format = VK_FORMAT_R32G32_SFLOAT,
      .offset = G_STRUCT_OFFSET (struct Vertex, s)
  };

  vertex_input_info = (VkPipelineVertexInputStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = NULL,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertex_binding_description,
      .vertexAttributeDescriptionCount = G_N_ELEMENTS (vertex_attribute_description),
      .pVertexAttributeDescriptions = vertex_attribute_description
  };

  input_assembly = (VkPipelineInputAssemblyStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .pNext = NULL,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable = VK_FALSE
  };

  viewport_state = (VkPipelineViewportStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .pNext = NULL,
      .viewportCount = 1,
      .pViewports = &(VkViewport) {
          .x = 0.0f,
          .y = 0.0f,
          .width = (float) GST_VIDEO_INFO_WIDTH (&render->out_info),
          .height = (float) GST_VIDEO_INFO_HEIGHT (&render->out_info),
          .minDepth = 0.0f,
          .maxDepth = 1.0f
      },
      .scissorCount = 1,
      .pScissors = &(VkRect2D) {
          .offset = { 0, 0 },
          .extent = {
              GST_VIDEO_INFO_WIDTH (&render->out_info),
              GST_VIDEO_INFO_HEIGHT (&render->out_info)
          }
      }
  };

  rasterizer = (VkPipelineRasterizationStateCreateInfo) {
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

  multisampling = (VkPipelineMultisampleStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .pNext = NULL,
      .sampleShadingEnable = VK_FALSE,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
  };

  color_blend_attachments[0] = (VkPipelineColorBlendAttachmentState) {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE
  };
  color_blend_attachments[1] = (VkPipelineColorBlendAttachmentState) {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE
  };
  color_blend_attachments[2] = (VkPipelineColorBlendAttachmentState) {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE
  };
  color_blend_attachments[3] = (VkPipelineColorBlendAttachmentState) {
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
      .blendEnable = VK_FALSE
  };

  color_blending = (VkPipelineColorBlendStateCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .pNext = NULL,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = GST_VIDEO_INFO_N_PLANES (&render->out_info),
      .pAttachments = color_blend_attachments,
      .blendConstants = { 0.0f, 0.0f, 0.0f, 0.0f }
  };

  pipeline_info = (VkGraphicsPipelineCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = NULL,
      .stageCount = render->n_shader_stages,
      .pStages = render->shader_create_info,
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pColorBlendState = &color_blending,
      .layout = render->pipeline_layout,
      .renderPass = render->render_pass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE
  };
  /* *INDENT-ON* */

  err =
      vkCreateGraphicsPipelines (render->device->device, VK_NULL_HANDLE, 1,
      &pipeline_info, NULL, &pipeline);
  clear_shader_create_info (render);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkCreateGraphicsPipelines") < 0) {
    GST_ERROR_OBJECT (render, "Failed to create pipeline layout: %s",
        error->message);
    g_clear_error (&error);
    return VK_NULL_HANDLE;
  }

  return pipeline;
}

static VkPipelineLayout
_create_pipeline_layout (GstVulkanFullScreenRender * render)
{
  GstVulkanFullScreenRenderClass *render_class =
      GST_VULKAN_FULL_SCREEN_RENDER_GET_CLASS (render);
  VkPipelineLayoutCreateInfo pipeline_layout_info;
  VkPipelineLayout pipeline_layout;
  VkPushConstantRange *constants = NULL;
  guint n_constants = 0;
  GError *error = NULL;
  VkResult err;

  if (render_class->push_constant_ranges)
    constants = render_class->push_constant_ranges (render, &n_constants);

  /* *INDENT-OFF* */
  pipeline_layout_info = (VkPipelineLayoutCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .setLayoutCount = 1,
      .pSetLayouts = (VkDescriptorSetLayout *) &render->descriptor_set_layout->handle,
      .pushConstantRangeCount = n_constants,
      .pPushConstantRanges = constants,
  };
  /* *INDENT-ON* */

  err =
      vkCreatePipelineLayout (render->device->device,
      &pipeline_layout_info, NULL, &pipeline_layout);
  g_free (constants);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreatePipelineLayout") < 0) {
    GST_ERROR_OBJECT (render, "Failed to create pipeline layout: %s",
        error->message);
    g_clear_error (&error);
    return VK_NULL_HANDLE;
  }

  return pipeline_layout;
}

static VkRenderPass
_create_render_pass (GstVulkanFullScreenRender * render)
{
  GstVulkanFullScreenRenderClass *render_class =
      GST_VULKAN_FULL_SCREEN_RENDER_GET_CLASS (render);

  guint n_descriptions;
  VkAttachmentDescription *descriptions =
      render_class->render_pass_attachment_descriptions (render,
      &n_descriptions);

  guint n_refs;
  VkAttachmentReference *color_attachment_refs =
      render_class->render_pass_attachment_references (render, &n_refs);

  /* *INDENT-OFF* */
  VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = n_refs,
      .pColorAttachments = color_attachment_refs
  };

  VkRenderPassCreateInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = NULL,
      .attachmentCount = n_descriptions,
      .pAttachments = descriptions,
      .subpassCount = 1,
      .pSubpasses = &subpass
  };
  /* *INDENT-ON* */
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkResult err;
  GError *error = NULL;

  err =
      vkCreateRenderPass (render->device->device, &render_pass_info, NULL,
      &render_pass);
  g_free (color_attachment_refs);
  g_free (descriptions);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateRenderPass") < 0) {
    GST_ERROR_OBJECT (render, "Failed to create renderpass: %s",
        error->message);
    return VK_NULL_HANDLE;
  }

  return render_pass;
}

static GstVulkanHandle *
_create_descriptor_set_layout (GstVulkanFullScreenRender * render)
{
  GstVulkanFullScreenRenderClass *render_class =
      GST_VULKAN_FULL_SCREEN_RENDER_GET_CLASS (render);
  guint n_bindings;
  VkDescriptorSetLayoutBinding *bindings =
      render_class->descriptor_set_layout_bindings (render, &n_bindings);

  /* *INDENT-OFF* */
  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .bindingCount = n_bindings,
      .pBindings = bindings
  };
  /* *INDENT-ON* */
  VkDescriptorSetLayout descriptor_set_layout;
  GstVulkanHandle *ret;
  VkResult err;
  GError *error = NULL;

  err =
      vkCreateDescriptorSetLayout (render->device->device, &layout_info,
      NULL, &descriptor_set_layout);
  g_free (bindings);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkCreateDescriptorSetLayout") < 0) {
    GST_ERROR_OBJECT (render, "Failed to create renderpass: %s",
        error->message);
    return VK_NULL_HANDLE;
  }

  ret = gst_vulkan_handle_new_wrapped (render->device,
      GST_VULKAN_HANDLE_TYPE_DESCRIPTOR_SET_LAYOUT,
      (GstVulkanHandleTypedef) descriptor_set_layout,
      gst_vulkan_handle_free_descriptor_set_layout, NULL);

  return ret;
}

static gboolean
gst_vulkan_full_screen_render_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);
  GstVulkanFence *last_fence;

  if (!gst_video_info_from_caps (&render->in_info, in_caps))
    return FALSE;
  if (!gst_video_info_from_caps (&render->out_info, out_caps))
    return FALSE;

  gst_caps_replace (&render->in_caps, in_caps);
  gst_caps_replace (&render->out_caps, out_caps);

  if (render->last_fence)
    last_fence = gst_vulkan_fence_ref (render->last_fence);
  else
    last_fence = gst_vulkan_fence_new_always_signalled (render->device);

  if (render->descriptor_set_layout) {
    gst_vulkan_trash_list_add (render->trash_list,
        gst_vulkan_trash_new_mini_object_unref (last_fence,
            (GstMiniObject *) render->descriptor_set_layout));
    render->descriptor_set_layout = NULL;
  }
  if (render->pipeline_layout) {
    gst_vulkan_trash_list_add (render->trash_list,
        gst_vulkan_trash_new_free_pipeline_layout (last_fence,
            render->pipeline_layout));
    render->pipeline_layout = VK_NULL_HANDLE;
  }
  if (render->render_pass) {
    gst_vulkan_trash_list_add (render->trash_list,
        gst_vulkan_trash_new_free_render_pass (last_fence,
            render->render_pass));
    render->render_pass = VK_NULL_HANDLE;
  }
  if (render->graphics_pipeline) {
    gst_vulkan_trash_list_add (render->trash_list,
        gst_vulkan_trash_new_free_pipeline (last_fence,
            render->graphics_pipeline));
    render->graphics_pipeline = VK_NULL_HANDLE;
  }

  gst_vulkan_fence_unref (last_fence);

  if (!(render->descriptor_set_layout = _create_descriptor_set_layout (render)))
    return FALSE;
  if (!(render->pipeline_layout = _create_pipeline_layout (render)))
    return FALSE;
  if (!(render->render_pass = _create_render_pass (render)))
    return FALSE;
  if (!(render->graphics_pipeline = _create_pipeline (render)))
    return FALSE;

  GST_DEBUG_OBJECT (bt, "set caps: %" GST_PTR_FORMAT, in_caps);

  return TRUE;
}

static gboolean
gst_vulkan_full_screen_render_propose_allocation (GstBaseTransform * bt,
    GstQuery * decide_query, GstQuery * query)
{
  /* FIXME: */
  return FALSE;
}

static gboolean
gst_vulkan_full_screen_render_decide_allocation (GstBaseTransform * bt,
    GstQuery * query)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);
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
    pool = gst_vulkan_image_buffer_pool_new (render->device);
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

static gboolean
_create_vertex_buffers (GstVulkanFullScreenRender * render)
{
  GstMapInfo map_info;

  render->vertices =
      gst_vulkan_buffer_memory_alloc (render->device, sizeof (vertices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (!gst_memory_map (render->vertices, &map_info, GST_MAP_WRITE)) {
    gst_memory_unref (render->vertices);
    render->vertices = NULL;
    return FALSE;
  }
  memcpy (map_info.data, vertices, sizeof (vertices));
  gst_memory_unmap (render->vertices, &map_info);

  render->indices =
      gst_vulkan_buffer_memory_alloc (render->device, sizeof (indices),
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (!gst_memory_map (render->indices, &map_info, GST_MAP_WRITE)) {
    gst_memory_unref (render->vertices);
    render->vertices = NULL;
    gst_memory_unref (render->indices);
    render->indices = NULL;
    return FALSE;
  }
  memcpy (map_info.data, indices, sizeof (indices));
  gst_memory_unmap (render->indices, &map_info);

  return TRUE;
}

static gboolean
gst_vulkan_full_screen_render_start (GstBaseTransform * bt)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);

  if (!gst_vulkan_ensure_element_data (GST_ELEMENT (bt), NULL,
          &render->instance)) {
    GST_ELEMENT_ERROR (render, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan instance"), (NULL));
    return FALSE;
  }
  if (!gst_vulkan_device_run_context_query (GST_ELEMENT (render),
          &render->device)) {
    GError *error = NULL;
    GST_DEBUG_OBJECT (render, "No device retrieved from peer elements");
    if (!(render->device =
            gst_vulkan_instance_create_device (render->instance, &error))) {
      GST_ELEMENT_ERROR (render, RESOURCE, NOT_FOUND,
          ("Failed to create vulkan device"), ("%s", error->message));
      g_clear_error (&error);
      return FALSE;
    }
  }

  if (!gst_vulkan_queue_run_context_query (GST_ELEMENT (render),
          &render->queue)) {
    GST_DEBUG_OBJECT (render, "No queue retrieved from peer elements");
    render->queue = _find_graphics_queue (render);
  }
  if (!render->queue)
    return FALSE;

  if (!_create_vertex_buffers (render))
    return FALSE;

  render->trash_list = gst_vulkan_trash_fence_list_new ();

  return TRUE;
}

static gboolean
gst_vulkan_full_screen_render_stop (GstBaseTransform * bt)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);

  if (render->device) {
    GstVulkanFence *last_fence;

    if (render->last_fence)
      last_fence = gst_vulkan_fence_ref (render->last_fence);
    else
      last_fence = gst_vulkan_fence_new_always_signalled (render->device);

    if (render->graphics_pipeline)
      gst_vulkan_trash_list_add (render->trash_list,
          gst_vulkan_trash_new_free_pipeline (last_fence,
              render->graphics_pipeline));
    render->graphics_pipeline = VK_NULL_HANDLE;
    if (render->pipeline_layout)
      gst_vulkan_trash_list_add (render->trash_list,
          gst_vulkan_trash_new_free_pipeline_layout (last_fence,
              render->pipeline_layout));
    render->pipeline_layout = VK_NULL_HANDLE;
    if (render->render_pass)
      gst_vulkan_trash_list_add (render->trash_list,
          gst_vulkan_trash_new_free_render_pass (last_fence,
              render->render_pass));
    render->render_pass = VK_NULL_HANDLE;
    if (render->descriptor_set_layout)
      gst_vulkan_trash_list_add (render->trash_list,
          gst_vulkan_trash_new_mini_object_unref (last_fence,
              (GstMiniObject *) render->descriptor_set_layout));
    render->descriptor_set_layout = NULL;

    gst_vulkan_fence_unref (last_fence);

    if (render->last_fence)
      gst_vulkan_fence_unref (render->last_fence);
    render->last_fence = NULL;

    if (!gst_vulkan_trash_list_wait (render->trash_list, -1))
      GST_WARNING_OBJECT (render,
          "Failed to wait for all resources to be freed");
    gst_object_unref (render->trash_list);
    render->trash_list = NULL;

    if (render->vertices)
      gst_memory_unref (render->vertices);
    render->vertices = NULL;

    if (render->indices)
      gst_memory_unref (render->indices);
    render->indices = NULL;

    gst_object_unref (render->device);
  }
  render->device = NULL;

  if (render->queue)
    gst_object_unref (render->queue);
  render->queue = NULL;

  if (render->instance)
    gst_object_unref (render->instance);
  render->instance = NULL;

  return TRUE;
}

/**
 * gst_vulkan_full_screen_render_fill_command_buffer:
 * @render: a #GstVulkanFullScreenRender
 * @cmd: a `VkCommandBuffer`
 * @framebuffer: a `VkFramebuffer`
 *
 * Returns: whether @cmd could be filled with the commands necessary to render
 */
gboolean
gst_vulkan_full_screen_render_fill_command_buffer (GstVulkanFullScreenRender *
    render, VkCommandBuffer cmd, VkFramebuffer framebuffer)
{
  /* *INDENT-OFF* */
  VkClearValue clearColor = {{{ 0.0f, 0.0f, 0.0f, 1.0f }}};
  VkClearValue clearColors[GST_VIDEO_MAX_PLANES] = {
    clearColor, clearColor, clearColor, clearColor,
  };
  VkRenderPassBeginInfo render_pass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = render->render_pass,
      .framebuffer = framebuffer,
      .renderArea.offset = { 0, 0 },
      .renderArea.extent = {
          GST_VIDEO_INFO_WIDTH (&render->out_info),
          GST_VIDEO_INFO_HEIGHT (&render->out_info)
      },
      .clearValueCount = GST_VIDEO_INFO_N_PLANES (&render->out_info),
      .pClearValues = clearColors,
  };
  /* *INDENT-ON* */
  VkDeviceSize offsets[] = { 0 };

  vkCmdBeginRenderPass (cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline (cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
      render->graphics_pipeline);
  vkCmdBindVertexBuffers (cmd, 0, 1,
      &((GstVulkanBufferMemory *) render->vertices)->buffer, offsets);
  vkCmdBindIndexBuffer (cmd,
      ((GstVulkanBufferMemory *) render->indices)->buffer, 0,
      VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed (cmd, G_N_ELEMENTS (indices), 1, 0, 0, 0);
  vkCmdEndRenderPass (cmd);

  return TRUE;
}

gboolean
gst_vulkan_full_screen_render_submit (GstVulkanFullScreenRender * render,
    VkCommandBuffer cmd, GstVulkanFence * fence)
{
  VkSubmitInfo submit_info;
  GError *error = NULL;
  VkResult err;

  if (!fence)
    fence = gst_vulkan_fence_new (render->device, 0, &error);
  if (!fence)
    goto error;

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

  if (render->last_fence)
    gst_vulkan_fence_unref (render->last_fence);
  render->last_fence = gst_vulkan_fence_ref (fence);

  gst_vulkan_queue_submit_lock (render->queue);
  err =
      vkQueueSubmit (render->queue->queue, 1, &submit_info,
      GST_VULKAN_FENCE_FENCE (fence));
  gst_vulkan_queue_submit_unlock (render->queue);
  if (gst_vulkan_error_to_g_error (err, &error, "vkQueueSubmit") < 0)
    goto error;

  gst_vulkan_trash_list_gc (render->trash_list);

  gst_vulkan_fence_unref (fence);

  return TRUE;

error:
  GST_ELEMENT_ERROR (render, LIBRARY, FAILED, ("%s", error->message), (NULL));
  g_clear_error (&error);
  return FALSE;
}

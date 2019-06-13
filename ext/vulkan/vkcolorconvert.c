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

#include "vkcolorconvert.h"
#include "vktrash.h"
#include "vkshader.h"
#include "shaders/identity.vert.h"
#include "shaders/swizzle.frag.h"
#include "shaders/swizzle_and_clobber_alpha.frag.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_color_convert);
#define GST_CAT_DEFAULT gst_debug_vulkan_color_convert

#define N_SHADER_INFO (8*8)
static struct shader_info shader_infos[N_SHADER_INFO];

static void
get_rgb_format_swizzle_order (GstVideoFormat format, gint * swizzle)
{
  const GstVideoFormatInfo *finfo = gst_video_format_get_info (format);
  int c_i = 0, i;

  g_return_if_fail (finfo->flags & GST_VIDEO_FORMAT_FLAG_RGB);

  for (i = 0; i < finfo->n_components; i++) {
    swizzle[c_i++] = finfo->poffset[i];
  }

  /* special case spaced RGB formats as the space does not contain a poffset
   * value and we need all four components to be valid in order to swizzle
   * correctly */
  if (format == GST_VIDEO_FORMAT_xRGB || format == GST_VIDEO_FORMAT_xBGR) {
    swizzle[c_i++] = 0;
  } else if (format == GST_VIDEO_FORMAT_RGBx || format == GST_VIDEO_FORMAT_BGRx) {
    swizzle[c_i++] = 3;
  } else {
    for (i = finfo->n_components; i < GST_VIDEO_MAX_COMPONENTS; i++) {
      swizzle[c_i++] = -1;
    }
  }
}

static void
get_vulkan_rgb_format_swizzle_order (VkFormat format, gint * swizzle)
{
  const GstVulkanFormatInfo *finfo = gst_vulkan_format_get_info (format);
  int i;

  g_return_if_fail (finfo->flags & GST_VULKAN_FORMAT_FLAG_RGB);

  for (i = 0; i < finfo->n_components; i++) {
    swizzle[i] = finfo->poffset[i];
  }
  for (i = finfo->n_components; i < GST_VULKAN_MAX_COMPONENTS; i++) {
    swizzle[i] = -1;
  }
}

/* given a swizzle index, produce an index such that:
 *
 * swizzle[idx[i]] == identity[i] where:
 * - swizzle is the original swizzle
 * - idx is the result
 * - identity = {0, 1, 2,...}
 * - unset fields are marked by -1
 */
static void
swizzle_identity_order (gint * swizzle, gint * idx)
{
  int i;

  for (i = 0; i < 4; i++) {
    idx[i] = -1;
  }

  for (i = 0; i < 4; i++) {
    if (swizzle[i] >= 0 && swizzle[i] < 4) {
      idx[swizzle[i]] = i;
    }
  }
}

static void
update_descriptor_set (GstVulkanColorConvert * conv, VkImageView view)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (conv);

  /* *INDENT-OFF* */
  VkDescriptorImageInfo image_info = {
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .imageView = view,
      .sampler = conv->sampler
  };

  VkWriteDescriptorSet writes = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = NULL,
      .dstSet = conv->descriptor_set,
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .pImageInfo = &image_info
  };
  /* *INDENT-ON* */
  vkUpdateDescriptorSets (render->device->device, 1, &writes, 0, NULL);
}

static gboolean
swizzle_rgb_update_command_state (GstVulkanColorConvert * conv,
    VkCommandBuffer cmd, GstVulkanImageMemory ** in_mems,
    GstVulkanImageMemory ** out_mems)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (conv);
  gint in_reorder[4], out_reorder[4], in_vk_order[4], out_vk_order[4], tmp[4],
      reorder[8];
  int i;

  get_vulkan_rgb_format_swizzle_order (in_mems[0]->create_info.format,
      in_vk_order);
  get_rgb_format_swizzle_order (GST_VIDEO_INFO_FORMAT (&render->in_info),
      in_reorder);

  get_rgb_format_swizzle_order (GST_VIDEO_INFO_FORMAT (&render->out_info),
      out_reorder);
  get_vulkan_rgb_format_swizzle_order (out_mems[0]->create_info.format,
      out_vk_order);

  for (i = 0; i < 4; i++)
    tmp[i] = out_vk_order[out_reorder[i]];
  for (i = 0; i < 4; i++)
    reorder[i] = in_vk_order[in_reorder[i]];
  /* find the identity order for RGBA->$format */
  swizzle_identity_order (tmp, &reorder[4]);

  vkCmdPushConstants (cmd, render->pipeline_layout,
      VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (reorder),
      (const void *) reorder);
  update_descriptor_set (conv, in_mems[0]->view);
  vkCmdBindDescriptorSets (cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
      render->pipeline_layout, 0, 1, &conv->descriptor_set, 0, NULL);

  return TRUE;
}

static gboolean gst_vulkan_color_convert_start (GstBaseTransform * bt);
static gboolean gst_vulkan_color_convert_stop (GstBaseTransform * bt);

static GstCaps *gst_vulkan_color_convert_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstFlowReturn gst_vulkan_color_convert_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_vulkan_color_convert_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps);

static VkAttachmentReference
    * gst_vulkan_color_convert_render_pass_attachment_references
    (GstVulkanFullScreenRender * render, guint * n_attachments);
static VkAttachmentDescription
    * gst_vulkan_color_convert_render_pass_attachment_descriptions
    (GstVulkanFullScreenRender * render, guint * n_descriptions);
static VkDescriptorSetLayoutBinding
    * gst_vulkan_color_convert_descriptor_set_layout_bindings
    (GstVulkanFullScreenRender * render, guint * n_bindings);
static void
gst_vulkan_color_convert_shader_create_info (GstVulkanFullScreenRender *
    render);
static VkPushConstantRange
    * gst_vulkan_color_convert_push_constant_ranges (GstVulkanFullScreenRender *
    render, guint * n_constants);

static GstStaticPadTemplate gst_vulkan_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
            "{ BGRA, RGBA, ABGR, ARGB, BGRx, RGBx, xBGR, xRGB }")));

static GstStaticPadTemplate gst_vulkan_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE,
            "{ BGRA, RGBA, ABGR, ARGB, BGRx, RGBx, xBGR, xRGB }")));

enum
{
  PROP_0,
};

enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* static guint gst_vulkan_color_convert_signals[LAST_SIGNAL] = { 0 }; */

#define gst_vulkan_color_convert_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanColorConvert, gst_vulkan_color_convert,
    GST_TYPE_VULKAN_FULL_SCREEN_RENDER,
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_color_convert,
        "vulkancolorconvert", 0, "Vulkan Color Convert"));

static void
fill_shader_info (void)
{
  GstVideoFormat rgba[] = { GST_VIDEO_FORMAT_RGBA, GST_VIDEO_FORMAT_ARGB,
    GST_VIDEO_FORMAT_BGRA, GST_VIDEO_FORMAT_ABGR
  };
  GstVideoFormat rgbx[] = { GST_VIDEO_FORMAT_RGBx, GST_VIDEO_FORMAT_xRGB,
    GST_VIDEO_FORMAT_BGRx, GST_VIDEO_FORMAT_xBGR
  };
  int info_i = 0;
  int i, j;

  /* standard RGB with alpha conversion all components are copied */
  for (i = 0; i < G_N_ELEMENTS (rgba); i++) {
    for (j = 0; j < G_N_ELEMENTS (rgba); j++) {
      /* *INDENT-OFF* */
      shader_infos[info_i++] = (struct shader_info) {
          .from = rgba[i],
          .to = rgba[j],
          .cmd_state_update = swizzle_rgb_update_command_state,
          .frag_code = swizzle_frag,
          .frag_size = swizzle_frag_size,
      };
      /* *INDENT-ON* */
    }
    /* copying to an RGBx variant means we can store whatever we like in the 'x'
     * component we choose to copy the alpha component like a standard RGBA->RGBA
     * swizzle */
    for (j = 0; j < G_N_ELEMENTS (rgbx); j++) {
      /* *INDENT-OFF* */
      shader_infos[info_i++] = (struct shader_info) {
          .from = rgba[i],
          .to = rgbx[j],
          .cmd_state_update = swizzle_rgb_update_command_state,
          .frag_code = swizzle_frag,
          .frag_size = swizzle_frag_size,
      };
      /* *INDENT-ON* */
    }
  }
  for (i = 0; i < G_N_ELEMENTS (rgbx); i++) {
    /* copying to an RGBx variant means we can store whatever we like in the 'x'
     * component we choose to copy the 'x' component like a standard RGBA->RGBA
     * swizzle */
    for (j = 0; j < G_N_ELEMENTS (rgbx); j++) {
      /* *INDENT-OFF* */
      shader_infos[info_i++] = (struct shader_info) {
          .from = rgbx[i],
          .to = rgbx[j],
          .cmd_state_update = swizzle_rgb_update_command_state,
          .frag_code = swizzle_frag,
          .frag_size = swizzle_frag_size,
      };
      /* *INDENT-ON* */
    }
    /* copying from RGBx to RGBA requires clobbering the destination alpha
     * with 1.0 */
    for (j = 0; j < G_N_ELEMENTS (rgba); j++) {
      /* *INDENT-OFF* */
      shader_infos[info_i++] = (struct shader_info) {
          .from = rgbx[i],
          .to = rgba[j],
          .cmd_state_update = swizzle_rgb_update_command_state,
          .frag_code = swizzle_and_clobber_alpha_frag,
          .frag_size = swizzle_and_clobber_alpha_frag_size,
      };
      /* *INDENT-ON* */
    }
  }

  g_assert (info_i == N_SHADER_INFO);
}

static void
gst_vulkan_color_convert_class_init (GstVulkanColorConvertClass * klass)
{
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;
  GstVulkanFullScreenRenderClass *fullscreenrender_class;

  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;
  fullscreenrender_class = (GstVulkanFullScreenRenderClass *) klass;

  gst_element_class_set_metadata (gstelement_class, "Vulkan Uploader",
      "Filter/Video/Convert", "A Vulkan Color Convert",
      "Matthew Waters <matthew@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_vulkan_src_template);

  gstbasetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_vulkan_color_convert_start);
  gstbasetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_vulkan_color_convert_stop);
  gstbasetransform_class->transform_caps =
      gst_vulkan_color_convert_transform_caps;
  gstbasetransform_class->set_caps = gst_vulkan_color_convert_set_caps;
  gstbasetransform_class->transform = gst_vulkan_color_convert_transform;

  fullscreenrender_class->render_pass_attachment_references =
      gst_vulkan_color_convert_render_pass_attachment_references;
  fullscreenrender_class->render_pass_attachment_descriptions =
      gst_vulkan_color_convert_render_pass_attachment_descriptions;
  fullscreenrender_class->descriptor_set_layout_bindings =
      gst_vulkan_color_convert_descriptor_set_layout_bindings;
  fullscreenrender_class->shader_create_info =
      gst_vulkan_color_convert_shader_create_info;
  fullscreenrender_class->push_constant_ranges =
      gst_vulkan_color_convert_push_constant_ranges;

  fill_shader_info ();
}

static void
gst_vulkan_color_convert_init (GstVulkanColorConvert * conv)
{
}

static void
_init_value_string_list (GValue * list, ...)
{
  GValue item = G_VALUE_INIT;
  gchar *str;
  va_list args;

  g_value_init (list, GST_TYPE_LIST);

  va_start (args, list);
  while ((str = va_arg (args, gchar *))) {
    g_value_init (&item, G_TYPE_STRING);
    g_value_set_string (&item, str);

    gst_value_list_append_value (list, &item);
    g_value_unset (&item);
  }
  va_end (args);
}

static void
_append_value_string_list (GValue * list, ...)
{
  GValue item = G_VALUE_INIT;
  gchar *str;
  va_list args;

  va_start (args, list);
  while ((str = va_arg (args, gchar *))) {
    g_value_init (&item, G_TYPE_STRING);
    g_value_set_string (&item, str);

    gst_value_list_append_value (list, &item);
    g_value_unset (&item);
  }
  va_end (args);
}

static void
_init_supported_formats (GstVulkanDevice * device, gboolean output,
    GValue * supported_formats)
{
  /* Assume if device == NULL that we don't have a Vulkan device and can
   * do the conversion */

  /* Always supported input and output formats */
  _init_value_string_list (supported_formats, "RGBA", "RGB", "RGBx", "BGR",
      "BGRx", "BGRA", "xRGB", "xBGR", "ARGB", NULL);

  _append_value_string_list (supported_formats, "ABGR", NULL);
}

/* copies the given caps */
static GstCaps *
gst_vulkan_color_convert_transform_format_info (GstVulkanDevice * device,
    gboolean output, GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GValue supported_formats = G_VALUE_INIT;
  GValue rgb_formats = G_VALUE_INIT;
  GValue supported_rgb_formats = G_VALUE_INIT;

  /* There are effectively two modes here with the RGB/YUV transition:
   * 1. There is a RGB-like format as input and we can transform to YUV or,
   * 2. No RGB-like format as input so we can only transform to RGB-like formats
   *
   * We also filter down the list of formats depending on what the OpenGL
   * device supports (when provided).
   */

  _init_value_string_list (&rgb_formats, "RGBA", "ARGB", "BGRA", "ABGR", "RGBx",
      "xRGB", "BGRx", "xBGR", "RGB", "BGR", "ARGB64", NULL);
  _init_supported_formats (device, output, &supported_formats);
  gst_value_intersect (&supported_rgb_formats, &rgb_formats,
      &supported_formats);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    const GValue *format;

    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    format = gst_structure_get_value (st, "format");
    st = gst_structure_copy (st);
    if (GST_VALUE_HOLDS_LIST (format)) {
      gboolean have_rgb_formats = FALSE;
      GValue passthrough_formats = G_VALUE_INIT;
      gint j, len;

      g_value_init (&passthrough_formats, GST_TYPE_LIST);
      len = gst_value_list_get_size (format);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          const gchar *format_str = g_value_get_string (val);
          GstVideoFormat v_format = gst_video_format_from_string (format_str);
          const GstVideoFormatInfo *t_info =
              gst_video_format_get_info (v_format);
          if (GST_VIDEO_FORMAT_INFO_FLAGS (t_info) & (GST_VIDEO_FORMAT_FLAG_YUV
                  | GST_VIDEO_FORMAT_FLAG_GRAY)) {
            gst_value_list_append_value (&passthrough_formats, val);
          } else if (GST_VIDEO_FORMAT_INFO_FLAGS (t_info) &
              GST_VIDEO_FORMAT_FLAG_RGB) {
            have_rgb_formats = TRUE;
            break;
          }
        }
      }
      if (have_rgb_formats) {
        gst_structure_set_value (st, "format", &supported_formats);
      } else {
        /* add passthrough structure, then the rgb conversion structure */
        gst_structure_set_value (st, "format", &passthrough_formats);
        gst_caps_append_structure_full (res, gst_structure_copy (st),
            gst_caps_features_copy (f));
        gst_structure_set_value (st, "format", &supported_rgb_formats);
      }
      g_value_unset (&passthrough_formats);
    } else if (G_VALUE_HOLDS_STRING (format)) {
      const gchar *format_str = g_value_get_string (format);
      GstVideoFormat v_format = gst_video_format_from_string (format_str);
      const GstVideoFormatInfo *t_info = gst_video_format_get_info (v_format);
      if (GST_VIDEO_FORMAT_INFO_FLAGS (t_info) & (GST_VIDEO_FORMAT_FLAG_YUV |
              GST_VIDEO_FORMAT_FLAG_GRAY)) {
        /* add passthrough structure, then the rgb conversion structure */
        gst_structure_set_value (st, "format", format);
        gst_caps_append_structure_full (res, gst_structure_copy (st),
            gst_caps_features_copy (f));
        gst_structure_set_value (st, "format", &supported_rgb_formats);
      } else {                  /* RGB */
        gst_structure_set_value (st, "format", &supported_formats);
      }
    }
    gst_structure_remove_fields (st, "colorimetry", "chroma-site", NULL);

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  g_value_unset (&supported_formats);
  g_value_unset (&rgb_formats);
  g_value_unset (&supported_rgb_formats);

  return res;
}

static GstCaps *
gst_vulkan_color_convert_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);

  caps = gst_vulkan_color_convert_transform_format_info (render->device,
      direction == GST_PAD_SRC, caps);

  if (filter) {
    GstCaps *tmp;

    tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  return caps;
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
gst_vulkan_color_convert_shader_create_info (GstVulkanFullScreenRender * render)
{
  GstVulkanColorConvert *conv = GST_VULKAN_COLOR_CONVERT (render);
  VkShaderModule vert_module, frag_module;

  vert_module =
      _vk_create_shader (render->device, identity_vert, identity_vert_size,
      NULL);
  frag_module =
      _vk_create_shader (render->device, conv->current_shader->frag_code,
      conv->current_shader->frag_size, NULL);

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
  /* *INDENT-ON* */
}

static VkPushConstantRange *
gst_vulkan_color_convert_push_constant_ranges (GstVulkanFullScreenRender *
    render, guint * n_constants)
{
  VkPushConstantRange *ranges;

  *n_constants = 1;
  ranges = g_new0 (VkPushConstantRange, *n_constants);

  /* *INDENT-OFF* */
  ranges[0] = (VkPushConstantRange) {
    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    .offset = 0,
    .size = 4 * sizeof(gint32),
  };
  /* *INDENT-ON* */

  return ranges;
}

static VkDescriptorSetLayoutBinding
    * gst_vulkan_color_convert_descriptor_set_layout_bindings
    (GstVulkanFullScreenRender * render, guint * n_bindings)
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
    * gst_vulkan_color_convert_render_pass_attachment_references
    (GstVulkanFullScreenRender * render, guint * n_attachments)
{
  VkAttachmentReference *attachments;
  int i;

  *n_attachments = GST_VIDEO_INFO_N_PLANES (&render->in_info);
  attachments = g_new0 (VkAttachmentReference, *n_attachments);

  for (i = 0; i < *n_attachments; i++) {
    /* *INDENT-OFF* */
    attachments[i] = (VkAttachmentReference) {
       .attachment = i,
       .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    /* *INDENT-ON* */
  }

  return attachments;
}

static VkAttachmentDescription
    * gst_vulkan_color_convert_render_pass_attachment_descriptions
    (GstVulkanFullScreenRender * render, guint * n_descriptions)
{
  VkAttachmentDescription *color_attachments;
  int i;

  *n_descriptions = GST_VIDEO_INFO_N_PLANES (&render->in_info);
  color_attachments = g_new0 (VkAttachmentDescription, *n_descriptions);
  for (i = 0; i < *n_descriptions; i++) {
    /* *INDENT-OFF* */
    color_attachments[i] = (VkAttachmentDescription) {
        .format = gst_vulkan_format_from_video_format (GST_VIDEO_INFO_FORMAT (&render->in_info), i),
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
  }

  return color_attachments;
}

static VkDescriptorPool
_create_descriptor_pool (GstVulkanColorConvert * conv)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (conv);

  /* *INDENT-OFF* */
  VkDescriptorPoolSize pool_sizes[] = {
      {
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1
      },
      {
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1
      },
  };

  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .pNext = NULL,
      .poolSizeCount = G_N_ELEMENTS (pool_sizes),
      .pPoolSizes = pool_sizes,
      .maxSets = 1
  };
  /* *INDENT-ON* */
  VkDescriptorPool pool;
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

  return pool;
}

static VkDescriptorSet
_create_descriptor_set (GstVulkanColorConvert * conv)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (conv);

  /* *INDENT-OFF* */
  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext = NULL,
      .descriptorPool = conv->descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &render->descriptor_set_layout
  };
  /* *INDENT-ON* */
  VkDescriptorSet descriptor;
  GError *error = NULL;
  VkResult err;

  err =
      vkAllocateDescriptorSets (render->device->device, &alloc_info,
      &descriptor);
  if (gst_vulkan_error_to_g_error (err, &error, "vkAllocateDescriptorSets") < 0) {
    GST_ERROR_OBJECT (conv, "Failed to allocate descriptor: %s",
        error->message);
    g_clear_error (&error);
    return NULL;
  }

  return descriptor;
}

static VkSampler
_create_sampler (GstVulkanColorConvert * conv)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (conv);

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

  err = vkCreateSampler (render->device->device, &samplerInfo, NULL, &sampler);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateSampler") < 0) {
    GST_ERROR_OBJECT (conv, "Failed to create sampler: %s", error->message);
    g_clear_error (&error);
    return NULL;
  }

  return sampler;
}

static gboolean
gst_vulkan_color_convert_start (GstBaseTransform * bt)
{
  GstVulkanColorConvert *conv = GST_VULKAN_COLOR_CONVERT (bt);

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->start (bt))
    return FALSE;

  if (!(conv->sampler = _create_sampler (conv)))
    return FALSE;
  if (!(conv->descriptor_pool = _create_descriptor_pool (conv)))
    return FALSE;
  if (!(conv->descriptor_set = _create_descriptor_set (conv)))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vulkan_color_convert_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstVulkanColorConvert *conv = GST_VULKAN_COLOR_CONVERT (bt);
  GstVideoInfo in_info, out_info;
  int i;

  conv->current_shader = NULL;

  if (!gst_video_info_from_caps (&in_info, in_caps))
    return FALSE;
  if (!gst_video_info_from_caps (&out_info, out_caps))
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (shader_infos); i++) {
    if (shader_infos[i].from != GST_VIDEO_INFO_FORMAT (&in_info))
      continue;
    if (shader_infos[i].to != GST_VIDEO_INFO_FORMAT (&out_info))
      continue;

    GST_INFO_OBJECT (conv,
        "Found compatible conversion information from %s to %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&in_info)),
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&out_info)));
    conv->current_shader = &shader_infos[i];
  }

  if (!conv->current_shader) {
    GST_ERROR_OBJECT (conv, "Could not find a conversion info for the "
        "requested formats");
    return FALSE;
  }

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->set_caps (bt, in_caps,
          out_caps))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vulkan_color_convert_stop (GstBaseTransform * bt)
{
  GstVulkanColorConvert *conv = GST_VULKAN_COLOR_CONVERT (bt);
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);

  conv->current_shader = NULL;

  if (render->device) {
    if (render->last_fence) {
      render->trash_list = g_list_prepend (render->trash_list,
          gst_vulkan_trash_new_free_descriptor_pool (gst_vulkan_fence_ref
              (render->last_fence), conv->descriptor_pool));
      conv->descriptor_set = NULL;
      conv->descriptor_pool = NULL;
      render->trash_list = g_list_prepend (render->trash_list,
          gst_vulkan_trash_new_free_sampler (gst_vulkan_fence_ref
              (render->last_fence), conv->sampler));
      conv->sampler = NULL;
    } else {
      vkDestroyDescriptorPool (render->device->device,
          conv->descriptor_pool, NULL);
      conv->descriptor_set = NULL;
      conv->descriptor_pool = NULL;
      vkDestroySampler (render->device->device, conv->sampler, NULL);
      conv->sampler = NULL;
    }
  }

  if (conv->cmd_pool)
    gst_object_unref (conv->cmd_pool);
  conv->cmd_pool = NULL;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (bt);
}

static VkFramebuffer
_create_framebuffer (GstVulkanColorConvert * conv, guint n_views,
    VkImageView * views)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (conv);

  /* *INDENT-OFF* */
  VkFramebufferCreateInfo framebuffer_info = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext = NULL,
      .renderPass = render->render_pass,
      .attachmentCount = n_views,
      .pAttachments = views,
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
    return NULL;
  }

  return framebuffer;
}

static GstFlowReturn
gst_vulkan_color_convert_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVulkanFullScreenRender *render = GST_VULKAN_FULL_SCREEN_RENDER (bt);
  GstVulkanColorConvert *conv = GST_VULKAN_COLOR_CONVERT (bt);
  GstVulkanImageMemory *in_img_mems[4] = { NULL, };
  GstVulkanImageMemory *out_img_mems[4] = { NULL, };
  GstVulkanFence *fence = NULL;
  VkFramebuffer framebuffer;
  VkCommandBuffer cmd;
  GError *error = NULL;
  VkResult err;
  int i;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&render->in_info); i++) {
    GstMemory *mem = gst_buffer_peek_memory (inbuf, i);
    if (!gst_is_vulkan_image_memory (mem)) {
      g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
          "Input memory must be a GstVulkanImageMemory");
      goto error;
    }
    in_img_mems[i] = (GstVulkanImageMemory *) mem;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&render->out_info); i++) {
    GstMemory *mem = gst_buffer_peek_memory (outbuf, i);
    if (!gst_is_vulkan_image_memory (mem)) {
      g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
          "Output memory must be a GstVulkanImageMemory");
      goto error;
    }
    out_img_mems[i] = (GstVulkanImageMemory *) mem;
  }

  if (!conv->cmd_pool) {
    if (!(conv->cmd_pool =
            gst_vulkan_queue_create_command_pool (render->queue, &error)))
      goto error;
  }

  if (!(cmd = gst_vulkan_command_pool_create (conv->cmd_pool, &error)))
    goto error;

  {
    VkImageView attachments[4] = { 0, };
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&render->out_info); i++) {
      attachments[i] = out_img_mems[i]->view;
    }

    if (!(framebuffer = _create_framebuffer (conv,
                GST_VIDEO_INFO_N_PLANES (&render->out_info), attachments))) {
      g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
          "Failed to create framebuffer");
      goto error;
    }
  }

  fence = gst_vulkan_fence_new (render->device, 0, &error);
  if (!fence)
    goto error;

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

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&render->in_info); i++) {
    /* *INDENT-OFF* */
    VkImageMemoryBarrier in_image_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = in_img_mems[i]->barrier.parent.access_flags,
        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
        .oldLayout = in_img_mems[i]->barrier.image_layout,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        /* FIXME: implement exclusive transfers */
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .image = in_img_mems[i]->image,
        .subresourceRange = in_img_mems[i]->barrier.subresource_range
    };
    /* *INDENT-ON* */

    vkCmdPipelineBarrier (cmd, in_img_mems[i]->barrier.parent.pipeline_stages,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1,
        &in_image_memory_barrier);

    in_img_mems[i]->barrier.parent.pipeline_stages =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    in_img_mems[i]->barrier.parent.access_flags =
        in_image_memory_barrier.dstAccessMask;
    in_img_mems[i]->barrier.image_layout = in_image_memory_barrier.newLayout;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&render->in_info); i++) {
    /* *INDENT-OFF* */
    VkImageMemoryBarrier out_image_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = NULL,
        .srcAccessMask = out_img_mems[i]->barrier.parent.access_flags,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = out_img_mems[i]->barrier.image_layout,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        /* FIXME: implement exclusive transfers */
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .image = out_img_mems[i]->image,
        .subresourceRange = out_img_mems[i]->barrier.subresource_range
    };
    /* *INDENT-ON* */

    vkCmdPipelineBarrier (cmd, out_img_mems[i]->barrier.parent.pipeline_stages,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
        &out_image_memory_barrier);

    out_img_mems[i]->barrier.parent.pipeline_stages =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    out_img_mems[i]->barrier.parent.access_flags =
        out_image_memory_barrier.dstAccessMask;
    out_img_mems[i]->barrier.image_layout = out_image_memory_barrier.newLayout;
  }

  conv->current_shader->cmd_state_update (conv, cmd, in_img_mems, out_img_mems);
  if (!gst_vulkan_full_screen_render_fill_command_buffer (render, cmd,
          framebuffer))
    return GST_FLOW_ERROR;

  err = vkEndCommandBuffer (cmd);
  if (gst_vulkan_error_to_g_error (err, &error, "vkEndCommandBuffer") < 0)
    goto error;

  render->trash_list =
      g_list_prepend (render->trash_list,
      gst_vulkan_trash_new_free_framebuffer (gst_vulkan_fence_ref (fence),
          framebuffer));
  render->trash_list = g_list_prepend (render->trash_list,
      gst_vulkan_trash_new_free_command_buffer (gst_vulkan_fence_ref (fence),
          conv->cmd_pool, cmd));

  if (!gst_vulkan_full_screen_render_submit (render, cmd, fence))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;

error:
  GST_ELEMENT_ERROR (bt, LIBRARY, FAILED, ("%s", error->message), (NULL));
  g_clear_error (&error);
  return GST_FLOW_ERROR;
}

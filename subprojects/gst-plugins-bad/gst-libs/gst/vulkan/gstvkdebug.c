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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gprintf.h>

#include "gstvkerror.h"
#include "gstvkdebug.h"
#include "gstvkapi.h"
#include "gstvkdebug-private.h"

/**
 * SECTION:vkdebug
 * @title: GstVulkanDebug
 * @short_description: Vulkan debugging utilities
 * @see_also: #GstVulkanDevice
 */

#define FLAGS_TO_STRING(under_name, VkType)                                     \
gchar * G_PASTE(G_PASTE(gst_vulkan_,under_name),_flags_to_string) (VkType flag_bits) \
{                                                                               \
  GString *s = g_string_new (NULL);                                             \
  gboolean first = TRUE;                                                        \
  int i;                                                                        \
  for (i = 0; i < G_N_ELEMENTS (G_PASTE(G_PASTE(vk_,under_name),_flags_map)); i++) { \
    if (flag_bits & G_PASTE(G_PASTE(vk_,under_name),_flags_map)[i].flag_bit) {  \
      if (!first) {                                                             \
        g_string_append (s, "|");                                               \
      }                                                                         \
      g_string_append (s, G_PASTE(G_PASTE(vk_,under_name),_flags_map)[i].str); \
      first = FALSE;                                                            \
    }                                                                           \
  }                                                                             \
  return g_string_free (s, FALSE);                                              \
}

/* *INDENT-OFF* */
static const struct
{
  VkMemoryPropertyFlagBits flag_bit;
  const char *str;
} vk_memory_property_flags_map[] = {
  {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "device-local"},
  {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "host-visible"},
  {VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "host-coherent"},
  {VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "host-cached"},
  {VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, "lazily-allocated"},
#if VK_HEADER_VERSION >= 70
  {VK_MEMORY_PROPERTY_PROTECTED_BIT, "protected"},
#endif
#if VK_HEADER_VERSION >= 121
  {VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD, "device-coherent"},
  {VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD, "device-uncached"},
#endif
};
/**
 * gst_vulkan_memory_property_flags_to_string:
 *
 * Since: 1.18
 */
FLAGS_TO_STRING(memory_property, VkMemoryPropertyFlags);

static const struct
{
  VkMemoryHeapFlagBits flag_bit;
  const char *str;
} vk_memory_heap_flags_map[] = {
  {VK_MEMORY_HEAP_DEVICE_LOCAL_BIT, "device-local"},
#if VK_HEADER_VERSION >= 70
  {VK_MEMORY_HEAP_MULTI_INSTANCE_BIT, "multi-instance"},
#endif
};
/**
 * gst_vulkan_memory_heap_flags_to_string:
 *
 * Since: 1.18
 */
FLAGS_TO_STRING(memory_heap, VkMemoryHeapFlags);

static const struct
{
  VkQueueFlagBits flag_bit;
  const char *str;
} vk_queue_flags_map[] = {
  {VK_QUEUE_GRAPHICS_BIT, "graphics"},
  {VK_QUEUE_COMPUTE_BIT, "compute"},
  {VK_QUEUE_TRANSFER_BIT, "transfer"},
  {VK_QUEUE_SPARSE_BINDING_BIT, "sparse-binding"},
#if VK_HEADER_VERSION >= 70
  {VK_QUEUE_PROTECTED_BIT, "protected"},
#endif
#if defined(VK_KHR_video_decode_queue)
  {VK_QUEUE_VIDEO_DECODE_BIT_KHR, "decode"},
#endif
#if defined(VK_KHR_video_encode_queue)
  {VK_QUEUE_VIDEO_ENCODE_BIT_KHR, "encode"}
#endif
};
/**
 * gst_vulkan_queue_flags_to_string:
 *
 * Since: 1.18
 */
FLAGS_TO_STRING(queue, VkQueueFlags);

static const struct
{
  VkSampleCountFlagBits flag_bit;
  const char *str;
} vk_sample_count_flags_map[] = {
  {VK_SAMPLE_COUNT_1_BIT, "1"},
  {VK_SAMPLE_COUNT_2_BIT, "2"},
  {VK_SAMPLE_COUNT_4_BIT, "4"},
  {VK_SAMPLE_COUNT_8_BIT, "8"},
  {VK_SAMPLE_COUNT_16_BIT, "16"},
  {VK_SAMPLE_COUNT_32_BIT, "32"},
  {VK_SAMPLE_COUNT_64_BIT, "64"},
};
/**
 * gst_vulkan_sample_count_flags_to_string:
 *
 * Since: 1.18
 */
FLAGS_TO_STRING(sample_count, VkSampleCountFlags);
/* *INDENT-ON* */

static const struct
{
  VkShaderStageFlagBits flag_bit;
  const char *str;
} vk_shader_stage_flags_map[] = {
  {VK_SHADER_STAGE_VERTEX_BIT, "vertex"},
  {VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "tessellation-control"},
  {VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "tessellation-evaluation"},
  {VK_SHADER_STAGE_GEOMETRY_BIT, "geometry"},
  {VK_SHADER_STAGE_FRAGMENT_BIT, "fragment"},
  {VK_SHADER_STAGE_COMPUTE_BIT, "compute"},
#if defined(VK_KHR_ray_tracing_pipeline)
  {VK_SHADER_STAGE_RAYGEN_BIT_KHR, "raygen"},
  {VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "any-hit"},
  {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "closest-hit"},
  {VK_SHADER_STAGE_MISS_BIT_KHR, "miss"},
  {VK_SHADER_STAGE_INTERSECTION_BIT_KHR, "intersection"},
  {VK_SHADER_STAGE_CALLABLE_BIT_KHR, "callable"},
#endif
#if defined(VK_EXT_mesh_shader)
  {VK_SHADER_STAGE_TASK_BIT_EXT, "task"},
  {VK_SHADER_STAGE_MESH_BIT_EXT, "mesh"},
#endif
};

/**
 * gst_vulkan_shader_stage_flags_to_string:
 *
 * Since: 1.30
 */
FLAGS_TO_STRING (shader_stage, VkShaderStageFlags);

static const struct
{
  VkResolveModeFlagBits flag_bit;
  const char *str;
} vk_resolve_mode_flags_map[] = {
#if defined(VK_VERSION_1_2)
  {VK_RESOLVE_MODE_SAMPLE_ZERO_BIT, "sample-zero"},
  {VK_RESOLVE_MODE_AVERAGE_BIT, "average"},
  {VK_RESOLVE_MODE_MIN_BIT, "min"},
  {VK_RESOLVE_MODE_MAX_BIT, "max"},
#if defined(VK_ANDROID_external_format_resolve)
  {VK_RESOLVE_MODE_EXTERNAL_FORMAT_DOWNSAMPLE_BIT_ANDROID_BIT,
      "external-format-downsample"},
#endif
#if defined(VK_EXT_custom_resolve)
  {VK_RESOLVE_MODE_CUSTOM_BIT_EXT, "custom"},
#endif
#endif
};

/**
 * gst_vulkan_resolve_mode_flags_to_string:
 *
 * Since: 1.30
 */
FLAGS_TO_STRING (resolve_mode, VkResolveModeFlags);

static const struct
{
  VkSubgroupFeatureFlagBits flag_bit;
  const char *str;
} vk_subgroup_feature_flags_map[] = {
#if defined(VK_VERSION_1_1)
  {VK_SUBGROUP_FEATURE_BASIC_BIT, "basic"},
  {VK_SUBGROUP_FEATURE_VOTE_BIT, "vote"},
  {VK_SUBGROUP_FEATURE_ARITHMETIC_BIT, "arithmetic"},
  {VK_SUBGROUP_FEATURE_BALLOT_BIT, "ballot"},
  {VK_SUBGROUP_FEATURE_SHUFFLE_BIT, "shuffle"},
  {VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT, "shuffle-relative"},
  {VK_SUBGROUP_FEATURE_CLUSTERED_BIT, "clustered"},
  {VK_SUBGROUP_FEATURE_QUAD_BIT, "quad"},
#if defined(VK_VERSION_1_4)
  {VK_SUBGROUP_FEATURE_ROTATE_BIT, "rotate"},
  {VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT, "rotate-clustered"},
#endif
#if defined(VK_EXT_shader_subgroup_partitioned)
  {VK_SUBGROUP_FEATURE_PARTITIONED_BIT_EXT, "partitioned"},
#endif
#endif
};

/**
 * gst_vulkan_subgroup_feature_flags_to_string:
 *
 * Since: 1.30
 */
FLAGS_TO_STRING (subgroup_feature, VkSubgroupFeatureFlags);

/**
 * gst_vulkan_physical_device_type_to_string:
 * @type: a `VkPhysicalDeviceType
 *
 * Returns: name of @type
 *
 * Since: 1.18
 */
const gchar *
gst_vulkan_physical_device_type_to_string (VkPhysicalDeviceType type)
{
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
      return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return "CPU";
    default:
      return "unknown";
  }
}

/**
 * gst_vulkan_present_mode_to_string:
 * @present_mode: a `VkPresentModeKHR`
 *
 * Returns: name of @present_mode
 *
 * Since: 1.20
 */
const gchar *
gst_vulkan_present_mode_to_string (VkPresentModeKHR present_mode)
{
  switch (present_mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
      return "FIFO";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
      return "immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
      return "mailbox";
      /* XXX: add other values as necessary */
    default:
      return "unknown";
  }
}

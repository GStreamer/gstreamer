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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvkimagebufferpool.h"
#include "gstvkphysicaldevice-private.h"

#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
#include "gst/vulkan/gstvkvideoutils-private.h"
#endif

/**
 * SECTION:vkimagebufferpool
 * @title: GstVulkanImageBufferPool
 * @short_description: buffer pool for #GstVulkanImageMemory objects
 * @see_also: #GstBufferPool, #GstVulkanImageMemory
 *
 * a #GstVulkanImageBufferPool is an object that allocates buffers with #GstVulkanImageMemory
 *
 * A #GstVulkanImageBufferPool is created with gst_vulkan_image_buffer_pool_new()
 */

const static VkImageUsageFlags default_usage =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
    | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

/* bufferpool */
struct _GstVulkanImageBufferPoolPrivate
{
  GstCaps *caps;
  gboolean raw_caps;
  GstVideoInfo v_info;
  VkImageUsageFlags usage;
  VkImageCreateFlags img_flags;
  VkMemoryPropertyFlags mem_props;
  VkImageLayout initial_layout;
  guint64 initial_access;
  VkFormat vk_fmts[GST_VIDEO_MAX_PLANES];
  int n_imgs;
  guint32 n_layers;
  guint32 n_profiles;
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
  GstVulkanVideoProfile profiles[2];
#endif
  GstVulkanOperation *exec;
  gboolean add_videometa;
  /* Set via gst_vulkan_image_buffer_pool_config_set_export_handle_types(). */
  VkExternalMemoryHandleTypeFlags export_mem_handle_types;
  VkExternalSemaphoreHandleTypeFlags export_sem_handle_types;
};

static void gst_vulkan_image_buffer_pool_finalize (GObject * object);

GST_DEBUG_CATEGORY_STATIC (GST_CAT_VULKAN_IMAGE_BUFFER_POOL);
#define GST_CAT_DEFAULT GST_CAT_VULKAN_IMAGE_BUFFER_POOL

#define GET_PRIV(pool) gst_vulkan_image_buffer_pool_get_instance_private (pool)

#define gst_vulkan_image_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanImageBufferPool, gst_vulkan_image_buffer_pool,
    GST_TYPE_BUFFER_POOL, G_ADD_PRIVATE (GstVulkanImageBufferPool)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_VULKAN_IMAGE_BUFFER_POOL,
        "vulkanimagebufferpool", 0, "Vulkan Image Buffer Pool"));

/**
 * gst_vulkan_image_buffer_pool_config_set_allocation_params:
 * @config: the #GstStructure with the pool's configuration.
 * @usage: The Vulkan image usage flags.
 * @mem_properties: Vulkan memory property flags.
 * @initial_layout: Initial Vulkan image layout.
 * @initial_access: Access flags for the layout transition if @initial_layout is
 * not VK_IMAGE_LAYOUT_UNDEFINED or VK_IMAGE_LAYOUT_PREINITIALIZED.
 *
 * Sets the @usage and @mem_properties, @initial_layout and @initial_access of
 * the images to setup.
 *
 * If @initial_access is VK_IMAGE_LAYOUT_UNDEFINED or
 * VK_IMAGE_LAYOUT_PREINITIALIZED, the image crated by this pool has not been
 * initialized to a particular layout
 *
 * Since: 1.24
 */
void
gst_vulkan_image_buffer_pool_config_set_allocation_params (GstStructure *
    config, VkImageUsageFlags usage, VkMemoryPropertyFlags mem_properties,
    VkImageLayout initial_layout, guint64 initial_access)
{
  g_return_if_fail (GST_IS_STRUCTURE (config));

  /* assumption: G_TYPE_UINT is compatible with uint32_t (VkFlags) */
  gst_structure_set (config, "usage", G_TYPE_UINT, usage, "memory-properties",
      G_TYPE_UINT, mem_properties, "initial-layout", G_TYPE_UINT,
      initial_layout, "initial-access", G_TYPE_UINT64, initial_access, NULL);
}

/**
 * gst_vulkan_image_buffer_pool_config_set_decode_caps:
 * @config: the #GstStructure with the pool's configuration.
 * @caps: Upstream decode caps.
 *
 * Decode @caps are used when the buffers are going to be used either as decoded
 * dest or DPB images.
 *
 * Since: 1.24
 */
void
gst_vulkan_image_buffer_pool_config_set_decode_caps (GstStructure * config,
    GstCaps * caps)
{
  g_return_if_fail (GST_IS_CAPS (caps));

  gst_structure_set (config, "decode-caps", GST_TYPE_CAPS, caps, NULL);
}

/**
 * gst_vulkan_image_buffer_pool_config_set_encode_caps:
 * @config: the #GstStructure with the pool's configuration.
 * @caps: Upstream encode caps.
 *
 * Encode @caps are used when the buffers are going to be used either as encoded
 * src or DPB images.
 *
 * Since: 1.26
 */
void
gst_vulkan_image_buffer_pool_config_set_encode_caps (GstStructure * config,
    GstCaps * caps)
{
  g_return_if_fail (GST_IS_CAPS (caps));

  gst_structure_set (config, "encode-caps", GST_TYPE_CAPS, caps, NULL);
}

/**
 * gst_vulkan_image_buffer_pool_config_set_export_handle_types:
 * @config: the #GstStructure with the pool's configuration.
 * @mem_handle_types: the VkExternalMemoryHandleTypeFlags to request.
 * @sem_handle_types: the VkExternalSemaphoreHandleTypeFlags to request.
 *
 * Activates exportable-memory mode: a non-zero @mem_handle_types or
 * @sem_handle_types requests the matching Vulkan export capability for each
 * allocation.
 *
 * Since: 1.30
 */
void
gst_vulkan_image_buffer_pool_config_set_export_handle_types (GstStructure *
    config, VkExternalMemoryHandleTypeFlags mem_handle_types,
    VkExternalSemaphoreHandleTypeFlags sem_handle_types)
{
  g_return_if_fail (GST_IS_STRUCTURE (config));

  gst_structure_set (config, "export-memory-handle-types", G_TYPE_UINT,
      (guint) mem_handle_types, NULL);
  gst_structure_set (config, "export-semaphore-handle-types", G_TYPE_UINT,
      (guint) sem_handle_types, NULL);
}

/**
 * gst_vulkan_image_buffer_pool_config_get_export_handle_types:
 * @config: the #GstStructure with the pool's configuration.
 * @mem_handle_types: (out): the VkExternalMemoryHandleTypeFlags value.
 * @sem_handle_types: (out): the VkExternalSemaphoreHandleTypeFlags value.
 *
 * Gets the handle types previously set via
 * gst_vulkan_image_buffer_pool_config_set_export_handle_types().
 *
 * Since: 1.30
 */
void
gst_vulkan_image_buffer_pool_config_get_export_handle_types (GstStructure *
    config, VkExternalMemoryHandleTypeFlags * mem_handle_types,
    VkExternalSemaphoreHandleTypeFlags * sem_handle_types)
{
  g_return_if_fail (GST_IS_STRUCTURE (config));
  g_return_if_fail (mem_handle_types != NULL);
  g_return_if_fail (sem_handle_types != NULL);

  if (!gst_structure_get_uint (config, "export-memory-handle-types",
          (guint *) mem_handle_types))
    *mem_handle_types = 0;
  if (!gst_structure_get_uint (config, "export-semaphore-handle-types",
          (guint *) sem_handle_types))
    *sem_handle_types = 0;

}

/**
 * gst_vulkan_image_buffer_pool_config_get_allocation_params:
 * @config: the #GstStructure with the pool's configuration.
 * @usage: (out) (optional): The Vulkan image usage flags.
 * @mem_props: (out) (optional): Vulkan memory property flags.
 * @initial_layout: (out) (optional): Initial Vulkan image layout.
 * @initial_access: (out) (optional): Initial Vulkan access flags.
 *
 * Gets the configuration of the Vulkan image buffer pool.
 *
 * Since: 1.26
 */
void
gst_vulkan_image_buffer_pool_config_get_allocation_params (GstStructure *
    config, VkImageUsageFlags * usage, VkMemoryPropertyFlags * mem_props,
    VkImageLayout * initial_layout, guint64 * initial_access)
{
  g_return_if_fail (GST_IS_STRUCTURE (config));

  if (usage) {
    if (!gst_structure_get_uint (config, "usage", usage))
      *usage = default_usage;
  }

  if (mem_props) {
    if (!gst_structure_get_uint (config, "memory-properties", mem_props))
      *mem_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }

  if (initial_layout) {
    if (!gst_structure_get_uint (config, "initial-layout", initial_layout))
      *initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  if (initial_access) {
    if (!gst_structure_get_uint64 (config, "initial-access", initial_access))
      *initial_access = 0;      /* VK_ACCESS_NONE */
  }
}

static inline void
internal_config_get_allocation_params (GstStructure * config,
    VkImageUsageFlags * usage, VkMemoryPropertyFlags * mem_props,
    VkImageLayout * initial_layout, guint64 * initial_access,
    guint32 * n_layers, GstCaps ** decode_caps, GstCaps ** encode_caps)
{
  gst_vulkan_image_buffer_pool_config_get_allocation_params (config, usage,
      mem_props, initial_layout, initial_access);

  if (!gst_structure_get_uint (config, "num-layers", n_layers))
    *n_layers = 1;

  if (decode_caps)
    gst_structure_get (config, "decode-caps", GST_TYPE_CAPS, decode_caps, NULL);

  if (encode_caps)
    gst_structure_get (config, "encode-caps", GST_TYPE_CAPS, encode_caps, NULL);
}

static gboolean
_is_video_usage (VkImageUsageFlags requested_usage)
{
  VkImageUsageFlags video_usage = 0;

#if defined(VK_KHR_video_decode_queue)
  video_usage |= (VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR);
#endif
#if defined(VK_KHR_video_encode_queue)
  video_usage |= (VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR);
#endif

  return ((requested_usage & video_usage) != 0);
}

#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
static gboolean
_is_video_profile_independent (VkImageUsageFlags requested_usage)
{
  VkImageUsageFlags video_dependent = 0;

#if defined(VK_KHR_video_decode_queue)
  if ((requested_usage & VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR) != 0
      && (requested_usage & VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR) == 0)
    return FALSE;
#endif
#if defined(VK_KHR_video_encode_queue)
  video_dependent |= VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;
#endif
#if defined(VK_KHR_video_encode_quantization_map)
  video_dependent |= VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR;
#endif
#if defined(VK_KHR_video_encode_quantization_map)
  video_dependent |= VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR;
#endif

  return ((requested_usage & video_dependent) == 0);
}

/* Resolves how an image/format configured with video decode/encode usage
 * bits must satisfy the Vulkan video-profile requirement (see
 * VUID-VkPhysicalDeviceImageFormatInfo2-pNext-pNext and its
 * VkImageCreateInfo equivalent): either the profile-independent creation
 * flag, or a VkVideoProfileListInfoKHR naming the pool's configured
 * profiles. Shared between the real image creation in fill_buffer() and the
 * capability probe in _check_exportable_memory_support(), so both see
 * exactly the same video-profile info.
 *
 * @profiles and @profile_list are caller-owned storage filled in when a
 * profile list is needed; the return value is that same @profile_list
 * pointer, or %NULL if no pNext chaining is required (no video usage, or
 * the profile-independent flag already covers it). @extra_flags is always
 * set to the VkImageCreateFlags bits (if any) the caller must OR in. */
static VkVideoProfileListInfoKHR *
_get_video_profile_list (GstVulkanImageBufferPool * vk_pool,
    VkImageUsageFlags usage, VkVideoProfileInfoKHR profiles[2],
    VkVideoProfileListInfoKHR * profile_list, VkImageCreateFlags * extra_flags)
{
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);

  *extra_flags = 0;

  if (!_is_video_usage (usage))
    return NULL;

  if (gst_vulkan_physical_device_has_feature_video_maintenance1
      (vk_pool->device->physical_device)
      && _is_video_profile_independent (usage)) {
#if defined(VK_KHR_video_maintenance1)
    *extra_flags = VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
#endif
    return NULL;
  }

  if (priv->n_profiles > 0) {
    int i;

    for (i = 0; i < priv->n_profiles; i++)
      profiles[i] = priv->profiles[i].profile;

    *profile_list = (VkVideoProfileListInfoKHR) {
    .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,.profileCount =
          priv->n_profiles,.pProfiles = profiles,};
    return profile_list;
  }

  return NULL;
}
#endif /* GST_VULKAN_HAVE_VIDEO_EXTENSIONS */

static gpointer
_get_vulkan_func_pointer (GstVulkanImageBufferPool * vk_pool, guint major,
    guint minor, guint patch, const char *func_name, const char *func_name2,
    const char *log_addon)
{
  GstVulkanDevice *device = vk_pool->device;
  gpointer func = NULL;

  if (gst_vulkan_physical_device_check_api_version (device->physical_device,
          major, minor, patch)) {
    func = gst_vulkan_instance_get_proc_address (device->instance, func_name);
  }
  if (!func) {
    func = gst_vulkan_instance_get_proc_address (device->instance, func_name2);
  }
  if (!func) {
    GST_WARNING_OBJECT (vk_pool,
        "Neither %s nor %s is available. %s", func_name, func_name2, log_addon);
    return NULL;
  }
  return func;
}

/* Verify that the driver actually advertises external-memory export for
 * the requested format/usage/tiling combination. We test every VkFormat
 * the pool plans to allocate: per-plane allocation goes through multiple
 * VkFormats (e.g. R8 + R8G8 for NV12), so all of them have to report
 * VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT or the import will fail on
 * the last plane and leave us with half-valid state. */
static gboolean
_check_exportable_memory_support (GstVulkanImageBufferPool * vk_pool,
    VkImageTiling tiling, VkImageUsageFlags usage,
    VkExternalMemoryHandleTypeFlags handle_types)
{
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  GstVulkanDevice *device = vk_pool->device;
  VkPhysicalDevice gpu = gst_vulkan_device_get_physical_device (device);
  PFN_vkGetPhysicalDeviceImageFormatProperties2KHR get_format_props2 = NULL;
  int i;
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
  VkVideoProfileInfoKHR video_profiles[2];
  VkVideoProfileListInfoKHR video_profile_list;
#endif
  gpointer video_profile_list_ptr = NULL;
  VkImageCreateFlags video_extra_flags = 0;

  get_format_props2 = (PFN_vkGetPhysicalDeviceImageFormatProperties2KHR)
      _get_vulkan_func_pointer (vk_pool, 1, 1, 0,
      "vkGetPhysicalDeviceImageFormatProperties2",
      "vkGetPhysicalDeviceImageFormatProperties2KHR",
      "Cannot verify external-memory export support");
  if (!get_format_props2)
    return FALSE;

  /* A video-usage query must chain the same VkVideoProfileListInfoKHR (or
   * carry the same profile-independent flag) the real image creation would
   * use, per VUID-VkPhysicalDeviceImageFormatInfo2-pNext-pNext. */
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
  video_profile_list_ptr = _get_video_profile_list (vk_pool, usage,
      video_profiles, &video_profile_list, &video_extra_flags);
#endif

  for (i = 0; i < priv->n_imgs; i++) {
    /* handleType takes exactly one bit per the spec's valid usage rules, so
     * a multi-bit handle_types mask has to be probed one bit at a time. */
    for (VkFlags bit = 1u; bit; bit <<= 1u) {
      if ((bit & handle_types) == 0)
        continue;

      VkPhysicalDeviceExternalImageFormatInfo ext_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .pNext = video_profile_list_ptr,
        .handleType = bit,
      };
      VkPhysicalDeviceImageFormatInfo2 fmt_info = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &ext_info,
        .format = priv->vk_fmts[i],
        .type = VK_IMAGE_TYPE_2D,
        .tiling = tiling,
        .usage = usage,
        .flags = priv->img_flags | video_extra_flags,
      };
      VkExternalImageFormatProperties ext_props = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
        .pNext = NULL,
      };
      VkImageFormatProperties2 fmt_props = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &ext_props,
      };
      VkResult err = get_format_props2 (gpu, &fmt_info, &fmt_props);

      if (err != VK_SUCCESS) {
        GST_WARNING_OBJECT (vk_pool,
            "vkGetPhysicalDeviceImageFormatProperties2 failed for "
            "vk_fmt=%d usage=0x%x flags=0x%x tiling=%d handle=0x%x: %d",
            (gint) priv->vk_fmts[i], (guint) usage, (guint) priv->img_flags,
            (gint) tiling, (guint) bit, (int) err);
        return FALSE;
      }

      if ((ext_props.externalMemoryProperties.externalMemoryFeatures
              & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) == 0) {
        GST_WARNING_OBJECT (vk_pool,
            "driver does not advertise EXPORTABLE for "
            "vk_fmt=%d usage=0x%x flags=0x%x tiling=%d handle=0x%x "
            "(features=0x%x compatHandles=0x%x exportFromImportedHandles=0x%x)",
            (gint) priv->vk_fmts[i], (guint) usage, (guint) priv->img_flags,
            (gint) tiling, (guint) bit,
            (guint) ext_props.externalMemoryProperties.externalMemoryFeatures,
            (guint) ext_props.externalMemoryProperties.compatibleHandleTypes,
            (guint) ext_props.externalMemoryProperties.
            exportFromImportedHandleTypes);
        return FALSE;
      }

      /* For a multi-bit handle_types mask, every other requested bit must
       * also be reported as compatible with this one, or the driver can't
       * actually export a single allocation satisfying all of them. */
      VkExternalMemoryHandleTypeFlags others = handle_types & ~bit;
      if (others & ~ext_props.externalMemoryProperties.compatibleHandleTypes) {
        GST_WARNING_OBJECT (vk_pool,
            "driver does not advertise handle=0x%x as compatible with the "
            "other requested handle types (0x%x) for vk_fmt=%d usage=0x%x "
            "flags=0x%x tiling=%d (compatHandles=0x%x)",
            (guint) bit, (guint) others, (gint) priv->vk_fmts[i],
            (guint) usage, (guint) priv->img_flags, (gint) tiling,
            (guint) ext_props.externalMemoryProperties.compatibleHandleTypes);
        return FALSE;
      }
    }
  }

  return TRUE;
}

/* Verify that the driver advertises export support for the timeline
 * semaphore handle type the pool was configured with. Unlike the image
 * memory query, this has no format/tiling/usage dependency, so it only
 * needs to be probed once per requested handle type. */
static gboolean
_check_exportable_semaphore_support (GstVulkanImageBufferPool * vk_pool,
    VkExternalSemaphoreHandleTypeFlags handle_types)
{
  GstVulkanDevice *device = vk_pool->device;
  VkPhysicalDevice gpu = gst_vulkan_device_get_physical_device (device);
  PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR
      get_external_semaphore_props = NULL;

  get_external_semaphore_props =
      (PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR)
      _get_vulkan_func_pointer (vk_pool, 1, 1, 0,
      "vkGetPhysicalDeviceExternalSemaphoreProperties",
      "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR",
      "Cannot verify external-semaphore export support");
  if (!get_external_semaphore_props)
    return FALSE;

  /* handleType takes exactly one bit per the spec's valid usage rules, so
   * a multi-bit handle_types mask has to be probed one bit at a time. */
  for (VkFlags bit = 1u; bit; bit <<= 1u) {
    if ((bit & handle_types) == 0)
      continue;

    VkPhysicalDeviceExternalSemaphoreInfo ext_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
      .pNext = NULL,
      .handleType = bit,
    };
    VkExternalSemaphoreProperties sem_props = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
      .pNext = NULL,
    };

    get_external_semaphore_props (gpu, &ext_info, &sem_props);

    if ((sem_props.externalSemaphoreFeatures
            & VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT) == 0) {
      GST_WARNING_OBJECT (vk_pool,
          "driver does not advertise EXPORTABLE for timeline semaphore "
          "handle=0x%x (features=0x%x compatHandles=0x%x "
          "exportFromImportedHandles=0x%x)",
          (guint) bit, (guint) sem_props.externalSemaphoreFeatures,
          (guint) sem_props.compatibleHandleTypes,
          (guint) sem_props.exportFromImportedHandleTypes);
      return FALSE;
    }

    /* For a multi-bit handle_types mask, every other requested bit must
     * also be reported as compatible with this one, or the driver can't
     * actually export a single semaphore satisfying all of them. */
    VkExternalSemaphoreHandleTypeFlags others = handle_types & ~bit;
    if (others & ~sem_props.compatibleHandleTypes) {
      GST_WARNING_OBJECT (vk_pool,
          "driver does not advertise timeline semaphore handle=0x%x as "
          "compatible with the other requested handle types (0x%x) "
          "(compatHandles=0x%x)",
          (guint) bit, (guint) others, (guint) sem_props.compatibleHandleTypes);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_vulkan_image_buffer_pool_fill_buffer (GstVulkanImageBufferPool * vk_pool,
    VkImageTiling tiling, gsize offset[GST_VIDEO_MAX_PLANES],
    GstBuffer * buffer)
{
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  int i;
  VkImageCreateInfo image_info;
  VkExternalMemoryImageCreateInfo ext_image_info = {
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .pNext = NULL,
    .handleTypes = priv->export_mem_handle_types,
  };
  gpointer profile_list_ptr = NULL;
  VkImageCreateFlags video_extra_flags = 0;
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
  VkVideoProfileInfoKHR profiles[2];
  VkVideoProfileListInfoKHR profile_list;

  profile_list_ptr = _get_video_profile_list (vk_pool, priv->usage, profiles,
      &profile_list, &video_extra_flags);
#endif

  /* *INDENT-OFF* */
  image_info = (VkImageCreateInfo) {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = profile_list_ptr,
    .flags = priv->img_flags | video_extra_flags,
    .imageType = VK_IMAGE_TYPE_2D,
    /* .format = fill per image,  */
    /* .extent = fill per plane, */
    .mipLevels = 1,
    .arrayLayers = priv->n_layers,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = tiling,
    .usage = priv->usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = NULL,
    .initialLayout = priv->initial_layout == VK_IMAGE_LAYOUT_PREINITIALIZED
                     ? VK_IMAGE_LAYOUT_PREINITIALIZED
                     : VK_IMAGE_LAYOUT_UNDEFINED,
  };
  /* *INDENT-ON* */

  /* Chain VkExternalMemoryImageCreateInfo on top of whatever pNext was
   * already set (NULL or the video profile list). The backing allocator
   * (gstvkimagememory.c) scans this same pNext chain to discover the
   * requested handle types and mirrors them into a
   * VkExportMemoryAllocateInfo for vkAllocateMemory. */
  if (priv->export_mem_handle_types != 0) {
    ext_image_info.pNext = image_info.pNext;
    image_info.pNext = &ext_image_info;
  }

  priv->v_info.size = 0;
  for (i = 0; i < priv->n_imgs; i++) {
    GstMemory *mem;
    guint width, height;

    if (GST_VIDEO_INFO_N_PLANES (&priv->v_info) != priv->n_imgs) {
      width = GST_VIDEO_INFO_WIDTH (&priv->v_info);
      height = GST_VIDEO_INFO_HEIGHT (&priv->v_info);
    } else {
      gint comp[GST_VIDEO_MAX_COMPONENTS];

      gst_video_format_info_component (priv->v_info.finfo, i, comp);
      width = GST_VIDEO_INFO_COMP_WIDTH (&priv->v_info, comp[0]);
      height = GST_VIDEO_INFO_COMP_HEIGHT (&priv->v_info, comp[0]);
    }

    image_info.format = priv->vk_fmts[i];
    /* *INDENT-OFF* */
    image_info.extent = (VkExtent3D) { width, height, 1 };
    /* *INDENT-ON* */

    mem = gst_vulkan_image_memory_alloc_with_image_info2 (vk_pool->device,
        &image_info, priv->mem_props, priv->export_sem_handle_types);
    if (!mem)
      return FALSE;

    if (buffer) {
      if (i < GST_VIDEO_MAX_PLANES - 1)
        offset[i + 1] = offset[i] + mem->size;

      gst_buffer_append_memory (buffer, mem);
    } else {
      GstVulkanImageMemory *img_mem = (GstVulkanImageMemory *) mem;

      priv->v_info.offset[i] = priv->v_info.size;
      priv->v_info.size += img_mem->requirements.size;

      gst_memory_unref (mem);
    }
  }

  return TRUE;
}

static gboolean
gst_vulkan_image_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstVulkanImageBufferPool *vk_pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (pool);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  VkImageTiling tiling;
  VkImageUsageFlags requested_usage;
  guint min_buffers, max_buffers;
  GstCaps *caps = NULL, *decode_caps = NULL, *encode_caps = NULL;
  GstCapsFeatures *features;
  gboolean found, no_multiplane;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&priv->v_info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, priv->v_info.width,
      priv->v_info.height, caps);

  gst_caps_replace (&priv->caps, caps);

  features = gst_caps_get_features (caps, 0);
  priv->raw_caps = features == NULL || gst_caps_features_is_equal (features,
      GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY);

  internal_config_get_allocation_params (config, &requested_usage,
      &priv->mem_props, &priv->initial_layout, &priv->initial_access,
      &priv->n_layers, &decode_caps, &encode_caps);

  priv->n_profiles = 0;

#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
  if (_is_video_usage (requested_usage)) {
    GstVulkanPhysicalDevice *gpu = vk_pool->device->physical_device;
    if (!gst_vulkan_physical_device_has_feature_video_maintenance1 (gpu)
        || !_is_video_profile_independent (requested_usage)) {
      guint n = 0;

#if defined(VK_KHR_video_decode_queue)
      if (decode_caps && ((requested_usage
                  & (VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR
                      | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR)) != 0)) {
        n++;
        if (gst_vulkan_video_profile_from_caps (&priv->
                profiles[priv->n_profiles], decode_caps,
                GST_VULKAN_VIDEO_OPERATION_DECODE))
          priv->n_profiles++;
      }
#endif
#if defined(VK_KHR_video_encode_queue)
      if (encode_caps && ((requested_usage
                  & (VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR
                      | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR)) != 0)) {
        n++;
        if (gst_vulkan_video_profile_from_caps (&priv->
                profiles[priv->n_profiles], encode_caps,
                GST_VULKAN_VIDEO_OPERATION_ENCODE))
          priv->n_profiles++;
      }
#endif
      if (priv->n_profiles != n)
        goto missing_profile;
      if (priv->n_profiles == 0)
        GST_WARNING ("Vulkan video image allocation without video profiles");
    }
  }
#endif /* GST_VULKAN_HAVE_VIDEO_EXTENSIONS */

  gst_clear_caps (&decode_caps);
  gst_clear_caps (&encode_caps);

  gboolean want_multiplanar_opt = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VULKAN_IMAGE_MULTIPLANAR_YUV);
  /* Composite YUV VkImage on two paths: Vulkan Video usage
   * (traditional), or explicit opt-in for non-video consumers
   * (DMABUF importers, compute colour-convert, external encoders). */
  no_multiplane = !(GST_VIDEO_INFO_IS_YUV (&priv->v_info)
      && (_is_video_usage (requested_usage) || want_multiplanar_opt));

  tiling = priv->raw_caps ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
  found = gst_vulkan_format_from_video_info_2 (vk_pool->device,
      &priv->v_info, tiling, no_multiplane, requested_usage, priv->vk_fmts,
      &priv->n_imgs, NULL);
  if (!found)
    goto no_vk_format;

  {
    gboolean sampleable;
    const GstVulkanFormatMap *vkmap;

    sampleable = ((requested_usage &
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0);

    priv->img_flags = 0;

    if (sampleable && !_is_video_usage (requested_usage)) {
      vkmap = gst_vulkan_format_get_map (GST_VIDEO_INFO_FORMAT (&priv->v_info));
      const gboolean is_per_plane_alloc = vkmap->vkfrmt != priv->vk_fmts[0];
      priv->img_flags = VK_IMAGE_CREATE_ALIAS_BIT;
      if (GST_VIDEO_INFO_N_PLANES (&priv->v_info) > 1
          && (is_per_plane_alloc || want_multiplanar_opt)) {
        priv->img_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
            | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
      }
    }
  }

  priv->usage = requested_usage;

  /* Exportable-memory: activated when the caller sets a non-zero handle-types
   * value via gst_vulkan_image_buffer_pool_config_set_export_handle_types(). */
  priv->export_mem_handle_types = 0;
  priv->export_sem_handle_types = 0;
  gst_vulkan_image_buffer_pool_config_get_export_handle_types (config,
      &priv->export_mem_handle_types, &priv->export_sem_handle_types);
  if (priv->export_mem_handle_types != 0 &&
      !_check_exportable_memory_support (vk_pool, tiling, requested_usage,
          priv->export_mem_handle_types))
    goto exportable_mem_unsupported;
  if (priv->export_sem_handle_types != 0 &&
      !_check_exportable_semaphore_support (vk_pool,
          priv->export_sem_handle_types))
    goto exportable_sem_unsupported;

  GST_DEBUG_OBJECT (pool,
      "pool config: usage=0x%x img_flags=0x%x n_imgs=%d vk_fmt[0]=%d "
      "video-usage=%d sampleable=%d export_mem_handle_types=0x%x "
      "export_sem_handle_types=0x%x",
      (guint) priv->usage, (guint) priv->img_flags, priv->n_imgs,
      (gint) priv->vk_fmts[0], _is_video_usage (requested_usage),
      ((requested_usage & (VK_IMAGE_USAGE_SAMPLED_BIT
                  | VK_IMAGE_USAGE_STORAGE_BIT)) != 0),
      (guint) priv->export_mem_handle_types,
      (guint) priv->export_sem_handle_types);

  /* get the size of the buffer to allocate */
  if (!gst_vulkan_image_buffer_pool_fill_buffer (vk_pool, tiling, NULL, NULL))
    goto image_failed;

  gst_buffer_pool_config_set_params (config, caps,
      priv->v_info.size, min_buffers, max_buffers);

  /* enable metadata based on config of the pool */
  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
no_vk_format:
  {
    GST_WARNING_OBJECT (pool, "no Vulkan format available for %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&priv->v_info)));
    return FALSE;
  }
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
missing_profile:
  {
    gst_clear_caps (&decode_caps);
    gst_clear_caps (&encode_caps);

    GST_WARNING_OBJECT (pool, "missing or invalid decode-caps");
    return FALSE;
  }
#endif
image_failed:
  {
    GST_WARNING_OBJECT (pool, "Failed to allocate image");
    return FALSE;
  }
exportable_mem_unsupported:
  {
    GST_WARNING_OBJECT (pool,
        "Exportable external memory (mem_handle_types=0x%x) is not advertised "
        "by this driver for the requested format/usage/tiling.",
        (guint) priv->export_mem_handle_types);
    return FALSE;
  }
exportable_sem_unsupported:
  {
    GST_WARNING_OBJECT (pool,
        "Exportable external timeline semaphore (sem_handle_types=0x%x) is "
        "not advertised by this driver.",
        (guint) priv->export_sem_handle_types);
    return FALSE;
  }
}

static gboolean
prepare_buffer (GstVulkanImageBufferPool * vk_pool, GstBuffer * buffer)
{
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  GError *error = NULL;

  if (priv->initial_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
      priv->initial_layout == VK_IMAGE_LAYOUT_PREINITIALIZED)
    return TRUE;

  if (!priv->exec) {
    GstVulkanCommandPool *cmd_pool;
    GstVulkanQueue *queue = NULL;

    queue =
        gst_vulkan_device_select_queue (vk_pool->device,
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
    if (!queue)
      return FALSE;
    cmd_pool = gst_vulkan_queue_create_command_pool (queue, &error);
    gst_object_unref (queue);
    if (error)
      goto error;
    priv->exec = gst_vulkan_operation_new (cmd_pool);
    gst_object_unref (cmd_pool);
  }

  if (!gst_vulkan_operation_add_dependency_frame (priv->exec, buffer,
          VK_PIPELINE_STAGE_NONE_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT))
    return FALSE;

  if (!gst_vulkan_operation_begin (priv->exec, &error))
    goto reset_and_error;

  if (!gst_vulkan_operation_add_frame_barrier (priv->exec, buffer,
          VK_PIPELINE_STAGE_NONE_KHR, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          priv->initial_access, priv->initial_layout, NULL))
    goto reset_and_error;

  GstVulkanBarrierState *barriers =
      gst_vulkan_operation_get_barriers (priv->exec);
  gst_vulkan_barrier_state_pipeline_barrier (barriers, priv->exec->cmd_buf, 0);

  if (!gst_vulkan_operation_end (priv->exec, &error))
    goto reset_and_error;

  return TRUE;

reset_and_error:
  {
    gst_vulkan_operation_reset (priv->exec);
    GST_WARNING_OBJECT (vk_pool, "Failed barrier operation");
    /* fallback to error */
  }

error:
  {
    if (error) {
      GST_WARNING_OBJECT (vk_pool, "Error: %s", error->message);
      g_clear_error (&error);
    }
    return FALSE;
  }
}

/* This function handles GstBuffer creation */
static GstFlowReturn
gst_vulkan_image_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstVulkanImageBufferPool *vk_pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (pool);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  VkImageTiling tiling =
      priv->raw_caps ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
  GstBuffer *buf;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };

  if (!(buf = gst_buffer_new ())) {
    goto no_buffer;
  }

  if (!gst_vulkan_image_buffer_pool_fill_buffer (vk_pool, tiling, offset, buf))
    goto mem_create_failed;

  if (!prepare_buffer (vk_pool, buf))
    goto mem_create_failed;

  if (priv->add_videometa) {
    gsize *off = (priv->n_imgs == 1) ? priv->v_info.offset : offset;

    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&priv->v_info),
        GST_VIDEO_INFO_WIDTH (&priv->v_info),
        GST_VIDEO_INFO_HEIGHT (&priv->v_info),
        GST_VIDEO_INFO_N_PLANES (&priv->v_info), off, priv->v_info.stride);
  }

  *buffer = buf;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "Could not create image");
    return GST_FLOW_ERROR;
  }
mem_create_failed:
  {
    gst_buffer_unref (buf);

    GST_WARNING_OBJECT (pool, "Could not create Vulkan Memory");
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_vulkan_image_buffer_pool_stop (GstBufferPool * pool)
{
  GstVulkanImageBufferPool *vk_pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (pool);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);

  if (priv->exec)
    gst_vulkan_operation_wait (priv->exec);

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (pool);
}

static void
gst_vulkan_image_buffer_pool_reset_buffer (GstBufferPool * pool,
    GstBuffer * buffer)
{
  GstVulkanImageBufferPool *vk_pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (pool);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  GstVulkanImageMemory *mem;
  guint i, n = gst_buffer_n_memory (buffer);

  GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (pool, buffer);

  for (i = 0; i < n; i++) {
    mem = (GstVulkanImageMemory *) gst_buffer_peek_memory (buffer, i);
    mem->barrier.parent.access_flags = priv->initial_access;
  }
}

static const gchar **
gst_vulkan_image_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VULKAN_IMAGE_MULTIPLANAR_YUV,
    NULL,
  };
  return options;
}

/**
 * gst_vulkan_image_buffer_pool_new:
 * @device: the #GstVulkanDevice to use
 *
 * Returns: (transfer full): a #GstBufferPool that allocates buffers with #GstGLMemory
 *
 * Since: 1.18
 */
GstBufferPool *
gst_vulkan_image_buffer_pool_new (GstVulkanDevice * device)
{
  GstVulkanImageBufferPool *pool;

  pool = g_object_new (GST_TYPE_VULKAN_IMAGE_BUFFER_POOL, NULL);
  g_object_ref_sink (pool);
  pool->device = gst_object_ref (device);

  GST_LOG_OBJECT (pool, "new Vulkan buffer pool for device %" GST_PTR_FORMAT,
      device);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_vulkan_image_buffer_pool_class_init (GstVulkanImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_vulkan_image_buffer_pool_finalize;

  gstbufferpool_class->set_config = gst_vulkan_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_vulkan_image_buffer_pool_alloc;
  gstbufferpool_class->stop = gst_vulkan_image_buffer_pool_stop;
  gstbufferpool_class->reset_buffer = gst_vulkan_image_buffer_pool_reset_buffer;
  gstbufferpool_class->get_options = gst_vulkan_image_buffer_pool_get_options;
}

static void
gst_vulkan_image_buffer_pool_init (GstVulkanImageBufferPool * pool)
{
}

static void
gst_vulkan_image_buffer_pool_finalize (GObject * object)
{
  GstVulkanImageBufferPool *pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (object);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (pool);

  GST_LOG_OBJECT (pool, "finalize Vulkan buffer pool %p", pool);

  if (priv->caps)
    gst_caps_unref (priv->caps);

  gst_clear_object (&priv->exec);

  G_OBJECT_CLASS (gst_vulkan_image_buffer_pool_parent_class)->finalize (object);

  /* only release the context once all our memory have been deleted */
  if (pool->device) {
    gst_object_unref (pool->device);
    pool->device = NULL;
  }
}

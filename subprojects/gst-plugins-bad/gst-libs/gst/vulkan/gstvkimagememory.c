/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include "gstvkimagememory.h"
#include "gstvkphysicaldevice-private.h"

/**
 * SECTION:vkimagememory
 * @title: GstVulkanImageMemory
 * @short_description: memory subclass for Vulkan image memory
 * @see_also: #GstVulkanDevice, #GstMemory, #GstAllocator
 *
 * GstVulkanImageMemory is a #GstMemory subclass providing support for the
 * mapping of Vulkan device memory.
 *
 * gst_memory_copy() is supported for the whole memory of images with one mip
 * level and one array layer that were created with
 * `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`. The copy is a new image with the same
 * format, extent, tiling and memory properties, and the usage of the source
 * image without the video usage flags. The image data is copied with
 * `vkCmdCopyImage` after the pending work signalled on the timeline semaphore
 * of the source image, and gst_memory_copy() waits for the copy to complete.
 */

#define GST_CAT_DEFUALT GST_CAT_VULKAN_IMAGE_MEMORY
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFUALT);

static GstAllocator *_vulkan_image_memory_allocator;

static gboolean
_create_info_from_args (VkImageCreateInfo * info, VkFormat format, gsize width,
    gsize height, VkImageTiling tiling, VkImageUsageFlags usage)
{
  /* FIXME: validate these */

  /* *INDENT-OFF* */
  *info = (VkImageCreateInfo) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = (VkExtent3D) { width, height, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = tiling,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  /* *INDENT-ON* */

  return TRUE;
}

/**
 * gst_vulkan_image_memory_init_with_external_semaphore:
 * @ext_flags: VkExternalSemaphoreHandleTypeFlags
 *
 * Like gst_vulkan_image_memory_init() but also creates the backing timeline
 * semaphore with the export capability given by @ext_flags.
 *
 * Returns: whether the memory was initialized
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_image_memory_init_with_external_semaphore (GstVulkanImageMemory *
    mem, GstAllocator * allocator, GstMemory * parent, GstVulkanDevice * device,
    VkFormat format, VkImageUsageFlags usage, VkImageLayout layout,
    GstAllocationParams * params, gsize size, gpointer user_data,
    GDestroyNotify notify, VkExternalSemaphoreHandleTypeFlags ext_flags)
{
  gsize align = gst_memory_alignment, offset = 0, maxsize = size;
  GstMemoryFlags flags = 0;

  if (params) {
    flags = params->flags;
    align |= params->align;
    offset = params->prefix;
    maxsize += params->prefix + params->padding + align;
  }

  gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, parent, maxsize,
      align, offset, size);

  mem->device = gst_object_ref (device);
  mem->barrier.parent.type = GST_VULKAN_BARRIER_TYPE_IMAGE;
  mem->barrier.parent.pipeline_stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  mem->barrier.parent.access_flags = 0;
  mem->barrier.image_layout = layout;
  /* *INDENT-OFF* */
  mem->barrier.subresource_range = (VkImageSubresourceRange) {
          .aspectMask = gst_vulkan_format_get_aspect (format),
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
  };
  /* *INDENT-ON* */
  mem->usage = usage;
  mem->wrapped = FALSE;
  mem->notify = notify;
  mem->user_data = user_data;

  g_mutex_init (&mem->lock);

  mem->views = g_ptr_array_new ();
  mem->outstanding_views = g_ptr_array_new ();
  mem->timeline_semaphore =
      gst_vulkan_timeline_semaphore_new_with_exportable (device, ext_flags);
  if (!mem->timeline_semaphore)
    return FALSE;

  GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY,
      "new Vulkan Image memory:%p size:%" G_GSIZE_FORMAT, mem, maxsize);

  return TRUE;
}

gboolean
gst_vulkan_image_memory_init (GstVulkanImageMemory * mem,
    GstAllocator * allocator, GstMemory * parent, GstVulkanDevice * device,
    VkFormat format, VkImageUsageFlags usage, VkImageLayout layout,
    GstAllocationParams * params, gsize size, gpointer user_data,
    GDestroyNotify notify)
{
  return gst_vulkan_image_memory_init_with_external_semaphore (mem, allocator,
      parent, device, format, usage, layout, params, size, user_data, notify,
      0);
}

static void
gst_get_memory_requirements2 (GstVulkanDevice * device, VkImage image,
    VkMemoryRequirements * requirements, void *pNext)
{
  VkMemoryRequirements2 reqs2 = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    .pNext = pNext,
  };
  VkImageMemoryRequirementsInfo2 reqs_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
    .pNext = NULL,
    .image = image,
  };
  PFN_vkGetImageMemoryRequirements2KHR get_reqs2 = NULL;

  if (gst_vulkan_physical_device_check_api_version (device->physical_device,
          1, 1, 0)) {
    get_reqs2 = (PFN_vkGetImageMemoryRequirements2)
        gst_vulkan_device_get_proc_address (device,
        "vkGetImageMemoryRequirements2");
  }
  if (!get_reqs2 && gst_vulkan_device_is_extension_enabled (device,
          VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME)) {
    get_reqs2 = (PFN_vkGetImageMemoryRequirements2KHR)
        gst_vulkan_device_get_proc_address (device,
        "vkGetImageMemoryRequirements2KHR");
  }

  if (get_reqs2) {
    get_reqs2 (device->device, &reqs_info, &reqs2);
    *requirements = reqs2.memoryRequirements;
  } else {
    /* No 2-variant available: base query, no dedicated-allocation hint. */
    vkGetImageMemoryRequirements (device->device, image, requirements);
  }
}

static GstVulkanImageMemory *
_vk_image_mem_new_alloc_with_image_info (GstAllocator * allocator,
    GstMemory * parent, GstVulkanDevice * device,
    const VkImageCreateInfo * image_info, VkMemoryPropertyFlags mem_prop_flags,
    gpointer user_data, GDestroyNotify notify,
    VkExternalSemaphoreHandleTypeFlags ext_sem_handle_types)
{
  GstVulkanImageMemory *mem = NULL;
  GstAllocationParams params = { 0, };
  VkPhysicalDevice gpu;
  GError *error = NULL;
  GArray *qfi = NULL;
  guint32 type_idx;
  VkImage image;
  VkResult err;
  gboolean wants_dedicated = FALSE;
  VkExternalMemoryImageCreateInfo *pInfoStruct =
      (VkExternalMemoryImageCreateInfo *) vk_find_struct (image_info->pNext,
      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);

  VkExternalMemoryHandleTypeFlags ext_mem_handle_types =
      pInfoStruct ? pInfoStruct->handleTypes : 0;

  gpu = gst_vulkan_device_get_physical_device (device);

  // Local copy to avoid altering image_info itself
  VkImageCreateInfo info = *image_info;
  if (!info.pQueueFamilyIndices) {
    /* Default to sharing across the device's created queue families */
    qfi = gst_vulkan_device_queue_family_indices (device);
    info.pQueueFamilyIndices = (uint32_t *) qfi->data;
    info.queueFamilyIndexCount = qfi->len;

    if (info.sharingMode == VK_SHARING_MODE_EXCLUSIVE && qfi->len > 1)
      GST_CAT_WARNING (GST_CAT_VULKAN_IMAGE_MEMORY,
          "%d queue families, overriding to VK_SHARING_MODE_CONCURRENT",
          qfi->len);
    info.sharingMode =
        qfi->len > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
  }

  err = vkCreateImage (device->device, &info, NULL, &image);

  if (qfi)
    g_array_unref (qfi);

  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateImage") < 0)
    goto vk_error;

  mem = g_new0 (GstVulkanImageMemory, 1);

  /* Ask the driver whether this image needs (or prefers) a dedicated
   * allocation for this external-memory export */
  VkMemoryDedicatedRequirements dedicated_reqs = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
    .pNext = NULL,
  };
  gst_get_memory_requirements2 (device, image, &mem->requirements,
      &dedicated_reqs);
  wants_dedicated = dedicated_reqs.requiresDedicatedAllocation
      || dedicated_reqs.prefersDedicatedAllocation;

  if (!gst_vulkan_image_memory_init_with_external_semaphore (mem, allocator,
          parent, device, info.format, info.usage, info.initialLayout, &params,
          mem->requirements.size, user_data, notify, ext_sem_handle_types))
    goto vk_error;
  mem->create_info = info;

  /* avoid handling pNext lifetime */
  mem->create_info.pNext = NULL;
  mem->image = image;
  mem->create_info.queueFamilyIndexCount = 0;
  mem->create_info.pQueueFamilyIndices = NULL;
  mem->exportable_handle_types = ext_mem_handle_types;

  err = vkGetPhysicalDeviceImageFormatProperties (gpu, info.format,
      info.imageType, info.tiling, info.usage, info.flags,
      &mem->format_properties);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkGetPhysicalDeviceImageFormatProperties") < 0)
    goto vk_error;

  if (!gst_vulkan_memory_find_memory_type_index_with_requirements (device,
          &mem->requirements, mem_prop_flags, &type_idx))
    goto error;

  if ((mem->requirements.alignment & (mem->requirements.alignment - 1)) != 0) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Vulkan implementation requires unsupported non-power-of 2 memory alignment");
    goto error;
  }
  params.align = mem->requirements.alignment - 1;
  if (ext_mem_handle_types != 0) {
    VkMemoryDedicatedAllocateInfo dedicated_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .buffer = VK_NULL_HANDLE,
      .image = image,
    };
    VkExportMemoryAllocateInfo export_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .pNext = wants_dedicated ? &dedicated_allocate_info : NULL,
      .handleTypes = ext_mem_handle_types,
    };
    mem->vk_mem = (GstVulkanMemory *)
        gst_vulkan_memory_alloc_with_pnext (device, type_idx, &params,
        mem->requirements.size, mem_prop_flags, &export_info);
  } else {
    mem->vk_mem = (GstVulkanMemory *) gst_vulkan_memory_alloc (device, type_idx,
        &params, mem->requirements.size, mem_prop_flags);
  }
  if (!mem->vk_mem)
    goto error;

  err = vkBindImageMemory (device->device, image, mem->vk_mem->mem_ptr, 0);
  if (gst_vulkan_error_to_g_error (err, &error, "vkBindImageMemory") < 0)
    goto vk_error;

  return mem;

vk_error:
  {
    GST_CAT_ERROR (GST_CAT_VULKAN_IMAGE_MEMORY,
        "Failed to allocate image memory %s", error->message);
    g_clear_error (&error);
    goto error;
  }

error:
  {
    if (mem)
      gst_memory_unref ((GstMemory *) mem);
    return NULL;
  }
}

static GstVulkanImageMemory *
_vk_image_mem_new_alloc (GstAllocator * allocator, GstMemory * parent,
    GstVulkanDevice * device, VkFormat format, gsize width, gsize height,
    VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags mem_prop_flags, gpointer user_data,
    GDestroyNotify notify)
{
  VkImageCreateInfo image_info;

  if (!_create_info_from_args (&image_info, format, width, height, tiling,
          usage)) {
    GST_CAT_ERROR (GST_CAT_VULKAN_IMAGE_MEMORY, "Incorrect image parameters");
    return NULL;
  }

  return _vk_image_mem_new_alloc_with_image_info (allocator, parent, device,
      &image_info, mem_prop_flags, user_data, notify, 0);
}

static GstVulkanImageMemory *
_vk_image_mem_new_wrapped_with_image_info (GstAllocator * allocator,
    GstMemory * parent, GstVulkanDevice * device, VkImage image,
    const VkImageCreateInfo * image_info, gpointer user_data,
    GDestroyNotify notify)
{
  GstVulkanImageMemory *mem = g_new0 (GstVulkanImageMemory, 1);
  GstAllocationParams params = { 0, };
  VkPhysicalDevice gpu;
  GError *error = NULL;
  VkResult err;

  gpu = gst_vulkan_device_get_physical_device (device);
  mem->image = image;

  vkGetImageMemoryRequirements (device->device, mem->image, &mem->requirements);

  /* XXX: assumes alignment is a power of 2 */
  params.align = mem->requirements.alignment - 1;
  params.flags = GST_MEMORY_FLAG_NOT_MAPPABLE;
  gst_vulkan_image_memory_init (mem, allocator, parent, device,
      image_info->format, image_info->usage, image_info->initialLayout, &params,
      mem->requirements.size, user_data, notify);
  mem->wrapped = TRUE;
  mem->create_info = *image_info;
  mem->create_info.pNext = NULL;
  mem->create_info.queueFamilyIndexCount = 0;
  mem->create_info.pQueueFamilyIndices = NULL;

  if (image_info->format != VK_FORMAT_UNDEFINED) {
    err = vkGetPhysicalDeviceImageFormatProperties (gpu, image_info->format,
        image_info->imageType, image_info->tiling, image_info->usage,
        image_info->flags, &mem->format_properties);
    if (gst_vulkan_error_to_g_error (err, &error,
            "vkGetPhysicalDeviceImageFormatProperties") < 0)
      goto vk_error;
  } else {
    mem->barrier.subresource_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  return mem;

vk_error:
  {
    GST_CAT_ERROR (GST_CAT_VULKAN_IMAGE_MEMORY,
        "Failed to allocate image memory %s", error->message);
    g_clear_error (&error);
    goto error;
  }

error:
  {
    gst_memory_unref ((GstMemory *) mem);
    return NULL;
  }
}

static GstVulkanImageMemory *
_vk_image_mem_new_wrapped (GstAllocator * allocator, GstMemory * parent,
    GstVulkanDevice * device, VkImage image, VkFormat format, gsize width,
    gsize height, VkImageTiling tiling, VkImageUsageFlags usage,
    gpointer user_data, GDestroyNotify notify)
{
  VkImageCreateInfo image_info;

  if (!_create_info_from_args (&image_info, format, width, height, tiling,
          usage)) {
    GST_CAT_ERROR (GST_CAT_VULKAN_IMAGE_MEMORY, "Incorrect image parameters");
    return NULL;
  }

  return _vk_image_mem_new_wrapped_with_image_info (allocator, parent, device,
      image, &image_info, user_data, notify);
}

static gpointer
_vk_image_mem_map_full (GstVulkanImageMemory * mem, GstMapInfo * info,
    gsize size)
{
  GstMapInfo *vk_map_info;

  /* FIXME: possible layout transformation needed */
  g_mutex_lock (&mem->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "locked image memory %p", mem);

  if (!mem->vk_mem) {
    g_mutex_unlock (&mem->lock);
    return NULL;
  }

  vk_map_info = g_new0 (GstMapInfo, 1);
  info->user_data[0] = vk_map_info;
  if (!gst_memory_map ((GstMemory *) mem->vk_mem, vk_map_info, info->flags)) {
    g_free (vk_map_info);
    g_mutex_unlock (&mem->lock);
    return NULL;
  }
  g_mutex_unlock (&mem->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "unlocked image memory %p", mem);

  return vk_map_info->data;
}

static void
_vk_image_mem_unmap_full (GstVulkanImageMemory * mem, GstMapInfo * info)
{
  g_mutex_lock (&mem->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "locked image memory %p", mem);
  gst_memory_unmap ((GstMemory *) mem->vk_mem, info->user_data[0]);
  g_mutex_unlock (&mem->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "unlocked image memory %p", mem);

  g_free (info->user_data[0]);
}

static guint
_fill_copy_regions (GstVulkanImageMemory * mem, VkImageCopy * regions)
{
  const GstVulkanFormatInfo *finfo = NULL;
  VkImageAspectFlags aspect;
  guint i, j, n_planes = 1;

  aspect = gst_vulkan_format_get_aspect (mem->create_info.format);
  if (aspect != VK_IMAGE_ASPECT_COLOR_BIT) {
    if (!(aspect & VK_IMAGE_ASPECT_PLANE_0_BIT))
      return 0;
    finfo = gst_vulkan_format_get_info (mem->create_info.format);
    if (!finfo)
      return 0;
    n_planes = finfo->n_planes;
  }

  for (i = 0; i < n_planes; i++) {
    VkImageAspectFlags plane_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    guint32 width = mem->create_info.extent.width;
    guint32 height = mem->create_info.extent.height;

    if (finfo) {
      plane_aspect = VK_IMAGE_ASPECT_PLANE_0_BIT << i;
      for (j = 0; j < finfo->n_components; j++) {
        if (finfo->plane[j] == i) {
          width = GST_VIDEO_SUB_SCALE (finfo->w_sub[j], width);
          height = GST_VIDEO_SUB_SCALE (finfo->h_sub[j], height);
          break;
        }
      }
    }

    VkImageSubresourceLayers subresource = {
      .aspectMask = plane_aspect,
      .mipLevel = 0,
      .baseArrayLayer = 0,
      .layerCount = 1,
    };
    VkImageCopy region = {
      .srcSubresource = subresource,
      .dstSubresource = subresource,
      .extent = {width, height, 1},
    };

    regions[i] = region;
  }

  return n_planes;
}

static gboolean
_drop_video_access (GstVulkanBarrierState * barriers,
    GstVulkanImageMemory * mem)
{
  GstVulkanBarrierImageInfo before = { 0, }, after = { 0, };
  guint64 video_access = 0;
  gboolean ret;

#if defined(VK_KHR_synchronization2)
#if defined(VK_KHR_video_decode_queue)
  video_access |= VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR
      | VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
#endif
#if defined(VK_KHR_video_encode_queue)
  video_access |= VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR
      | VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR;
#endif
#endif

  gst_vulkan_image_memory_lock (mem);
  gst_vulkan_image_memory_peek_barrier_unlocked (mem, &before);
  gst_vulkan_image_memory_unlock (mem);

  if (!(before.parent.access_flags & video_access)) {
    gst_vulkan_barrier_image_info_clear (&before);
    return TRUE;
  }

  gst_vulkan_barrier_state_update_image_barrier (barriers, mem,
      before.parent.pipeline_stages,
      before.parent.access_flags & ~video_access, before.image_layout,
      before.parent.queue);

  gst_vulkan_image_memory_lock (mem);
  gst_vulkan_image_memory_peek_barrier_unlocked (mem, &after);
  gst_vulkan_image_memory_unlock (mem);

  ret = gst_vulkan_barrier_image_info_is_equal (&before, &after);

  gst_vulkan_barrier_image_info_clear (&before);
  gst_vulkan_barrier_image_info_clear (&after);

  return ret;
}

static GstMemory *
_vk_image_mem_copy (GstVulkanImageMemory * src, gssize offset, gssize size)
{
  VkImageUsageFlags video_usage = 0;
  VkImageCopy regions[GST_VULKAN_MAX_COMPONENTS];
  VkMemoryPropertyFlags mem_prop_flags;
  VkImageCreateInfo image_info;
  GstVulkanImageMemory *dst = NULL;
  GstVulkanQueue *queue = NULL;
  GstVulkanCommandPool *cmd_pool = NULL;
  GstVulkanOperation *exec = NULL;
  GstVulkanBarrierState *barriers;
  GstBuffer *frame = NULL;
  GError *error = NULL;
  guint n_regions;

  if (offset != 0 || (size != -1 && size != src->parent.size)) {
    GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY, "image memory %p: only "
        "copies of the whole memory are supported", src);
    return NULL;
  }

  if (!(src->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
    GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY, "image memory %p: cannot "
        "copy an image without VK_IMAGE_USAGE_TRANSFER_SRC_BIT", src);
    return NULL;
  }

  if (src->create_info.mipLevels != 1 || src->create_info.arrayLayers != 1) {
    GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY, "image memory %p: only "
        "images with one mip level and one array layer can be copied", src);
    return NULL;
  }

  n_regions = _fill_copy_regions (src, regions);
  if (n_regions == 0) {
    GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY, "image memory %p: cannot "
        "copy an image with format %u", src, src->create_info.format);
    return NULL;
  }
#if defined(VK_KHR_video_decode_queue)
  video_usage |= VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_DECODE_SRC_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;
#endif
#if defined(VK_KHR_video_encode_queue)
  video_usage |= VK_IMAGE_USAGE_VIDEO_ENCODE_DST_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;
#endif
#if defined(VK_KHR_video_encode_quantization_map)
  video_usage |= VK_IMAGE_USAGE_VIDEO_ENCODE_QUANTIZATION_DELTA_MAP_BIT_KHR
      | VK_IMAGE_USAGE_VIDEO_ENCODE_EMPHASIS_MAP_BIT_KHR;
#endif

  image_info = src->create_info;
  image_info.pNext = NULL;
#if defined(VK_KHR_video_maintenance1)
  image_info.flags &= ~VK_IMAGE_CREATE_VIDEO_PROFILE_INDEPENDENT_BIT_KHR;
#endif
  image_info.usage = (src->usage & ~video_usage)
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = NULL;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  mem_prop_flags = src->vk_mem ? src->vk_mem->properties :
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  dst = _vk_image_mem_new_alloc_with_image_info (_vulkan_image_memory_allocator,
      NULL, src->device, &image_info, mem_prop_flags, NULL, NULL, 0);
  if (!dst)
    return NULL;

  queue = gst_vulkan_device_select_queue (src->device, VK_QUEUE_GRAPHICS_BIT);
  if (!queue) {
    queue = gst_vulkan_device_select_queue (src->device,
        VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
  }
  if (!queue) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "No queue supporting transfer operations");
    goto error;
  }

  cmd_pool = gst_vulkan_queue_create_command_pool (queue, &error);
  if (!cmd_pool)
    goto error;
  exec = gst_vulkan_operation_new (cmd_pool);

  frame = gst_buffer_new ();
  gst_buffer_append_memory (frame, gst_memory_ref (GST_MEMORY_CAST (src)));
  gst_buffer_append_memory (frame, gst_memory_ref (GST_MEMORY_CAST (dst)));

again:
  if (!gst_vulkan_operation_begin (exec, &error))
    goto error;

  if (!gst_vulkan_operation_add_dependency_frame (exec, frame,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT)) {
    gst_vulkan_operation_reset (exec);
    goto again;
  }

  barriers = gst_vulkan_operation_get_barriers (exec);
  if (!_drop_video_access (barriers, src)) {
    gst_vulkan_operation_reset (exec);
    goto again;
  }
  if (!gst_vulkan_barrier_state_add_image_barrier (barriers, src,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          NULL))
    goto reset_error;
  if (!gst_vulkan_barrier_state_add_image_barrier (barriers, dst,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          NULL))
    goto reset_error;
  gst_vulkan_barrier_state_pipeline_barrier (barriers, exec->cmd_buf, 0);

  gst_vulkan_command_buffer_lock (exec->cmd_buf);
  vkCmdCopyImage (exec->cmd_buf->cmd, src->image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n_regions, regions);
  gst_vulkan_command_buffer_unlock (exec->cmd_buf);

  if (!gst_vulkan_operation_end (exec, &error)) {
    if (g_error_matches (error, GST_VULKAN_ERROR, VK_ERROR_OUT_OF_DATE_KHR)) {
      GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY, "image memory %p: barrier "
          "state changed during the copy, retrying", src);
      g_clear_error (&error);
      goto again;
    }
    goto error;
  }

  if (!gst_vulkan_operation_wait (exec)) {
    g_set_error_literal (&error, GST_VULKAN_ERROR, GST_VULKAN_FAILED,
        "Failed to wait for the copy");
    goto error;
  }

  gst_buffer_unref (frame);
  gst_object_unref (exec);
  gst_object_unref (cmd_pool);
  gst_object_unref (queue);

  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "copied image memory %p into "
      "%p", src, dst);

  return GST_MEMORY_CAST (dst);

reset_error:
  gst_vulkan_operation_reset (exec);

error:
  GST_CAT_WARNING (GST_CAT_VULKAN_IMAGE_MEMORY, "Failed to copy image memory "
      "%p: %s", src, error ? error->message : "unknown error");
  g_clear_error (&error);
  gst_clear_buffer (&frame);
  gst_clear_object (&exec);
  gst_clear_object (&cmd_pool);
  gst_clear_object (&queue);
  gst_memory_unref (GST_MEMORY_CAST (dst));
  return NULL;
}

static GstMemory *
_vk_image_mem_share (GstVulkanImageMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
_vk_image_mem_is_span (GstVulkanImageMemory * mem1, GstVulkanImageMemory * mem2,
    gsize * offset)
{
  return FALSE;
}

static GstMemory *
_vk_image_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_critical ("Subclass should override GstAllocatorClass::alloc() function");

  return NULL;
}

static void
_free_view (GstVulkanImageView * view, gpointer unused)
{
  gst_vulkan_image_view_unref (view);
}

static void
_vk_image_mem_free (GstAllocator * allocator, GstMemory * memory)
{
  GstVulkanImageMemory *mem = (GstVulkanImageMemory *) memory;

  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "freeing image memory:%p "
      "id:%" G_GUINT64_FORMAT, mem, (guint64) mem->image);

  g_mutex_lock (&mem->lock);
  GPtrArray *outstanding_views = mem->outstanding_views;
  mem->outstanding_views = NULL;

  GPtrArray *views = mem->views;
  mem->views = NULL;
  g_mutex_unlock (&mem->lock);

  g_warn_if_fail (outstanding_views->len == 0);
  g_ptr_array_unref (outstanding_views);

  g_ptr_array_foreach (views, (GFunc) _free_view, NULL);
  g_ptr_array_unref (views);

  if (mem->image && !mem->wrapped)
    vkDestroyImage (mem->device->device, mem->image, NULL);

  gst_vulkan_barrier_image_info_clear (&mem->barrier);

  if (mem->vk_mem)
    gst_memory_unref ((GstMemory *) mem->vk_mem);

  if (mem->notify)
    mem->notify (mem->user_data);

  gst_clear_vulkan_timeline_semaphore (&mem->timeline_semaphore);

  gst_object_unref (mem->device);

  g_mutex_clear (&mem->lock);

  g_free (mem);
}

/**
 * gst_vulkan_image_memory_alloc_with_image_info:
 * @device: a #GstVulkanDevice
 * @image_info: VkImageCreateInfo structure
 * @mem_prop_flags: VkMemoryPropertyFlags flags
 *
 * Returns: a #GstMemory object backed by a vulkan device memory
 *
 * Since: 1.24
 */
GstMemory *
gst_vulkan_image_memory_alloc_with_image_info (GstVulkanDevice * device,
    VkImageCreateInfo * image_info, VkMemoryPropertyFlags mem_prop_flags)
{
  GstVulkanImageMemory *mem;

  g_return_val_if_fail (image_info
      && image_info->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL);

  mem = _vk_image_mem_new_alloc_with_image_info (_vulkan_image_memory_allocator,
      NULL, device, image_info, mem_prop_flags, NULL, NULL, 0);

  return (GstMemory *) mem;
}

/**
 * gst_vulkan_image_memory_alloc_with_image_info2:
 * @device: a #GstVulkanDevice
 * @image_info: VkImageCreateInfo structure
 * @mem_prop_flags: VkMemoryPropertyFlags flags
 * @ext_sem_handle_types: VkExternalSemaphoreHandleTypeFlags
 *
 * Like gst_vulkan_image_memory_alloc_with_image_info() but also creates the
 * backing timeline semaphore with the export capability given by
 * @ext_sem_handle_types.
 *
 * Returns: a #GstMemory object backed by a vulkan device memory
 *
 * Since: 1.30
 */
GstMemory *
gst_vulkan_image_memory_alloc_with_image_info2 (GstVulkanDevice * device,
    VkImageCreateInfo * image_info, VkMemoryPropertyFlags mem_prop_flags,
    VkExternalSemaphoreHandleTypeFlags ext_sem_handle_types)
{
  GstVulkanImageMemory *mem;

  g_return_val_if_fail (image_info
      && image_info->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL);

  mem = _vk_image_mem_new_alloc_with_image_info (_vulkan_image_memory_allocator,
      NULL, device, image_info, mem_prop_flags, NULL, NULL,
      ext_sem_handle_types);

  return (GstMemory *) mem;
}

/**
 * gst_vulkan_image_memory_alloc:
 * @device: a #GstVulkanDevice
 * @format: the VkFormat for the new image
 * @width: width for the new image
 * @height: height for the new image
 * @tiling: tiling for the new image
 * @usage: usage flags for the new image
 * @mem_prop_flags: VkDeviceMemory property flags for the new image
 *
 * Allocated a new #GstVulkanImageMemory.
 *
 * Returns: a #GstMemory object backed by a vulkan device memory
 *
 * Since: 1.18
 */
GstMemory *
gst_vulkan_image_memory_alloc (GstVulkanDevice * device, VkFormat format,
    gsize width, gsize height, VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags mem_prop_flags)
{
  GstVulkanImageMemory *mem;

  mem = _vk_image_mem_new_alloc (_vulkan_image_memory_allocator, NULL, device,
      format, width, height, tiling, usage, mem_prop_flags, NULL, NULL);

  return (GstMemory *) mem;
}

/**
 * gst_vulkan_image_memory_wrapped:
 * @device: a #GstVulkanDevice
 * @image: a VkImage
 * @format: the VkFormat for @image
 * @width: width of @image
 * @height: height of @image
 * @tiling: tiling of @image
 * @usage: usage flags of @image
 * @user_data: (nullable): user data for @notify
 * @notify: a #GDestroyNotify when @image is no longer needed
 *
 * Return: a new #GstVulkanImageMemory wrapping @image
 *
 * Since: 1.18
 */
GstMemory *
gst_vulkan_image_memory_wrapped (GstVulkanDevice * device, VkImage image,
    VkFormat format, gsize width, gsize height, VkImageTiling tiling,
    VkImageUsageFlags usage, gpointer user_data, GDestroyNotify notify)
{
  GstVulkanImageMemory *mem;

  mem = _vk_image_mem_new_wrapped (_vulkan_image_memory_allocator, NULL, device,
      image, format, width, height, tiling, usage, user_data, notify);

  return (GstMemory *) mem;
}

/**
 * gst_vulkan_image_memory_wrapped_with_image_info:
 * @device: a #GstVulkanDevice
 * @image: a `VkImage`
 * @image_info: the `VkImageCreateInfo` used to create @image
 * @user_data: (nullable): user data for @notify
 * @notify: a #GDestroyNotify when @image is no longer needed
 *
 * Returns: (transfer full) (nullable): a new #GstVulkanImageMemory wrapping
 *   @image
 *
 * Since: 1.30
 */
GstMemory *
gst_vulkan_image_memory_wrapped_with_image_info (GstVulkanDevice * device,
    VkImage image, const VkImageCreateInfo * image_info, gpointer user_data,
    GDestroyNotify notify)
{
  GstVulkanImageMemory *mem;

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);
  g_return_val_if_fail (image != VK_NULL_HANDLE, NULL);
  g_return_val_if_fail (image_info != NULL, NULL);
  g_return_val_if_fail (image_info->sType ==
      VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL);

  mem = _vk_image_mem_new_wrapped_with_image_info
      (_vulkan_image_memory_allocator, NULL, device, image, image_info,
      user_data, notify);

  return (GstMemory *) mem;
}

/**
 * gst_vulkan_image_memory_get_width:
 * @image: a #GstVulkanImageMemory
 *
 * Return: the width of @image
 *
 * Since: 1.18
 */
guint32
gst_vulkan_image_memory_get_width (GstVulkanImageMemory * image)
{
  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      0);

  return image->create_info.extent.width;
}

/**
 * gst_vulkan_image_memory_get_height:
 * @image: a #GstVulkanImageMemory
 *
 * Return: the height of @image
 *
 * Since: 1.18
 */
guint32
gst_vulkan_image_memory_get_height (GstVulkanImageMemory * image)
{
  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      0);

  return image->create_info.extent.height;
}

static gint
find_view_index_unlocked (GstVulkanImageMemory * image,
    GstVulkanImageView * view)
{
  guint index;

  if (!g_ptr_array_find (image->views, view, &index))
    return -1;

  return (gint) index;
}

extern gboolean
gst_vulkan_image_memory_release_view (GstVulkanImageView * view);

gboolean
gst_vulkan_image_memory_release_view (GstVulkanImageView * view)
{
  GstVulkanImageMemory *image;
  int old_ref;
  guint index;

  g_mutex_lock (&view->lock);

  old_ref = g_atomic_int_add (&view->parent.refcount, -1);

  if (old_ref != 1) {
    GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY,
        "not ready to free view %p as ref %d != 1", view, old_ref);
    g_mutex_unlock (&view->lock);
    return FALSE;
  }
  gst_vulkan_image_view_ref (view);

  image = view->image;
  if (!image) {
    GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "view %p without image -> free",
        view);
    g_mutex_unlock (&view->lock);
    return TRUE;
  }

  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      FALSE);

  g_mutex_lock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "locked image memory %p", image);
  view->image = NULL;
  g_mutex_unlock (&view->lock);

  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "image %p removing view %p",
      image, view);
  if (g_ptr_array_find (image->outstanding_views, view, &index)) {
    g_ptr_array_steal_index_fast (image->outstanding_views, index);
    g_ptr_array_add (image->views, view);
  } else {
    g_warning ("GstVulkanImageMemory:%p attempt to remove a view %p "
        "that we do not own", image, view);
  }
  g_mutex_unlock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY,
      "unlocked image memory %p view %p", image, view);
  gst_memory_unref ((GstMemory *) image);

  return FALSE;
}

/**
 * gst_vulkan_image_memory_add_view:
 * @image: a #GstVulkanImageMemory
 * @view: a #GstVulkanImageView
 *
 * Since: 1.18
 */
void
gst_vulkan_image_memory_add_view (GstVulkanImageMemory * image,
    GstVulkanImageView * view)
{
  g_return_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)));
  g_return_if_fail (view != NULL);
  g_return_if_fail (image == view->image);

  g_mutex_lock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "locked image memory %p", image);
  if (find_view_index_unlocked (image, view) != -1) {
    g_warn_if_reached ();
    GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "unlocked image memory %p",
        image);
    g_mutex_unlock (&image->lock);
    return;
  }
  g_ptr_array_add (image->outstanding_views, view);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "Image %p adding view %p",
      image, view);

  g_mutex_unlock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "unlocked image memory %p",
      image);
}

struct view_data
{
  GstVulkanImageMemory *img;
  GstVulkanImageMemoryFindViewFunc find_func;
  gpointer find_data;
};

static gboolean
find_view_func (GstVulkanImageView * view, gpointer user_data)
{
  struct view_data *data = user_data;
  GstVulkanImageMemory *previous;
  gboolean ret;

  previous = view->image;
  view->image = data->img;

  ret = data->find_func (view, data->find_data);

  view->image = previous;

  return ret;
}

/**
 * gst_vulkan_image_memory_find_view:
 * @image: a #GstVulkanImageMemory
 * @find_func: (scope call): #GstVulkanImageMemoryFindViewFunc to search with
 * @user_data: user data to call @finc_func with
 *
 * Return: (transfer full) (nullable): the first #GstVulkanImageView that
 *                                     @find_func returns %TRUE for, or %NULL
 *
 * Since: 1.18
 */
GstVulkanImageView *
gst_vulkan_image_memory_find_view (GstVulkanImageMemory * image,
    GstVulkanImageMemoryFindViewFunc find_func, gpointer user_data)
{
  GstVulkanImageView *ret = NULL;
  struct view_data view;
  guint index;

  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      NULL);
  g_return_val_if_fail (find_func != NULL, NULL);

  g_mutex_lock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "locked image memory %p", image);
  view.img = image;
  view.find_func = find_func;
  view.find_data = user_data;

  if (g_ptr_array_find_with_equal_func (image->outstanding_views, &view,
          (GEqualFunc) find_view_func, &index)) {
    ret =
        gst_vulkan_image_view_ref (g_ptr_array_index (image->outstanding_views,
            index));
    g_assert (image == ret->image);
    GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY,
        "Image %p found outstanding view %p", image, ret);
  } else if (g_ptr_array_find_with_equal_func (image->views, &view,
          (GEqualFunc) find_view_func, &index)) {
    ret = g_ptr_array_steal_index_fast (image->views, index);
    g_ptr_array_add (image->outstanding_views, ret);
    g_assert (!ret->image);
    ret->image = (GstVulkanImageMemory *) gst_memory_ref ((GstMemory *) image);
    GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "Image %p found stored view %p",
        image, ret);
  }

  g_mutex_unlock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "unlocked image memory %p",
      image);

  return ret;
}

/**
 * gst_vulkan_image_memory_lock:
 * @image: a #GstVulkanImageMemory
 *
 * Exclusively lock the image memory.
 *
 * Note: if you need to lock multiple memories at the same time, you should
 * use #GstVulkanBarrierState to ensure a determinstic locking order and avoid
 * deadlocks.
 *
 * Since: 1.30
 */
void
gst_vulkan_image_memory_lock (GstVulkanImageMemory * image)
{
  g_return_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)));

  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "locking image memory %p", image);
  g_mutex_lock (&image->lock);
}

/**
 * gst_vulkan_image_memory_unlock:
 * @image: a #GstVulkanImageMemory
 *
 * Exclusively unlock the image memory.
 *
 * Since: 1.30
 */
void
gst_vulkan_image_memory_unlock (GstVulkanImageMemory * image)
{
  g_return_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)));

  g_mutex_unlock (&image->lock);
  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "unlocked image memory %p",
      image);
}

/**
 * gst_vulkan_image_memory_peek_barrier_unlocked:
 * @image: a #GstVulkanImageMemory
 * @info: (out caller-allocates): destination for the #GstVulkanBarrierImageInfo
 *
 * Retrieve the current #GstVulkanBarrierImageInfo for this memory.
 *
 * Must be called with gst_vulkan_image_memory_lock() called.
 *
 * Since: 1.30
 */
void
gst_vulkan_image_memory_peek_barrier_unlocked (GstVulkanImageMemory * image,
    GstVulkanBarrierImageInfo * info)
{
  g_return_if_fail (info != NULL);
  g_return_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)));

  gst_vulkan_barrier_image_info_copy_into (&image->barrier, info);
}

/**
 * gst_vulkan_image_memory_compare_exchange_barrier_unlocked:
 * @image: a #GstVulkanImageMemory
 * @old_info: the previous #GstVulkanBarrierImageInfo
 * @new_info: the new #GstVulkanBarrierImageInfo
 *
 * Attempts to atomically update the barrier stored in @image to @new_info. Only
 * succeeds if the current barrier is the same as @old_info.
 *
 * On failure, the operation should be retried with updated information.
 *
 * Must be called with gst_vulkan_image_memory_lock() called.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_image_memory_compare_exchange_barrier_unlocked (GstVulkanImageMemory
    * image, GstVulkanBarrierImageInfo * old_info,
    GstVulkanBarrierImageInfo * new_info)
{
  g_return_val_if_fail (old_info != NULL, FALSE);
  g_return_val_if_fail (new_info != NULL, FALSE);
  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      FALSE);

  if (!gst_vulkan_barrier_image_info_is_equal (&image->barrier, old_info))
    goto fail;

  gst_vulkan_barrier_image_info_copy_into (new_info, &image->barrier);

  return TRUE;

fail:
  return FALSE;
}

/**
 * gst_vulkan_barrier_image_info_clear:
 * @info: a #GstVulkanBarrierImageInfo
 *
 * Clear any referenced objects from this @info.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_image_info_clear (GstVulkanBarrierImageInfo * info)
{
  gst_vulkan_barrier_memory_info_clear (&info->parent);
}

/**
 * gst_vulkan_barrier_image_info_is_equal:
 * @info: a #GstVulkanBarrierImageInfo
 * @other: the other #GstVulkanBarrierImageInfo to compare against
 *
 * Returns: whether @info and @other_info are the same.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_image_info_is_equal (GstVulkanBarrierImageInfo * info,
    GstVulkanBarrierImageInfo * other)
{
  if (!gst_vulkan_barrier_memory_info_is_equal (&info->parent, &other->parent))
    return FALSE;
  if (info->image_layout != other->image_layout)
    return FALSE;
  if (memcmp (&info->subresource_range, &other->subresource_range,
          sizeof (info->subresource_range)) != 0)
    return FALSE;
  return TRUE;
}

/**
 * gst_vulkan_barrier_image_info_copy_into:
 * @info: a #GstVulkanBarrierImageInfo
 * @other: the other #GstVulkanBarrierImageInfo to copy into
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_image_info_copy_into (GstVulkanBarrierImageInfo * info,
    GstVulkanBarrierImageInfo * other)
{
  g_return_if_fail (info);
  g_return_if_fail (other);

  gst_vulkan_barrier_image_info_clear (other);
  gst_vulkan_barrier_memory_info_copy_into (&info->parent, &other->parent);
  other->image_layout = info->image_layout;
  other->subresource_range = info->subresource_range;
}

G_DEFINE_TYPE (GstVulkanImageMemoryAllocator, gst_vulkan_image_memory_allocator,
    GST_TYPE_ALLOCATOR);

static void
gst_vulkan_image_memory_allocator_class_init (GstVulkanImageMemoryAllocatorClass
    * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = _vk_image_mem_alloc;
  allocator_class->free = _vk_image_mem_free;
}

static void
gst_vulkan_image_memory_allocator_init (GstVulkanImageMemoryAllocator *
    allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_VULKAN_IMAGE_MEMORY_ALLOCATOR_NAME;
  alloc->mem_map_full = (GstMemoryMapFullFunction) _vk_image_mem_map_full;
  alloc->mem_unmap_full = (GstMemoryUnmapFullFunction) _vk_image_mem_unmap_full;
  alloc->mem_copy = (GstMemoryCopyFunction) _vk_image_mem_copy;
  alloc->mem_share = (GstMemoryShareFunction) _vk_image_mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _vk_image_mem_is_span;
}

/**
 * gst_vulkan_image_memory_init_once:
 *
 * Initializes the Vulkan image memory allocator. It is safe to call this function
 * multiple times.  This must be called before any other #GstVulkanImageMemory operation.
 *
 * Since: 1.18
 */
void
gst_vulkan_image_memory_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_VULKAN_IMAGE_MEMORY, "vulkanimagememory",
        0, "Vulkan Image Memory");

    _vulkan_image_memory_allocator =
        g_object_new (gst_vulkan_image_memory_allocator_get_type (), NULL);
    gst_object_ref_sink (_vulkan_image_memory_allocator);

    gst_allocator_register (GST_VULKAN_IMAGE_MEMORY_ALLOCATOR_NAME,
        gst_object_ref (_vulkan_image_memory_allocator));
    g_once_init_leave (&_init, 1);
  }
}

/**
 * gst_is_vulkan_image_memory:
 * @mem: a #GstMemory
 *
 * Returns: whether the memory at @mem is a #GstVulkanImageMemory
 *
 * Since: 1.18
 */
gboolean
gst_is_vulkan_image_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      g_type_is_a (G_OBJECT_TYPE (mem->allocator),
      GST_TYPE_VULKAN_IMAGE_MEMORY_ALLOCATOR);
}

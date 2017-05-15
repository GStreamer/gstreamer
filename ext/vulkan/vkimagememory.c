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

#include "vkimagememory.h"

/**
 * SECTION:vkimagememory
 * @title: GstVkImageMemory
 * @short_description: memory subclass for Vulkan image memory
 * @see_also: #GstMemory, #GstAllocator
 *
 * GstVulkanImageMemory is a #GstMemory subclass providing support for the
 * mapping of Vulkan device memory.
 */

#define GST_CAT_DEFUALT GST_CAT_VULKAN_IMAGE_MEMORY
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFUALT);

static GstAllocator *_vulkan_image_memory_allocator;

VkFormat
gst_vulkan_format_from_video_format (GstVideoFormat v_format, guint plane)
{
  guint n_plane_components;

  switch (v_format) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      n_plane_components = 4;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      n_plane_components = 3;
      break;
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      n_plane_components = 2;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      n_plane_components = plane == 0 ? 1 : 2;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      n_plane_components = 1;
      break;
    default:
      n_plane_components = 4;
      g_assert_not_reached ();
      break;
  }

  switch (n_plane_components) {
    case 4:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case 3:
      return VK_FORMAT_R8G8B8_UNORM;
    case 2:
      return VK_FORMAT_R8G8_UNORM;
    case 1:
      return VK_FORMAT_R8_UNORM;
    default:
      g_assert_not_reached ();
      return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

static void
_view_create_info (VkImage image, VkFormat format, VkImageViewCreateInfo * info)
{
  info->sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  info->pNext = NULL;
  info->image = image;
  info->format = format;
  info->viewType = VK_IMAGE_VIEW_TYPE_2D;
  info->flags = 0;

  GST_VK_COMPONENT_MAPPING (info->components, VK_COMPONENT_SWIZZLE_R,
      VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A);
  GST_VK_IMAGE_SUBRESOURCE_RANGE (info->subresourceRange,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
}

static gboolean
_create_info_from_args (VkImageCreateInfo * info, VkFormat format, gsize width,
    gsize height, VkImageTiling tiling, VkImageUsageFlags usage)
{
  /* FIXME: validate these */

  info->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info->pNext = NULL;
  info->flags = 0;
  info->imageType = VK_IMAGE_TYPE_2D;
  info->format = format;
  GST_VK_EXTENT3D (info->extent, width, height, 1);
  info->mipLevels = 1;
  info->arrayLayers = 1;
  info->samples = VK_SAMPLE_COUNT_1_BIT;
  info->tiling = tiling;
  info->usage = usage;
  info->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info->queueFamilyIndexCount = 0;
  info->pQueueFamilyIndices = NULL;
  info->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  return TRUE;
}

static void
_vk_image_mem_init (GstVulkanImageMemory * mem, GstAllocator * allocator,
    GstMemory * parent, GstVulkanDevice * device, VkImageUsageFlags usage,
    GstAllocationParams * params, gsize size, gpointer user_data,
    GDestroyNotify notify)
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
  mem->image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  mem->usage = usage;
  mem->wrapped = FALSE;
  mem->notify = notify;
  mem->user_data = user_data;

  g_mutex_init (&mem->lock);

  GST_CAT_DEBUG (GST_CAT_VULKAN_IMAGE_MEMORY,
      "new Vulkan Image memory:%p size:%" G_GSIZE_FORMAT, mem, maxsize);
}

static GstVulkanImageMemory *
_vk_image_mem_new_alloc (GstAllocator * allocator, GstMemory * parent,
    GstVulkanDevice * device, VkFormat format, gsize width, gsize height,
    VkImageTiling tiling, VkImageUsageFlags usage,
    VkMemoryPropertyFlags mem_prop_flags, gpointer user_data,
    GDestroyNotify notify)
{
  GstVulkanImageMemory *mem = NULL;
  GstAllocationParams params = { 0, };
  VkImageViewCreateInfo view_info;
  VkImageCreateInfo image_info;
  VkPhysicalDevice gpu;
  GError *error = NULL;
  guint32 type_idx;
  VkImage image;
  VkResult err;

  gpu = gst_vulkan_device_get_physical_device (device);
  if (!_create_info_from_args (&image_info, format, width, height, tiling,
          usage)) {
    GST_CAT_ERROR (GST_CAT_VULKAN_IMAGE_MEMORY, "Incorrect image parameters");
    goto error;
  }

  err = vkCreateImage (device->device, &image_info, NULL, &image);
  if (gst_vulkan_error_to_g_error (err, &error, "vkCreateImage") < 0)
    goto vk_error;

  mem = g_new0 (GstVulkanImageMemory, 1);
  _vk_image_mem_init (mem, allocator, parent, device, usage, &params,
      mem->requirements.size, user_data, notify);
  mem->create_info = image_info;
  mem->image = image;

  vkGetImageMemoryRequirements (device->device, image, &mem->requirements);
  err = vkGetPhysicalDeviceImageFormatProperties (gpu, format, VK_IMAGE_TYPE_2D,
      tiling, usage, 0, &mem->format_properties);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkGetPhysicalDeviceImageFormatProperties") < 0)
    goto vk_error;

  if (!gst_vulkan_memory_find_memory_type_index_with_type_properties (device,
          mem->requirements.memoryTypeBits, mem_prop_flags, &type_idx))
    goto error;

  /* XXX: assumes alignment is a power of 2 */
  params.align = mem->requirements.alignment - 1;
  mem->vk_mem = (GstVulkanMemory *) gst_vulkan_memory_alloc (device, type_idx,
      &params, mem->requirements.size, mem_prop_flags);
  if (!mem->vk_mem)
    goto error;

  err = vkBindImageMemory (device->device, image, mem->vk_mem->mem_ptr, 0);
  if (gst_vulkan_error_to_g_error (err, &error, "vkBindImageMemory") < 0)
    goto vk_error;

  if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    _view_create_info (mem->image, format, &view_info);
    err = vkCreateImageView (device->device, &view_info, NULL, &mem->view);
    if (gst_vulkan_error_to_g_error (err, &error, "vkCreateImageView") < 0)
      goto vk_error;
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
    if (mem)
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
  GstVulkanImageMemory *mem = g_new0 (GstVulkanImageMemory, 1);
  GstAllocationParams params = { 0, };
  VkImageViewCreateInfo view_info;
  VkPhysicalDevice gpu;
  GError *error = NULL;
  VkResult err;

  gpu = gst_vulkan_device_get_physical_device (device);
  mem->image = image;

  vkGetImageMemoryRequirements (device->device, mem->image, &mem->requirements);

  /* XXX: assumes alignment is a power of 2 */
  params.align = mem->requirements.alignment - 1;
  params.flags = GST_MEMORY_FLAG_NOT_MAPPABLE;
  _vk_image_mem_init (mem, allocator, parent, device, usage, &params,
      mem->requirements.size, user_data, notify);
  mem->wrapped = TRUE;

  if (!_create_info_from_args (&mem->create_info, format, width, height, tiling,
          usage)) {
    GST_CAT_ERROR (GST_CAT_VULKAN_IMAGE_MEMORY, "Incorrect image parameters");
    goto error;
  }

  err = vkGetPhysicalDeviceImageFormatProperties (gpu, format, VK_IMAGE_TYPE_2D,
      tiling, usage, 0, &mem->format_properties);
  if (gst_vulkan_error_to_g_error (err, &error,
          "vkGetPhysicalDeviceImageFormatProperties") < 0)
    goto vk_error;

  /* XXX: we don't actually if the image has a vkDeviceMemory bound so
   * this may fail */
  if (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    _view_create_info (mem->image, format, &view_info);
    err = vkCreateImageView (device->device, &view_info, NULL, &mem->view);
    if (gst_vulkan_error_to_g_error (err, &error, "vkCreateImageView") < 0)
      goto vk_error;
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

static gpointer
_vk_image_mem_map_full (GstVulkanImageMemory * mem, GstMapInfo * info,
    gsize size)
{
  GstMapInfo *vk_map_info;

  /* FIXME: possible layout transformation needed */
  g_mutex_lock (&mem->lock);

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

  return vk_map_info->data;
}

static void
_vk_image_mem_unmap_full (GstVulkanImageMemory * mem, GstMapInfo * info)
{
  g_mutex_lock (&mem->lock);
  gst_memory_unmap ((GstMemory *) mem->vk_mem, info->user_data[0]);
  g_mutex_unlock (&mem->lock);

  g_free (info->user_data[0]);
}

static GstMemory *
_vk_image_mem_copy (GstVulkanImageMemory * src, gssize offset, gssize size)
{
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
_vk_image_mem_free (GstAllocator * allocator, GstMemory * memory)
{
  GstVulkanImageMemory *mem = (GstVulkanImageMemory *) memory;

  GST_CAT_TRACE (GST_CAT_VULKAN_IMAGE_MEMORY, "freeing image memory:%p "
      "id:%" G_GUINT64_FORMAT, mem, (guint64) mem->image);

  if (mem->image && !mem->wrapped)
    vkDestroyImage (mem->device->device, mem->image, NULL);

  if (mem->view)
    vkDestroyImageView (mem->device->device, mem->view, NULL);

  if (mem->vk_mem)
    gst_memory_unref ((GstMemory *) mem->vk_mem);

  if (mem->notify)
    mem->notify (mem->user_data);

  gst_object_unref (mem->device);

  g_free (mem);
}

static VkAccessFlags
_access_flags_from_layout (VkImageLayout image_layout)
{
  if (image_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    return VK_ACCESS_TRANSFER_WRITE_BIT;

  if (image_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    return VK_ACCESS_TRANSFER_READ_BIT;

  if (image_layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  if (image_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  if (image_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;

  return 0;
}

gboolean
gst_vulkan_image_memory_set_layout (GstVulkanImageMemory * vk_mem,
    VkImageLayout image_layout, VkImageMemoryBarrier * barrier)
{
  /* validate vk_mem->usage with image_layout */

  barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier->pNext = NULL;
  barrier->dstAccessMask = _access_flags_from_layout (image_layout);
  barrier->srcAccessMask = _access_flags_from_layout (vk_mem->image_layout);
  barrier->oldLayout = vk_mem->image_layout;
  barrier->newLayout = image_layout;
  barrier->srcQueueFamilyIndex = 0;
  barrier->dstQueueFamilyIndex = 0;
  barrier->image = vk_mem->image;
  GST_VK_IMAGE_SUBRESOURCE_RANGE (barrier->subresourceRange,
      VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

  /* FIXME: what if the barrier is never submitted or is submitted out of order? */
  vk_mem->image_layout = image_layout;

  return TRUE;
}

/**
 * gst_vulkan_image_memory_alloc:
 *
 * Allocated a new #GstVulkanImageMemory.
 *
 * Returns: a #GstMemory object backed by a vulkan device memory
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

guint32
gst_vulkan_image_memory_get_width (GstVulkanImageMemory * image)
{
  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      0);

  return image->create_info.extent.width;
}

guint32
gst_vulkan_image_memory_get_height (GstVulkanImageMemory * image)
{
  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      0);

  return image->create_info.extent.height;
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
 * Initializes the Vulkan memory allocator. It is safe to call this function
 * multiple times.  This must be called before any other #GstVulkanImageMemory operation.
 */
void
gst_vulkan_image_memory_init_once (void)
{
  static volatile gsize _init = 0;

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
 * @mem:a #GstMemory
 *
 * Returns: whether the memory at @mem is a #GstVulkanImageMemory
 */
gboolean
gst_is_vulkan_image_memory (GstMemory * mem)
{
  return mem != NULL && mem->allocator != NULL &&
      g_type_is_a (G_OBJECT_TYPE (mem->allocator),
      GST_TYPE_VULKAN_IMAGE_MEMORY_ALLOCATOR);
}

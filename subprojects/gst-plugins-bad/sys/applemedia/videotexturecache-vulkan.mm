/*
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
# include "config.h"
#endif

#include <TargetConditionals.h>
#include <Metal/Metal.h>

#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#endif
#include "iosurfacevulkanmemory.h"

/* silence macor redefinition warnings */
#undef VK_USE_PLATFORM_MACOS_MVK
#undef VK_USE_PLATFORM_IOS_MVK
#include <MoltenVK/vk_mvk_moltenvk.h>
/* MoltenVK uses some enums/typedefs that are only available in newer macOS/iOS
 * versions. At time of writing:
 *  - MTLTextureSwizzle
 *  - MTLTextureSwizzleChannels
 *  - MTLMultisampleDepthResolveFilter
 */
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wunguarded-availability-new"
#include <MoltenVK/mvk_datatypes.h>
#pragma clang diagnostic pop
/* silence macro redefinition warnings */
#undef VK_USE_PLATFORM_MACOS_MVK
#undef VK_USE_PLATFORM_IOS_MVK

#include "coremediabuffer.h"
#include "corevideobuffer.h"
#include "vtutil.h"

#include "videotexturecache-vulkan.h"
#include "metal-helpers.h"

typedef struct _GstVideoTextureCacheVulkanPrivate
{
  GstBufferPool *pool;
} GstVideoTextureCacheVulkanPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GstVideoTextureCacheVulkan, gst_video_texture_cache_vulkan, GST_TYPE_VIDEO_TEXTURE_CACHE);

#define GET_PRIV(instance) \
    G_TYPE_INSTANCE_GET_PRIVATE (instance, GST_TYPE_VIDEO_TEXTURE_CACHE_VULKAN, GstVideoTextureCacheVulkanPrivate)

typedef struct _IOSurfaceTextureWrapper
{
  CVPixelBufferRef pixbuf;
  gpointer texture; /* id<MTLTexture> */
} IOSurfaceTextureWrapper;

enum
{
  PROP_0,
  PROP_DEVICE,
};

static GstMemory * gst_video_texture_cache_vulkan_create_memory (GstVideoTextureCache * cache,
      GstAppleCoreVideoPixelBuffer *gpixbuf, guint plane, gsize size);

static void
free_texture_wrapper (IOSurfaceTextureWrapper * wrapper)
{
  CFRelease (wrapper->pixbuf);
  id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>) wrapper->texture;
  (void) tex;
  g_free (wrapper);
}

GstVideoTextureCache *
gst_video_texture_cache_vulkan_new (GstVulkanDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);

  return (GstVideoTextureCache *) g_object_new (GST_TYPE_VIDEO_TEXTURE_CACHE_VULKAN,
      "device", device, NULL);
}

static void
gst_video_texture_cache_vulkan_finalize (GObject * object)
{
  GstVideoTextureCacheVulkan *cache_vulkan = GST_VIDEO_TEXTURE_CACHE_VULKAN (object);

#if 0
  gst_buffer_pool_set_active (cache->pool, FALSE);
  gst_object_unref (cache->pool);
#endif
  gst_object_unref (cache_vulkan->device);

  G_OBJECT_CLASS (gst_video_texture_cache_vulkan_parent_class)->finalize (object);
}

static void
gst_video_texture_cache_vulkan_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoTextureCacheVulkan *cache_vulkan = GST_VIDEO_TEXTURE_CACHE_VULKAN (object);

  switch (prop_id) {
    case PROP_DEVICE:
      /* G_PARAM_CONSTRUCT_ONLY */
      cache_vulkan->device = (GstVulkanDevice *) g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_texture_cache_vulkan_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoTextureCacheVulkan *cache_vulkan = GST_VIDEO_TEXTURE_CACHE_VULKAN (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, cache_vulkan->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_texture_cache_vulkan_constructed (GObject * object)
{
  GstVideoTextureCacheVulkan *cache_vulkan = GST_VIDEO_TEXTURE_CACHE_VULKAN (object);

  G_OBJECT_CLASS (gst_video_texture_cache_vulkan_parent_class)->constructed (object);
  g_return_if_fail (GST_IS_VULKAN_DEVICE (cache_vulkan->device));

  gst_io_surface_vulkan_memory_init ();
#if 0
  cache->pool = GST_BUFFER_POOL (gst_vulkan_buffer_pool_new (ctx));
#endif
}

static void
gst_video_texture_cache_vulkan_init (GstVideoTextureCacheVulkan * cache_vulkan)
{
}

static void
gst_video_texture_cache_vulkan_class_init (GstVideoTextureCacheVulkanClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoTextureCacheClass *cache_class = (GstVideoTextureCacheClass *) klass;

  gobject_class->set_property = gst_video_texture_cache_vulkan_set_property;
  gobject_class->get_property = gst_video_texture_cache_vulkan_get_property;
  gobject_class->constructed = gst_video_texture_cache_vulkan_constructed;
  gobject_class->finalize = gst_video_texture_cache_vulkan_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_object ("device", "device",
          "Associated Vulkan device", GST_TYPE_VULKAN_DEVICE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS)));

  cache_class->create_memory = gst_video_texture_cache_vulkan_create_memory;
}

static GstMemory *
gst_video_texture_cache_vulkan_create_memory (GstVideoTextureCache * cache,
      GstAppleCoreVideoPixelBuffer *gpixbuf, guint plane, gsize size)
{
  return (GstMemory *) gpixbuf;
}

VkFormat
metal_format_to_vulkan (unsigned int fmt)
{
  MTLPixelFormat mtl_fmt = (MTLPixelFormat) fmt;

  switch (mtl_fmt) {
    case MTLPixelFormatRGBA8Unorm:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case MTLPixelFormatRG8Unorm:
      return VK_FORMAT_R8G8_UNORM;
    case MTLPixelFormatR8Unorm:
      return VK_FORMAT_R8_UNORM;
    default:
      g_assert_not_reached ();
  }
}

unsigned int
video_info_to_metal_format (GstVideoInfo * info, guint plane)
{
  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_BGRA:
      return (unsigned int) MTLPixelFormatRGBA8Unorm;
    case GST_VIDEO_FORMAT_NV12:
      if (plane == 0)
        return (unsigned int) MTLPixelFormatR8Unorm;
      else
        return (unsigned int) MTLPixelFormatRG8Unorm;
    default:
      g_assert_not_reached ();
  }
}

GstMemory *
_create_vulkan_memory (GstAppleCoreVideoPixelBuffer * gpixbuf,
    GstVideoInfo * info, guint plane, gsize size, GstVideoTextureCache * cache)
{
  GstIOSurfaceVulkanMemory *mem;
  CVPixelBufferRef pixel_buf = gpixbuf->buf;
  IOSurfaceRef surface = CVPixelBufferGetIOSurface (pixel_buf);
  GstVideoTextureCacheVulkan *cache_vulkan =
      GST_VIDEO_TEXTURE_CACHE_VULKAN (cache);
  MTLPixelFormat fmt = (MTLPixelFormat) video_info_to_metal_format (info, plane);
  GstVulkanDevice *device = cache_vulkan->device;
  VkPhysicalDevice gpu;
  id<MTLDevice> mtl_dev = nil;
  MTLTextureDescriptor *tex_desc;
  id<MTLTexture> texture = nil;

  gpu = gst_vulkan_device_get_physical_device (device);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS;
  vkGetMTLDeviceMVK (gpu, &mtl_dev);
  G_GNUC_END_IGNORE_DEPRECATIONS;

  tex_desc = [MTLTextureDescriptor new];
  tex_desc.pixelFormat = fmt;
  tex_desc.textureType = MTLTextureType2D;
  tex_desc.width = GST_VIDEO_INFO_COMP_WIDTH (info, plane);
  tex_desc.height = GST_VIDEO_INFO_COMP_HEIGHT (info, plane);
  tex_desc.depth = 1;
  tex_desc.mipmapLevelCount = 1;
  tex_desc.sampleCount = 1;
  tex_desc.arrayLength = 1;
  tex_desc.usage = MTLTextureUsageShaderRead;
  tex_desc.cpuCacheMode = MTLCPUCacheModeDefaultCache;

  texture = [mtl_dev newTextureWithDescriptor:tex_desc iosurface:surface plane:plane];
  if (!texture)
    return NULL;

  IOSurfaceTextureWrapper *texture_data = g_new0 (IOSurfaceTextureWrapper, 1);
  texture_data->pixbuf = (CVPixelBufferRef) CFRetain (pixel_buf);
  texture_data->texture = (__bridge_retained gpointer) texture;

  mem = gst_io_surface_vulkan_memory_wrapped (device, surface, fmt,
      (__bridge void *) texture, info, plane, texture_data,
      (GDestroyNotify) free_texture_wrapper);
  if (!mem)
    free_texture_wrapper (texture_data);

  return GST_MEMORY_CAST (mem);
}

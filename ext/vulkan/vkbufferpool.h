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

#ifndef _GST_VULKAN_BUFFER_POOL_H_
#define _GST_VULKAN_BUFFER_POOL_H_

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include <vk.h>

G_BEGIN_DECLS

/* buffer pool functions */
GType gst_vulkan_buffer_pool_get_type (void);
#define GST_TYPE_VULKAN_BUFFER_POOL      (gst_vulkan_buffer_pool_get_type())
#define GST_IS_VULKAN_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VULKAN_BUFFER_POOL))
#define GST_VULKAN_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VULKAN_BUFFER_POOL, GstVulkanBufferPool))
#define GST_VULKAN_BUFFER_POOL_CAST(obj) ((GstVulkanBufferPool*)(obj))

/**
 * GstVulkanBufferPool:
 *
 * Opaque GstVulkanBufferPool struct
 */
struct _GstVulkanBufferPool
{
  GstBufferPool bufferpool;

  GstVulkanDevice *device;

  GstVulkanBufferPoolPrivate *priv;
};

/**
 * GstVulkanBufferPoolClass:
 *
 * The #GstVulkanBufferPoolClass structure contains only private data
 */
struct _GstVulkanBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GstBufferPool *gst_vulkan_buffer_pool_new (GstVulkanDevice * device);

G_END_DECLS

#endif /* _GST_VULKAN_BUFFER_POOL_H_ */

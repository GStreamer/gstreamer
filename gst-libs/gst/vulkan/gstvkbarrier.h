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

#ifndef __GST_VULKAN_BARRIER_H__
#define __GST_VULKAN_BARRIER_H__

#include <gst/vulkan/vulkan_fwd.h>
#include <gst/vulkan/gstvkapi.h>

G_BEGIN_DECLS

typedef enum
{
  GST_VULKAN_BARRIER_NONE = 0,
  GST_VULKAN_BARRIER_TYPE_MEMORY = 1,
  GST_VULKAN_BARRIER_TYPE_BUFFER = 2,
  GST_VULKAN_BARRIER_TYPE_IMAGE = 3,
} GstVulkanBarrierType;

typedef enum
{
  GST_VULKAN_BARRIER_FLAG_NONE = 0,
} GstVulkanBarrierFlags;

struct _GstVulkanBarrierMemoryInfo
{
  GstVulkanBarrierType type;
  GstVulkanBarrierFlags flags;
  GstVulkanQueue * queue;
  VkPipelineStageFlags pipeline_stages;
  VkAccessFlags access_flags;
};

G_END_DECLS

#endif /* __GST_VULKAN_BARRIER_H__ */

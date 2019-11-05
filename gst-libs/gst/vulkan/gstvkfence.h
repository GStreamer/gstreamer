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

#ifndef __GST_VULKAN_FENCE_H__
#define __GST_VULKAN_FENCE_H__

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_FENCE (gst_vulkan_fence_get_type ())
GST_VULKAN_API
GType gst_vulkan_fence_get_type (void);

#define GST_VULKAN_FENCE_CAST(f) ((GstVulkanFence *) f)
#define GST_VULKAN_FENCE_FENCE(f) (GST_VULKAN_FENCE_CAST(f)->fence)

struct _GstVulkanFence
{
  GstMiniObject parent;

  GstVulkanDevice *device;

  VkFence fence;
};

GST_VULKAN_API
GstVulkanFence *    gst_vulkan_fence_new            (GstVulkanDevice * device,
                                                     VkFenceCreateFlags flags,
                                                     GError ** error);
GST_VULKAN_API
GstVulkanFence *    gst_vulkan_fence_new_always_signalled (GstVulkanDevice *device);

GST_VULKAN_API
gboolean            gst_vulkan_fence_is_signaled    (GstVulkanFence * fence);

static inline GstVulkanFence *
gst_vulkan_fence_ref (GstVulkanFence * fence)
{
  return GST_VULKAN_FENCE_CAST (gst_mini_object_ref (GST_MINI_OBJECT_CAST (fence)));
}

static inline void
gst_vulkan_fence_unref (GstVulkanFence * fence)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (fence));
}

G_END_DECLS

#endif /* __GST_VULKAN_FENCE_H__ */

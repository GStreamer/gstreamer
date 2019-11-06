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

#ifndef __GST_VULKAN_SWAPPER_H__
#define __GST_VULKAN_SWAPPER_H__

#include <gst/video/video.h>

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_SWAPPER         (gst_vulkan_swapper_get_type())
#define GST_VULKAN_SWAPPER(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_SWAPPER, GstVulkanSwapper))
#define GST_VULKAN_SWAPPER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_SWAPPER, GstVulkanSwapperClass))
#define GST_IS_VULKAN_SWAPPER(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_SWAPPER))
#define GST_IS_VULKAN_SWAPPER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_SWAPPER))
#define GST_VULKAN_SWAPPER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_SWAPPER, GstVulkanSwapperClass))
GST_VULKAN_API
GType gst_vulkan_swapper_get_type       (void);

#define GST_VULKAN_SWAPPER_VIDEO_FORMATS " { RGBA, BGRA, RGB, BGR } "

typedef struct _GstVulkanSwapper GstVulkanSwapper;
typedef struct _GstVulkanSwapperClass GstVulkanSwapperClass;
typedef struct _GstVulkanSwapperPrivate GstVulkanSwapperPrivate;

struct _GstVulkanSwapper
{
  GstObject parent;

  GstVulkanDevice *device;
  GstVulkanWindow *window;
  GstVulkanQueue *queue;
  GstVulkanCommandPool *cmd_pool;
};

struct _GstVulkanSwapperClass
{
  GstObjectClass parent_class;
};

GST_VULKAN_API
GstVulkanSwapper *  gst_vulkan_swapper_new                      (GstVulkanDevice * device,
                                                                 GstVulkanWindow * window);

GST_VULKAN_API
gboolean            gst_vulkan_swapper_choose_queue             (GstVulkanSwapper * swapper,
                                                                 GstVulkanQueue * available_queue,
                                                                 GError ** error);
GST_VULKAN_API
GstCaps *           gst_vulkan_swapper_get_supported_caps       (GstVulkanSwapper * swapper,
                                                                 GError ** error);
GST_VULKAN_API
gboolean            gst_vulkan_swapper_set_caps                 (GstVulkanSwapper * swapper,
                                                                 GstCaps * caps,
                                                                 GError ** error);
GST_VULKAN_API
gboolean            gst_vulkan_swapper_render_buffer            (GstVulkanSwapper * swapper,
                                                                 GstBuffer * buffer,
                                                                 GError ** error);

G_END_DECLS

#endif /* __GST_VULKAN_SWAPPER_H__ */

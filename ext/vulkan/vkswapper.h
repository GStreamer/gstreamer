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

#ifndef _VK_SWAPPER_H_
#define _VK_SWAPPER_H_

#include <gst/video/video.h>

#include <vk.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_SWAPPER         (gst_vulkan_swapper_get_type())
#define GST_VULKAN_SWAPPER(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_SWAPPER, GstVulkanSwapper))
#define GST_VULKAN_SWAPPER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_SWAPPER, GstVulkanSwapperClass))
#define GST_IS_VULKAN_SWAPPER(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_SWAPPER))
#define GST_IS_VULKAN_SWAPPER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_SWAPPER))
#define GST_VULKAN_SWAPPER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_SWAPPER, GstVulkanSwapperClass))
GType gst_vulkan_swapper_get_type       (void);

#define GST_VULKAN_SWAPPER_VIDEO_FORMATS " { RGBA, BGRA, RGB, BGR } "

struct _GstVulkanSwapper
{
  GstObject parent;

  GstVulkanDevice *device;
  GstVulkanWindow *window;
  GstVulkanQueue *queue;

  VkSurfaceKHR    surface;

  VkSurfaceCapabilitiesKHR surf_props;
  VkSurfaceFormatKHR *surf_formats;
  guint32 n_surf_formats;
  VkPresentModeKHR *surf_present_modes;
  guint32 n_surf_present_modes;

  VkSwapchainKHR swap_chain;
  GstVulkanImageMemory **swap_chain_images;
  guint32 n_swap_chain_images;

  GstCaps *caps;
  GstVideoInfo v_info;

  PFN_vkGetPhysicalDeviceSurfaceSupportKHR GetPhysicalDeviceSurfaceSupportKHR;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetPhysicalDeviceSurfaceCapabilitiesKHR;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR GetPhysicalDeviceSurfaceFormatsKHR;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR GetPhysicalDeviceSurfacePresentModesKHR;
  PFN_vkCreateSwapchainKHR CreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR DestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR;
  PFN_vkAcquireNextImageKHR AcquireNextImageKHR;
  PFN_vkQueuePresentKHR QueuePresentKHR;

  /* <private> */
  /* runtime variables */
  gint to_quit;
  GstBuffer *current_buffer;

  /* signal handlers */
  gulong close_id;
  gulong draw_id;

  GstVulkanSwapperPrivate *priv;
};

struct _GstVulkanSwapperClass
{
  GstObjectClass parent_class;
};

GstVulkanSwapper *  gst_vulkan_swapper_new                      (GstVulkanDevice * device,
                                                                 GstVulkanWindow * window);

GstCaps *           gst_vulkan_swapper_get_supported_caps       (GstVulkanSwapper * swapper,
                                                                 GError ** error);
gboolean            gst_vulkan_swapper_set_caps                 (GstVulkanSwapper * swapper,
                                                                 GstCaps * caps,
                                                                 GError ** error);
gboolean            gst_vulkan_swapper_render_buffer            (GstVulkanSwapper * swapper,
                                                                 GstBuffer * buffer,
                                                                 GError ** error);

G_END_DECLS

#endif /* _VK_INSTANCE_H_ */

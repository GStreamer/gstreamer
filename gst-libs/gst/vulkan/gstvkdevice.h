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

#ifndef __GST_VULKAN_DEVICE_H__
#define __GST_VULKAN_DEVICE_H__

#include <gst/vulkan/gstvkphysicaldevice.h>
#include <gst/vulkan/gstvkqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_VULKAN_DEVICE         (gst_vulkan_device_get_type())
#define GST_VULKAN_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_DEVICE, GstVulkanDevice))
#define GST_VULKAN_DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_DEVICE, GstVulkanDeviceClass))
#define GST_IS_VULKAN_DEVICE(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_DEVICE))
#define GST_IS_VULKAN_DEVICE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_DEVICE))
#define GST_VULKAN_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_DEVICE, GstVulkanDeviceClass))
GST_VULKAN_API
GType gst_vulkan_device_get_type       (void);

#define GST_VULKAN_DEVICE_CONTEXT_TYPE_STR "gst.vulkan.device"

typedef gboolean (*GstVulkanDeviceForEachQueueFunc) (GstVulkanDevice * device, GstVulkanQueue * queue, gpointer user_data);

struct _GstVulkanDevice
{
  GstObject parent;

  GstVulkanInstance *instance;
  GstVulkanPhysicalDevice *physical_device;
  VkDevice device; /* hides a pointer */
};

struct _GstVulkanDeviceClass
{
  GstObjectClass parent_class;
};

GST_VULKAN_API
GstVulkanDevice *   gst_vulkan_device_new                   (GstVulkanPhysicalDevice * physical_device);
GST_VULKAN_API
GstVulkanDevice *   gst_vulkan_device_new_with_index        (GstVulkanInstance * instance, guint device_index);
GST_VULKAN_API
GstVulkanInstance * gst_vulkan_device_get_instance          (GstVulkanDevice * device);
GST_VULKAN_API
gboolean            gst_vulkan_device_open                  (GstVulkanDevice * device,
                                                             GError ** error);

GST_VULKAN_API
gboolean            gst_vulkan_device_enable_extension      (GstVulkanDevice * device,
                                                             const gchar * name);
GST_VULKAN_API
gboolean            gst_vulkan_device_disable_extension     (GstVulkanDevice * device,
                                                             const gchar * name);
GST_VULKAN_API
gboolean            gst_vulkan_device_is_extension_enabled  (GstVulkanDevice * device,
                                                             const gchar * name);
GST_VULKAN_API
gboolean            gst_vulkan_device_enable_layer          (GstVulkanDevice * device,
                                                             const gchar * name);
GST_VULKAN_API
gboolean            gst_vulkan_device_is_layer_enabled      (GstVulkanDevice * device,
                                                             const gchar * name);

GST_VULKAN_API
gpointer            gst_vulkan_device_get_proc_address      (GstVulkanDevice * device,
                                                             const gchar * name);
GST_VULKAN_API
void                gst_vulkan_device_foreach_queue         (GstVulkanDevice * device,
                                                             GstVulkanDeviceForEachQueueFunc func,
                                                             gpointer user_data);
GST_VULKAN_API
GstVulkanQueue *    gst_vulkan_device_get_queue             (GstVulkanDevice * device,
                                                             guint32 queue_family,
                                                             guint32 queue_i);
GST_VULKAN_API
VkPhysicalDevice    gst_vulkan_device_get_physical_device   (GstVulkanDevice * device);

GST_VULKAN_API
void                gst_context_set_vulkan_device           (GstContext * context,
                                                             GstVulkanDevice * device);
GST_VULKAN_API
gboolean            gst_context_get_vulkan_device           (GstContext * context,
                                                             GstVulkanDevice ** device);
GST_VULKAN_API
gboolean            gst_vulkan_device_handle_context_query  (GstElement * element,
                                                             GstQuery * query,
                                                             GstVulkanDevice * device);
GST_VULKAN_API
gboolean            gst_vulkan_device_run_context_query     (GstElement * element,
                                                             GstVulkanDevice ** device);

GST_VULKAN_API
GstVulkanFence *    gst_vulkan_device_create_fence          (GstVulkanDevice * device,
                                                             GError ** error);

G_END_DECLS

#endif /* __GST_VULKAN_DEVICE_H__ */

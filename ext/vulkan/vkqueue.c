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

#include "vkqueue.h"

#define GST_CAT_DEFAULT gst_vulkan_queue_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

G_DEFINE_TYPE_WITH_CODE (GstVulkanQueue, gst_vulkan_queue, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkanqueue", 0,
        "Vulkan Queue"));

static void gst_vulkan_queue_dispose (GObject * object);

static void
gst_vulkan_queue_init (GstVulkanQueue * device)
{
}

static void
gst_vulkan_queue_class_init (GstVulkanQueueClass * device_class)
{
  GObjectClass *gobject_class = (GObjectClass *) device_class;

  gobject_class->dispose = gst_vulkan_queue_dispose;
}

static void
gst_vulkan_queue_dispose (GObject * object)
{
  GstVulkanQueue *queue = GST_VULKAN_QUEUE (object);

  if (queue->device)
    gst_object_unref (queue->device);
  queue->device = NULL;
}

GstVulkanDevice *
gst_vulkan_queue_get_device (GstVulkanQueue * queue)
{
  g_return_val_if_fail (GST_IS_VULKAN_QUEUE (queue), NULL);

  return queue->device ? gst_object_ref (queue->device) : NULL;
}

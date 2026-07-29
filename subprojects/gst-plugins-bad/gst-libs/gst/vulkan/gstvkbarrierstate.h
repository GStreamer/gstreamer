/*
 * GStreamer
 * Copyright (C) 2026 Matthew Waters <matthew@centricular.com>
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

#pragma once

#include <gst/vulkan/gstvkdevice.h>

#define GST_TYPE_VULKAN_BARRIER_STATE         (gst_vulkan_barrier_state_get_type())
#define GST_VULKAN_BARRIER_STATE(o)           (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_VULKAN_BARRIER_STATE, GstVulkanBarrierState))
#define GST_VULKAN_BARRIER_STATE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GST_TYPE_VULKAN_BARRIER_STATE, GstVulkanBarrierStateClass))
#define GST_IS_VULKAN_BARRIER_STATE(o)        (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_VULKAN_BARRIER_STATE))
#define GST_IS_VULKAN_BARRIER_STATE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_VULKAN_BARRIER_STATE))
#define GST_VULKAN_BARRIER_STATE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_VULKAN_BARRIER_STATE, GstVulkanBarrierStateClass))

GST_VULKAN_API
GType gst_vulkan_barrier_state_get_type       (void);

/**
 * GstVulkanBarrierState:
 * @parent: the parent #GstObject
 *
 * Since: 1.30
 */
struct _GstVulkanBarrierState
{
  GstObject parent;

  GstVulkanDevice *device;

  /* <private> */
  gpointer _reserved        [GST_PADDING];
};

struct _GstVulkanBarrierStateClass
{
  GstObjectClass parent_class;

  /* <private> */
  gpointer _reserved        [GST_PADDING];
};

/**
 * GstVulkanBarrierStateForEachImageFunc:
 *
 * Since: 1.30
 */
typedef void (*GstVulkanBarrierStateForEachImageFunc) (GstVulkanImageMemory * image, gpointer user_data);
/**
 * GstVulkanBarrierStateForEachBufferFunc:
 *
 * Since: 1.30
 */
typedef void (*GstVulkanBarrierStateForEachBufferFunc) (GstVulkanBufferMemory * buffer, gpointer user_data);
/**
 * GstVulkanBarrierStateForEachTimelineSemaphoreFunc:
 *
 * Since: 1.30
 */
typedef void (*GstVulkanBarrierStateForEachTimelineSemaphoreFunc) (GstVulkanTimelineSemaphore * timeline, gpointer user_data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstVulkanBarrierState, gst_object_unref)

GST_VULKAN_API
GstVulkanBarrierState * gst_vulkan_barrier_state_new            (GstVulkanDevice *device) G_GNUC_WARN_UNUSED_RESULT;
GST_VULKAN_API
void                    gst_vulkan_barrier_state_reset          (GstVulkanBarrierState *self);
GST_VULKAN_API
gboolean                gst_vulkan_barrier_state_add_image_barrier (GstVulkanBarrierState *self,
                                                                 GstVulkanImageMemory * image,
                                                                 guint64 src_stage,
                                                                 guint64 dst_stage,
                                                                 guint64 new_access,
                                                                 VkImageLayout new_layout,
                                                                 GstVulkanQueue * new_queue);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_update_image_barrier (GstVulkanBarrierState * self,
                                                                 GstVulkanImageMemory * image,
                                                                 guint64 dst_stage,
                                                                 guint64 new_access,
                                                                 VkImageLayout new_layout,
                                                                 GstVulkanQueue * new_queue);
GST_VULKAN_API
gboolean                gst_vulkan_barrier_state_update_timeline_semaphore (GstVulkanBarrierState * self,
                                                                 GstVulkanTimelineSemaphore * timeline,
                                                                 guint64 new_value);
GST_VULKAN_API
gboolean                gst_vulkan_barrier_state_add_buffer_barrier (GstVulkanBarrierState *self,
                                                                 GstVulkanBufferMemory * buffer,
                                                                 guint64 src_stage,
                                                                 guint64 dst_stage,
                                                                 guint64 new_access,
                                                                 GstVulkanQueue * new_queue);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_update_buffer_barrier (GstVulkanBarrierState * self,
                                                                 GstVulkanBufferMemory * buffer,
                                                                 guint64 dst_stage,
                                                                 guint64 new_access,
                                                                 GstVulkanQueue * new_queue);
GST_VULKAN_API
gboolean                gst_vulkan_barrier_state_add_raw_barrier (GstVulkanBarrierState *self,
                                                                 gconstpointer barrier,
                                                                 guint64 src_stage,
                                                                 guint64 dst_stage);
GST_VULKAN_API
gboolean                gst_vulkan_barrier_state_pipeline_barrier (GstVulkanBarrierState *self,
                                                                 GstVulkanCommandBuffer *cmd,
                                                                 VkDependencyFlags dep_flags);
GST_VULKAN_API
gpointer                gst_vulkan_barrier_state_lock           (GstVulkanBarrierState *self);
GST_VULKAN_API
gboolean                gst_vulkan_barrier_state_commit         (GstVulkanBarrierState *self,
                                                                 gpointer state);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_rollback       (GstVulkanBarrierState *self,
                                                                 gpointer state);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_foreach_image_unlocked (GstVulkanBarrierState *self,
                                                                 gpointer state,
                                                                 GstVulkanBarrierStateForEachImageFunc func,
                                                                 gpointer user_data);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_foreach_buffer_unlocked (GstVulkanBarrierState *self,
                                                                 gpointer state,
                                                                 GstVulkanBarrierStateForEachBufferFunc func,
                                                                 gpointer user_data);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_foreach_timeline_semaphore_unlocked (GstVulkanBarrierState *self,
                                                                 gpointer state,
                                                                 GstVulkanBarrierStateForEachTimelineSemaphoreFunc func,
                                                                 gpointer user_data);
GST_VULKAN_API
void                    gst_vulkan_barrier_state_unlock         (GstVulkanBarrierState *self,
                                                                 gpointer state);

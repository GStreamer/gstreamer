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

#include <gst/vulkan/gstvkhandle.h>

G_BEGIN_DECLS

/**
 * gst_vulkan_timeline_semaphore_get_type:
 *
 * Since: 1.30
 */
GST_VULKAN_API
GType gst_vulkan_timeline_semaphore_get_type (void);
/**
 * GST_TYPE_VULKAN_TIMELINE_SEMAPHORE:
 *
 * Since: 1.30
 */
#define GST_TYPE_VULKAN_TIMELINE_SEMAPHORE (gst_vulkan_timeline_semaphore_get_type ())

/**
 * GstVulkanTimelineSemaphore:
 * @parent: parent #GstMiniObject
 * @semaphore: the #GstVulkanHandle containing the sempahore
 *
 * Since: 1.30
 */
struct _GstVulkanTimelineSemaphore
{
  GstMiniObject parent;

  GstVulkanHandle *semaphore;

  /* <private> */
  GMutex lock;
  guint64 value;

  gpointer _reserved        [GST_PADDING];
};

/**
 * gst_vulkan_timeline_semaphore_ref: (skip)
 * @timeline: a #GstVulkanTimelineSemaphore.
 *
 * Increases the refcount of the given @timeline by one.
 *
 * Returns: (transfer full): @timeline
 *
 * Since: 1.30
 */
static inline GstVulkanTimelineSemaphore* gst_vulkan_timeline_semaphore_ref(GstVulkanTimelineSemaphore* timeline);
static inline GstVulkanTimelineSemaphore *
gst_vulkan_timeline_semaphore_ref (GstVulkanTimelineSemaphore * timeline)
{
  return (GstVulkanTimelineSemaphore *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (timeline));
}

/**
 * gst_vulkan_timeline_semaphore_unref: (skip)
 * @timeline: (transfer full): a #GstVulkanTimelineSemaphore.
 *
 * Decreases the refcount of the @timeline. If the refcount reaches 0, the buffer
 * will be freed.
 *
 * Since: 1.30
 */
static inline void gst_vulkan_timeline_semaphore_unref(GstVulkanTimelineSemaphore* timeline);
static inline void
gst_vulkan_timeline_semaphore_unref (GstVulkanTimelineSemaphore * timeline)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (timeline));
}

/**
 * gst_clear_vulkan_timeline_semaphore: (skip)
 * @timeline_ptr: a pointer to a #GstVulkanTimelineSemaphore reference
 *
 * Clears a reference to a #GstVulkanTimelineSemaphore.
 *
 * @timeline_ptr must not be %NULL.
 *
 * If the reference is %NULL then this function does nothing. Otherwise, the
 * reference count of the handle is decreased and the pointer is set to %NULL.
 *
 * Since: 1.30
 */
static inline void gst_clear_vulkan_timeline_semaphore(GstVulkanTimelineSemaphore ** timeline_ptr);
static inline void
gst_clear_vulkan_timeline_semaphore (GstVulkanTimelineSemaphore ** timeline_ptr)
{
  gst_clear_mini_object ((GstMiniObject **) timeline_ptr);
}

GST_VULKAN_API
GstVulkanTimelineSemaphore * gst_vulkan_timeline_semaphore_new (GstVulkanDevice * device);

GST_VULKAN_API
void            gst_vulkan_timeline_semaphore_lock            (GstVulkanTimelineSemaphore * timeline);
GST_VULKAN_API
void            gst_vulkan_timeline_semaphore_unlock          (GstVulkanTimelineSemaphore * timeline);
GST_VULKAN_API
guint64         gst_vulkan_timeline_semaphore_peek_unlocked   (GstVulkanTimelineSemaphore * timeline);

GST_VULKAN_API
gboolean        gst_vulkan_timeline_semaphore_compare_exchange_unlocked (GstVulkanTimelineSemaphore * timeline,
                                                                  guint64 value,
                                                                  guint64 new_value);

G_END_DECLS

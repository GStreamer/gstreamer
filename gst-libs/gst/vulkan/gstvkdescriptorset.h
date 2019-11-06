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

#ifndef __GST_VULKAN_DESCRIPTOR_SET_H__
#define __GST_VULKAN_DESCRIPTOR_SET_H__

#include <gst/gst.h>

#include <gst/vulkan/vulkan_fwd.h>
#include <gst/vulkan/gstvkapi.h>

G_BEGIN_DECLS

GST_VULKAN_API
GType gst_vulkan_descriptor_set_get_type (void);
#define GST_TYPE_VULKAN_DESCRIPTOR_SET (gst_vulkan_descriptor_set_get_type ())

typedef struct _GstVulkanDescriptorSet GstVulkanDescriptorSet;

struct _GstVulkanDescriptorSet
{
  GstMiniObject             parent;

  VkDescriptorSet           set;

  /* <protected> */
  GstVulkanDescriptorPool  *pool;
  GstVulkanDescriptorCache *cache;

  guint                     n_layouts;
  GstVulkanHandle         **layouts;

  GMutex                    lock;
};

/**
 * gst_vulkan_descriptor_set_ref: (skip)
 * @set: a #GstVulkanDescriptorSet.
 *
 * Increases the refcount of the given buffer by one.
 *
 * Returns: (transfer full): @buf
 */
static inline GstVulkanDescriptorSet* gst_vulkan_descriptor_set_ref(GstVulkanDescriptorSet* set);
static inline GstVulkanDescriptorSet *
gst_vulkan_descriptor_set_ref (GstVulkanDescriptorSet * set)
{
  return (GstVulkanDescriptorSet *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (set));
}

/**
 * gst_vulkan_descriptor_set_unref: (skip)
 * @set: (transfer full): a #GstVulkanDescriptorSet.
 *
 * Decreases the refcount of the buffer. If the refcount reaches 0, the buffer
 * will be freed.
 */
static inline void gst_vulkan_descriptor_set_unref(GstVulkanDescriptorSet* set);
static inline void
gst_vulkan_descriptor_set_unref (GstVulkanDescriptorSet * set)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (set));
}

/**
 * gst_clear_vulkan_descriptor_set: (skip)
 * @set_ptr: a pointer to a #GstVulkanDescriptorSet reference
 *
 * Clears a reference to a #GstVulkanDescriptorSet.
 *
 * @buf_ptr must not be %NULL.
 *
 * If the reference is %NULL then this function does nothing. Otherwise, the
 * reference count of the descriptor set is decreased and the pointer is set
 * to %NULL.
 *
 * Since: 1.16
 */
static inline void
gst_clear_vulkan_descriptor_set (GstVulkanDescriptorSet ** set_ptr)
{
  gst_clear_mini_object ((GstMiniObject **) set_ptr);
}

#define gst_vulkan_descriptor_set_lock(set) g_mutex_lock (&((set)->lock))
#define gst_vulkan_descriptor_set_unlock(set) g_mutex_unlock (&((set)->lock))

GST_VULKAN_API
GstVulkanDescriptorSet *    gst_vulkan_descriptor_set_new_wrapped       (GstVulkanDescriptorPool * pool,
                                                                         VkDescriptorSet set,
                                                                         guint n_layouts,
                                                                         GstVulkanHandle ** layouts);

G_END_DECLS

#endif /* _GST_VULKAN_DESCRIPTOR_SET_H_ */

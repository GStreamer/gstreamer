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

#ifndef __GST_VULKAN_TRASH_H__
#define __GST_VULKAN_TRASH_H__

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

typedef void (*GstVulkanTrashNotify) (GstVulkanDevice * device, gpointer user_data);

struct _GstVulkanTrash
{
  GstMiniObject         parent;

  GstVulkanFence       *fence;

  GstVulkanTrashNotify  notify;
  gpointer              user_data;
};

#define GST_TYPE_VULKAN_TRASH gst_vulkan_trash_get_type()
GST_VULKAN_API
GType               gst_vulkan_trash_get_type (void);

/**
 * gst_vulkan_trash_ref: (skip)
 * @trash: a #GstVulkanTrash.
 *
 * Increases the refcount of the given trash object by one.
 *
 * Returns: (transfer full): @trash
 */
static inline GstVulkanTrash* gst_vulkan_trash_ref(GstVulkanTrash* trash);
static inline GstVulkanTrash *
gst_vulkan_trash_ref (GstVulkanTrash * trash)
{
  return (GstVulkanTrash *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (trash));
}

/**
 * gst_vulkan_trash_unref: (skip)
 * @trash: (transfer full): a #GstVulkanTrash.
 *
 * Decreases the refcount of the trash object. If the refcount reaches 0, the
 * trash will be freed.
 */
static inline void gst_vulkan_trash_unref(GstVulkanTrash* trash);
static inline void
gst_vulkan_trash_unref (GstVulkanTrash * trash)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (trash));
}

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVulkanTrash, gst_vulkan_trash_unref)
#endif

GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new                            (GstVulkanFence * fence,
                                                                     GstVulkanTrashNotify notify,
                                                                     gpointer user_data);

GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_descriptor_pool       (GstVulkanFence * fence,
                                                                     VkDescriptorPool descriptor_pool);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_descriptor_set_layout (GstVulkanFence * fence,
                                                                     VkDescriptorSetLayout descriptor_set_layout);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_framebuffer           (GstVulkanFence * fence,
                                                                     VkFramebuffer framebuffer);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_pipeline              (GstVulkanFence * fence,
                                                                     VkPipeline pipeline);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_pipeline_layout       (GstVulkanFence * fence,
                                                                     VkPipelineLayout pipeline_layout);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_render_pass           (GstVulkanFence * fence,
                                                                     VkRenderPass render_pass);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_sampler               (GstVulkanFence * fence,
                                                                     VkSampler sampler);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_semaphore             (GstVulkanFence * fence,
                                                                     VkSemaphore semaphore);

GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_free_descriptor_set        (GstVulkanFence * fence,
                                                                     VkDescriptorPool parent,
                                                                     VkDescriptorSet descriptor_set);

GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_object_unref               (GstVulkanFence * fence,
                                                                     GstObject * object);
GST_VULKAN_API
GstVulkanTrash *    gst_vulkan_trash_new_mini_object_unref          (GstVulkanFence * fence,
                                                                     GstMiniObject * object);

#define GST_TYPE_VULKAN_TRASH_LIST gst_vulkan_trash_list_get_type()
GST_VULKAN_API
GType gst_vulkan_trash_list_get_type (void);
#define GST_VULKAN_TRASH_LIST(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VULKAN_TRASH_LIST,GstVulkanTrashList))
#define GST_VULKAN_TRASH_LIST_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VULKAN_TRASH_LIST,GstVulkanTrashListClass))
#define GST_VULKAN_TRASH_LIST_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_VULKAN_TRASH_LIST,GstVulkanTrashListClass))
#define GST_IS_VULKAN_TRASH_LIST(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VULKAN_TRASH_LIST))
#define GST_IS_VULKAN_TRASH_LIST_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VULKAN_TRASH_LIST))

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVulkanTrashList, gst_object_unref)
#endif

struct _GstVulkanTrashList
{
  GstObject         parent;
};

typedef void        (*GstVulkanTrashListGC)     (GstVulkanTrashList * trash_list);
typedef gboolean    (*GstVulkanTrashListAdd)    (GstVulkanTrashList * trash_list, GstVulkanTrash * trash);
typedef gboolean    (*GstVulkanTrashListWait)   (GstVulkanTrashList * trash_list, guint64 timeout);

struct _GstVulkanTrashListClass
{
  GstObjectClass            parent_class;

  GstVulkanTrashListAdd     add_func;
  GstVulkanTrashListGC      gc_func;
  GstVulkanTrashListWait    wait_func;

  gpointer                 _padding[GST_PADDING];
};

GST_VULKAN_API
void                gst_vulkan_trash_list_gc                        (GstVulkanTrashList * trash_list);
GST_VULKAN_API
gboolean            gst_vulkan_trash_list_wait                      (GstVulkanTrashList * trash_list,
                                                                     guint64 timeout);
GST_VULKAN_API
gboolean            gst_vulkan_trash_list_add                       (GstVulkanTrashList * trash_list,
                                                                     GstVulkanTrash * trash);

GST_VULKAN_API
G_DECLARE_FINAL_TYPE (GstVulkanTrashFenceList, gst_vulkan_trash_fence_list, GST, VULKAN_TRASH_FENCE_LIST, GstVulkanTrashList);
GST_VULKAN_API
GstVulkanTrashList * gst_vulkan_trash_fence_list_new                (void);

G_END_DECLS

#endif /* __GST_VULKAN_TRASH_H__ */

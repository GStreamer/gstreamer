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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvktrash.h"
#include "gstvkhandle.h"

GST_DEBUG_CATEGORY (gst_debug_vulkan_trash);
#define GST_CAT_DEFAULT gst_debug_vulkan_trash

static void
_init_debug (void)
{
  static volatile gsize init;

  if (g_once_init_enter (&init)) {
    GST_DEBUG_CATEGORY_INIT (gst_debug_vulkan_trash,
        "vulkantrash", 0, "Vulkan Trash");
    g_once_init_leave (&init, 1);
  }
}

static void
gst_vulkan_trash_free (GstMiniObject * object)
{
  GstVulkanTrash *trash = (GstVulkanTrash *) object;

  g_warn_if_fail (gst_vulkan_fence_is_signaled (trash->fence));

  GST_TRACE ("Freeing trash object %p with fence %" GST_PTR_FORMAT, trash,
      trash->fence);

  gst_vulkan_fence_unref (trash->fence);

  g_free (trash);
}

/**
 * gst_vulkan_trash_new:
 * @fence: a #GstVulkanFence
 * @notify: (scope async): a #GstVulkanTrashNotify
 * @user_data: (closure notify): user data for @notify
 *
 * Create and return a new #GstVulkanTrash object that will stores a callback
 * to call when @fence is signalled.
 *
 * Returns: (transfer full): a new #GstVulkanTrash
 */
GstVulkanTrash *
gst_vulkan_trash_new (GstVulkanFence * fence, GstVulkanTrashNotify notify,
    gpointer user_data)
{
  GstVulkanTrash *ret = NULL;

  g_return_val_if_fail (fence != NULL, NULL);
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (fence->device), NULL);
  g_return_val_if_fail (notify != NULL, NULL);

  _init_debug ();

  ret = g_new0 (GstVulkanTrash, 1);
  GST_TRACE ("Creating new trash object %p with fence %" GST_PTR_FORMAT
      " on device %" GST_PTR_FORMAT, ret, fence, fence->device);
  gst_mini_object_init ((GstMiniObject *) ret, 0, gst_vulkan_trash_get_type (),
      NULL, NULL, (GstMiniObjectFreeFunction) gst_vulkan_trash_free);
  ret->fence = gst_vulkan_fence_ref (fence);
  ret->notify = notify;
  ret->user_data = user_data;

  return ret;
}

#if GLIB_SIZEOF_VOID_P == 8
# define PUSH_NON_DISPATCHABLE_HANDLE_TO_GPOINTER(pointer, handle) pointer = (gpointer) handle
# define TAKE_NON_DISPATCHABLE_HANDLE_FROM_GPOINTER(handle, type, pointer) handle = (type) pointer
#else
# define PUSH_NON_DISPATCHABLE_HANDLE_TO_GPOINTER(pointer, handle) \
    G_STMT_START { \
      pointer = g_new0(guint64, 1); \
      *((GstVulkanHandleTypedef *) pointer) = (GstVulkanHandleTypedef) handle; \
    } G_STMT_END
# define TAKE_NON_DISPATCHABLE_HANDLE_FROM_GPOINTER(handle, type, pointer) \
    G_STMT_START { \
      handle = *((type *) pointer); \
      g_free (pointer); \
    } G_STMT_END
#endif

#define FREE_DESTROY_FUNC(func, type, type_name) \
static void \
G_PASTE(_free_,type_name) (GstVulkanDevice * device, gpointer resource_handle) \
{ \
  type resource; \
  TAKE_NON_DISPATCHABLE_HANDLE_FROM_GPOINTER(resource, type, resource_handle); \
  GST_TRACE_OBJECT (device, "Freeing vulkan " G_STRINGIFY (type) \
      " %" GST_VULKAN_NON_DISPATCHABLE_HANDLE_FORMAT, resource); \
  func (device->device, resource, NULL); \
} \
GstVulkanTrash * \
G_PASTE(gst_vulkan_trash_new_free_,type_name) (GstVulkanFence * fence, \
    type type_name) \
{ \
  GstVulkanTrash *trash; \
  gpointer handle_data; \
  PUSH_NON_DISPATCHABLE_HANDLE_TO_GPOINTER(handle_data, type_name); \
  g_return_val_if_fail (type_name != VK_NULL_HANDLE, VK_NULL_HANDLE); \
  trash = gst_vulkan_trash_new (fence, \
      (GstVulkanTrashNotify) G_PASTE(_free_,type_name), handle_data); \
  return trash; \
}

FREE_DESTROY_FUNC (vkDestroyDescriptorPool, VkDescriptorPool, descriptor_pool);
FREE_DESTROY_FUNC (vkDestroyDescriptorSetLayout, VkDescriptorSetLayout,
    descriptor_set_layout);
FREE_DESTROY_FUNC (vkDestroyFramebuffer, VkFramebuffer, framebuffer);
FREE_DESTROY_FUNC (vkDestroyPipeline, VkPipeline, pipeline);
FREE_DESTROY_FUNC (vkDestroyPipelineLayout, VkPipelineLayout, pipeline_layout);
FREE_DESTROY_FUNC (vkDestroyRenderPass, VkRenderPass, render_pass);
FREE_DESTROY_FUNC (vkDestroySemaphore, VkSemaphore, semaphore)
    FREE_DESTROY_FUNC (vkDestroySampler, VkSampler, sampler);
#define FREE_WITH_VK_PARENT(func, type, type_name, parent_type) \
struct G_PASTE(free_parent_info_,type_name) \
{ \
  parent_type parent; \
  type resource; \
}; \
static void \
G_PASTE(_free_,type_name) (GstVulkanDevice * device, struct G_PASTE(free_parent_info_,type_name) *info) \
{ \
  GST_TRACE_OBJECT (device, "Freeing vulkan " G_STRINGIFY (type) \
      " %" GST_VULKAN_NON_DISPATCHABLE_HANDLE_FORMAT, info->resource); \
  func (device->device, info->parent, 1, &info->resource); \
  g_free (info); \
} \
GstVulkanTrash * \
G_PASTE(gst_vulkan_trash_new_free_,type_name) (GstVulkanFence * fence, \
    parent_type parent, type type_name) \
{ \
  struct G_PASTE(free_parent_info_,type_name) *info; \
  GstVulkanTrash *trash; \
  g_return_val_if_fail (type_name != VK_NULL_HANDLE, VK_NULL_HANDLE); \
  info = g_new0 (struct G_PASTE(free_parent_info_,type_name), 1); \
  /* FIXME: keep parent alive ? */\
  info->parent = parent; \
  info->resource = type_name; \
  trash = gst_vulkan_trash_new (fence, \
      (GstVulkanTrashNotify) G_PASTE(_free_,type_name), info); \
  return trash; \
}
FREE_WITH_VK_PARENT (vkFreeDescriptorSets, VkDescriptorSet, descriptor_set,
    VkDescriptorPool);

     static void
         _trash_object_unref (GstVulkanDevice * device, GstObject * object)
{
  gst_object_unref (object);
}

GstVulkanTrash *
gst_vulkan_trash_new_object_unref (GstVulkanFence * fence, GstObject * object)
{
  GstVulkanTrash *trash;
  g_return_val_if_fail (GST_IS_OBJECT (object), NULL);
  trash = gst_vulkan_trash_new (fence,
      (GstVulkanTrashNotify) _trash_object_unref, object);
  return trash;
}

static void
_trash_mini_object_unref (GstVulkanDevice * device, GstMiniObject * object)
{
  gst_mini_object_unref (object);
}

GstVulkanTrash *
gst_vulkan_trash_new_mini_object_unref (GstVulkanFence * fence,
    GstMiniObject * object)
{
  GstVulkanTrash *trash;
  g_return_val_if_fail (object != NULL, NULL);
  trash = gst_vulkan_trash_new (fence,
      (GstVulkanTrashNotify) _trash_mini_object_unref, object);
  return trash;
}

G_DEFINE_TYPE (GstVulkanTrashList, gst_vulkan_trash_list, GST_TYPE_OBJECT);

void
gst_vulkan_trash_list_gc (GstVulkanTrashList * trash_list)
{
  GstVulkanTrashListClass *trash_class;
  g_return_if_fail (GST_IS_VULKAN_TRASH_LIST (trash_list));
  trash_class = GST_VULKAN_TRASH_LIST_GET_CLASS (trash_list);
  g_return_if_fail (trash_class->gc_func != NULL);

  trash_class->gc_func (trash_list);
}

gboolean
gst_vulkan_trash_list_add (GstVulkanTrashList * trash_list,
    GstVulkanTrash * trash)
{
  GstVulkanTrashListClass *trash_class;
  g_return_val_if_fail (GST_IS_VULKAN_TRASH_LIST (trash_list), FALSE);
  trash_class = GST_VULKAN_TRASH_LIST_GET_CLASS (trash_list);
  g_return_val_if_fail (trash_class->add_func != NULL, FALSE);

  return trash_class->add_func (trash_list, trash);
}

gboolean
gst_vulkan_trash_list_wait (GstVulkanTrashList * trash_list, guint64 timeout)
{
  GstVulkanTrashListClass *trash_class;
  g_return_val_if_fail (GST_IS_VULKAN_TRASH_LIST (trash_list), FALSE);
  trash_class = GST_VULKAN_TRASH_LIST_GET_CLASS (trash_list);
  g_return_val_if_fail (trash_class->wait_func != NULL, FALSE);

  return trash_class->wait_func (trash_list, timeout);
}

static void
gst_vulkan_trash_list_class_init (GstVulkanTrashListClass * klass)
{
}

static void
gst_vulkan_trash_list_init (GstVulkanTrashList * trash_list)
{
}

typedef struct _GstVulkanTrashFenceList GstVulkanTrashFenceList;

struct _GstVulkanTrashFenceList
{
  GstVulkanTrashList parent;

  GList *list;
};

G_DEFINE_TYPE (GstVulkanTrashFenceList, gst_vulkan_trash_fence_list,
    GST_TYPE_VULKAN_TRASH_LIST);

static void
gst_vulkan_trash_fence_list_gc (GstVulkanTrashList * trash_list)
{
  GstVulkanTrashFenceList *fence_list = (GstVulkanTrashFenceList *) trash_list;
  GList *l = fence_list->list;

  while (l) {
    GstVulkanTrash *trash = l->data;

    if (gst_vulkan_fence_is_signaled (trash->fence)) {
      GList *next = g_list_next (l);
      GST_TRACE ("fence %" GST_PTR_FORMAT " has been signalled, notifying",
          trash->fence);
      trash->notify (trash->fence->device, trash->user_data);
      gst_vulkan_trash_unref (trash);
      fence_list->list = g_list_delete_link (fence_list->list, l);
      l = next;
    } else {
      l = g_list_next (l);
    }
  }
}

static gboolean
gst_vulkan_trash_fence_list_wait (GstVulkanTrashList * trash_list,
    guint64 timeout)
{
  GstVulkanTrashFenceList *fence_list = (GstVulkanTrashFenceList *) trash_list;
  VkResult err = VK_SUCCESS;
  guint i, n;

  /* remove all the previously signaled fences */
  gst_vulkan_trash_fence_list_gc (trash_list);

  n = g_list_length (fence_list->list);
  if (n > 0) {
    VkFence *fences;
    GstVulkanDevice *device = NULL;
    GList *l = NULL;

    fences = g_new0 (VkFence, n);
    for (i = 0, l = fence_list->list; i < n; i++, l = g_list_next (l)) {
      GstVulkanTrash *trash = l->data;

      if (device == NULL)
        device = trash->fence->device;

      fences[i] = trash->fence->fence;

      /* only support waiting on fences from the same device */
      g_assert (device == trash->fence->device);
    }

    GST_TRACE ("Waiting on %d fences with timeout %" GST_TIME_FORMAT, n,
        GST_TIME_ARGS (timeout));
    err = vkWaitForFences (device->device, n, fences, TRUE, timeout);
    g_free (fences);

    gst_vulkan_trash_fence_list_gc (trash_list);
  }

  return err == VK_SUCCESS;
}

static gboolean
gst_vulkan_trash_fence_list_add (GstVulkanTrashList * trash_list,
    GstVulkanTrash * trash)
{
  GstVulkanTrashFenceList *fence_list = (GstVulkanTrashFenceList *) trash_list;

  g_return_val_if_fail (GST_MINI_OBJECT_TYPE (trash) == GST_TYPE_VULKAN_TRASH,
      FALSE);

  /* XXX: do something better based on the actual fence */
  fence_list->list = g_list_prepend (fence_list->list, trash);

  return TRUE;
}

static void
gst_vulkan_trash_fence_list_finalize (GObject * object)
{
  GstVulkanTrashList *trash_list = (GstVulkanTrashList *) object;
  GstVulkanTrashFenceList *fence_list = (GstVulkanTrashFenceList *) object;

  gst_vulkan_trash_fence_list_gc (trash_list);
  g_warn_if_fail (fence_list->list == NULL);

  G_OBJECT_CLASS (gst_vulkan_trash_fence_list_parent_class)->finalize (object);
}

static void
gst_vulkan_trash_fence_list_class_init (GstVulkanTrashFenceListClass * klass)
{
  GstVulkanTrashListClass *trash_class = (GstVulkanTrashListClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;

  trash_class->add_func = gst_vulkan_trash_fence_list_add;
  trash_class->gc_func = gst_vulkan_trash_fence_list_gc;
  trash_class->wait_func = gst_vulkan_trash_fence_list_wait;

  object_class->finalize = gst_vulkan_trash_fence_list_finalize;
}

static void
gst_vulkan_trash_fence_list_init (GstVulkanTrashFenceList * trash_list)
{
}

GstVulkanTrashList *
gst_vulkan_trash_fence_list_new (void)
{
  return g_object_new (gst_vulkan_trash_fence_list_get_type (), NULL);
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVulkanTrash, gst_vulkan_trash);

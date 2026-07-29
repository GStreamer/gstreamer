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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvkbarrierstate.h"
#include "gstvkphysicaldevice-private.h"

/**
 * SECTION:vkbarrierstate
 * @title: GstVulkanBarrierState
 * @short_description: Vulkan Barrier State
 * @see_also: #GstVulkanCommandBuffer, #GstVulkanOperation
 *
 * A #GstVulkanBarrierState tracks a set of barriers transitions that could be
 * applied within a command buffer.
 *
 * Since: 1.30
 */

#if !GLIB_CHECK_VERSION(2, 81, 1)
#define g_sort_array(a,n,s,f,udata) gst_vulkan_sort_array(a,n,s,f,udata)

// Don't need to maintain ABI compat here (n_elements), since we never pass
// the function as pointer but always call it directly ourselves.
static inline void
gst_vulkan_sort_array (const void *array,
    gssize n_elements,
    size_t element_size, GCompareDataFunc compare_func, void *user_data)
{
  if (n_elements >= 0 && n_elements <= G_MAXINT) {
    g_qsort_with_data (array, n_elements, element_size, compare_func,
        user_data);
  } else {
    g_abort ();
  }
}
#endif

struct semaphore_update
{
  GstVulkanTimelineSemaphore *timeline;
  guint64 old_value;
  guint64 new_value;
};

struct dependency_buffer
{
  GstVulkanBufferMemory *mem;
  GstVulkanBarrierBufferInfo old_info;
  GstVulkanBarrierBufferInfo new_info;
};

static void
clear_dependency_buffer (struct dependency_buffer *dep)
{
  gst_vulkan_barrier_buffer_info_clear (&dep->old_info);
  gst_vulkan_barrier_buffer_info_clear (&dep->new_info);
  gst_clear_mini_object ((GstMiniObject **) & dep->mem);
}

struct dependency_image
{
  GstVulkanImageMemory *mem;
  GstVulkanBarrierImageInfo old_info;
  GstVulkanBarrierImageInfo new_info;
};

static void
clear_dependency_image (struct dependency_image *dep)
{
  gst_vulkan_barrier_image_info_clear (&dep->old_info);
  gst_vulkan_barrier_image_info_clear (&dep->new_info);
  gst_clear_mini_object ((GstMiniObject **) & dep->mem);
}

struct dependency
{
  GstVulkanBarrierType type;

  union
  {
    struct dependency_buffer buffer;
    struct dependency_image image;
  };
};

static void
clear_dependency (struct dependency *dep)
{
  switch (dep->type) {
    case GST_VULKAN_BARRIER_TYPE_MEMORY:
      // clear_dependency_memory (&dep->memory);
      break;
    case GST_VULKAN_BARRIER_TYPE_BUFFER:
      clear_dependency_buffer (&dep->buffer);
      break;
    case GST_VULKAN_BARRIER_TYPE_IMAGE:
      clear_dependency_image (&dep->image);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

struct vk_pipeline_stages
{
  VkPipelineStageFlags src_stage;
  VkPipelineStageFlags dst_stage;
};

typedef struct _GstVulkanBarrierStatePrivate GstVulkanBarrierStatePrivate;

struct _GstVulkanBarrierStatePrivate
{
  gboolean has_sync2;
  gsize state_cookie;

  // array of struct dependency
  GArray *deps;
  // array of VkImageMemoryBarrier or VkImageMemoryBarrier2
  GArray *image_barriers;
  // array of struct vk_pipeline_stages for each barrier in @image_barriers if VkCmdPipelineBarrier2 is not in use.
  GArray *image_barrier_stages;
  // array of VkBufferMemoryBarrier or VkBufferMemoryBarrier2
  GArray *buffer_barriers;
  // array of struct vk_pipeline_stages for each barrier in @buffer_barriers if VkCmdPipelineBarrier2 is not in use.
  GArray *buffer_barrier_stages;
  // array of VkMemoryBarrier or VkMemoryBarrier2
  GArray *memory_barriers;
  // array of struct vk_pipeline_stages for each barrier in @memory_barriers if VkCmdPipelineBarrier2 is not in use.
  GArray *memory_barrier_stages;

  // array of struct semaphore_update
  GArray *semaphore_updates;

#if defined(VK_KHR_synchronization2)
  PFN_vkCmdPipelineBarrier2KHR CmdPipelineBarrier2;
#endif
};

enum
{
  PROP_DEVICE = 1,
  N_PROPERTIES,
};

static GParamSpec *g_properties[N_PROPERTIES];

GST_DEBUG_CATEGORY_STATIC (GST_CAT_VULKAN_BARRIER_STATE);
#define GST_CAT_DEFAULT GST_CAT_VULKAN_BARRIER_STATE

#define GET_PRIV(self) ((GstVulkanBarrierStatePrivate *) \
  gst_vulkan_barrier_state_get_instance_private (GST_VULKAN_BARRIER_STATE (self)))

#define gst_vulkan_barrier_state_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanBarrierState, gst_vulkan_barrier_state,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstVulkanBarrierState)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_VULKAN_BARRIER_STATE,
        "vulkanbarrierstate", 0, "Vulkan Barrier State"));

static void
gst_vulkan_barrier_state_constructed (GObject * object)
{
  GstVulkanBarrierState *self = GST_VULKAN_BARRIER_STATE (object);
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  priv->has_sync2 =
      gst_vulkan_physical_device_has_feature_synchronization2
      (self->device->physical_device);

#if defined(VK_KHR_synchronization2)
  if (priv->has_sync2) {
    if (gst_vulkan_physical_device_check_api_version (self->
            device->physical_device, 1, 3, 0)) {
      priv->CmdPipelineBarrier2 =
          gst_vulkan_device_get_proc_address (self->device,
          "vkCmdPipelineBarrier2");
    }

    if (!priv->CmdPipelineBarrier2) {
      priv->CmdPipelineBarrier2 =
          gst_vulkan_device_get_proc_address (self->device,
          "vkCmdPipelineBarrier2KHR");
    }

    priv->image_barriers =
        g_array_new (FALSE, TRUE, sizeof (VkImageMemoryBarrier2KHR));
    priv->buffer_barriers =
        g_array_new (FALSE, TRUE, sizeof (VkBufferMemoryBarrier2KHR));
    priv->memory_barriers =
        g_array_new (FALSE, TRUE, sizeof (VkMemoryBarrier2KHR));
  } else
#endif
  {
    priv->image_barriers =
        g_array_new (FALSE, TRUE, sizeof (VkImageMemoryBarrier));
    priv->image_barrier_stages =
        g_array_new (FALSE, TRUE, sizeof (struct vk_pipeline_stages));
    priv->buffer_barriers =
        g_array_new (FALSE, TRUE, sizeof (VkBufferMemoryBarrier));
    priv->buffer_barrier_stages =
        g_array_new (FALSE, TRUE, sizeof (struct vk_pipeline_stages));
    priv->memory_barriers = g_array_new (FALSE, TRUE, sizeof (VkMemoryBarrier));
    priv->memory_barrier_stages =
        g_array_new (FALSE, TRUE, sizeof (struct vk_pipeline_stages));
  }

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_vulkan_barrier_state_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanBarrierState *self = GST_VULKAN_BARRIER_STATE (object);

  switch (prop_id) {
    case PROP_DEVICE:
      /* G_PARAM_CONSTRUCT_ONLY */
      g_assert (!self->device);
      self->device = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_barrier_state_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanBarrierState *self = GST_VULKAN_BARRIER_STATE (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_object (value, self->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_barrier_state_finalize (GObject * object)
{
  GstVulkanBarrierState *self = GST_VULKAN_BARRIER_STATE (object);
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  g_clear_pointer (&priv->deps, g_array_unref);
  g_clear_pointer (&priv->semaphore_updates, g_array_unref);
  g_clear_pointer (&priv->image_barriers, g_array_unref);
  g_clear_pointer (&priv->buffer_barriers, g_array_unref);
  g_clear_pointer (&priv->memory_barriers, g_array_unref);
  g_clear_pointer (&priv->image_barrier_stages, g_array_unref);
  g_clear_pointer (&priv->buffer_barrier_stages, g_array_unref);
  g_clear_pointer (&priv->memory_barrier_stages, g_array_unref);

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_barrier_state_init (GstVulkanBarrierState * self)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  priv->deps = g_array_new (FALSE, TRUE, sizeof (struct dependency));
  g_array_set_clear_func (priv->deps, (GDestroyNotify) clear_dependency);
  priv->semaphore_updates =
      g_array_new (FALSE, TRUE, sizeof (struct semaphore_update));
}

static void
gst_vulkan_barrier_state_class_init (GstVulkanBarrierStateClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_vulkan_barrier_state_set_property;
  gobject_class->get_property = gst_vulkan_barrier_state_get_property;
  gobject_class->constructed = gst_vulkan_barrier_state_constructed;
  gobject_class->finalize = gst_vulkan_barrier_state_finalize;

  g_properties[PROP_DEVICE] =
      g_param_spec_object ("device", "GstVulkanDevice",
      "Vulkan device", GST_TYPE_VULKAN_DEVICE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, g_properties);
}

GstVulkanBarrierState *
gst_vulkan_barrier_state_new (GstVulkanDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);

  GstVulkanBarrierState *barriers =
      g_object_new (GST_TYPE_VULKAN_BARRIER_STATE, "device", device, NULL);

  gst_object_ref_sink (barriers);

  return barriers;
}

/**
 * gst_vulkan_barrier_state_reset:
 * @self: a #GstVulkanBarrierState
 *
 * Reset @self to that of a newly created instance. All stored data is removed.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_reset (GstVulkanBarrierState * self)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  priv->state_cookie++;
  g_array_set_size (priv->deps, 0);
  g_array_set_size (priv->image_barriers, 0);
  g_array_set_size (priv->buffer_barriers, 0);
  g_array_set_size (priv->memory_barriers, 0);
  if (priv->image_barrier_stages)
    g_array_set_size (priv->image_barrier_stages, 0);
  if (priv->buffer_barrier_stages)
    g_array_set_size (priv->buffer_barrier_stages, 0);
  if (priv->memory_barrier_stages)
    g_array_set_size (priv->memory_barrier_stages, 0);
  g_array_set_size (priv->semaphore_updates, 0);
}

struct barrier_lock
{
  // image or buffer memory
  GstVulkanBarrierType type;
  gpointer mem;
  gsize barrier_i;
};

static int
cmp_barrier_lock (const struct barrier_lock *mem1,
    const struct barrier_lock *mem2, gpointer user_data)
{
  if (mem1->mem == mem2->mem)
    return 0;
  else if (mem1->mem < mem2->mem)
    return -1;
  else
    return 1;
}

struct semaphore_lock
{
  GstVulkanTimelineSemaphore *timeline;
  gsize timeline_i;
};

static int
cmp_semaphore_lock (const struct semaphore_lock *mem1,
    const struct semaphore_lock *mem2, gpointer user_data)
{
  if (mem1->timeline == mem2->timeline)
    return 0;
  else if (mem1->timeline < mem2->timeline)
    return -1;
  else
    return 1;
}

struct lock_state
{
  gsize state_cookie;
  gssize last_applied_barrier_idx;
  gssize last_applied_semaphore_idx;
  gsize n_semaphore_locks;
  struct semaphore_lock *semaphore_locks;
  gsize n_locks;
  struct barrier_lock locks[];
};

/**
 * gst_vulkan_barrier_state_lock:
 * @self: a #GstVulkanBarrierState
 *
 * Lock all the #GstVulkanImageMemory, or #GstVulkanBufferMemory tracked by
 * @self.
 *
 * It is strongly recommended to call this just before submission of
 * the command buffer.
 *
 * Returns: A state cookie that must be passed to
 *          gst_vulkan_barrier_state_unlock().
 *
 * Since: 1.30
 */
gpointer
gst_vulkan_barrier_state_lock (GstVulkanBarrierState * self)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), NULL);
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  struct lock_state *locks;
  gsize i, n_locks = 0;

  GST_OBJECT_LOCK (self);

  for (i = 0; i < priv->deps->len; i++) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency, i);

    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_BUFFER:
        n_locks++;
        break;
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        n_locks++;
        break;
      default:
        break;
    }
  }

  locks =
      g_malloc0 (sizeof (struct lock_state) +
      sizeof (struct barrier_lock) * n_locks);
  locks->state_cookie = priv->state_cookie;
  locks->last_applied_barrier_idx = -1;
  locks->last_applied_semaphore_idx = -1;
  locks->n_semaphore_locks = priv->semaphore_updates->len;
  locks->semaphore_locks =
      g_new0 (struct semaphore_lock, locks->n_semaphore_locks);

  for (i = 0; i < priv->deps->len; i++) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency, i);

    locks->locks[locks->n_locks].type = dep->type;
    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_BUFFER:
        locks->locks[locks->n_locks].mem = dep->buffer.mem;
        locks->locks[locks->n_locks].barrier_i = i;
        locks->n_locks++;
        break;
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        locks->locks[locks->n_locks].mem = dep->image.mem;
        locks->locks[locks->n_locks].barrier_i = i;
        locks->n_locks++;
        break;
      default:
        break;
    }
  }

  GST_TRACE_OBJECT (self, "locking %" G_GSIZE_FORMAT " memory locks and %"
      G_GSIZE_FORMAT " semaphore locks", locks->n_locks,
      locks->n_semaphore_locks);

  // dynamic but deterministic locking order to avoid deadlocks if another set of memories is
  // being locked at the same time, sort the list of memories to lock by pointer address of the
  // memory.
  //
  // i.e. with memories A, B, C sorting them in a deterministic order ensures
  // that locking any set will always cause at least one set to succeed. For a
  // list of three memories, this is all the possible sorted combinations:
  //
  // 1. A, B, C
  // 2. A, B
  // 3. A, C
  // 4. B, C
  // 5. A
  // 6. B
  // 7. C
  //
  // Running any 2 of these combinations on two separate threads will never
  // result in a deadlock.
  g_sort_array (locks->locks, locks->n_locks, sizeof (struct barrier_lock),
      (GCompareDataFunc) cmp_barrier_lock, NULL);

  for (i = 0; i < locks->n_locks; i++) {
    switch (locks->locks[i].type) {
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        gst_vulkan_image_memory_lock (locks->locks[i].mem);
        break;
      default:
        gst_vulkan_buffer_memory_lock (locks->locks[i].mem);
        break;
    }
  }

  for (i = 0; i < priv->semaphore_updates->len; i++) {
    struct semaphore_update *update =
        &g_array_index (priv->semaphore_updates, struct semaphore_update, i);
    locks->semaphore_locks[i].timeline_i = i;
    locks->semaphore_locks[i].timeline = update->timeline;
  }

  g_sort_array (locks->semaphore_locks, locks->n_semaphore_locks,
      sizeof (struct semaphore_lock), (GCompareDataFunc) cmp_semaphore_lock,
      NULL);

  for (i = 0; i < locks->n_semaphore_locks; i++) {
    gst_vulkan_timeline_semaphore_lock (locks->semaphore_locks[i].timeline);
  }

  GST_OBJECT_UNLOCK (self);

  return locks;
}

/**
 * gst_vulkan_barrier_state_unlock:
 * @self: a #GstVulkanBarrierState
 * @state: a state pointer returned from gst_vulkan_barrier_state_lock().
 *
 * Unlock all the #GstVulkanImageMemory, or #GstVulkanBufferMemory tracked by
 * @self.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_unlock (GstVulkanBarrierState * self, gpointer state)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (state != NULL);

  struct lock_state *locks = (struct lock_state *) state;

  GST_TRACE_OBJECT (self, "unlocking %" G_GSIZE_FORMAT " memory locks and %"
      G_GSIZE_FORMAT " semaphore locks", locks->n_locks,
      locks->n_semaphore_locks);

  for (int i = 0; i < locks->n_locks; i++) {
    switch (locks->locks[i].type) {
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        gst_vulkan_image_memory_unlock (locks->locks[i].mem);
        break;
      default:
        gst_vulkan_buffer_memory_unlock (locks->locks[i].mem);
        break;
    }
  }

  for (gsize i = 0; i < locks->n_semaphore_locks; i++) {
    gst_vulkan_timeline_semaphore_unlock (locks->semaphore_locks[i].timeline);
  }

  g_free (locks->semaphore_locks);
  g_free (state);
}

static void
gst_vulkan_barrier_state_rollback_unlocked (GstVulkanBarrierState * self,
    struct lock_state *locks)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  // resetting to the original barrier information should always succeed
  // unless another thread is modifying the same barrier without the
  // relevant locking.
  for (gssize i = locks->last_applied_semaphore_idx; i >= 0; i--) {
    struct semaphore_update *update =
        &g_array_index (priv->semaphore_updates, struct semaphore_update,
        locks->semaphore_locks[i].timeline_i);

    if (!gst_vulkan_timeline_semaphore_compare_exchange_unlocked
        (update->timeline, update->new_value, update->old_value)) {
      g_warn_if_reached ();
    }
  }

  for (gssize i = locks->last_applied_barrier_idx; i >= 0; i--) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency,
        locks->locks[i].barrier_i);
    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        GST_TRACE_OBJECT (self, "rollback of barrier state for image %p",
            dep->image.mem);
        if (!gst_vulkan_image_memory_compare_exchange_barrier_unlocked
            (dep->image.mem, &dep->image.new_info, &dep->image.old_info)) {
          g_warn_if_reached ();
        }
        break;
      case GST_VULKAN_BARRIER_TYPE_BUFFER:
        // TODO
        break;
      default:
        break;
    }
  }
}

/**
 * gst_vulkan_barrier_state_commit:
 * @self: a #GstVulkanBarrierState
 * @state: a state cookie from gst_vulkan_barrier_state_lock()
 *
 * Commit all the barrier states to their respective #GstVulkanImageMemory, or
 * #GstVulkanBufferMemory.
 *
 * Can fail if the original state of the image/buffer barrier has changed
 * after gst_vulkan_barrier_state_add_buffer_barrier() or
 * gst_vulkan_barrier_state_add_image_barrier() was originally called. The
 * operation (entire command buffer) should be retried in this case.
 *
 * It is strongly recommended to only call this just before submission
 * of associated the command buffer.
 *
 * Returns: %TRUE on success, %FALSE if at least one state transition failed.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_state_commit (GstVulkanBarrierState * self, gpointer state)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), FALSE);
  g_return_val_if_fail (state != NULL, FALSE);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);
  struct lock_state *locks = (struct lock_state *) state;
  gboolean ret = TRUE;
  int i;

  g_return_val_if_fail (priv->state_cookie == locks->state_cookie, FALSE);

  GST_OBJECT_LOCK (self);

  for (i = 0; i < locks->n_semaphore_locks; i++) {
    struct semaphore_update *update =
        &g_array_index (priv->semaphore_updates, struct semaphore_update,
        locks->semaphore_locks[i].timeline_i);

    if (!gst_vulkan_timeline_semaphore_compare_exchange_unlocked
        (update->timeline, update->old_value, update->new_value)) {
      GST_DEBUG_OBJECT (self,
          "Failed to updated timeline semaphore value for %p",
          update->timeline);
      goto undo;
    }
    GST_TRACE_OBJECT (self,
        "succesfully updated timeline semaphore value for %p",
        update->timeline);

    locks->last_applied_semaphore_idx = i;
  }

  for (i = 0; i < locks->n_locks; i++) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency,
        locks->locks[i].barrier_i);

    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        if (!gst_vulkan_image_memory_compare_exchange_barrier_unlocked
            (dep->image.mem, &dep->image.old_info, &dep->image.new_info)) {
          // updated barrier info failed to apply, need to unwind and redo later.
          i--;
          GST_DEBUG_OBJECT (self, "failed to update barrier state for image %p",
              dep->image.mem);
          goto undo;
        }
        GST_TRACE_OBJECT (self,
            "succesfully updated barrier state for image %p", dep->image.mem);
        break;
      case GST_VULKAN_BARRIER_TYPE_BUFFER:
        if (!gst_vulkan_buffer_memory_compare_exchange_barrier_unlocked
            (dep->buffer.mem, &dep->buffer.old_info, &dep->buffer.new_info)) {
          // updated barrier info failed to apply, need to unwind and redo later.
          i--;
          GST_DEBUG_OBJECT (self,
              "failed to update barrier state for buffer %p", dep->buffer.mem);
          goto undo;
        }
        break;
      default:
        break;
    }
    locks->last_applied_barrier_idx = i;
  }

  if (0) {
  undo:
    ret = FALSE;
    gst_vulkan_barrier_state_rollback_unlocked (self, state);
  }

  GST_OBJECT_UNLOCK (self);

  return ret;
}

/**
 * gst_vulkan_barrier_state_rollback:
 * @self: a #GstVulkanBarrierState
 * @state: a state cookie from gst_vulkan_barrier_state_lock()
 *
 * Rollback all barrier updates performed by gst_vulkan_barrier_state_commit().
 *
 * It is strongly recommended to only call this if command buffer submission
 * fails.  Do not call this if gst_vulkan_barrier_state_commit() fails.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_rollback (GstVulkanBarrierState * self, gpointer state)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (state != NULL);
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);
  struct lock_state *locks = (struct lock_state *) state;

  g_return_if_fail (priv->state_cookie == locks->state_cookie);

  GST_OBJECT_LOCK (self);

  gst_vulkan_barrier_state_rollback_unlocked (self, locks);

  GST_OBJECT_UNLOCK (self);
}

static struct dependency *
find_dependency (GstVulkanBarrierState * self, gpointer mem)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  for (int i = 0; i < priv->deps->len; i++) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency, i);

    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_BUFFER:
        if (dep->buffer.mem == mem)
          return dep;
        break;
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        if (dep->image.mem == mem)
          return dep;
        break;
      default:
        break;
    }
  }

  return NULL;
}

/**
 * gst_vulkan_barrier_state_foreach_image_unlocked:
 * @self: a #GstVulkanBarrierState
 * @state: a state cookie from gst_vulkan_barrier_state_lock()
 * @func: (scope call): function to call on every #GstVulkanImageMemory
 *        currently tracked by @self.
 * @user_data: user provided data for @func
 *
 * Call a user provided function for every #GstVulkanImageMemory currently locked by @self.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_foreach_image_unlocked (GstVulkanBarrierState * self,
    gpointer state, GstVulkanBarrierStateForEachImageFunc func,
    gpointer user_data)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (state != NULL);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);
  struct lock_state *locks = (struct lock_state *) state;
  int i;

  g_return_if_fail (priv->state_cookie == locks->state_cookie);

  GST_OBJECT_LOCK (self);

  for (i = 0; i < locks->n_locks; i++) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency,
        locks->locks[i].barrier_i);

    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_IMAGE:
        func (dep->image.mem, user_data);
      default:
        break;
    }
  }

  GST_OBJECT_UNLOCK (self);
}

/**
 * gst_vulkan_barrier_state_foreach_buffer_unlocked:
 * @self: a #GstVulkanBarrierState
 * @state: a state cookie from gst_vulkan_barrier_state_lock()
 * @func: (scope call): function to call on every #GstVulkanBufferMemory
 *        currently tracked by @self.
 * @user_data: user provided data for @func
 *
 * Call a user provided function for every #GstVulkanBufferMemory currently locked by @self.
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_foreach_buffer_unlocked (GstVulkanBarrierState * self,
    gpointer state, GstVulkanBarrierStateForEachBufferFunc func,
    gpointer user_data)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (state != NULL);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);
  struct lock_state *locks = (struct lock_state *) state;
  int i;

  g_return_if_fail (priv->state_cookie == locks->state_cookie);

  GST_OBJECT_LOCK (self);

  for (i = 0; i < locks->n_locks; i++) {
    struct dependency *dep = &g_array_index (priv->deps, struct dependency,
        locks->locks[i].barrier_i);

    switch (dep->type) {
      case GST_VULKAN_BARRIER_TYPE_BUFFER:
        func (dep->buffer.mem, user_data);
      default:
        break;
    }
  }

  GST_OBJECT_UNLOCK (self);
}

/**
 * gst_vulkan_barrier_state_foreach_timeline_semaphore_unlocked:
 * @self: a #GstVulkanBarrierState
 * @state: a state cookie from gst_vulkan_barrier_state_lock()
 * @func: (scope call): function to call on every #GstVulkanTimelineSemaphore
 *        currently tracked by @self.
 * @user_data: user provided data for @func
 *
 * Call a user provided function for every #GstVulkanTimelineSemaphore currently locked by @self.
 *
 * Since: 1.30
 */
void gst_vulkan_barrier_state_foreach_timeline_semaphore_unlocked
    (GstVulkanBarrierState * self, gpointer state,
    GstVulkanBarrierStateForEachTimelineSemaphoreFunc func, gpointer user_data)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (state != NULL);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);
  struct lock_state *locks = (struct lock_state *) state;
  int i;

  g_return_if_fail (priv->state_cookie == locks->state_cookie);

  GST_OBJECT_LOCK (self);

  for (i = 0; i < locks->n_semaphore_locks; i++) {
    struct semaphore_update *update =
        &g_array_index (priv->semaphore_updates, struct semaphore_update,
        locks->semaphore_locks[i].timeline_i);

    func (update->timeline, user_data);
  }

  GST_OBJECT_UNLOCK (self);
}

static struct dependency *
ensure_image_dep (GstVulkanBarrierState * self, GstVulkanImageMemory * image)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  struct dependency *dep = find_dependency (self, image);
  if (!dep) {
    struct dependency new_dep = {
      .type = GST_VULKAN_BARRIER_TYPE_IMAGE,
    };
    new_dep.image.mem =
        (GstVulkanImageMemory *) gst_memory_ref (GST_MEMORY_CAST (image));
    gst_vulkan_image_memory_lock (image);
    gst_vulkan_image_memory_peek_barrier_unlocked (image,
        &new_dep.image.old_info);
    gst_vulkan_image_memory_unlock (image);
    gst_vulkan_barrier_image_info_copy_into (&new_dep.image.old_info,
        &new_dep.image.new_info);
    g_array_append_val (priv->deps, new_dep);
    dep = &g_array_index (priv->deps, struct dependency, priv->deps->len - 1);
  }

  return dep;
}

static void
update_image_barrier (struct dependency *dep,
    guint64 dst_stage, guint64 new_access,
    VkImageLayout new_layout, GstVulkanQueue * new_queue)
{
  g_assert (dep->type == GST_VULKAN_BARRIER_TYPE_IMAGE);

  dep->image.new_info.image_layout = new_layout;
  dep->image.new_info.parent.access_flags = new_access;
  dep->image.new_info.parent.pipeline_stages = dst_stage;
  gst_object_replace ((GstObject **) & dep->image.new_info.parent.queue,
      (GstObject *) new_queue);
}

/**
 * gst_vulkan_barrier_state_add_image_barrier:
 * @self: a #GstVulkanBarrierState
 * @image: the #GstVulkanImageMemory the barrier is representing
 * @src_stage: the pipeline stage to wait for (`VkPipelineStageFlags` or
 *     `VkPipelineStageFlags2`)
 * @dst_stage: the pipeline stage to signal (`VkPipelineStageFlags` or
 *     `VkPipelineStageFlags2`)
 * @new_access: the new access flags for @image (`VkAccessFlags` or
 *     `VkAccessFlags2`)
 * @new_layout: the new `VkImageLayout` for @image
 * @new_queue: (nullable): destination queue for transfer of the @image
 *     ownership
 *
 * Adds an image barrier for @image to the list of barriers that will be used
 * when calling gst_vulkan_barrier_state_pipeline_barrier() and applied to the
 * @image when calling gst_vulkan_barrier_state_commit().
 *
 * Returns: whether the barrier could be added.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_state_add_image_barrier (GstVulkanBarrierState * self,
    GstVulkanImageMemory * image, guint64 src_stage, guint64 dst_stage,
    guint64 new_access, VkImageLayout new_layout, GstVulkanQueue * new_queue)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), FALSE);
  g_return_val_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)),
      FALSE);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  GST_OBJECT_LOCK (self);

  struct dependency *dep = ensure_image_dep (self, image);

  guint32 queue_family_index = VK_QUEUE_FAMILY_IGNORED;
  if (dep->image.new_info.parent.queue)
    queue_family_index = dep->image.new_info.parent.queue->family;

#if defined (VK_KHR_synchronization2)
  if (priv->has_sync2) {
    VkImageMemoryBarrier2KHR barrier2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
      .pNext = NULL,
      .srcStageMask = src_stage,
      .dstStageMask = dst_stage,
      .srcAccessMask = dep->image.new_info.parent.access_flags,
      .dstAccessMask = new_access,
      .oldLayout = dep->image.new_info.image_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = queue_family_index,
      .dstQueueFamilyIndex = new_queue ?
          new_queue->family : VK_QUEUE_FAMILY_IGNORED,
      .image = image->image,
      .subresourceRange = dep->image.new_info.subresource_range,
    };

    g_array_append_val (priv->image_barriers, barrier2);
  } else
#endif
  {
    /* this might overflow */
    if (new_access > VK_ACCESS_FLAG_BITS_MAX_ENUM) {
      GST_OBJECT_UNLOCK (self);
      GST_ERROR_OBJECT (self, "Invalid new access value: %" G_GUINT64_FORMAT,
          new_access);
      return FALSE;
    }

    VkImageMemoryBarrier barrier = (VkImageMemoryBarrier) {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = dep->image.new_info.parent.access_flags,
      .dstAccessMask = (VkAccessFlags) new_access,
      .oldLayout = dep->image.new_info.image_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = queue_family_index,
      .dstQueueFamilyIndex = new_queue ?
          new_queue->family : VK_QUEUE_FAMILY_IGNORED,
      .image = image->image,
      .subresourceRange = dep->image.new_info.subresource_range,
    };

    g_array_append_val (priv->image_barriers, barrier);

    struct vk_pipeline_stages stages = {
      .src_stage = src_stage,
      .dst_stage = dst_stage,
    };
    g_array_append_val (priv->image_barrier_stages, stages);
  }

  update_image_barrier (dep, dst_stage, new_access, new_layout, new_queue);

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/**
 * gst_vulkan_barrier_state_update_image_barrier:
 * @self: a #GstVulkanBarrierState
 * @image: the #GstVulkanImageMemory the barrier is representing
 * @dst_stage: the pipeline stage to signal (`VkPipelineStageFlags` or
 *     `VkPipelineStageFlags2`)
 * @new_access: the new access flags for @image (`VkAccessFlags` or
 *     `VkAccessFlags2`)
 * @new_layout: the new `VkImageLayout` for @image
 * @new_queue: (nullable): destination queue for transfer of the @image
 *     ownership
 *
 * Adds (or updates) the image barrier state that will be applied to the
 * @image when calling gst_vulkan_barrier_state_commit().
 *
 * This does not add any barrier to the vulkan command stream. This would need
 * to be done manually or through gst_vulkan_barrier_state_add_raw_barrier().
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_update_image_barrier (GstVulkanBarrierState * self,
    GstVulkanImageMemory * image, guint64 dst_stage, guint64 new_access,
    VkImageLayout new_layout, GstVulkanQueue * new_queue)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (gst_is_vulkan_image_memory (GST_MEMORY_CAST (image)));

  GST_OBJECT_LOCK (self);
  struct dependency *dep = ensure_image_dep (self, image);
  update_image_barrier (dep, dst_stage, new_access, new_layout, new_queue);
  GST_OBJECT_UNLOCK (self);
}

static struct semaphore_update *
find_semaphore_update (GstVulkanBarrierState * self,
    GstVulkanTimelineSemaphore * timeline)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  for (int i = 0; i < priv->semaphore_updates->len; i++) {
    struct semaphore_update *update =
        &g_array_index (priv->semaphore_updates, struct semaphore_update, i);

    if (update->timeline == timeline)
      return update;
  }

  return NULL;
}

static struct semaphore_update *
ensure_semaphore_update (GstVulkanBarrierState * self,
    GstVulkanTimelineSemaphore * timeline, guint64 old_value)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  struct semaphore_update *update = find_semaphore_update (self, timeline);
  if (!update) {
    struct semaphore_update new_update = {
      .timeline = timeline,
      .old_value = old_value,
    };
    g_array_append_val (priv->semaphore_updates, new_update);
    update =
        &g_array_index (priv->semaphore_updates, struct semaphore_update,
        priv->semaphore_updates->len - 1);
  }
  return update;
}

/**
 * gst_vulkan_barrier_state_update_timeline_semaphore:
 * @self: a #GstVulkanBarrierState
 * @timeline: the #GstVulkanTimelineSemaphore
 * @new_value: the new value of the timeline semaphore
 *
 * Adds (or updates) the image's @timeline semaphore value when calling
 * gst_vulkan_barrier_state_commit().
 *
 * This does not add any semaphore waits or signal to the vulkan command stream.
 * This would need to be done manually or by using #GstVulkanOperation.
 *
 * This can fail if the existing value of @image's timeline semaphore does not
 * currently match any previously stored value.
 *
 * Returns: whether the semaphore value could be updated.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_state_update_timeline_semaphore (GstVulkanBarrierState
    * self, GstVulkanTimelineSemaphore * timeline, guint64 new_value)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), FALSE);
  g_return_val_if_fail (timeline != NULL, FALSE);

  gst_vulkan_timeline_semaphore_lock (timeline);
  guint64 old_value = gst_vulkan_timeline_semaphore_peek_unlocked (timeline);
  gst_vulkan_timeline_semaphore_unlock (timeline);

  if (old_value >= new_value)
    return FALSE;

  GST_OBJECT_LOCK (self);
  struct semaphore_update *update = ensure_semaphore_update (self, timeline,
      old_value);
  if (old_value != update->old_value) {
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }
  update->new_value = new_value;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static struct dependency *
ensure_buffer_dep (GstVulkanBarrierState * self, GstVulkanBufferMemory * buffer)
{
  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  struct dependency *dep = find_dependency (self, buffer);
  if (!dep) {
    struct dependency new_dep = {
      .type = GST_VULKAN_BARRIER_TYPE_BUFFER,
    };
    new_dep.buffer.mem =
        (GstVulkanBufferMemory *) gst_memory_ref (GST_MEMORY_CAST (buffer));
    gst_vulkan_buffer_memory_lock (buffer);
    gst_vulkan_buffer_memory_peek_barrier_unlocked (buffer,
        &new_dep.buffer.old_info);
    gst_vulkan_buffer_memory_unlock (buffer);
    gst_vulkan_barrier_buffer_info_copy_into (&new_dep.buffer.old_info,
        &new_dep.buffer.new_info);
    g_array_append_val (priv->deps, new_dep);
    dep = &g_array_index (priv->deps, struct dependency, priv->deps->len - 1);
  }

  return dep;
}

static void
update_buffer_barrier (struct dependency *dep, guint64 dst_stage,
    guint64 new_access, GstVulkanQueue * new_queue)
{
  g_assert (dep->type == GST_VULKAN_BARRIER_TYPE_BUFFER);

  dep->buffer.new_info.parent.access_flags = new_access;
  dep->buffer.new_info.parent.pipeline_stages = dst_stage;
  gst_object_replace ((GstObject **) & dep->buffer.new_info.parent.queue,
      (GstObject *) new_queue);
}

/**
 * gst_vulkan_barrier_state_add_buffer_barrier:
 * @self: a #GstVulkanBarrierState
 * @buffer: the #GstVulkanBufferMemory the barrier is representing
 * @src_stage: the pipeline stage to wait for (`VkPipelineStageFlags` or
 *     `VkPipelineStageFlags2`)
 * @dst_stage: the pipeline stage to signal (`VkPipelineStageFlags` or
 *     `VkPipelineStageFlags2`)
 * @new_access: the new access flags for @buffer (`VkAccessFlags` or
 *     `VkAccessFlags2`)
 * @new_queue: (nullable): destination queue for transfer of the @buffer
 *     ownership
 *
 * Adds a buffer barrier for @buffer to the list of barriers that will be used
 * when calling gst_vulkan_barrier_state_pipeline_barrier() and applied to the
 * @buffer when calling gst_vulkan_barrier_state_commit().
 *
 * Returns: whether the barrier could be added.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_state_add_buffer_barrier (GstVulkanBarrierState * self,
    GstVulkanBufferMemory * buffer, guint64 src_stage, guint64 dst_stage,
    guint64 new_access, GstVulkanQueue * new_queue)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), FALSE);
  g_return_val_if_fail (gst_is_vulkan_buffer_memory (GST_MEMORY_CAST (buffer)),
      FALSE);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

  GST_OBJECT_LOCK (self);

  struct dependency *dep = ensure_buffer_dep (self, buffer);

  guint32 queue_family_index = VK_QUEUE_FAMILY_IGNORED;
  if (dep->buffer.new_info.parent.queue)
    queue_family_index = dep->buffer.new_info.parent.queue->family;

#if defined (VK_KHR_synchronization2)
  if (priv->has_sync2) {
    VkBufferMemoryBarrier2KHR barrier2 = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR,
      .pNext = NULL,
      .srcStageMask = src_stage,
      .dstStageMask = dst_stage,
      .srcAccessMask = dep->buffer.new_info.parent.access_flags,
      .dstAccessMask = new_access,
      .srcQueueFamilyIndex = queue_family_index,
      .dstQueueFamilyIndex = new_queue ?
          new_queue->family : VK_QUEUE_FAMILY_IGNORED,
      .buffer = buffer->buffer,
      .size = VK_WHOLE_SIZE,
    };

    g_array_append_val (priv->buffer_barriers, barrier2);
  } else
#endif
  {
    /* this might overflow */
    if (new_access > VK_ACCESS_FLAG_BITS_MAX_ENUM) {
      GST_OBJECT_UNLOCK (self);
      GST_ERROR_OBJECT (self, "Invalid new access value: %" G_GUINT64_FORMAT,
          new_access);
      return FALSE;
    }

    VkBufferMemoryBarrier barrier = (VkBufferMemoryBarrier) {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext = NULL,
      .srcAccessMask = dep->buffer.new_info.parent.access_flags,
      .dstAccessMask = (VkAccessFlags) new_access,
      .srcQueueFamilyIndex = queue_family_index,
      .dstQueueFamilyIndex = new_queue ?
          new_queue->family : VK_QUEUE_FAMILY_IGNORED,
      .buffer = buffer->buffer,
      .size = VK_WHOLE_SIZE,
    };

    g_array_append_val (priv->buffer_barriers, barrier);

    struct vk_pipeline_stages stages = {
      .src_stage = src_stage,
      .dst_stage = dst_stage,
    };
    g_array_append_val (priv->buffer_barrier_stages, stages);
  }

  update_buffer_barrier (dep, dst_stage, new_access, new_queue);

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/**
 * gst_vulkan_barrier_state_update_buffer_barrier:
 * @self: a #GstVulkanBarrierState
 * @buffer: the #GstVulkanBufferMemory the barrier is representing
 * @dst_stage: the pipeline stage to signal (`VkPipelineStageFlags` or
 *     `VkPipelineStageFlags2`)
 * @new_access: the new access flags for @buffer (`VkAccessFlags` or
 *     `VkAccessFlags2`)
 * @new_queue: (nullable): destination queue for transfer of the @buffer
 *     ownership
 *
 * Adds (or updates) the buffer barrier state that will be applied to the
 * @buffer when calling gst_vulkan_barrier_state_commit().
 *
 * This does not add any barrier to the vulkan command stream. This would need
 * to be done manually or through gst_vulkan_barrier_state_add_raw_barrier().
 *
 * Since: 1.30
 */
void
gst_vulkan_barrier_state_update_buffer_barrier (GstVulkanBarrierState * self,
    GstVulkanBufferMemory * buffer, guint64 dst_stage, guint64 new_access,
    GstVulkanQueue * new_queue)
{
  g_return_if_fail (GST_IS_VULKAN_BARRIER_STATE (self));
  g_return_if_fail (gst_is_vulkan_buffer_memory (GST_MEMORY_CAST (buffer)));

  GST_OBJECT_LOCK (self);
  struct dependency *dep = ensure_buffer_dep (self, buffer);
  update_buffer_barrier (dep, dst_stage, new_access, new_queue);
  GST_OBJECT_UNLOCK (self);
}

/**
 * gst_vulkan_barrier_state_add_raw_barrier:
 * @self: a #GstVulkanBarrierState
 * @barrier: a `VkImageMemoryBarrier`, `VkImageMemoryBarrier2`,
 * `VkBufferMemoryBarrier`, `VkBufferMemoryBarrier2`, `VkMemoryBarrier`, or
 * `VkMemoryBarrier2`
 *
 * Adds a vulkan memory barrier to the list of barriers performed when using
 * gst_vulkan_barrier_state_pipeline_barrier(). Does not do any tracking of
 * any resources (if any) mentioned in the barrier and does not impact the
 * result of gst_vulkan_barrier_state_commit().
 *
 * Currently supported barriers are `VkImageMemoryBarrier`,
 * `VkImageMemoryBarrier2`, `VkBufferMemoryBarrier`, `VkBufferMemoryBarrier2`,
 * `VkMemoryBarrier`, and `VkMemoryBarrier2`.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_state_add_raw_barrier (GstVulkanBarrierState * self,
    gconstpointer barrier, guint64 src_stage, guint64 dst_stage)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), FALSE);
  g_return_val_if_fail (barrier != NULL, FALSE);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);
  VkBaseInStructure *in = (VkBaseInStructure *) barrier;

  GST_OBJECT_LOCK (self);

  struct vk_pipeline_stages stages = {
    .src_stage = src_stage,
    .dst_stage = dst_stage,
  };
  switch (in->sType) {
    case VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER:{
      g_return_val_if_fail (!priv->has_sync2, FALSE);
      g_array_append_vals (priv->image_barriers, barrier, 1);
      g_array_append_val (priv->image_barrier_stages, stages);
      break;
    }
    case VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR:{
      g_return_val_if_fail (priv->has_sync2, FALSE);
      g_array_append_vals (priv->image_barriers, barrier, 1);
      break;
    }
    case VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER:
      g_return_val_if_fail (!priv->has_sync2, FALSE);
      g_array_append_vals (priv->buffer_barriers, barrier, 1);
      g_array_append_val (priv->buffer_barrier_stages, stages);
      break;
    case VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR:
      g_return_val_if_fail (priv->has_sync2, FALSE);
      g_array_append_vals (priv->buffer_barriers, barrier, 1);
      break;
    case VK_STRUCTURE_TYPE_MEMORY_BARRIER:
      g_return_val_if_fail (!priv->has_sync2, FALSE);
      g_array_append_vals (priv->memory_barriers, barrier, 1);
      g_array_append_val (priv->memory_barrier_stages, stages);
      break;
    case VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR:
      g_return_val_if_fail (priv->has_sync2, FALSE);
      g_array_append_vals (priv->memory_barriers, barrier, 1);
      break;
    default:
      g_assert_not_reached ();
      GST_OBJECT_UNLOCK (self);
      return FALSE;
  }

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

/**
 * gst_vulkan_barrier_state_pipeline_barrier:
 * @self: a #GstVulkanBarrierState
 * @cmd: the #GstVulkanCommandBuffer to record the barriers into.
 *
 * Performs the equivalent of `vmCmdPipelineBarrier/2` in @cmd for the currently
 * stored list of barriers. The list of barriers is then reset to an empty list.
 *
 * Returns: whether the pipeline barrier was successfully recorded to @cmd.
 *
 * Since: 1.30
 */
gboolean
gst_vulkan_barrier_state_pipeline_barrier (GstVulkanBarrierState * self,
    GstVulkanCommandBuffer * cmd, VkDependencyFlags dep_flags)
{
  g_return_val_if_fail (GST_IS_VULKAN_BARRIER_STATE (self), FALSE);
  g_return_val_if_fail (cmd != NULL, FALSE);

  GstVulkanBarrierStatePrivate *priv = GET_PRIV (self);

#if defined(VK_KHR_synchronization2)
  if (priv->has_sync2) {
    VkDependencyInfoKHR dep_info = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .dependencyFlags = dep_flags,
      .imageMemoryBarrierCount = priv->image_barriers->len,
      .pImageMemoryBarriers =
          (VkImageMemoryBarrier2KHR *) priv->image_barriers->data,
      .bufferMemoryBarrierCount = priv->buffer_barriers->len,
      .pBufferMemoryBarriers =
          (VkBufferMemoryBarrier2KHR *) priv->buffer_barriers->data,
      .memoryBarrierCount = priv->memory_barriers->len,
      .pMemoryBarriers = (VkMemoryBarrier2KHR *) priv->memory_barriers->data,
    };

    gst_vulkan_command_buffer_lock (cmd);
    priv->CmdPipelineBarrier2 (cmd->cmd, &dep_info);
    gst_vulkan_command_buffer_unlock (cmd);

    g_array_set_size (priv->image_barriers, 0);
    g_array_set_size (priv->buffer_barriers, 0);
  } else
#endif
  {
    g_assert (priv->image_barriers->len == priv->image_barrier_stages->len);
    g_assert (priv->buffer_barriers->len == priv->buffer_barrier_stages->len);
    g_assert (priv->memory_barriers->len == priv->memory_barrier_stages->len);

    gst_vulkan_command_buffer_lock (cmd);
    // TODO: combine barriers for the same set of stages
    for (int i = 0; i < priv->image_barriers->len; i++) {
      struct vk_pipeline_stages *stages =
          &g_array_index (priv->image_barrier_stages, struct vk_pipeline_stages,
          i);
      VkImageMemoryBarrier *barrier =
          &g_array_index (priv->image_barriers, VkImageMemoryBarrier, i);

      vkCmdPipelineBarrier (cmd->cmd, stages->src_stage, stages->dst_stage,
          dep_flags, 0, NULL, 0, NULL, 1, barrier);
    }

    for (int i = 0; i < priv->buffer_barriers->len; i++) {
      struct vk_pipeline_stages *stages =
          &g_array_index (priv->buffer_barrier_stages,
          struct vk_pipeline_stages, i);
      VkBufferMemoryBarrier *barrier =
          &g_array_index (priv->buffer_barriers, VkBufferMemoryBarrier, i);

      vkCmdPipelineBarrier (cmd->cmd, stages->src_stage, stages->dst_stage,
          dep_flags, 0, NULL, 1, barrier, 0, NULL);
    }

    for (int i = 0; i < priv->memory_barriers->len; i++) {
      struct vk_pipeline_stages *stages =
          &g_array_index (priv->memory_barrier_stages,
          struct vk_pipeline_stages, i);
      VkMemoryBarrier *barrier =
          &g_array_index (priv->memory_barriers, VkMemoryBarrier, i);

      vkCmdPipelineBarrier (cmd->cmd, stages->src_stage, stages->dst_stage,
          dep_flags, 1, barrier, 0, NULL, 0, NULL);
    }

    gst_vulkan_command_buffer_unlock (cmd);

    g_array_set_size (priv->image_barriers, 0);
    g_array_set_size (priv->image_barrier_stages, 0);

    g_array_set_size (priv->buffer_barriers, 0);
    g_array_set_size (priv->buffer_barrier_stages, 0);

    g_array_set_size (priv->memory_barriers, 0);
    g_array_set_size (priv->memory_barrier_stages, 0);
  }

  return TRUE;
}

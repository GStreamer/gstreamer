/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gsthip.h"
#include "gsthip-private.h"
#include <mutex>
#include <condition_variable>
#include <new>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_HIP_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("hiphostallocator", 0, "hiphostallocator");
  } GST_HIP_CALL_ONCE_END;

  return cat;
}
#endif

static GstHipHostAllocator *_hip_host_allocator = nullptr;

/* *INDENT-OFF* */
struct _GstHipHostMemoryPrivate
{
  ~_GstHipHostMemoryPrivate ()
  {
    if (data && !is_shared) {
      gst_hip_device_set_current (mem.device);
      HipHostFree (vendor, data);
    }

    gst_clear_object (&mem.device);
  }

  GstHipHostMemory mem = { };

  GstHipVendor vendor;
  gpointer data = nullptr;
  gpointer device_ptr = nullptr;
  guint flags = 0;
  gboolean is_shared = FALSE;
  std::mutex lock;
};
/* *INDENT-ON* */

/**
 * gst_is_hip_host_memory:
 * @mem: a #GstMemory
 *
 * Returns: %TRUE if @mem is allocated by #GstHipHostAllocator
 *
 * Since: 1.30
 */
gboolean
gst_is_hip_host_memory (GstMemory * mem)
{
  return mem != nullptr && mem->allocator != nullptr &&
      (GST_IS_HIP_HOST_ALLOCATOR (mem->allocator));
}

/**
 * gst_hip_host_memory_get_device_pointer:
 * @mem: a #GstMemory
 *
 * Gets device pointer mapped to host pinned memory
 *
 * Returns: (nullable): device pointer mapped to the host memory
 *
 * Since: 1.30
 */
gpointer
gst_hip_host_memory_get_device_pointer (GstHipHostMemory * mem)
{
  g_return_val_if_fail (gst_is_hip_host_memory (GST_MEMORY_CAST (mem)),
      nullptr);

  auto priv = mem->priv;

  /* Need host mapped flag during allocation */
  if ((priv->flags & hipHostMallocMapped) != hipHostMallocMapped)
    return nullptr;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->device_ptr)
    return priv->device_ptr;

  if (!gst_hip_device_set_current (mem->device)) {
    GST_ERROR_OBJECT (mem->device, "Couldn't make device current");
    return nullptr;
  }

  auto hip_ret = HipHostGetDevicePointer (priv->vendor, &priv->device_ptr,
      priv->data, 0);
  if (!gst_hip_result (hip_ret, priv->vendor)) {
    GST_WARNING_OBJECT (mem->device, "Couldn't get device pointer");
    priv->device_ptr = nullptr;
    return nullptr;
  }

  return priv->device_ptr;
}

static gpointer
gst_hip_host_memory_map_full (GstMemory * mem, GstMapInfo * info, gsize maxsize)
{
  auto self = GST_HIP_HOST_MEMORY_CAST (mem);

  return self->priv->data;
}

static void
gst_hip_host_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
}

static GstMemory *
gst_hip_host_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  auto parent = mem->parent;
  if (!parent)
    parent = mem;

  if (size == -1)
    size = mem->size - offset;

  auto hmem = (GstHipHostMemory *) mem;
  auto priv = hmem->priv;

  auto new_priv = new GstHipHostMemoryPrivate ();
  auto new_mem = &new_priv->mem;
  new_mem->device = (GstHipDevice *) gst_object_ref (hmem->device);
  new_mem->priv = new_priv;

  new_priv->vendor = priv->vendor;
  new_priv->data = priv->data;
  new_priv->flags = priv->flags;
  new_priv->is_shared = TRUE;

  gst_memory_init (GST_MEMORY_CAST (new_mem),
      GST_MEMORY_FLAG_READONLY, mem->allocator, parent,
      mem->maxsize, 0, mem->offset + offset, size);

  return GST_MEMORY_CAST (new_mem);
}

static gboolean
gst_hip_host_memory_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  auto hmem1 = (GstHipHostMemory *) mem1;
  auto hmem2 = (GstHipHostMemory *) mem2;
  auto data1 = (guint8 *) hmem1->priv->data;
  auto data2 = (guint8 *) hmem2->priv->data;

  if (offset) {
    auto parent = mem1->parent;
    *offset = mem1->offset - parent->offset;
  }

  return data1 + mem1->offset + mem1->size == data2 + mem2->offset;
}

#define gst_hip_host_allocator_parent_class parent_class
G_DEFINE_TYPE (GstHipHostAllocator, gst_hip_host_allocator, GST_TYPE_ALLOCATOR);

static GstMemory *gst_hip_host_allocator_dummy_alloc (GstAllocator *
    allocator, gsize size, GstAllocationParams * params);
static void gst_hip_host_allocator_free (GstAllocator * allocator,
    GstMemory * mem);

static void
gst_hip_host_allocator_class_init (GstHipHostAllocatorClass * klass)
{
  auto allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = gst_hip_host_allocator_dummy_alloc;
  allocator_class->free = gst_hip_host_allocator_free;
}

static void
gst_hip_host_allocator_init (GstHipHostAllocator * self)
{
  auto alloc = GST_ALLOCATOR_CAST (self);

  alloc->mem_type = GST_HIP_HOST_MEMORY_NAME;
  alloc->mem_map_full = gst_hip_host_memory_map_full;
  alloc->mem_unmap_full = gst_hip_host_memory_unmap_full;
  alloc->mem_share = gst_hip_host_memory_share;
  alloc->mem_is_span = gst_hip_host_memory_is_span;

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_hip_host_allocator_dummy_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (nullptr);
}

static void
gst_hip_host_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  auto hmem = GST_HIP_HOST_MEMORY_CAST (mem);
  auto priv = hmem->priv;

  GST_LOG_OBJECT (allocator, "Free memory %p", mem);

  delete priv;
}

static void
gst_hip_host_memory_init_once (void)
{
  GST_HIP_CALL_ONCE_BEGIN {
    _hip_host_allocator = (GstHipHostAllocator *)
        g_object_new (GST_TYPE_HIP_HOST_ALLOCATOR, nullptr);
    gst_object_ref_sink (_hip_host_allocator);
    gst_object_ref (_hip_host_allocator);

    gst_allocator_register (GST_HIP_HOST_MEMORY_NAME,
        GST_ALLOCATOR_CAST (_hip_host_allocator));
  } GST_HIP_CALL_ONCE_END;
}

/**
 * gst_hip_host_allocator_alloc:
 * @allocator: (allow-none): a #GstHipHostAllocator
 * @device: a #GstHipDevice
 * @size: Total bytes to allocate
 * @flags: flags value for hipHostMalloc
 *
 * Allocates HIP host pinned memory
 *
 * Returns: (transfer full) (nullable): a newly allocated #GstHipHostMemory
 * or otherwise %NULL if allocation failed
 *
 * Since: 1.30
 */
GstMemory *
gst_hip_host_allocator_alloc (GstHipHostAllocator * allocator,
    GstHipDevice * device, gsize size, guint flags)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);
  g_return_val_if_fail (size > 0, nullptr);

  if (!allocator) {
    gst_hip_host_memory_init_once ();
    allocator = _hip_host_allocator;
  }

  if (!gst_hip_device_set_current (device)) {
    GST_ERROR_OBJECT (device, "Couldn't make device current");
    return nullptr;
  }

  auto vendor = gst_hip_device_get_vendor (device);
  gpointer data;
  auto hip_ret = HipHostMalloc (vendor, &data, size, flags);
  if (!gst_hip_result (hip_ret, vendor)) {
    GST_ERROR_OBJECT (device, "Couldn't allocate host memory");
    return nullptr;
  }

  auto priv = new GstHipHostMemoryPrivate ();
  auto mem = &priv->mem;
  mem->device = (GstHipDevice *) gst_object_ref (device);
  mem->priv = priv;

  priv->vendor = vendor;
  priv->data = data;
  priv->flags = flags;

  GST_LOG_OBJECT (allocator, "Allocated host memory, size %" G_GSIZE_FORMAT
      ", flags: 0x%x", size, flags);

  gst_memory_init (GST_MEMORY_CAST (mem),
      (GstMemoryFlags) 0, GST_ALLOCATOR_CAST (allocator), nullptr,
      size, 0, 0, size);

  return GST_MEMORY_CAST (mem);
}

/**
 * gst_hip_host_allocator_set_active:
 * @allocator: a #GstHipHostAllocator
 * @active: the new active state
 *
 * Controls the active state of @allocator.
 *
 * Returns: %TRUE if active state of @allocator was successfully updated.
 *
 * Since: 1.30
 */
gboolean
gst_hip_host_allocator_set_active (GstHipHostAllocator * allocator,
    gboolean active)
{
  g_return_val_if_fail (GST_IS_HIP_HOST_ALLOCATOR (allocator), FALSE);

  auto klass = GST_HIP_HOST_ALLOCATOR_GET_CLASS (allocator);
  if (klass->set_active)
    return klass->set_active (allocator, active);

  return TRUE;
}

struct _GstHipHostPoolAllocatorPrivate
{
  _GstHipHostPoolAllocatorPrivate ()
  {
    queue = gst_vec_deque_new (16);
  }

   ~_GstHipHostPoolAllocatorPrivate ()
  {
    gst_vec_deque_free (queue);
  }

  GstVecDeque *queue;
  guint flags = 0;
  gsize size = 0;

  std::mutex lock;
  std::condition_variable cond;
  gboolean started = FALSE;
  gboolean active = FALSE;

  guint outstanding = 0;
  guint cur_mems = 0;
  gboolean flushing = TRUE;
};

static void gst_hip_host_pool_allocator_finalize (GObject * object);

static gboolean
gst_hip_host_pool_allocator_set_active (GstHipHostAllocator * allocator,
    gboolean active);

static gboolean gst_hip_host_pool_allocator_start (GstHipHostPoolAllocator *
    self);
static gboolean gst_hip_host_pool_allocator_stop (GstHipHostPoolAllocator *
    self);

#define gst_hip_host_pool_allocator_parent_class pool_alloc_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstHipHostPoolAllocator,
    gst_hip_host_pool_allocator, GST_TYPE_HIP_HOST_ALLOCATOR);

static void
gst_hip_host_pool_allocator_class_init (GstHipHostPoolAllocatorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto hipalloc_class = GST_HIP_HOST_ALLOCATOR_CLASS (klass);

  object_class->finalize = gst_hip_host_pool_allocator_finalize;
  hipalloc_class->set_active = gst_hip_host_pool_allocator_set_active;
}

static void
gst_hip_host_pool_allocator_init (GstHipHostPoolAllocator * self)
{
  auto storage = gst_hip_host_pool_allocator_get_instance_private (self);
  self->priv = new (storage) GstHipHostPoolAllocatorPrivate ();
}

static void
gst_hip_host_pool_allocator_finalize (GObject * object)
{
  auto self = GST_HIP_HOST_POOL_ALLOCATOR (object);

  GST_DEBUG_OBJECT (self, "Finalize");

  gst_hip_host_pool_allocator_stop (self);
  self->priv->~_GstHipHostPoolAllocatorPrivate ();

  g_clear_object (&self->device);

  G_OBJECT_CLASS (pool_alloc_parent_class)->finalize (object);
}

static gboolean
gst_hip_host_pool_allocator_start (GstHipHostPoolAllocator * self)
{
  auto priv = self->priv;

  priv->started = TRUE;
  return TRUE;
}

static gboolean
gst_hip_host_pool_allocator_set_active (GstHipHostAllocator * allocator,
    gboolean active)
{
  auto self = GST_HIP_HOST_POOL_ALLOCATOR (allocator);
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "active %d", active);

  std::unique_lock < std::mutex > lk (priv->lock);
  /* just return if we are already in the right state */
  if (priv->active == active) {
    GST_LOG_OBJECT (self, "allocator was in the right state");
    return TRUE;
  }

  if (active) {
    if (!gst_hip_host_pool_allocator_start (self)) {
      GST_ERROR_OBJECT (self, "start failed");
      return FALSE;
    }

    priv->active = TRUE;
    priv->flushing = FALSE;
  } else {
    priv->flushing = TRUE;
    priv->active = FALSE;

    priv->cond.notify_all ();

    /* when all memory objects are in the pool, free them. Else they will be
     * freed when they are released */
    GST_LOG_OBJECT (self, "outstanding memories %d, (in queue %u)",
        priv->outstanding, (guint) gst_vec_deque_get_length (priv->queue));
    if (priv->outstanding == 0) {
      if (!gst_hip_host_pool_allocator_stop (self)) {
        GST_ERROR_OBJECT (self, "stop failed");
        return FALSE;
      }
    }
  }

  return TRUE;
}

static void
gst_hip_host_pool_allocator_free_memory (GstHipHostPoolAllocator * self,
    GstMemory * mem)
{
  auto priv = self->priv;

  priv->cur_mems--;
  GST_LOG_OBJECT (self, "freeing memory %p (%u left)", mem, priv->cur_mems);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  gst_memory_unref (mem);
}

/* must be called with the lock */
static void
gst_hip_host_pool_allocator_clear_queue (GstHipHostPoolAllocator * self)
{
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "Clearing queue");

  while (!gst_vec_deque_is_empty (priv->queue)) {
    auto mem = (GstMemory *) gst_vec_deque_pop_head (priv->queue);
    gst_hip_host_pool_allocator_free_memory (self, mem);
  }

  GST_LOG_OBJECT (self, "Clear done");
}

/* must be called with the lock */
static gboolean
gst_hip_host_pool_allocator_stop (GstHipHostPoolAllocator * self)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->started) {
    gst_hip_host_pool_allocator_clear_queue (self);
    priv->started = FALSE;
  }

  return TRUE;
}

static void
gst_hip_host_pool_allocator_release_memory (GstHipHostPoolAllocator * self,
    GstMemory * mem)
{
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "Released memory %p", mem);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  mem->allocator = (GstAllocator *) gst_object_ref (_hip_host_allocator);

  /* keep it around in our queue */
  gst_vec_deque_push_tail (priv->queue, mem);
  priv->outstanding--;
  if (priv->outstanding == 0 && priv->flushing)
    gst_hip_host_pool_allocator_stop (self);
  priv->cond.notify_all ();
  priv->lock.unlock ();

  gst_object_unref (self);
}

static gboolean
gst_hip_host_memory_release (GstMiniObject * obj)
{
  GstMemory *mem = GST_MEMORY_CAST (obj);

  g_assert (mem->allocator);

  if (!GST_IS_HIP_HOST_POOL_ALLOCATOR (mem->allocator)) {
    GST_LOG_OBJECT (mem->allocator, "Not our memory, free");
    return TRUE;
  }

  auto self = GST_HIP_HOST_POOL_ALLOCATOR (mem->allocator);
  auto priv = self->priv;

  priv->lock.lock ();
  /* return the memory to the allocator */
  gst_memory_ref (mem);
  gst_hip_host_pool_allocator_release_memory (self, mem);

  return FALSE;
}

static GstFlowReturn
gst_hip_host_pool_allocator_alloc (GstHipHostPoolAllocator * self,
    GstMemory ** mem)
{
  auto priv = self->priv;

  auto new_mem = gst_hip_host_allocator_alloc (nullptr, self->device,
      priv->size, priv->flags);

  if (!new_mem) {
    GST_ERROR_OBJECT (self, "Failed to allocate new memory");
    return GST_FLOW_ERROR;
  }

  priv->cur_mems++;
  *mem = new_mem;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hip_host_pool_allocator_acquire_memory_internal (GstHipHostPoolAllocator *
    self, GstMemory ** memory, std::unique_lock < std::mutex > &lk)
{
  auto priv = self->priv;
  GstFlowReturn ret = GST_FLOW_ERROR;

  do {
    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "we are flushing");
      return GST_FLOW_FLUSHING;
    }

    if (!gst_vec_deque_is_empty (priv->queue)) {
      *memory = (GstMemory *) gst_vec_deque_pop_head (priv->queue);
      GST_LOG_OBJECT (self, "acquired memory %p", *memory);
      return GST_FLOW_OK;
    }

    /* no memory, try to allocate some more */
    GST_LOG_OBJECT (self, "no memory, trying to allocate");
    ret = gst_hip_host_pool_allocator_alloc (self, memory);
    if (ret == GST_FLOW_OK)
      return ret;

    /* something went wrong, return error */
    if (ret != GST_FLOW_EOS)
      break;

    GST_LOG_OBJECT (self, "waiting for free memory or flushing");
    priv->cond.wait (lk);
  } while (TRUE);

  return ret;
}

/**
 * gst_hip_host_pool_allocator_new:
 * @device: a #GstHipDevice
 * @size: the size of the memory to allocate
 * @flags: flags value for hipHostMalloc
 *
 * Creates a new #GstHipHostPoolAllocator instance
 *
 * Returns: (transfer full): a #GstHipHostPoolAllocator
 *
 * Since: 1.30
 */
GstHipHostPoolAllocator *
gst_hip_host_pool_allocator_new (GstHipDevice * device, gsize size, guint flags)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), nullptr);
  g_return_val_if_fail (size > 0, nullptr);

  auto self = (GstHipHostPoolAllocator *)
      g_object_new (GST_TYPE_HIP_HOST_POOL_ALLOCATOR,
      nullptr);
  gst_object_ref_sink (self);

  self->device = (GstHipDevice *) gst_object_ref (device);
  self->priv->size = size;
  self->priv->flags = flags;

  return self;
}

/**
 * gst_hip_host_pool_allocator_acquire_memory:
 * @allocator: a #GstHipHostPoolAllocator
 * @memory: (out) (transfer full) (nullable): a #GstMemory
 *
 * Acquires a #GstMemory from @allocator. @memory should point to a memory
 * location that can hold a pointer to the new #GstMemory.
 *
 * Returns: a #GstFlowReturn such as %GST_FLOW_FLUSHING when the allocator is
 * inactive.
 *
 * Since: 1.30
 */
GstFlowReturn
gst_hip_host_pool_allocator_acquire_memory (GstHipHostPoolAllocator * allocator,
    GstMemory ** memory)
{
  g_return_val_if_fail (GST_IS_HIP_HOST_POOL_ALLOCATOR (allocator),
      GST_FLOW_ERROR);
  g_return_val_if_fail (memory, GST_FLOW_ERROR);
  GstFlowReturn ret;

  auto priv = allocator->priv;

  GST_LOG_OBJECT (allocator, "Acquiring memory");

  std::unique_lock < std::mutex > lk (priv->lock);
  ret = gst_hip_host_pool_allocator_acquire_memory_internal (allocator, memory,
      lk);

  if (ret == GST_FLOW_OK) {
    GstMemory *mem = *memory;
    /* Replace default allocator with ours */
    gst_object_unref (mem->allocator);
    mem->allocator = (GstAllocator *) gst_object_ref (allocator);
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_hip_host_memory_release;
    priv->outstanding++;
  }

  return ret;
}

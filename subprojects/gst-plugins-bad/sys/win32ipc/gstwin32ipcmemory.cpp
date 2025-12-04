/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstwin32ipcmemory.h"
#include <string>
#include <mutex>
#include <condition_variable>
#include <string.h>
#include <atomic>
#include <queue>

GST_DEBUG_CATEGORY_STATIC (gst_win32_ipc_allocator_debug);
#define GST_CAT_DEFAULT gst_win32_ipc_allocator_debug

#define GST_WIN32_IPC_MEMORY_NAME "Win32IpcMemory"
#define GST_WIN32_IPC_ALLOCATOR_IS_FLUSHING(alloc) \
    (g_atomic_int_get (&alloc->flushing))

static GstWin32IpcAllocator *gc_allocator = nullptr;

/* *INDENT-OFF* */
struct GstWin32IpcAllocatorPrivate
{
  gsize size;

  bool is_gc = false;

  std::mutex lock;
  std::condition_variable cond;

  std::atomic<guint64> seqnum = { 0 };
  std::queue<GstMemory *>queue;

  bool started = false;
  bool active = false;
  std::atomic<guint> outstanding = { 0 };
  guint cur_mems = 0;
  bool flushing = false;
};
/* *INDENT-ON* */

struct _GstWin32IpcAllocator
{
  GstAllocator parent;

  GstWin32IpcAllocatorPrivate *priv;
};

static void gst_win32_ipc_allocator_finalize (GObject * object);
static GstMemory *gst_win32_ipc_allocator_dummy_alloc (GstAllocator * alloc,
    gsize size, GstAllocationParams * params);
static void gst_win32_ipc_allocator_free (GstAllocator * alloc,
    GstMemory * mem);

static gpointer gst_win32_ipc_allocator_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags);
static void gst_win32_ipc_allocator_unmap (GstMemory * mem);
static GstMemory *gst_win32_ipc_allocator_share (GstMemory * mem,
    gssize offset, gssize size);

static void gst_win32_ipc_allocator_start (GstWin32IpcAllocator * self);
static void gst_win32_ipc_allocator_stop (GstWin32IpcAllocator * self);
static gboolean gst_win32_ipc_memory_release (GstMiniObject * mini_object);

#define gst_win32_ipc_allocator_parent_class parent_class
G_DEFINE_TYPE (GstWin32IpcAllocator, gst_win32_ipc_allocator,
    GST_TYPE_ALLOCATOR);

static void
gst_win32_ipc_allocator_class_init (GstWin32IpcAllocatorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto alloc_class = GST_ALLOCATOR_CLASS (klass);

  object_class->finalize = gst_win32_ipc_allocator_finalize;

  alloc_class->alloc = gst_win32_ipc_allocator_dummy_alloc;
  alloc_class->free = gst_win32_ipc_allocator_free;

  GST_DEBUG_CATEGORY_INIT (gst_win32_ipc_allocator_debug,
      "win32ipcallocator", 0, "win32ipcallocator");
}

static void
gst_win32_ipc_allocator_init (GstWin32IpcAllocator * self)
{
  self->priv = new GstWin32IpcAllocatorPrivate ();

  auto alloc = GST_ALLOCATOR (self);

  alloc->mem_type = GST_WIN32_IPC_MEMORY_NAME;
  alloc->mem_map = gst_win32_ipc_allocator_map;
  alloc->mem_unmap = gst_win32_ipc_allocator_unmap;
  alloc->mem_share = gst_win32_ipc_allocator_share;

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
gst_win32_ipc_allocator_finalize (GObject * object)
{
  auto self = GST_WIN32_IPC_ALLOCATOR (object);

  GST_DEBUG_OBJECT (self, "Finalize");

  gst_win32_ipc_allocator_stop (self);
  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstMemory *
gst_win32_ipc_allocator_dummy_alloc (GstAllocator * alloc, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (nullptr);
}

static void
gst_win32_ipc_allocator_free (GstAllocator * alloc, GstMemory * mem)
{
  auto imem = (GstWin32IpcMemory *) mem;

  gst_win32_ipc_mmf_unref (imem->mmf);
  g_free (imem);
}

static gpointer
gst_win32_ipc_allocator_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  auto imem = (GstWin32IpcMemory *) mem;

  return gst_win32_ipc_mmf_get_raw (imem->mmf);
}

static void
gst_win32_ipc_allocator_unmap (GstMemory * mem)
{
  /* do nothing */
}

static GstMemory *
gst_win32_ipc_allocator_share (GstMemory * mem, gssize offset, gssize size)
{
  /* not supported */
  return nullptr;
}

static void
gst_win32_ipc_allocator_start (GstWin32IpcAllocator * self)
{
  auto priv = self->priv;
  priv->started = true;
}

gboolean
gst_win32_ipc_allocator_set_active (GstWin32IpcAllocator * self,
    gboolean active)
{
  g_return_val_if_fail (GST_IS_WIN32_IPC_ALLOCATOR (self), FALSE);

  auto priv = self->priv;

  std::unique_lock < std::mutex > lk (priv->lock);
  if ((priv->active && active) || (!priv->active && !active))
    return TRUE;

  if (active) {
    gst_win32_ipc_allocator_start (self);

    priv->active = true;
    priv->flushing = false;
  } else {
    priv->flushing = true;
    priv->active = false;

    priv->cond.notify_all ();

    /* when all memory objects are in the pool, free them. Else they will be
     * freed when they are released */
    GST_LOG_OBJECT (self, "outstanding memories %d, (in queue %u)",
        priv->outstanding.load (), (guint) priv->queue.size ());
    if (priv->outstanding == 0)
      gst_win32_ipc_allocator_stop (self);
  }

  return TRUE;
}

static void
gst_win32_ipc_allocator_free_memory (GstWin32IpcAllocator * self,
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
gst_win32_ipc_allocator_clear_queue (GstWin32IpcAllocator * self)
{
  auto priv = self->priv;

  GST_LOG_OBJECT (self, "Clearing queue");

  while (!priv->queue.empty ()) {
    auto mem = priv->queue.front ();
    priv->queue.pop ();
    gst_win32_ipc_allocator_free_memory (self, mem);
  }

  GST_LOG_OBJECT (self, "Clear done");
}

static void
gst_win32_ipc_allocator_stop (GstWin32IpcAllocator * self)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->started) {
    gst_win32_ipc_allocator_clear_queue (self);
    priv->started = false;
  }
}

static void
gst_win32_ipc_allocator_release_memory (GstWin32IpcAllocator * self,
    GstMemory * mem)
{
  auto priv = self->priv;

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  mem->allocator = (GstAllocator *) gst_object_ref (gc_allocator);

  /* keep it around in our queue */
  priv->queue.push (mem);
  priv->outstanding--;
  if (priv->outstanding == 0 && priv->flushing)
    gst_win32_ipc_allocator_stop (self);
  priv->cond.notify_all ();
  priv->lock.unlock ();

  gst_object_unref (self);
}

static gboolean
gst_win32_ipc_memory_release (GstMiniObject * mini_object)
{
  auto mem = GST_MEMORY_CAST (mini_object);

  g_assert (mem->allocator != nullptr);

  auto self = GST_WIN32_IPC_ALLOCATOR (mem->allocator);
  auto priv = self->priv;

  /* Memory belongs to garbage collector, free this */
  if (priv->is_gc)
    return TRUE;

  priv->lock.lock ();
  gst_memory_ref (mem);
  gst_win32_ipc_allocator_release_memory (self, mem);

  return FALSE;
}

static GstFlowReturn
gst_win32_ipc_allocator_alloc (GstWin32IpcAllocator * self, GstMemory ** mem)
{
  auto priv = self->priv;

  auto mmf = gst_win32_ipc_mmf_alloc (priv->size);
  if (!mmf) {
    GST_ERROR_OBJECT (self, "Couldn't allocate memory");
    return GST_FLOW_ERROR;
  }

  memset (gst_win32_ipc_mmf_get_raw (mmf), 0, gst_win32_ipc_mmf_get_size (mmf));

  priv->cur_mems++;

  auto new_mem = g_new0 (GstWin32IpcMemory, 1);
  gst_memory_init (GST_MEMORY_CAST (new_mem), (GstMemoryFlags) 0,
      GST_ALLOCATOR_CAST (gc_allocator), nullptr, priv->size, 0, 0, priv->size);
  new_mem->mmf = mmf;

  *mem = GST_MEMORY_CAST (new_mem);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_win32_ipc_allocator_acquire_memory_internal (GstWin32IpcAllocator * self,
    GstMemory ** memory)
{
  auto priv = self->priv;

  do {
    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "we are flushing");
      return GST_FLOW_FLUSHING;
    }

    if (!priv->queue.empty ()) {
      *memory = priv->queue.front ();
      priv->queue.pop ();
      GST_LOG_OBJECT (self, "acquired memory %p", *memory);
      break;
    }

    /* no memory, try to allocate some more */
    GST_LOG_OBJECT (self, "no memory, trying to allocate");
    auto ret = gst_win32_ipc_allocator_alloc (self, memory);
    if (ret != GST_FLOW_OK)
      return ret;

    break;
  } while (true);

  return GST_FLOW_OK;
}

gboolean
gst_is_win32_ipc_memory (GstMemory * mem)
{
  return mem != nullptr && mem->allocator != nullptr &&
      GST_IS_WIN32_IPC_ALLOCATOR (mem->allocator);
}

static void
gst_win32_ipc_allocator_init_once (void)
{
  static std::once_flag once_flag;
  std::call_once (once_flag,[]() {
        gc_allocator = (GstWin32IpcAllocator *)
        g_object_new (GST_TYPE_WIN32_IPC_ALLOCATOR, nullptr);
        gst_object_ref_sink (gc_allocator);
        GST_OBJECT_FLAG_SET (gc_allocator, GST_OBJECT_FLAG_MAY_BE_LEAKED);
        gc_allocator->priv->is_gc = true;
      });
}

GstWin32IpcAllocator *
gst_win32_ipc_allocator_new (gsize size)
{
  g_return_val_if_fail (size != 0, nullptr);

  gst_win32_ipc_allocator_init_once ();

  auto self = (GstWin32IpcAllocator *)
      g_object_new (GST_TYPE_WIN32_IPC_ALLOCATOR, nullptr);
  auto priv = self->priv;
  priv->size = size;

  gst_object_ref_sink (self);

  return self;
}

GstFlowReturn
gst_win32_ipc_allocator_acquire_memory (GstWin32IpcAllocator * alloc,
    GstMemory ** memory)
{
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_WIN32_IPC_ALLOCATOR (alloc), GST_FLOW_ERROR);
  g_return_val_if_fail (memory != nullptr, GST_FLOW_ERROR);

  *memory = nullptr;

  auto priv = alloc->priv;

  std::unique_lock < std::mutex > lk (priv->lock);
  ret = gst_win32_ipc_allocator_acquire_memory_internal (alloc, memory);

  if (ret == GST_FLOW_OK) {
    GstMemory *mem = *memory;
    /* Replace default allocator with ours */
    gst_object_unref (mem->allocator);
    mem->allocator = (GstAllocator *) gst_object_ref (alloc);
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_win32_ipc_memory_release;
    priv->outstanding++;
  }

  return ret;
}

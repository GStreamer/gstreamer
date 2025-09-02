/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include <wrl.h>
#include <directx/d3dx12.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12stagingmemory",
        0, "d3d12stagingmemory");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif

static GstD3D12StagingAllocator *_d3d12_memory_allocator = nullptr;

/* *INDENT-OFF* */
struct _GstD3D12StagingMemoryPrivate
{
  ~_GstD3D12StagingMemoryPrivate ()
  {
    SetFence (nullptr, 0, true);
  }

  void SetFence (ID3D12Fence * new_fence, guint64 new_fence_val, bool wait)
  {
    if (fence && fence.Get () != new_fence && wait) {
      auto completed = fence->GetCompletedValue ();
      if (completed < fence_val)
        fence->SetEventOnCompletion (fence_val, nullptr);
    }

    fence = new_fence;
    if (new_fence)
      fence_val = new_fence_val;
    else
      fence_val = 0;
  }

  ComPtr<ID3D12Resource> resource;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES];
  guint num_layouts;

  std::mutex lock;

  ComPtr<ID3D12Fence> fence;
  UINT64 fence_val = 0;
  INT64 cpu_write_count = 0;
};
/* *INDENT-ON* */

/**
 * gst_is_d3d12_staging_memory:
 * @mem: a #GstMemory
 *
 * Returns: %TRUE if @mem is allocated by #GstD3D12StagingAllocator
 *
 * Since: 1.28
 */
gboolean
gst_is_d3d12_staging_memory (GstMemory * mem)
{
  return mem != nullptr && mem->allocator != nullptr &&
      (GST_IS_D3D12_STAGING_ALLOCATOR (mem->allocator));
}

/**
 * gst_d3d12_staging_memory_sync:
 * @mem: a #GstD3D12StagingMemory
 *
 * Wait for pending GPU operation
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.28
 */
gboolean
gst_d3d12_staging_memory_sync (GstD3D12StagingMemory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_staging_memory (GST_MEMORY_CAST (mem)),
      FALSE);

  auto priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  priv->SetFence (nullptr, 0, true);

  return TRUE;
}

/**
 * gst_d3d12_staging_memory_get_layout:
 * @mem: a #GstD3D12StagingMemory
 * @index: layout index
 * @layout: D3D12_PLACED_SUBRESOURCE_FOOTPRINT
 *
 * Gets copyable resource layout for @index
 *
 * Returns: %TRUE if layout information is available for @index
 *
 * Since: 1.28
 */
gboolean
gst_d3d12_staging_memory_get_layout (GstD3D12StagingMemory * mem,
    guint index, D3D12_PLACED_SUBRESOURCE_FOOTPRINT * layout)
{
  g_return_val_if_fail (gst_is_d3d12_staging_memory (GST_MEMORY_CAST (mem)),
      FALSE);
  g_return_val_if_fail (layout, FALSE);

  auto priv = mem->priv;
  if (index >= priv->num_layouts)
    return FALSE;

  *layout = priv->layout[index];

  return TRUE;
}

/**
 * gst_d3d12_staging_memory_set_fence:
 * @mem: a #GstD3D12StagingMemory
 * @fence: (allow-none): a ID3D12Fence
 * @fence_value: fence value
 * @wait: waits for previously configured fence if any
 *
 * Replace fence object of @mem with new @fence.
 * This method will block calling thread for synchronization
 * if @wait is %TRUE and configured fence is different from new @fence
 *
 * Since: 1.28
 */
void
gst_d3d12_staging_memory_set_fence (GstD3D12StagingMemory * mem,
    ID3D12Fence * fence, guint64 fence_value, gboolean wait)
{
  g_return_if_fail (gst_is_d3d12_staging_memory (GST_MEMORY_CAST (mem)));
  g_return_if_fail (gst_mini_object_is_writable ((GstMiniObject *) mem));

  auto priv = mem->priv;
  std::lock_guard < std::mutex > lk (priv->lock);
  priv->SetFence (fence, fence_value, wait);
}

/**
 * gst_d3d12_staging_memory_get_fence:
 * @mem: a #GstD3D12StagingMemory
 * @fence: (out) (transfer full) (allow-none): a ID3D12Fence
 * @fence_value: (out) (allow-none): fence value
 *
 * Gets configured fence and fence value. Valid operations against returned
 * fence object are ID3D12Fence::GetCompletedValue() and
 * ID3D12Fence::SetEventOnCompletion(). Caller should not try to update
 * completed value via ID3D12Fence::Signal() since the fence is likely
 * owned by external component and shared only for read-only operations.
 *
 * Returns: %TRUE if @mem has configured fence object
 *
 * Since: 1.28
 */
gboolean
gst_d3d12_staging_memory_get_fence (GstD3D12StagingMemory * mem,
    ID3D12Fence ** fence, guint64 * fence_value)
{
  g_return_val_if_fail (gst_is_d3d12_staging_memory (GST_MEMORY_CAST (mem)),
      FALSE);

  auto priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->fence) {
    if (fence) {
      *fence = priv->fence.Get ();
      (*fence)->AddRef ();
    }

    if (fence_value)
      *fence_value = priv->fence_val;

    return TRUE;
  }

  return FALSE;
}

static gpointer
gst_d3d12_staging_memory_map_full (GstMemory * mem, GstMapInfo * info,
    gsize maxsize)
{
  auto dmem = GST_D3D12_STAGING_MEMORY_CAST (mem);
  auto priv = dmem->priv;
  auto flags = info->flags;

  std::lock_guard < std::mutex > lk (priv->lock);
  if ((flags & GST_MAP_D3D12) == GST_MAP_D3D12) {
    if (priv->cpu_write_count > 0) {
      GST_INFO_OBJECT (dmem->device, "CPU write map count %" G_GINT64_FORMAT,
          priv->cpu_write_count);
      return nullptr;
    }

    return priv->resource.Get ();
  }

  priv->SetFence (nullptr, 0, true);

  gpointer ret;
  D3D12_RANGE range = { };
  if ((flags & GST_MAP_READ) == GST_MAP_READ)
    range.End = mem->size;

  auto hr = priv->resource->Map (0, &range, &ret);
  if (!gst_d3d12_result (hr, dmem->device)) {
    GST_ERROR_OBJECT (dmem->device, "Couldn't map memory");
    return nullptr;
  }

  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    priv->cpu_write_count++;

  return ret;
}

static void
gst_d3d12_staging_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  auto dmem = GST_D3D12_STAGING_MEMORY_CAST (mem);
  auto priv = dmem->priv;
  auto flags = info->flags;

  std::lock_guard < std::mutex > lk (priv->lock);
  if ((flags & GST_MAP_D3D12) == GST_MAP_D3D12)
    return;

  D3D12_RANGE range = { };
  if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
    range.End = mem->size;
    if (priv->cpu_write_count <= 0)
      GST_WARNING_OBJECT (dmem->device, "Couldn't trace CPU write map count");
    else
      priv->cpu_write_count--;
  }

  priv->resource->Unmap (0, &range);
}

static GstMemory *
gst_d3d12_staging_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  return nullptr;
}

struct _GstD3D12StagingAllocatorPrivate
{
  gpointer padding;
};

#define gst_d3d12_staging_allocator_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D12StagingAllocator,
    gst_d3d12_staging_allocator, GST_TYPE_ALLOCATOR);

static GstMemory *gst_d3d12_staging_allocator_dummy_alloc (GstAllocator *
    allocator, gsize size, GstAllocationParams * params);
static void gst_d3d12_staging_allocator_free (GstAllocator * allocator,
    GstMemory * mem);

static void
gst_d3d12_staging_allocator_class_init (GstD3D12StagingAllocatorClass * klass)
{
  auto allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = gst_d3d12_staging_allocator_dummy_alloc;
  allocator_class->free = gst_d3d12_staging_allocator_free;
}

static void
gst_d3d12_staging_allocator_init (GstD3D12StagingAllocator * self)
{
  auto alloc = GST_ALLOCATOR_CAST (self);

  self->priv = (GstD3D12StagingAllocatorPrivate *)
      gst_d3d12_staging_allocator_get_instance_private (self);

  alloc->mem_type = GST_D3D12_STAGING_MEMORY_NAME;
  alloc->mem_map_full = gst_d3d12_staging_memory_map_full;
  alloc->mem_unmap_full = gst_d3d12_staging_memory_unmap_full;
  alloc->mem_share = gst_d3d12_staging_memory_share;

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_d3d12_staging_allocator_dummy_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (nullptr);
}

static void
gst_d3d12_staging_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  auto dmem = GST_D3D12_STAGING_MEMORY_CAST (mem);

  GST_LOG_OBJECT (allocator, "Free memory %p", mem);

  delete dmem->priv;

  gst_clear_object (&dmem->device);
  g_free (dmem);
}

static void
gst_d3d12_staging_memory_init_once (void)
{
  GST_D3D12_CALL_ONCE_BEGIN {
    _d3d12_memory_allocator = (GstD3D12StagingAllocator *)
        g_object_new (GST_TYPE_D3D12_STAGING_ALLOCATOR, nullptr);
    gst_object_ref_sink (_d3d12_memory_allocator);
    gst_object_ref (_d3d12_memory_allocator);

    gst_allocator_register (GST_D3D12_STAGING_MEMORY_NAME,
        GST_ALLOCATOR_CAST (_d3d12_memory_allocator));
  } GST_D3D12_CALL_ONCE_END;
}

/**
 * gst_d3d12_staging_allocator_alloc:
 * @allocator: (allow-none): a #GstD3D12StagingAllocator
 * @device: a GstD3D12Device
 * @num_layouts: layout count
 * @layouts: an array of D3D12_PLACED_SUBRESOURCE_FOOTPRINT
 * @total_bytes: Total bytes to allocate
 *
 * Allocates staging resource allocated in custom heap
 * D3D12_CPU_PAGE_PROPERTY_WRITE_BACK + D3D12_MEMORY_POOL_L0.
 *
 * Returns: (transfer full) (nullable): a newly allocated #GstD3D12StagingMemory
 * or otherwise %NULL if allocation failed
 *
 * Since: 1.28
 */
GstMemory *
gst_d3d12_staging_allocator_alloc (GstD3D12StagingAllocator * allocator,
    GstD3D12Device * device, guint num_layouts,
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT * layouts, gsize total_bytes)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (num_layouts > 0, nullptr);
  g_return_val_if_fail (num_layouts <= GST_VIDEO_MAX_PLANES, nullptr);
  g_return_val_if_fail (layouts, nullptr);
  g_return_val_if_fail (total_bytes > 0, nullptr);

  if (!allocator) {
    gst_d3d12_staging_memory_init_once ();
    allocator = _d3d12_memory_allocator;
  }

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  D3D12_HEAP_PROPERTIES prop =
      CD3DX12_HEAP_PROPERTIES (D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
      D3D12_MEMORY_POOL_L0);
  D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer (total_bytes);
  D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  if (gst_d3d12_device_non_zeroed_supported (device))
    heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

  ComPtr < ID3D12Resource > resource;
  auto hr = device_handle->CreateCommittedResource (&prop, heap_flags,
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS (&resource));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (device, "Couldn't allocate resource");
    return nullptr;
  }

  auto priv = new GstD3D12StagingMemoryPrivate ();

  priv->num_layouts = num_layouts;
  priv->resource = resource;

  for (guint i = 0; i < num_layouts; i++)
    priv->layout[i] = layouts[i];

  auto mem = g_new0 (GstD3D12StagingMemory, 1);
  mem->priv = priv;
  mem->device = (GstD3D12Device *) gst_object_ref (device);

  gst_memory_init (GST_MEMORY_CAST (mem),
      (GstMemoryFlags) 0, GST_ALLOCATOR_CAST (allocator), nullptr,
      total_bytes, 0, 0, total_bytes);

  return GST_MEMORY_CAST (mem);
}

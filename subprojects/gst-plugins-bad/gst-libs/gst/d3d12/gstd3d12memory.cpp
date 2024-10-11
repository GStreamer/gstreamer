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

#include "gstd3d12.h"
#include "gstd3d12memory-private.h"
#include "gstd3d12-private.h"
#include <directx/d3dx12.h>
#include <string.h>
#include <wrl.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <d3d11_1.h>

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
    cat = _gst_debug_category_new ("d3d12allocator", 0, "d3d12allocator");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif

static GstD3D12Allocator *_d3d12_memory_allocator = nullptr;

static gint
gst_d3d12_allocation_params_compare (const GstD3D12AllocationParams * p1,
    const GstD3D12AllocationParams * p2)
{
  g_return_val_if_fail (p1, -1);
  g_return_val_if_fail (p2, -1);

  if (p1 == p2)
    return 0;

  return -1;
}

static void
gst_d3d12_allocation_params_init (GType type)
{
  static GstValueTable table = {
    0, (GstValueCompareFunc) gst_d3d12_allocation_params_compare,
    nullptr, nullptr
  };

  table.type = type;
  gst_value_register (&table);
}

G_DEFINE_BOXED_TYPE_WITH_CODE (GstD3D12AllocationParams,
    gst_d3d12_allocation_params,
    (GBoxedCopyFunc) gst_d3d12_allocation_params_copy,
    (GBoxedFreeFunc) gst_d3d12_allocation_params_free,
    gst_d3d12_allocation_params_init (g_define_type_id));

/**
 * gst_d3d12_allocation_params_new:
 * @device: a #GstD3D12Device
 * @info: a #GstVideoInfo
 * @flags: a #GstD3D12AllocationFlags
 * @resource_flags: D3D12_RESOURCE_FLAGS value used for creating texture
 * @heap_flags: D3D12_HEAP_FLAGS value used for creating texture
 *
 * Create #GstD3D12AllocationParams object which is used by #GstD3D12BufferPool
 * and #GstD3D12Allocator in order to allocate new ID3D12Resource
 * object with given configuration
 *
 * Returns: (transfer full) (nullable): a #GstD3D12AllocationParams
 * or %NULL if given configuration is not supported
 *
 * Since: 1.26
 */
GstD3D12AllocationParams *
gst_d3d12_allocation_params_new (GstD3D12Device * device,
    const GstVideoInfo * info, GstD3D12AllocationFlags flags,
    D3D12_RESOURCE_FLAGS resource_flags, D3D12_HEAP_FLAGS heap_flags)
{
  GstD3D12AllocationParams *ret;
  GstD3D12Format d3d12_format;
  GstVideoFormat format;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (info, nullptr);

  format = GST_VIDEO_INFO_FORMAT (info);
  if (!gst_d3d12_device_get_format (device, format, &d3d12_format)) {
    GST_WARNING_OBJECT (device, "%s is not supported",
        gst_video_format_to_string (format));
    return nullptr;
  }

  ret = g_new0 (GstD3D12AllocationParams, 1);
  ret->info = *info;
  ret->aligned_info = *info;
  ret->d3d12_format = d3d12_format;
  ret->array_size = 1;
  ret->mip_levels = 1;
  ret->flags = flags;
  ret->heap_flags = heap_flags;
  ret->resource_flags = resource_flags;

  return ret;
}

/**
 * gst_d3d12_allocation_params_copy:
 * @src: a #GstD3D12AllocationParams
 *
 * Returns: (transfer full): a copy of @src
 *
 * Since: 1.26
 */
GstD3D12AllocationParams *
gst_d3d12_allocation_params_copy (GstD3D12AllocationParams * src)
{
  GstD3D12AllocationParams *dst;

  g_return_val_if_fail (src != NULL, NULL);

  dst = g_new0 (GstD3D12AllocationParams, 1);
  memcpy (dst, src, sizeof (GstD3D12AllocationParams));

  return dst;
}

/**
 * gst_d3d12_allocation_params_free:
 * @params: a #GstD3D12AllocationParams
 *
 * Free @params
 *
 * Since: 1.26
 */
void
gst_d3d12_allocation_params_free (GstD3D12AllocationParams * params)
{
  g_free (params);
}

/**
 * gst_d3d12_allocation_params_alignment:
 * @params: a #GstD3D12AllocationParams
 * @align: a #GstVideoAlignment
 *
 * Adjust alignment
 *
 * Returns: %TRUE if alignment could be applied
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocation_params_alignment (GstD3D12AllocationParams * params,
    const GstVideoAlignment * align)
{
  guint padding_width, padding_height;
  GstVideoInfo *info;
  GstVideoInfo new_info;

  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (align, FALSE);

  /* d3d11 does not support stride align. Consider padding only */
  padding_width = align->padding_left + align->padding_right;
  padding_height = align->padding_top + align->padding_bottom;

  info = &params->info;

  if (!gst_video_info_set_format (&new_info, GST_VIDEO_INFO_FORMAT (info),
          GST_VIDEO_INFO_WIDTH (info) + padding_width,
          GST_VIDEO_INFO_HEIGHT (info) + padding_height)) {
    GST_WARNING ("Set format failed");
    return FALSE;
  }

  params->aligned_info = new_info;

  return TRUE;
}

/**
 * gst_d3d12_allocation_params_set_resource_flags:
 * @params: a #GstD3D12AllocationParams
 * @resource_flags: D3D12_RESOURCE_FLAGS
 *
 * Set @resource_flags
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocation_params_set_resource_flags (GstD3D12AllocationParams *
    params, D3D12_RESOURCE_FLAGS resource_flags)
{
  g_return_val_if_fail (params, FALSE);

  params->resource_flags |= resource_flags;

  return TRUE;
}

/**
 * gst_d3d12_allocation_params_unset_resource_flags:
 * @params: a #GstD3D12AllocationParams
 * @resource_flags: D3D12_RESOURCE_FLAGS
 *
 * Unset @resource_flags
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocation_params_unset_resource_flags (GstD3D12AllocationParams *
    params, D3D12_RESOURCE_FLAGS resource_flags)
{
  g_return_val_if_fail (params, FALSE);

  params->resource_flags &= ~resource_flags;

  return TRUE;
}

/**
 * gst_d3d12_allocation_params_set_heap_flags:
 * @params: a #GstD3D12AllocationParams
 * @heap_flags: D3D12_HEAP_FLAGS
 *
 * Set @heap_flags
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocation_params_set_heap_flags (GstD3D12AllocationParams *
    params, D3D12_HEAP_FLAGS heap_flags)
{
  g_return_val_if_fail (params, FALSE);

  params->heap_flags |= heap_flags;

  return TRUE;
}

/**
 * gst_d3d12_allocation_params_set_array_size:
 * @params: a #GstD3D12AllocationParams
 * @size: a texture array size
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocation_params_set_array_size (GstD3D12AllocationParams * params,
    guint size)
{
  g_return_val_if_fail (params, FALSE);
  g_return_val_if_fail (size > 0, FALSE);
  g_return_val_if_fail (size <= G_MAXUINT16, FALSE);

  params->array_size = size;

  return TRUE;
}

/**
 * gst_d3d12_allocation_params_set_mip_levels:
 * @params: a #GstD3D12AllocationParams
 * @mip_levels: a texture mip levels
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocation_params_set_mip_levels (GstD3D12AllocationParams * params,
    guint mip_levels)
{
  g_return_val_if_fail (params, FALSE);

  params->mip_levels = mip_levels;

  return TRUE;
}

/* *INDENT-OFF* */
struct GstD3D12MemoryTokenData
{
  GstD3D12MemoryTokenData (gpointer data, GDestroyNotify notify_func)
    : user_data (data), notify (notify_func)
  {
  }

  ~GstD3D12MemoryTokenData ()
  {
    if (notify)
      notify (user_data);
  }

  gpointer user_data;
  GDestroyNotify notify;
};

struct D3D11Interop
{
  ComPtr<ID3D11Device> device11;
  ComPtr<ID3D11Texture2D> texture11;
};

struct _GstD3D12MemoryPrivate
{
  ~_GstD3D12MemoryPrivate ()
  {
    if (nt_handle)
      CloseHandle (nt_handle);

    token_map.clear ();
  }

  ComPtr<ID3D12Resource> resource;
  ComPtr<ID3D12Resource> staging;

  ComPtr<ID3D12DescriptorHeap> srv_heap;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  ComPtr<ID3D12DescriptorHeap> uav_heap;

  gpointer staging_ptr = nullptr;

  D3D12_RESOURCE_DESC desc;
  D3D12_HEAP_FLAGS heap_flags;

  HANDLE nt_handle = nullptr;
  std::map<gint64, std::unique_ptr<GstD3D12MemoryTokenData>> token_map;
  std::vector<std::shared_ptr<D3D11Interop>> shared_texture11;

  /* Queryied via ID3D12Device::GetCopyableFootprints */
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES];
  guint64 size;
  guint num_subresources;
  D3D12_RECT subresource_rect[GST_VIDEO_MAX_PLANES];
  guint subresource_index[GST_VIDEO_MAX_PLANES];
  DXGI_FORMAT resource_formats[GST_VIDEO_MAX_PLANES];
  guint srv_inc_size;
  guint rtv_inc_size;
  guint64 cpu_map_count = 0;

  std::mutex lock;

  gpointer user_data = nullptr;
  GDestroyNotify notify = nullptr;

  ComPtr<ID3D12Fence> fence;
  UINT64 fence_val = 0;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12Memory, gst_d3d12_memory);

static gboolean
gst_d3d12_memory_ensure_staging_resource (GstD3D12Memory * dmem)
{
  auto priv = dmem->priv;

  if (priv->staging)
    return TRUE;

  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0) {
    GST_ERROR_OBJECT (dmem->device, "simultaneous access is not supported");
    return FALSE;
  }

  HRESULT hr;
  auto device = gst_d3d12_device_get_device_handle (dmem->device);
  D3D12_HEAP_PROPERTIES prop =
      CD3DX12_HEAP_PROPERTIES (D3D12_CPU_PAGE_PROPERTY_WRITE_BACK,
      D3D12_MEMORY_POOL_L0);
  D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer (priv->size);
  D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  if (gst_d3d12_device_non_zeroed_supported (dmem->device))
    heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

  ComPtr < ID3D12Resource > staging;
  hr = device->CreateCommittedResource (&prop, heap_flags,
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS (&staging));
  if (!gst_d3d12_result (hr, dmem->device)) {
    GST_ERROR_OBJECT (dmem->device, "Couldn't create staging resource");
    return FALSE;
  }

  priv->staging = staging;

  GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return TRUE;
}

static void
gst_d3d12_memory_set_fence_unlocked (GstD3D12Memory * dmem,
    ID3D12Fence * fence, guint64 fence_val, gboolean wait)
{
  auto priv = dmem->priv;

  if (priv->fence && priv->fence.Get () != fence && wait) {
    auto completed = priv->fence->GetCompletedValue ();
    if (completed < priv->fence_val) {
      auto hr = priv->fence->SetEventOnCompletion (priv->fence_val, nullptr);
      /* For debugging */
      gst_d3d12_result (hr, dmem->device);
    }
  }

  priv->fence = fence;
  if (fence)
    priv->fence_val = fence_val;
  else
    priv->fence_val = 0;
}

static gboolean
gst_d3d12_memory_download (GstD3D12Memory * dmem)
{
  auto priv = dmem->priv;

  if (!priv->staging ||
      !GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD)) {
    return TRUE;
  }

  std::vector < GstD3D12CopyTextureRegionArgs > copy_args;
  for (guint i = 0; i < priv->num_subresources; i++) {
    GstD3D12CopyTextureRegionArgs args;
    memset (&args, 0, sizeof (args));

    args.dst = CD3DX12_TEXTURE_COPY_LOCATION (priv->staging.Get (),
        priv->layout[i]);
    args.src = CD3DX12_TEXTURE_COPY_LOCATION (priv->resource.Get (),
        priv->subresource_index[i]);

    copy_args.push_back (args);
  }

  guint64 fence_val = 0;
  guint num_fences_to_wait = 0;
  ID3D12Fence *fences_to_wait[] = { priv->fence.Get () };
  guint64 fence_values_to_wait[] = { priv->fence_val };
  if (priv->fence)
    num_fences_to_wait = 1;

  /* Use async copy queue when downloading */
  if (!gst_d3d12_device_copy_texture_region (dmem->device, copy_args.size (),
          copy_args.data (), nullptr, num_fences_to_wait, fences_to_wait,
          fence_values_to_wait, D3D12_COMMAND_LIST_TYPE_COPY, &fence_val)) {
    GST_ERROR_OBJECT (dmem->device, "Couldn't download texture to staging");
    return FALSE;
  }

  gst_d3d12_device_fence_wait (dmem->device, D3D12_COMMAND_LIST_TYPE_COPY,
      fence_val);

  priv->fence = nullptr;
  priv->fence_val = 0;

  GST_MEMORY_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return TRUE;
}

static gboolean
gst_d3d12_memory_upload (GstD3D12Memory * dmem)
{
  auto priv = dmem->priv;

  if (!priv->staging ||
      !GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD)) {
    return TRUE;
  }

  std::vector < GstD3D12CopyTextureRegionArgs > copy_args;
  for (guint i = 0; i < priv->num_subresources; i++) {
    GstD3D12CopyTextureRegionArgs args;
    memset (&args, 0, sizeof (args));

    args.dst = CD3DX12_TEXTURE_COPY_LOCATION (priv->resource.Get (),
        priv->subresource_index[i]);
    args.src = CD3DX12_TEXTURE_COPY_LOCATION (priv->staging.Get (),
        priv->layout[i]);

    copy_args.push_back (args);
  }

  guint num_fences_to_wait = 0;
  ID3D12Fence *fences_to_wait[] = { priv->fence.Get () };
  guint64 fence_values_to_wait[] = { priv->fence_val };
  if (fences_to_wait[0])
    num_fences_to_wait = 1;

  if (!gst_d3d12_device_copy_texture_region (dmem->device, copy_args.size (),
          copy_args.data (), nullptr, num_fences_to_wait, fences_to_wait,
          fence_values_to_wait, D3D12_COMMAND_LIST_TYPE_DIRECT,
          &priv->fence_val)) {
    GST_ERROR_OBJECT (dmem->device, "Couldn't upload texture");
    return FALSE;
  }

  priv->fence = gst_d3d12_device_get_fence_handle (dmem->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  GST_MEMORY_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  return TRUE;
}

static gpointer
gst_d3d12_memory_map_full (GstMemory * mem, GstMapInfo * info, gsize maxsize)
{
  auto dmem = GST_D3D12_MEMORY_CAST (mem);
  auto priv = dmem->priv;
  GstMapFlags flags = info->flags;
  std::lock_guard < std::mutex > lk (priv->lock);

  if ((flags & GST_MAP_D3D12) != 0) {
    gst_d3d12_memory_upload (dmem);

    if ((flags & GST_MAP_WRITE) != 0)
      GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);

    return priv->resource.Get ();
  }

  if (priv->cpu_map_count == 0) {
    if (!gst_d3d12_memory_ensure_staging_resource (dmem)) {
      GST_ERROR_OBJECT (mem->allocator,
          "Couldn't create readback_staging resource");
      return nullptr;
    }

    if (!gst_d3d12_memory_download (dmem)) {
      GST_ERROR_OBJECT (mem->allocator, "Couldn't download resource");
      return nullptr;
    }

    auto hr = priv->staging->Map (0, nullptr, &priv->staging_ptr);
    if (!gst_d3d12_result (hr, dmem->device)) {
      GST_ERROR_OBJECT (dmem->device, "Couldn't map readback resource");
      return nullptr;
    }
  }

  if ((flags & GST_MAP_WRITE) != 0)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  priv->cpu_map_count++;

  return priv->staging_ptr;
}

static void
gst_d3d12_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  auto dmem = GST_D3D12_MEMORY_CAST (mem);
  auto priv = dmem->priv;
  GstMapFlags flags = info->flags;

  if ((flags & GST_MAP_D3D12) == 0) {
    std::lock_guard < std::mutex > lk (priv->lock);

    g_assert (priv->cpu_map_count != 0);
    priv->cpu_map_count--;
    if (priv->cpu_map_count == 0)
      priv->staging->Unmap (0, nullptr);
  }
}

static GstMemory *
gst_d3d12_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  /* TODO: impl. */
  return nullptr;
}

/**
 * gst_is_d3d12_memory:
 * @mem: a #GstMemory
 *
 * Returns: %TRUE if @mem is allocated by #GstD3D12Allocator
 *
 * Since: 1.26
 */
gboolean
gst_is_d3d12_memory (GstMemory * mem)
{
  return mem != nullptr && mem->allocator != nullptr &&
      (GST_IS_D3D12_ALLOCATOR (mem->allocator) ||
      GST_IS_D3D12_POOL_ALLOCATOR (mem->allocator));
}

/**
 * gst_d3d12_memory_sync:
 * @mem: a #GstD3D12Memory
 *
 * Wait for pending GPU operation
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_memory_sync (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);

  auto priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  gst_d3d12_memory_upload (mem);
  gst_d3d12_memory_set_fence_unlocked (mem, nullptr, 0, TRUE);

  return TRUE;
}

/**
 * gst_d3d12_memory_init_once:
 *
 * Initializes the Direct3D12 Texture allocator. It is safe to call
 * this function multiple times. This must be called before any other
 * GstD3D12Memory operation.
 *
 * Since: 1.26
 */
void
gst_d3d12_memory_init_once (void)
{
  GST_D3D12_CALL_ONCE_BEGIN {
    _d3d12_memory_allocator =
        (GstD3D12Allocator *) g_object_new (GST_TYPE_D3D12_ALLOCATOR, nullptr);
    gst_object_ref_sink (_d3d12_memory_allocator);
    gst_object_ref (_d3d12_memory_allocator);

    gst_allocator_register (GST_D3D12_MEMORY_NAME,
        GST_ALLOCATOR_CAST (_d3d12_memory_allocator));
  } GST_D3D12_CALL_ONCE_END;
}

/**
 * gst_d3d12_memory_get_resource_handle:
 * @mem: a #GstD3D12Memory
 *
 * Returns: (transfer none) (nullable): ID3D12Resource handle
 *
 * Since: 1.26
 */
ID3D12Resource *
gst_d3d12_memory_get_resource_handle (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);

  return mem->priv->resource.Get ();
}

/**
 * gst_d3d12_memory_get_subresource_index:
 * @mem: a #GstD3D12Memory
 * @plane: a plane index
 * @index: (out): subresource index of @plane
 *
 * Returns: %TRUE if returned @index is valid
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_memory_get_subresource_index (GstD3D12Memory * mem, guint plane,
    guint * index)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (index != nullptr, FALSE);

  if (plane >= mem->priv->num_subresources) {
    GST_WARNING_OBJECT (GST_MEMORY_CAST (mem)->allocator, "Invalid plane %d",
        plane);
    return FALSE;
  }

  *index = mem->priv->subresource_index[plane];

  return TRUE;
}

/**
 * gst_d3d12_memory_get_plane_count:
 * @mem: a #GstD3D12Memory
 *
 * Returns: the number of planes of resource
 *
 * Since: 1.26
 */
guint
gst_d3d12_memory_get_plane_count (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), 0);

  return mem->priv->num_subresources;
}

/**
 * gst_d3d12_memory_get_plane_rectangle:
 * @mem: a #GstD3D12Memory
 * @plane: a plane index
 * @rect: (out): a rectangle of @plane
 *
 * Returns: %TRUE if returned @rect is valid
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_memory_get_plane_rectangle (GstD3D12Memory * mem, guint plane,
    D3D12_RECT * rect)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (rect, FALSE);

  if (plane >= mem->priv->num_subresources)
    return FALSE;

  *rect = mem->priv->subresource_rect[plane];

  return TRUE;
}

/**
 * gst_d3d12_memory_get_shader_resource_view_heap:
 * @mem: a #GstD3D12Memory
 *
 * Gets shader invisible shader resource view descriptor heap.
 * Caller needs to copy returned descriptor heap to another shader visible
 * descriptor heap in order for resource to be used in shader.
 *
 * Returns: (transfer none) (nullable): ID3D12DescriptorHeap handle or %NULL
 * if the resource was allocated without shader resource view enabled
 *
 * Since: 1.26
 */
ID3D12DescriptorHeap *
gst_d3d12_memory_get_shader_resource_view_heap (GstD3D12Memory * mem)
{
  auto priv = mem->priv;
  auto allocator = GST_MEMORY_CAST (mem)->allocator;
  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
    GST_LOG_OBJECT (allocator,
        "Shader resource was denied, configured flags 0x%x",
        (guint) priv->desc.Flags);
    return nullptr;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->srv_heap) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = priv->num_subresources;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    auto device = gst_d3d12_device_get_device_handle (mem->device);

    ComPtr < ID3D12DescriptorHeap > srv_heap;
    auto hr = device->CreateDescriptorHeap (&desc, IID_PPV_ARGS (&srv_heap));
    if (!gst_d3d12_result (hr, mem->device)) {
      GST_ERROR_OBJECT (allocator, "Couldn't create descriptor heap");
      return nullptr;
    }

    priv->srv_heap = srv_heap;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels = priv->desc.MipLevels;

    auto cpu_handle =
        CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
        (srv_heap));

    for (guint i = 0; i < priv->num_subresources; i++) {
      srv_desc.Format = priv->resource_formats[i];
      srv_desc.Texture2D.PlaneSlice = i;
      device->CreateShaderResourceView (priv->resource.Get (), &srv_desc,
          cpu_handle);
      cpu_handle.Offset (priv->srv_inc_size);
    }
  }

  return priv->srv_heap.Get ();
}

/**
 * gst_d3d12_memory_get_unordered_access_view_heap:
 * @mem: a #GstD3D12Memory
 *
 * Gets shader invisible unordered access view descriptor heap.
 * Caller needs to copy returned descriptor heap to another shader visible
 * descriptor heap in order for resource to be used in shader.
 *
 * Returns: (transfer none) (nullable): ID3D12DescriptorHeap handle or %NULL
 * if the resource was allocated without unordered access view enabled
 *
 * Since: 1.26
 */
ID3D12DescriptorHeap *
gst_d3d12_memory_get_unordered_access_view_heap (GstD3D12Memory * mem)
{
  auto priv = mem->priv;
  auto allocator = GST_MEMORY_CAST (mem)->allocator;
  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
    GST_LOG_OBJECT (allocator,
        "Unordered access view is not allowed, configured flags 0x%x",
        (guint) priv->desc.Flags);
    return nullptr;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->uav_heap) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = priv->num_subresources;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    auto device = gst_d3d12_device_get_device_handle (mem->device);

    ComPtr < ID3D12DescriptorHeap > uav_heap;
    auto hr = device->CreateDescriptorHeap (&desc, IID_PPV_ARGS (&uav_heap));
    if (!gst_d3d12_result (hr, mem->device)) {
      GST_ERROR_OBJECT (allocator, "Couldn't create descriptor heap");
      return nullptr;
    }

    priv->uav_heap = uav_heap;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = { };
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    auto cpu_handle =
        CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
        (uav_heap));

    for (guint i = 0; i < priv->num_subresources; i++) {
      uav_desc.Format = priv->resource_formats[i];
      uav_desc.Texture2D.PlaneSlice = i;
      device->CreateUnorderedAccessView (priv->resource.Get (), nullptr,
          &uav_desc, cpu_handle);
      cpu_handle.Offset (priv->srv_inc_size);
    }
  }

  return priv->uav_heap.Get ();
}

/**
 * gst_d3d12_memory_get_render_target_view_heap:
 * @mem: a #GstD3D12Memory
 *
 * Gets render target view descriptor heap
 *
 * Returns: (transfer none) (nullable): ID3D12DescriptorHeap handle or %NULL
 * if the resource was allocated without render target view enabled
 *
 * Since: 1.26
 */
ID3D12DescriptorHeap *
gst_d3d12_memory_get_render_target_view_heap (GstD3D12Memory * mem)
{
  auto priv = mem->priv;
  auto allocator = GST_MEMORY_CAST (mem)->allocator;
  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0) {
    GST_LOG_OBJECT (allocator,
        "Render target is not allowed, configured flags 0x%x",
        (guint) priv->desc.Flags);
    return nullptr;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (!priv->rtv_heap) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = priv->num_subresources;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    auto device = gst_d3d12_device_get_device_handle (mem->device);

    ComPtr < ID3D12DescriptorHeap > rtv_heap;
    auto hr = device->CreateDescriptorHeap (&desc, IID_PPV_ARGS (&rtv_heap));
    if (!gst_d3d12_result (hr, mem->device)) {
      GST_ERROR_OBJECT (allocator, "Couldn't create descriptor heap");
      return nullptr;
    }

    priv->rtv_heap = rtv_heap;

    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = { };
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    if (priv->desc.SampleDesc.Count > 1)
      rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

    auto cpu_handle =
        CD3DX12_CPU_DESCRIPTOR_HANDLE (GetCPUDescriptorHandleForHeapStart
        (rtv_heap));

    for (guint i = 0; i < priv->num_subresources; i++) {
      rtv_desc.Format = priv->resource_formats[i];
      if (priv->desc.SampleDesc.Count == 1)
        rtv_desc.Texture2D.PlaneSlice = i;
      device->CreateRenderTargetView (priv->resource.Get (), &rtv_desc,
          cpu_handle);
      cpu_handle.Offset (priv->rtv_inc_size);
    }
  }

  return priv->rtv_heap.Get ();
}

static gboolean
gst_d3d12_memory_get_nt_handle_unlocked (GstD3D12Memory * mem, HANDLE * handle)
{
  auto priv = mem->priv;

  *handle = nullptr;

  if (priv->nt_handle) {
    *handle = priv->nt_handle;
    return TRUE;
  }

  if ((priv->heap_flags & D3D12_HEAP_FLAG_SHARED) != D3D12_HEAP_FLAG_SHARED)
    return FALSE;

  auto device = gst_d3d12_device_get_device_handle (mem->device);
  auto hr = device->CreateSharedHandle (priv->resource.Get (), nullptr,
      GENERIC_ALL, nullptr, &priv->nt_handle);
  if (!gst_d3d12_result (hr, mem->device))
    return FALSE;

  *handle = priv->nt_handle;
  return TRUE;
}

/**
 * gst_d3d12_memory_get_nt_handle:
 * @mem: a #GstD3D12Memory
 * @handle: (out) (transfer none) a sharable NT handle
 *
 * Gets NT handle created via ID3D12Device::CreateSharedHandle().
 * Returned NT handle is owned by @mem, thus caller should not close
 * the @handle
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_memory_get_nt_handle (GstD3D12Memory * mem, HANDLE * handle)
{
  auto priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);

  return gst_d3d12_memory_get_nt_handle_unlocked (mem, handle);
}

/**
 * gst_d3d12_memory_set_token_data:
 * @mem: a #GstD3D12Memory
 * @token: an user token
 * @data: an user data
 * @notify: a #GDestroyNotify
 *
 * Sets an opaque user data on a #GstD3D12Memory
 *
 * Since: 1.26
 */
void
gst_d3d12_memory_set_token_data (GstD3D12Memory * mem, gint64 token,
    gpointer data, GDestroyNotify notify)
{
  auto priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  auto old_token = priv->token_map.find (token);
  if (old_token != priv->token_map.end ())
    priv->token_map.erase (old_token);

  if (data) {
    priv->token_map[token] = std::unique_ptr < GstD3D12MemoryTokenData >
        (new GstD3D12MemoryTokenData (data, notify));
  }
}

/**
 * gst_d3d12_memory_get_token_data:
 * @mem: a #GstD3D12Memory
 * @token: an user token
 *
 * Gets back user data pointer stored via gst_d3d12_memory_set_token_data()
 *
 * Returns: (transfer none) (nullable): user data pointer or %NULL
 *
 * Since: 1.26
 */
gpointer
gst_d3d12_memory_get_token_data (GstD3D12Memory * mem, gint64 token)
{
  auto priv = mem->priv;
  gpointer ret = nullptr;

  std::lock_guard < std::mutex > lk (priv->lock);
  auto old_token = priv->token_map.find (token);
  if (old_token != priv->token_map.end ())
    ret = old_token->second->user_data;

  return ret;
}

/**
 * gst_d3d12_memory_set_fence:
 * @mem: a #GstD3D12Memory
 * @fence: (allow-none): a ID3D12Fence
 * @fence_value: fence value
 * @wait: waits for previously configured fence if any
 *
 * Replace fence object of @mem with new @fence.
 * This method will block calling thread for synchronization
 * if @wait is %TRUE and configured fence is different from new @fence
 *
 * Since: 1.26
 */
void
gst_d3d12_memory_set_fence (GstD3D12Memory * mem, ID3D12Fence * fence,
    guint64 fence_value, gboolean wait)
{
  g_return_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)));

  auto priv = mem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  gst_d3d12_memory_set_fence_unlocked (mem, fence, fence_value, wait);
}

/**
 * gst_d3d12_memory_get_fence:
 * @mem: a #GstD3D12Memory
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
 * Since: 1.26
 */
gboolean
gst_d3d12_memory_get_fence (GstD3D12Memory * mem, ID3D12Fence ** fence,
    guint64 * fence_value)
{
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

/**
 * gst_d3d12_memory_get_d3d11_texture:
 * @mem: a #GstD3D12Memory
 * @device11: a ID3D11Device
 *
 * Opens ID3D11Texture2D texture from ID3D12Resource
 *
 * Returns: (transfer none) (nullable): ID3D11Texture2D handle or %NULL
 * if resource sharing is not supported
 *
 * Since: 1.26
 */
ID3D11Texture2D *
gst_d3d12_memory_get_d3d11_texture (GstD3D12Memory * mem,
    ID3D11Device * device11)
{
  auto priv = mem->priv;

  g_return_val_if_fail (mem, nullptr);
  g_return_val_if_fail (device11, nullptr);

  std::lock_guard < std::mutex > lk (priv->lock);
  auto it = std::find_if (priv->shared_texture11.begin (),
      priv->shared_texture11.end (),[&](const auto & shared)->bool {
        return shared->device11.Get () == device11;
      }
  );

  if (it != priv->shared_texture11.end ())
    return (*it)->texture11.Get ();

  HANDLE shared_handle;
  /* d3d11 interop requires rtv binding and shared heap */
  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) !=
      D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET ||
      !gst_d3d12_memory_get_nt_handle_unlocked (mem, &shared_handle)) {
    return nullptr;
  }

  ComPtr < ID3D11Device1 > device11_1;
  auto hr = device11->QueryInterface (IID_PPV_ARGS (&device11_1));
  if (FAILED (hr))
    return nullptr;

  ComPtr < ID3D11Texture2D > texture11;
  hr = device11_1->OpenSharedResource1 (shared_handle,
      IID_PPV_ARGS (&texture11));
  if (FAILED (hr))
    return nullptr;

  auto storage = std::make_shared < D3D11Interop > ();
  storage->device11 = device11;
  storage->texture11 = texture11;
  priv->shared_texture11.push_back (std::move (storage));

  return texture11.Get ();
}

/* GstD3D12Allocator */
struct _GstD3D12AllocatorPrivate
{
  GstMemoryCopyFunction fallback_copy;
};

#define gst_d3d12_allocator_parent_class alloc_parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstD3D12Allocator,
    gst_d3d12_allocator, GST_TYPE_ALLOCATOR);

static GstMemory *gst_d3d12_allocator_dummy_alloc (GstAllocator * allocator,
    gsize size, GstAllocationParams * params);
static GstMemory *gst_d3d12_allocator_alloc_internal (GstD3D12Allocator * self,
    GstD3D12Device * device, const D3D12_HEAP_PROPERTIES * heap_props,
    D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC * desc,
    D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE * optimized_clear_value);
static void gst_d3d12_allocator_free (GstAllocator * allocator,
    GstMemory * mem);

static void
gst_d3d12_allocator_class_init (GstD3D12AllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = gst_d3d12_allocator_dummy_alloc;
  allocator_class->free = gst_d3d12_allocator_free;
}

static GstMemory *
gst_d3d12_memory_copy (GstMemory * mem, gssize offset, gssize size)
{
  auto self = GST_D3D12_ALLOCATOR (mem->allocator);
  auto priv = self->priv;
  auto dmem = GST_D3D12_MEMORY_CAST (mem);

  /* non-zero offset or different size is not supported */
  if (offset != 0 || (size != -1 && (gsize) size != mem->size)) {
    GST_DEBUG_OBJECT (self, "Different size/offset, try fallback copy");
    return priv->fallback_copy (mem, offset, size);
  }

  GstMapInfo info;
  if (!gst_memory_map (mem, &info, GST_MAP_READ_D3D12)) {
    GST_WARNING_OBJECT (self, "Failed to map memory, try fallback copy");
    return priv->fallback_copy (mem, offset, size);
  }

  GstMemory *dst = nullptr;
  /* Try pool allocator first */
  if (GST_IS_D3D12_POOL_ALLOCATOR (self)) {
    auto pool = GST_D3D12_POOL_ALLOCATOR (self);
    gst_d3d12_pool_allocator_acquire_memory (pool, &dst);
  }

  auto src_tex = (ID3D12Resource *) info.data;
  if (!dst) {
    D3D12_RESOURCE_DESC desc;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_HEAP_FLAGS heap_flags;

    desc = GetDesc (src_tex);
    desc.DepthOrArraySize = 1;
    src_tex->GetHeapProperties (&heap_props, &heap_flags);
    dst = gst_d3d12_allocator_alloc_internal (nullptr, dmem->device,
        &heap_props, heap_flags, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
  }

  if (!dst) {
    GST_ERROR_OBJECT (self, "Couldn't allocate texture");
    gst_memory_unmap (mem, &info);
    return priv->fallback_copy (mem, offset, size);
  }

  std::vector < GstD3D12CopyTextureRegionArgs > copy_args;
  auto dst_dmem = GST_D3D12_MEMORY_CAST (dst);
  auto dst_priv = dst_dmem->priv;
  auto mem_priv = dmem->priv;
  for (guint i = 0; i < mem_priv->num_subresources; i++) {
    GstD3D12CopyTextureRegionArgs args;
    memset (&args, 0, sizeof (args));

    args.dst = CD3DX12_TEXTURE_COPY_LOCATION (dst_priv->resource.Get (),
        dst_priv->subresource_index[i]);
    args.src = CD3DX12_TEXTURE_COPY_LOCATION (mem_priv->resource.Get (),
        mem_priv->subresource_index[i]);
    copy_args.push_back (args);
  }
  gst_memory_unmap (mem, &info);

  ComPtr < ID3D12Fence > fence_to_wait;
  guint64 fence_value_to_wait[1];
  {
    std::lock_guard < std::mutex > lk (mem_priv->lock);
    fence_to_wait = mem_priv->fence;
    fence_value_to_wait[0] = mem_priv->fence_val;
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_device_acquire_fence_data (dmem->device, &fence_data);
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_memory_ref (mem)));

  ID3D12Fence *fences_to_wait[] = { fence_to_wait.Get () };
  guint num_fences_to_wait = 0;
  if (fence_to_wait)
    num_fences_to_wait = 1;

  gst_d3d12_device_copy_texture_region (dmem->device,
      copy_args.size (), copy_args.data (), fence_data, num_fences_to_wait,
      fences_to_wait, fence_value_to_wait, D3D12_COMMAND_LIST_TYPE_DIRECT,
      &dst_priv->fence_val);
  dst_priv->fence = gst_d3d12_device_get_fence_handle (dmem->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);

  GST_MINI_OBJECT_FLAG_SET (dst, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return dst;
}

static void
gst_d3d12_allocator_init (GstD3D12Allocator * self)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (self);

  self->priv = (GstD3D12AllocatorPrivate *)
      gst_d3d12_allocator_get_instance_private (self);

  alloc->mem_type = GST_D3D12_MEMORY_NAME;
  alloc->mem_map_full = gst_d3d12_memory_map_full;
  alloc->mem_unmap_full = gst_d3d12_memory_unmap_full;
  alloc->mem_share = gst_d3d12_memory_share;

  /* Store pointer to default mem_copy method for fallback copy */
  self->priv->fallback_copy = alloc->mem_copy;
  alloc->mem_copy = gst_d3d12_memory_copy;

  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_d3d12_allocator_dummy_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_return_val_if_reached (nullptr);
}

static void
gst_d3d12_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  auto dmem = GST_D3D12_MEMORY_CAST (mem);

  GST_LOG_OBJECT (allocator, "Free memory %p", mem);

  gst_d3d12_memory_set_fence_unlocked (dmem, nullptr, 0, TRUE);

  if (dmem->priv->notify)
    dmem->priv->notify (dmem->priv->user_data);

  dmem->priv->shared_texture11.clear ();

  delete dmem->priv;

  gst_clear_object (&dmem->device);

  g_free (dmem);
}

/**
 * gst_d3d12_allocator_alloc_wrapped:
 * @allocator:  (allow-none): a #GstD3D12Allocator
 * @device: a #GstD3D12Device
 * @resource: a ID3D12Resource
 * @array_slice: array slice index of the first plane
 * @user_data: (allow-none): an user data
 * @notify: a #GDestroyNotify
 *
 * Allocates memory object with @resource. The refcount of @resource
 * will be increased by one.
 *
 * Returns: (transfer full) (nullable): a newly allocated #GstD3D12Memory
 * with given @resource if successful, otherwise %NULL.
 *
 * Since: 1.26
 */
GstMemory *
gst_d3d12_allocator_alloc_wrapped (GstD3D12Allocator * allocator,
    GstD3D12Device * device, ID3D12Resource * resource, guint array_slice,
    gpointer user_data, GDestroyNotify notify)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (resource, nullptr);

  if (!allocator) {
    gst_d3d12_memory_init_once ();
    allocator = _d3d12_memory_allocator;
  }

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  auto desc = GetDesc (resource);
  guint8 num_subresources =
      D3D12GetFormatPlaneCount (device_handle, desc.Format);
  D3D12_HEAP_FLAGS heap_flags;

  if (num_subresources == 0) {
    GST_ERROR_OBJECT (allocator, "Couldn't get format info");
    return nullptr;
  }

  if (array_slice >= desc.DepthOrArraySize) {
    GST_ERROR_OBJECT (allocator, "Invalid array slice");
    return nullptr;
  }

  auto hr = resource->GetHeapProperties (nullptr, &heap_flags);
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (allocator, "Couldn't get heap property");
    return nullptr;
  }

  auto mem = g_new0 (GstD3D12Memory, 1);
  mem->priv = new GstD3D12MemoryPrivate ();

  auto priv = mem->priv;
  priv->desc = desc;
  priv->heap_flags = heap_flags;
  priv->num_subresources = num_subresources;
  priv->resource = resource;
  gst_d3d12_dxgi_format_get_resource_format (priv->desc.Format,
      priv->resource_formats);
  priv->srv_inc_size =
      device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  priv->rtv_inc_size =
      device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  priv->user_data = user_data;
  priv->notify = notify;

  mem->device = (GstD3D12Device *) gst_object_ref (device);

  for (guint i = 0; i < num_subresources; i++) {
    /* One notable difference between D3D12/D3D11 is that, D3D12 introduced
     * *PLANE* slice concept. That means, Each plane of YUV format
     * (e.g, DXGI_FORMAT_NV12) can be accessible in D3D12 but that wasn't
     * allowed in D3D11. As a result, the way for calculating subresource index
     * is changed. This is an example of subresource indexing
     * for array size == 3 with NV12 format.
     *
     *     Array 0       Array 1       Array 2
     * +-------------+-------------+-------------+
     * | Y plane : 0 | Y plane : 1 | Y plane : 2 |
     * +-------------+-------------+-------------+
     * | UV plane: 3 | UV plane: 4 | UV plane: 5 |
     * +-------------+-------------+-------------+
     */
    mem->priv->subresource_index[i] = D3D12CalcSubresource (0,
        array_slice, i, 1, desc.DepthOrArraySize);
  }

  /* Then calculate staging memory size and copyable layout */
  UINT64 size;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  device_handle->GetCopyableFootprints (&desc, 0,
      num_subresources, 0, priv->layout, nullptr, nullptr, &size);
  priv->size = size;

  priv->subresource_rect[0].left = 0;
  priv->subresource_rect[0].top = 0;
  priv->subresource_rect[0].right = (LONG) desc.Width;
  priv->subresource_rect[0].bottom = (LONG) desc.Height;

  for (guint i = 1; i < num_subresources; i++) {
    priv->subresource_rect[i].left = 0;
    priv->subresource_rect[i].top = 0;
    switch (desc.Format) {
      case DXGI_FORMAT_NV12:
      case DXGI_FORMAT_P010:
      case DXGI_FORMAT_P016:
        priv->subresource_rect[i].right = (LONG) desc.Width / 2;
        priv->subresource_rect[i].bottom = (LONG) desc.Height / 2;
        break;
      default:
        GST_WARNING_OBJECT (allocator, "Unexpected multi-plane format %d",
            desc.Format);
        priv->subresource_rect[i].right = (LONG) desc.Width / 2;
        priv->subresource_rect[i].bottom = (LONG) desc.Height / 2;
        break;
    }
  }

  gst_memory_init (GST_MEMORY_CAST (mem),
      (GstMemoryFlags) 0, GST_ALLOCATOR_CAST (allocator), nullptr,
      mem->priv->size, 0, 0, mem->priv->size);

  GST_LOG_OBJECT (allocator,
      "Allocated new memory %p with size %" G_GUINT64_FORMAT, mem, priv->size);

  return GST_MEMORY_CAST (mem);
}

static GstMemory *
gst_d3d12_allocator_alloc_internal (GstD3D12Allocator * self,
    GstD3D12Device * device, const D3D12_HEAP_PROPERTIES * heap_props,
    D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC * desc,
    D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE * optimized_clear_value)
{
  ID3D12Device *device_handle;
  HRESULT hr;
  ComPtr < ID3D12Resource > resource;

  if (!self) {
    gst_d3d12_memory_init_once ();
    self = _d3d12_memory_allocator;
  }

  device_handle = gst_d3d12_device_get_device_handle (device);
  hr = device_handle->CreateCommittedResource (heap_props, heap_flags,
      desc, initial_state, optimized_clear_value, IID_PPV_ARGS (&resource));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create texture");
    return nullptr;
  }

  auto mem =
      gst_d3d12_allocator_alloc_wrapped (self, device, resource.Get (), 0,
      nullptr, nullptr);
  if (!mem)
    return nullptr;

  /* Initialize YUV texture with black color */
  if (desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
      (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
      (heap_flags & D3D12_HEAP_FLAG_CREATE_NOT_ZEROED) == 0 &&
      desc->DepthOrArraySize == 1) {
    gst_d3d12_device_clear_yuv_texture (device, mem);
  }

  return mem;
}

/**
 * gst_d3d12_allocator_alloc:
 * @allocator: (allow-none): a #GstD3D12Allocator
 * @device: a #GstD3D12Device
 * @heap_prop: a D3D12_HEAP_PROPERTIES
 * @heap_flags: a D3D12_HEAP_FLAGS
 * @desc: a D3D12_RESOURCE_DESC
 * @initial_state: initial resource state
 * @optimized_clear_value: (allow-none): optimized clear value
 *
 * Returns: (transfer full) (nullable): a newly allocated #GstD3D12Memory
 * with given parameters.
 *
 * Since: 1.26
 */
GstMemory *
gst_d3d12_allocator_alloc (GstD3D12Allocator * allocator,
    GstD3D12Device * device, const D3D12_HEAP_PROPERTIES * heap_props,
    D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC * desc,
    D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE * optimized_clear_value)
{
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (heap_props != nullptr, nullptr);
  g_return_val_if_fail (desc != nullptr, nullptr);

  if (!allocator) {
    gst_d3d12_memory_init_once ();
    allocator = _d3d12_memory_allocator;
  }

  if (desc->DepthOrArraySize > 1) {
    GST_ERROR_OBJECT (allocator, "Array is not supported, use pool allocator");
    return nullptr;
  }

  return gst_d3d12_allocator_alloc_internal (allocator, device, heap_props,
      heap_flags, desc, initial_state, optimized_clear_value);
}

/**
 * gst_d3d12_allocator_set_active:
 * @allocator: a #GstD3D12Allocator
 * @active: the new active state
 *
 * Controls the active state of @allocator. Default #GstD3D12Allocator is
 * stateless and therefore active state is ignored, but subclass implementation
 * (e.g., #GstD3D12PoolAllocator) will require explicit active state control
 * for its internal resource management.
 *
 * This method is conceptually identical to gst_buffer_pool_set_active method.
 *
 * Returns: %TRUE if active state of @allocator was successfully updated.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_allocator_set_active (GstD3D12Allocator * allocator, gboolean active)
{
  GstD3D12AllocatorClass *klass;

  g_return_val_if_fail (GST_IS_D3D12_ALLOCATOR (allocator), FALSE);

  klass = GST_D3D12_ALLOCATOR_GET_CLASS (allocator);
  if (klass->set_actvie)
    return klass->set_actvie (allocator, active);

  return TRUE;
}

/* GstD3D12PoolAllocator */

/* *INDENT-OFF* */
struct _GstD3D12PoolAllocatorPrivate
{
  _GstD3D12PoolAllocatorPrivate()
  {
    outstanding = 0;
  }

  /* For the case where DepthOrArraySize > 1 */
  ComPtr<ID3D12Resource> resource;

  D3D12_HEAP_PROPERTIES heap_props;
  D3D12_HEAP_FLAGS heap_flags;
  D3D12_RESOURCE_DESC desc;
  D3D12_RESOURCE_STATES initial_state;
  D3D12_CLEAR_VALUE clear_value;
  gboolean clear_value_is_valid = FALSE;

  std::queue<GstMemory *> queue;

  std::mutex lock;
  std::condition_variable cond;
  gboolean started = FALSE;
  gboolean active = FALSE;

  std::atomic<guint> outstanding;
  guint cur_mems = 0;
  gboolean flushing = FALSE;
};
/* *INDENT-ON* */

static void gst_d3d12_pool_allocator_finalize (GObject * object);

static gboolean
gst_d3d12_pool_allocator_set_active (GstD3D12Allocator * allocator,
    gboolean active);

static gboolean gst_d3d12_pool_allocator_start (GstD3D12PoolAllocator * self);
static gboolean gst_d3d12_pool_allocator_stop (GstD3D12PoolAllocator * self);
static gboolean gst_d3d12_memory_release (GstMiniObject * mini_object);

#define gst_d3d12_pool_allocator_parent_class pool_alloc_parent_class
G_DEFINE_TYPE (GstD3D12PoolAllocator, gst_d3d12_pool_allocator,
    GST_TYPE_D3D12_ALLOCATOR);

static void
gst_d3d12_pool_allocator_class_init (GstD3D12PoolAllocatorClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstD3D12AllocatorClass *d3d12alloc_class = GST_D3D12_ALLOCATOR_CLASS (klass);

  gobject_class->finalize = gst_d3d12_pool_allocator_finalize;

  d3d12alloc_class->set_actvie = gst_d3d12_pool_allocator_set_active;
}

static void
gst_d3d12_pool_allocator_init (GstD3D12PoolAllocator * self)
{
  self->priv = new GstD3D12PoolAllocatorPrivate ();
}

static void
gst_d3d12_pool_allocator_finalize (GObject * object)
{
  GstD3D12PoolAllocator *self = GST_D3D12_POOL_ALLOCATOR (object);

  GST_DEBUG_OBJECT (self, "Finalize");

  gst_d3d12_pool_allocator_stop (self);
  delete self->priv;

  g_clear_object (&self->device);

  G_OBJECT_CLASS (pool_alloc_parent_class)->finalize (object);
}

/* must be called with the lock */
static gboolean
gst_d3d12_pool_allocator_start (GstD3D12PoolAllocator * self)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;
  ID3D12Device *device_handle;
  HRESULT hr;

  if (priv->started)
    return TRUE;

  /* Nothing to do */
  if (priv->desc.DepthOrArraySize == 1) {
    priv->started = TRUE;
    return TRUE;
  }

  device_handle = gst_d3d12_device_get_device_handle (self->device);
  if (!priv->resource) {
    ComPtr < ID3D12Resource > resource;
    hr = device_handle->CreateCommittedResource (&priv->heap_props,
        priv->heap_flags, &priv->desc, priv->initial_state,
        priv->clear_value_is_valid ? &priv->clear_value : nullptr,
        IID_PPV_ARGS (&resource));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Failed to allocate texture");
      return FALSE;
    }

    priv->resource = resource;
  }

  for (guint i = 0; i < priv->desc.DepthOrArraySize; i++) {
    GstMemory *mem;

    mem = gst_d3d12_allocator_alloc_wrapped (_d3d12_memory_allocator,
        self->device, priv->resource.Get (), i, nullptr, nullptr);

    priv->cur_mems++;
    priv->queue.push (mem);
  }

  priv->started = TRUE;

  return TRUE;
}

static gboolean
gst_d3d12_pool_allocator_set_active (GstD3D12Allocator * allocator,
    gboolean active)
{
  GstD3D12PoolAllocator *self = GST_D3D12_POOL_ALLOCATOR (allocator);
  GstD3D12PoolAllocatorPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "active %d", active);

  std::unique_lock < std::mutex > lk (priv->lock);
  /* just return if we are already in the right state */
  if (priv->active == active) {
    GST_LOG_OBJECT (self, "allocator was in the right state");
    return TRUE;
  }

  if (active) {
    if (!gst_d3d12_pool_allocator_start (self)) {
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
        priv->outstanding.load (), (guint) priv->queue.size ());
    if (priv->outstanding == 0) {
      if (!gst_d3d12_pool_allocator_stop (self)) {
        GST_ERROR_OBJECT (self, "stop failed");
        return FALSE;
      }
    }
  }

  return TRUE;
}

static void
gst_d3d12_pool_allocator_free_memory (GstD3D12PoolAllocator * self,
    GstMemory * mem)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;

  priv->cur_mems--;
  GST_LOG_OBJECT (self, "freeing memory %p (%u left)", mem, priv->cur_mems);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  gst_memory_unref (mem);
}

/* must be called with the lock */
static void
gst_d3d12_pool_allocator_clear_queue (GstD3D12PoolAllocator * self)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "Clearing queue");

  while (!priv->queue.empty ()) {
    GstMemory *mem = priv->queue.front ();
    priv->queue.pop ();
    gst_d3d12_pool_allocator_free_memory (self, mem);
  }

  GST_LOG_OBJECT (self, "Clear done");
}

/* must be called with the lock */
static gboolean
gst_d3d12_pool_allocator_stop (GstD3D12PoolAllocator * self)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  if (priv->started) {
    gst_d3d12_pool_allocator_clear_queue (self);

    priv->started = FALSE;
  } else {
    GST_DEBUG_OBJECT (self, "Wasn't started");
  }

  return TRUE;
}

static void
gst_d3d12_pool_allocator_release_memory (GstD3D12PoolAllocator * self,
    GstMemory * mem)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;

  GST_LOG_OBJECT (self, "Released memory %p", mem);

  GST_MINI_OBJECT_CAST (mem)->dispose = nullptr;
  mem->allocator = (GstAllocator *) gst_object_ref (_d3d12_memory_allocator);

  /* keep it around in our queue */
  priv->queue.push (mem);
  priv->outstanding--;
  if (priv->outstanding == 0 && priv->flushing)
    gst_d3d12_pool_allocator_stop (self);
  priv->cond.notify_all ();
  priv->lock.unlock ();

  gst_object_unref (self);
}

static gboolean
gst_d3d12_memory_release (GstMiniObject * mini_object)
{
  GstMemory *mem = GST_MEMORY_CAST (mini_object);
  GstD3D12PoolAllocator *alloc;
  GstD3D12PoolAllocatorPrivate *priv;

  g_assert (mem->allocator != nullptr);

  if (!GST_IS_D3D12_POOL_ALLOCATOR (mem->allocator)) {
    GST_LOG_OBJECT (mem->allocator, "Not our memory, free");
    return TRUE;
  }

  alloc = GST_D3D12_POOL_ALLOCATOR (mem->allocator);
  priv = alloc->priv;

  priv->lock.lock ();
  /* return the memory to the allocator */
  gst_memory_ref (mem);
  gst_d3d12_pool_allocator_release_memory (alloc, mem);

  return FALSE;
}

/* must be called with the lock */
static GstFlowReturn
gst_d3d12_pool_allocator_alloc (GstD3D12PoolAllocator * self, GstMemory ** mem)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;
  GstMemory *new_mem;

  /* we allcates texture array during start */
  if (priv->desc.DepthOrArraySize > 1)
    return GST_FLOW_EOS;

  /* increment the allocation counter */
  new_mem =
      gst_d3d12_allocator_alloc_internal (_d3d12_memory_allocator,
      self->device, &priv->heap_props,
      priv->heap_flags, &priv->desc, priv->initial_state,
      priv->clear_value_is_valid ? &priv->clear_value : nullptr);

  if (!new_mem) {
    GST_ERROR_OBJECT (self, "Failed to allocate new memory");
    return GST_FLOW_ERROR;
  }

  priv->cur_mems++;

  *mem = new_mem;

  return GST_FLOW_OK;
}

/* must be called with the lock */
static GstFlowReturn
gst_d3d12_pool_allocator_acquire_memory_internal (GstD3D12PoolAllocator * self,
    GstMemory ** memory, std::unique_lock < std::mutex > &lk)
{
  GstD3D12PoolAllocatorPrivate *priv = self->priv;
  GstFlowReturn ret = GST_FLOW_ERROR;

  do {
    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "we are flushing");
      return GST_FLOW_FLUSHING;
    }

    if (!priv->queue.empty ()) {
      *memory = priv->queue.front ();
      priv->queue.pop ();
      GST_LOG_OBJECT (self, "acquired memory %p", *memory);
      return GST_FLOW_OK;
    }

    /* no memory, try to allocate some more */
    GST_LOG_OBJECT (self, "no memory, trying to allocate");
    ret = gst_d3d12_pool_allocator_alloc (self, memory);
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
 * gst_d3d12_pool_allocator_new:
 * @device: a #GstD3D12Device
 * @heap_prop: a D3D12_HEAP_PROPERTIES
 * @heap_flags: a D3D12_HEAP_FLAGS
 * @desc: a D3D12_RESOURCE_DESC
 * @initial_state: initial resource state
 * @optimized_clear_value: (allow-none) optimized clear value
 *
 * Returns: (transfer full) (nullable): a new #GstD3D12PoolAllocator instance
 *
 * Since: 1.26
 */
GstD3D12PoolAllocator *
gst_d3d12_pool_allocator_new (GstD3D12Device * device,
    const D3D12_HEAP_PROPERTIES * heap_props, D3D12_HEAP_FLAGS heap_flags,
    const D3D12_RESOURCE_DESC * desc, D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE * optimized_clear_value)
{
  GstD3D12PoolAllocator *self;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (heap_props != nullptr, nullptr);
  g_return_val_if_fail (desc != nullptr, nullptr);

  gst_d3d12_memory_init_once ();

  self = (GstD3D12PoolAllocator *)
      g_object_new (GST_TYPE_D3D12_POOL_ALLOCATOR, nullptr);
  gst_object_ref_sink (self);

  self->device = (GstD3D12Device *) gst_object_ref (device);
  self->priv->heap_props = *heap_props;
  self->priv->heap_flags = heap_flags;
  self->priv->desc = *desc;
  self->priv->initial_state = initial_state;
  if (optimized_clear_value) {
    self->priv->clear_value = *optimized_clear_value;
    self->priv->clear_value_is_valid = TRUE;
  } else {
    self->priv->clear_value_is_valid = FALSE;
  }

  return self;
}

/**
 * gst_d3d12_pool_allocator_acquire_memory:
 * @allocator: a #GstD3D12PoolAllocator
 * @memory: (out) (transfer full): a #GstMemory
 *
 * Acquires a #GstMemory from @allocator. @memory should point to a memory
 * location that can hold a pointer to the new #GstMemory.
 *
 * Returns: a #GstFlowReturn such as %GST_FLOW_FLUSHING when the allocator is
 * inactive.
 *
 * Since: 1.26
 */
GstFlowReturn
gst_d3d12_pool_allocator_acquire_memory (GstD3D12PoolAllocator * allocator,
    GstMemory ** memory)
{
  GstFlowReturn ret;
  GstD3D12PoolAllocatorPrivate *priv;

  g_return_val_if_fail (GST_IS_D3D12_POOL_ALLOCATOR (allocator),
      GST_FLOW_ERROR);
  g_return_val_if_fail (memory != nullptr, GST_FLOW_ERROR);

  priv = allocator->priv;

  std::unique_lock < std::mutex > lk (priv->lock);
  ret = gst_d3d12_pool_allocator_acquire_memory_internal (allocator,
      memory, lk);

  if (ret == GST_FLOW_OK) {
    GstMemory *mem = *memory;
    /* Replace default allocator with ours */
    gst_object_unref (mem->allocator);
    mem->allocator = (GstAllocator *) gst_object_ref (allocator);
    GST_MINI_OBJECT_CAST (mem)->dispose = gst_d3d12_memory_release;
    priv->outstanding++;
  }

  return ret;
}

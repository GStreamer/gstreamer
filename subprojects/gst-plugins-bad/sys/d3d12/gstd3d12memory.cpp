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

#include "gstd3d12memory.h"
#include "gstd3d12device.h"
#include "gstd3d12utils.h"
#include "gstd3d12format.h"
#include "gstd3d12fence.h"
#include <string.h>
#include <wrl.h>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_allocator_debug);
#define GST_CAT_DEFAULT gst_d3d12_allocator_debug

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

GstD3D12AllocationParams *
gst_d3d12_allocation_params_new (GstD3D12Device * device,
    const GstVideoInfo * info, GstD3D12AllocationFlags flags,
    D3D12_RESOURCE_FLAGS resource_flags)
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

  if (d3d12_format.dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
      g_assert (d3d12_format.resource_format[i] != DXGI_FORMAT_UNKNOWN);

      ret->desc[i] =
          CD3D12_RESOURCE_DESC::Tex2D (d3d12_format.resource_format[i],
          GST_VIDEO_INFO_COMP_WIDTH (info, i),
          GST_VIDEO_INFO_COMP_HEIGHT (info, i), resource_flags);
    }
  } else {
    ret->desc[0] = CD3D12_RESOURCE_DESC::Tex2D (d3d12_format.dxgi_format,
        info->width, info->height, resource_flags);
  }

  ret->flags = flags;

  return ret;
}

GstD3D12AllocationParams *
gst_d3d12_allocation_params_copy (GstD3D12AllocationParams * src)
{
  GstD3D12AllocationParams *dst;

  g_return_val_if_fail (src != NULL, NULL);

  dst = g_new0 (GstD3D12AllocationParams, 1);
  memcpy (dst, src, sizeof (GstD3D12AllocationParams));

  return dst;
}

void
gst_d3d12_allocation_params_free (GstD3D12AllocationParams * params)
{
  g_free (params);
}

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

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    params->desc[i].Width = GST_VIDEO_INFO_COMP_WIDTH (&new_info, i);
    params->desc[i].Height = GST_VIDEO_INFO_COMP_HEIGHT (&new_info, i);
  }

  return TRUE;
}

/* *INDENT-OFF* */
struct _GstD3D12MemoryPrivate
{
  ComPtr<ID3D12Resource> resource;
  ComPtr<ID3D12Resource> staging;

  ComPtr<ID3D12DescriptorHeap> srv_heap;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;

  ComPtr<ID3D12CommandAllocator> copy_ca;
  ComPtr<ID3D12GraphicsCommandList> copy_cl;

  guint srv_increment_size = 0;
  guint rtv_increment_size = 0;

  guint num_srv = 0;
  guint num_rtv = 0;

  guint cpu_map_count = 0;
  gpointer staging_ptr = nullptr;

  D3D12_RESOURCE_DESC desc;
  D3D12_RESOURCE_STATES state;

  /* Queryied via ID3D12Device::GetCopyableFootprints */
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES];
  guint64 size;
  guint num_subresources;
  guint subresource_index[GST_VIDEO_MAX_PLANES];

  std::mutex lock;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12Memory, gst_d3d12_memory);

static gboolean
gst_d3d12_memory_ensure_staging_resource (GstD3D12Memory * dmem)
{
  GstD3D12MemoryPrivate *priv = dmem->priv;

  if (priv->staging)
    return TRUE;

  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0) {
    GST_ERROR_OBJECT (dmem->device, "simultaneous access is not supported");
    return FALSE;
  }

  HRESULT hr;
  ID3D12Device *device = gst_d3d12_device_get_device_handle (dmem->device);
  D3D12_HEAP_PROPERTIES prop =
      CD3D12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_READBACK);
  D3D12_RESOURCE_DESC desc = CD3D12_RESOURCE_DESC::Buffer (priv->size);
  ComPtr < ID3D12Resource > staging;

  hr = device->CreateCommittedResource (&prop, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS (&staging));
  if (!gst_d3d12_result (hr, dmem->device)) {
    GST_ERROR_OBJECT (dmem->device, "Couldn't create readback resource");
    return FALSE;
  }

  /* And map persistently */
  hr = staging->Map (0, nullptr, &priv->staging_ptr);
  if (!gst_d3d12_result (hr, dmem->device)) {
    GST_ERROR_OBJECT (dmem->device, "Couldn't map readback resource");
    return FALSE;
  }

  ComPtr < ID3D12CommandAllocator > copy_ca;
  ComPtr < ID3D12GraphicsCommandList > copy_cl;

  hr = device->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_COPY,
      IID_PPV_ARGS (&copy_ca));
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_COPY,
      copy_ca.Get (), nullptr, IID_PPV_ARGS (&copy_cl));
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  hr = copy_cl->Close ();
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  priv->staging = staging;
  priv->copy_ca = copy_ca;
  priv->copy_cl = copy_cl;

  GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return TRUE;
}

static gboolean
gst_d3d12_memory_download (GstD3D12Memory * dmem)
{
  GstD3D12MemoryPrivate *priv = dmem->priv;
  HRESULT hr;
  ID3D12CommandQueue *queue;

  if (!priv->staging ||
      !GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD)) {
    return TRUE;
  }

  gst_d3d12_fence_wait (dmem->fence);

  queue = gst_d3d12_device_get_copy_queue (dmem->device);
  if (!queue)
    return FALSE;

  hr = priv->copy_ca->Reset ();
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  hr = priv->copy_cl->Reset (priv->copy_ca.Get (), nullptr);
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  for (guint i = 0; i < priv->num_subresources; i++) {
    D3D12_TEXTURE_COPY_LOCATION src =
        CD3D12_TEXTURE_COPY_LOCATION (priv->resource.Get (),
        priv->subresource_index[i]);
    D3D12_TEXTURE_COPY_LOCATION dst =
        CD3D12_TEXTURE_COPY_LOCATION (priv->staging.Get (), priv->layout[i]);

    priv->copy_cl->CopyTextureRegion (&dst, 0, 0, 0, &src, nullptr);
  }

  hr = priv->copy_cl->Close ();
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  ID3D12CommandList *list[] = { priv->copy_cl.Get () };
  queue->ExecuteCommandLists (1, list);

  guint64 fence_value = gst_d3d12_device_get_fence_value (dmem->device);
  hr = queue->Signal (gst_d3d12_fence_get_handle (dmem->fence), fence_value);
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  gst_d3d12_fence_set_event_on_completion_value (dmem->fence, fence_value);
  gst_d3d12_fence_wait (dmem->fence);

  GST_MEMORY_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);

  return TRUE;
}

static gboolean
gst_d3d12_memory_upload (GstD3D12Memory * dmem)
{
  GstD3D12MemoryPrivate *priv = dmem->priv;
  HRESULT hr;
  ID3D12CommandQueue *queue;

  if (!priv->staging ||
      !GST_MEMORY_FLAG_IS_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD)) {
    return TRUE;
  }

  queue = gst_d3d12_device_get_copy_queue (dmem->device);
  if (!queue)
    return FALSE;

  hr = priv->copy_ca->Reset ();
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  hr = priv->copy_cl->Reset (priv->copy_ca.Get (), nullptr);
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  for (guint i = 0; i < priv->num_subresources; i++) {
    D3D12_TEXTURE_COPY_LOCATION src =
        CD3D12_TEXTURE_COPY_LOCATION (priv->staging.Get (), priv->layout[i]);
    D3D12_TEXTURE_COPY_LOCATION dst =
        CD3D12_TEXTURE_COPY_LOCATION (priv->resource.Get (),
        priv->subresource_index[i]);

    priv->copy_cl->CopyTextureRegion (&dst, 0, 0, 0, &src, nullptr);
  }

  hr = priv->copy_cl->Close ();
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  ID3D12CommandList *list[] = { priv->copy_cl.Get () };
  queue->ExecuteCommandLists (1, list);

  guint64 fence_value = gst_d3d12_device_get_fence_value (dmem->device);
  hr = queue->Signal (gst_d3d12_fence_get_handle (dmem->fence), fence_value);
  if (!gst_d3d12_result (hr, dmem->device))
    return FALSE;

  gst_d3d12_fence_set_event_on_completion_value (dmem->fence, fence_value);
  gst_d3d12_fence_wait (dmem->fence);

  GST_MEMORY_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  return TRUE;
}

static gpointer
gst_d3d12_memory_map_full (GstMemory * mem, GstMapInfo * info, gsize maxsize)
{
  GstD3D12Memory *dmem = GST_D3D12_MEMORY_CAST (mem);
  GstD3D12MemoryPrivate *priv = dmem->priv;
  GstMapFlags flags = info->flags;
  std::lock_guard < std::mutex > lk (priv->lock);

  if ((flags & GST_MAP_D3D12) == GST_MAP_D3D12) {
    if (!gst_d3d12_memory_upload (dmem)) {
      GST_ERROR_OBJECT (mem->allocator, "Upload failed");
      return nullptr;
    }

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE) {
      GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
    }

    return priv->resource.Get ();
  }

  if (priv->cpu_map_count == 0) {
    if (!gst_d3d12_memory_ensure_staging_resource (dmem)) {
      GST_ERROR_OBJECT (mem->allocator, "Couldn't create staging resource");
      return nullptr;
    }

    if (!gst_d3d12_memory_download (dmem)) {
      GST_ERROR_OBJECT (mem->allocator, "Couldn't download resource");
      return nullptr;
    }

    if ((flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);
  }

  priv->cpu_map_count++;

  return priv->staging_ptr;
}

static void
gst_d3d12_memory_unmap_full (GstMemory * mem, GstMapInfo * info)
{
  GstD3D12Memory *dmem = GST_D3D12_MEMORY_CAST (mem);
  GstD3D12MemoryPrivate *priv = dmem->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  if ((info->flags & GST_MAP_D3D12) == GST_MAP_D3D12) {
    if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
    return;
  }

  if ((info->flags & GST_MAP_WRITE) == GST_MAP_WRITE)
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

  priv->cpu_map_count--;
}

static GstMemory *
gst_d3d12_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  /* TODO: impl. */
  return nullptr;
}

gboolean
gst_is_d3d12_memory (GstMemory * mem)
{
  return mem != nullptr && mem->allocator != nullptr &&
      (GST_IS_D3D12_ALLOCATOR (mem->allocator) ||
      GST_IS_D3D12_POOL_ALLOCATOR (mem->allocator));
}

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

ID3D12Resource *
gst_d3d12_memory_get_resource_handle (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);

  return mem->priv->resource.Get ();
}

gboolean
gst_d3d12_memory_get_state (GstD3D12Memory * mem, D3D12_RESOURCE_STATES * state)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);

  if (!mem->priv->lock.try_lock ()) {
    GST_WARNING ("Resource %p is owned by other thread, try map first", mem);
    return FALSE;
  }

  if (state)
    *state = mem->priv->state;

  mem->priv->lock.unlock ();

  return TRUE;
}

gboolean
gst_d3d12_memory_set_state (GstD3D12Memory * mem, D3D12_RESOURCE_STATES state)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);

  if (!mem->priv->lock.try_lock ()) {
    GST_WARNING ("Resource %p is owned by other thread, try map first", mem);
    return FALSE;
  }

  mem->priv->state = state;
  mem->priv->lock.unlock ();

  /* XXX: This might not be sufficient. We should know the type of command list
   * (queue) where the resource was used in for the later use.
   * Probably we can infer it by using state though.
   */

  return TRUE;
}

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

guint
gst_d3d12_memory_get_plane_count (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), 0);

  return mem->priv->num_subresources;
}

gboolean
gst_d3d12_memory_get_plane_size (GstD3D12Memory * mem, guint plane,
    gint * width, gint * height, gint * stride, gsize * offset)
{
  GstD3D12MemoryPrivate *priv;

  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);

  priv = mem->priv;

  if (plane >= priv->num_subresources) {
    GST_WARNING_OBJECT (GST_MEMORY_CAST (mem)->allocator, "Invalid plane %d",
        plane);
    return FALSE;
  }

  if (width)
    *width = (gint) priv->layout[plane].Footprint.Width;
  if (height)
    *height = (gint) priv->layout[plane].Footprint.Height;
  if (stride)
    *stride = (gint) priv->layout[plane].Footprint.RowPitch;
  if (offset)
    *offset = (gsize) priv->layout[plane].Offset;

  return TRUE;
}

static gboolean
create_shader_resource_views (GstD3D12Memory * mem)
{
  GstD3D12MemoryPrivate *priv = mem->priv;
  HRESULT hr;
  guint num_formats = 0;
  ID3D12Device *device;
  DXGI_FORMAT formats[GST_VIDEO_MAX_PLANES];

  if (!gst_d3d12_dxgi_format_to_resource_formats (priv->desc.Format, formats)) {
    GST_ERROR_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Failed to get resource formats for DXGI format %d", priv->desc.Format);
    return FALSE;
  }

  for (guint i = 0; i < G_N_ELEMENTS (formats); i++) {
    if (formats[i] == DXGI_FORMAT_UNKNOWN)
      break;

    num_formats++;
  }

  g_assert (priv->srv_heap == nullptr);
  device = gst_d3d12_device_get_device_handle (mem->device);

  priv->srv_increment_size =
      device->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = { };
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.NumDescriptors = num_formats;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;

  ComPtr < ID3D12DescriptorHeap > srv_heap;
  hr = device->CreateDescriptorHeap (&heap_desc, IID_PPV_ARGS (&srv_heap));
  if (!gst_d3d12_result (hr, mem->device)) {
    GST_ERROR_OBJECT (mem->device, "Failed to create SRV descriptor heap");
    return FALSE;
  }

  auto srv_handle = srv_heap->GetCPUDescriptorHandleForHeapStart ();
  for (guint i = 0; i < num_formats; i++) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
    srv_desc.Format = formats[i];
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.PlaneSlice = i;
    srv_desc.Texture2D.ResourceMinLODClamp = 0xf;

    device->CreateShaderResourceView (priv->resource.Get (), &srv_desc,
        srv_handle);
    srv_handle.ptr += priv->srv_increment_size;
  }

  priv->srv_heap = srv_heap;
  priv->num_srv = num_formats;

  return TRUE;
}

static gboolean
gst_d3d12_memory_ensure_shader_resource_view (GstD3D12Memory * mem)
{
  GstD3D12MemoryPrivate *priv = mem->priv;

  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
    GST_LOG_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Shader resource was denied");
    return FALSE;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->num_srv)
    return TRUE;

  return create_shader_resource_views (mem);
}

guint
gst_d3d12_memory_get_shader_resource_view_size (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), 0);

  if (!gst_d3d12_memory_ensure_shader_resource_view (mem))
    return 0;

  return mem->priv->num_srv;
}

gboolean
gst_d3d12_memory_get_shader_resource_view (GstD3D12Memory * mem, guint index,
    D3D12_CPU_DESCRIPTOR_HANDLE * srv)
{
  GstD3D12MemoryPrivate *priv;

  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (srv != nullptr, FALSE);

  if (!gst_d3d12_memory_ensure_shader_resource_view (mem))
    return FALSE;

  priv = mem->priv;

  if (index >= priv->num_srv) {
    GST_ERROR ("Invalid SRV index %d", index);
    return FALSE;
  }

  g_assert (priv->srv_heap != nullptr);

  auto srv_handle = priv->srv_heap->GetCPUDescriptorHandleForHeapStart ();
  srv_handle.ptr += ((gsize) index * priv->srv_increment_size);

  *srv = srv_handle;
  return TRUE;
}

static gboolean
create_render_target_views (GstD3D12Memory * mem)
{
  GstD3D12MemoryPrivate *priv = mem->priv;
  HRESULT hr;
  guint num_formats = 0;
  ID3D12Device *device;
  DXGI_FORMAT formats[GST_VIDEO_MAX_PLANES];

  if (!gst_d3d12_dxgi_format_to_resource_formats (priv->desc.Format, formats)) {
    GST_ERROR_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Failed to get resource formats for DXGI format %d", priv->desc.Format);
    return FALSE;
  }

  for (guint i = 0; i < G_N_ELEMENTS (formats); i++) {
    if (formats[i] == DXGI_FORMAT_UNKNOWN)
      break;

    num_formats++;
  }

  g_assert (priv->rtv_heap == nullptr);
  device = gst_d3d12_device_get_device_handle (mem->device);

  priv->rtv_increment_size =
      device->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = { };
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heap_desc.NumDescriptors = num_formats;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heap_desc.NodeMask = 0;

  ComPtr < ID3D12DescriptorHeap > rtv_heap;
  hr = device->CreateDescriptorHeap (&heap_desc, IID_PPV_ARGS (&rtv_heap));
  if (!gst_d3d12_result (hr, mem->device)) {
    GST_ERROR_OBJECT (mem->device, "Failed to create SRV descriptor heap");
    return FALSE;
  }

  auto rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart ();
  for (guint i = 0; i < num_formats; i++) {
    D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = { };
    rtv_desc.Format = formats[i];
    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtv_desc.Texture2D.MipSlice = 0;
    rtv_desc.Texture2D.PlaneSlice = i;

    device->CreateRenderTargetView (priv->resource.Get (), &rtv_desc,
        rtv_handle);
    rtv_handle.ptr += priv->rtv_increment_size;
  }

  priv->rtv_heap = rtv_heap;
  priv->num_rtv = num_formats;

  return TRUE;
}

static gboolean
gst_d3d12_memory_ensure_render_target_view (GstD3D12Memory * mem)
{
  GstD3D12MemoryPrivate *priv = mem->priv;

  if ((priv->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0) {
    GST_LOG_OBJECT (GST_MEMORY_CAST (mem)->allocator,
        "Render target is not allowed");
    return FALSE;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->num_rtv)
    return TRUE;

  return create_render_target_views (mem);
}

guint
gst_d3d12_memory_get_render_target_view_size (GstD3D12Memory * mem)
{
  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), 0);

  if (!gst_d3d12_memory_ensure_render_target_view (mem))
    return 0;

  return mem->priv->num_rtv;
}

gboolean
gst_d3d12_memory_get_render_target_view (GstD3D12Memory * mem, guint index,
    D3D12_CPU_DESCRIPTOR_HANDLE * rtv)
{
  GstD3D12MemoryPrivate *priv;

  g_return_val_if_fail (gst_is_d3d12_memory (GST_MEMORY_CAST (mem)), FALSE);
  g_return_val_if_fail (rtv != nullptr, FALSE);

  if (!gst_d3d12_memory_ensure_render_target_view (mem))
    return FALSE;

  priv = mem->priv;

  if (index >= priv->num_rtv) {
    GST_ERROR ("Invalid RTV index %d", index);
    return FALSE;
  }

  g_assert (priv->rtv_heap != nullptr);

  auto rtv_handle = priv->rtv_heap->GetCPUDescriptorHandleForHeapStart ();
  rtv_handle.ptr += ((gsize) index * priv->rtv_increment_size);

  *rtv = rtv_handle;

  return TRUE;
}

/* GstD3D12Allocator */
#define gst_d3d12_allocator_parent_class alloc_parent_class
G_DEFINE_TYPE (GstD3D12Allocator, gst_d3d12_allocator, GST_TYPE_ALLOCATOR);

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

static void
gst_d3d12_allocator_init (GstD3D12Allocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_D3D12_MEMORY_NAME;
  alloc->mem_map_full = gst_d3d12_memory_map_full;
  alloc->mem_unmap_full = gst_d3d12_memory_unmap_full;
  alloc->mem_share = gst_d3d12_memory_share;

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
  GstD3D12Memory *dmem = GST_D3D12_MEMORY_CAST (mem);

  GST_LOG_OBJECT (allocator, "Free memory %p", mem);

  if (dmem->fence) {
    gst_d3d12_fence_wait (dmem->fence);
    gst_d3d12_fence_unref (dmem->fence);
  }

  delete dmem->priv;

  gst_clear_object (&dmem->device);

  g_free (dmem);
}

static GstMemory *
gst_d3d12_allocator_alloc_wrapped (GstD3D12Allocator * self,
    GstD3D12Device * device, const D3D12_RESOURCE_DESC * desc,
    D3D12_RESOURCE_STATES initial_state, ID3D12Resource * resource,
    guint array_slice)
{
  GstD3D12Memory *mem;
  GstD3D12MemoryPrivate *priv;
  ID3D12Device *device_handle = gst_d3d12_device_get_device_handle (device);
  guint8 num_subresources =
      gst_d3d12_get_format_plane_count (device, desc->Format);

  if (num_subresources == 0) {
    GST_ERROR_OBJECT (self, "Couldn't get format info");
    return nullptr;
  }

  mem = g_new0 (GstD3D12Memory, 1);
  mem->priv = priv = new GstD3D12MemoryPrivate ();

  priv->desc = *desc;
  priv->num_subresources = num_subresources;
  priv->resource = resource;
  priv->state = initial_state;

  mem->device = (GstD3D12Device *) gst_object_ref (device);
  mem->fence = gst_d3d12_fence_new (device);

  mem->priv->size = 0;
  for (guint i = 0; i < mem->priv->num_subresources; i++) {
    UINT64 size;

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
    mem->priv->subresource_index[i] = gst_d3d12_calculate_subresource (0,
        array_slice, i, 1, desc->DepthOrArraySize);

    device_handle->GetCopyableFootprints (&priv->desc,
        priv->subresource_index[i], 1, 0, &priv->layout[i], nullptr, nullptr,
        &size);

    /* Update offset manually */
    priv->layout[i].Offset = priv->size;
    priv->size += size;
  }

  gst_memory_init (GST_MEMORY_CAST (mem),
      (GstMemoryFlags) 0, GST_ALLOCATOR_CAST (self), nullptr,
      mem->priv->size, 0, 0, mem->priv->size);

  GST_LOG_OBJECT (self, "Allocated new memory %p with size %" G_GUINT64_FORMAT,
      mem, priv->size);

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

  device_handle = gst_d3d12_device_get_device_handle (device);
  hr = device_handle->CreateCommittedResource (heap_props, heap_flags,
      desc, initial_state, optimized_clear_value, IID_PPV_ARGS (&resource));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create texture");
    return nullptr;
  }

  return gst_d3d12_allocator_alloc_wrapped (self, device, desc,
      initial_state, resource.Get (), 0);
}

GstMemory *
gst_d3d12_allocator_alloc (GstD3D12Allocator * allocator,
    GstD3D12Device * device, const D3D12_HEAP_PROPERTIES * heap_props,
    D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC * desc,
    D3D12_RESOURCE_STATES initial_state,
    const D3D12_CLEAR_VALUE * optimized_clear_value)
{
  g_return_val_if_fail (GST_IS_D3D12_ALLOCATOR (allocator), nullptr);
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (heap_props != nullptr, nullptr);
  g_return_val_if_fail (desc != nullptr, nullptr);

  if (desc->DepthOrArraySize > 1) {
    GST_ERROR_OBJECT (allocator, "Array is not supported, use pool allocator");
    return nullptr;
  }

  return gst_d3d12_allocator_alloc_internal (allocator, device, heap_props,
      heap_flags, desc, initial_state, optimized_clear_value);
}

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
        self->device, &priv->desc,
        priv->initial_state, priv->resource.Get (), i);

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
  /* if flushing, free this memory */
  if (alloc->priv->flushing) {
    priv->lock.unlock ();
    GST_LOG_OBJECT (alloc, "allocator is flushing, free %p", mem);
    return TRUE;
  }

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

gboolean
gst_d3d12_pool_allocator_get_pool_size (GstD3D12PoolAllocator * allocator,
    guint * max_size, guint * outstanding_size)
{
  GstD3D12PoolAllocatorPrivate *priv;

  g_return_val_if_fail (GST_IS_D3D12_POOL_ALLOCATOR (allocator), FALSE);

  priv = allocator->priv;

  if (max_size) {
    if (priv->desc.DepthOrArraySize > 1)
      *max_size = (guint) priv->desc.DepthOrArraySize;
    else
      *max_size = 0;
  }

  if (outstanding_size)
    *outstanding_size = priv->outstanding;

  return TRUE;
}

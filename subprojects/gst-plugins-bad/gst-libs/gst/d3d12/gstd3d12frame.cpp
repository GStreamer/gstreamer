/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12frame.h"
#include "gstd3d12memory.h"
#include "gstd3d12device.h"
#include "gstd3d12-private.h"
#include <string.h>
#include <directx/d3dx12.h>
#include <vector>
#include <wrl.h>

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
    cat = _gst_debug_category_new ("d3d12frame", 0, "d3d12frame");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif

/**
 * gst_d3d12_frame_map:
 * @frame: (out caller-allocates): pointer to #GstD3D12Frame
 * @info: a #GstVideoInfo
 * @buffer: the buffer to map
 * @map_flags: #GstMapFlags
 * @d3d12_flags: #GstD3D12FrameFlags
 *
 * Executes memory map operation fills @frame with extracted Direct3D12 resource
 * information
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_frame_map (GstD3D12Frame * frame, const GstVideoInfo * info,
    GstBuffer * buffer, GstMapFlags map_flags,
    GstD3D12FrameMapFlags d3d12_flags)
{
  g_return_val_if_fail (frame, FALSE);
  g_return_val_if_fail (info, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  memset (frame, 0, sizeof (GstD3D12Frame));

  auto is_write = (map_flags & GST_MAP_WRITE) != 0;
  auto is_writable = gst_buffer_is_writable (buffer);

  if (is_write && !is_writable) {
    GST_ERROR ("Buffer is not writable");
    return FALSE;
  }

  bool need_map = (map_flags & GST_MAP_READWRITE) != 0;
  map_flags = (GstMapFlags) (map_flags | GST_MAP_D3D12);

  guint num_mem = gst_buffer_n_memory (buffer);
  if (!num_mem) {
    GST_ERROR ("Empty buffer");
    return FALSE;
  }

  GstD3D12Device *device = nullptr;
  for (guint i = 0; i < num_mem; i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_d3d12_memory (mem)) {
      GST_LOG ("memory %u is not a d3d12 memory", i);
      return FALSE;
    }

    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    if (!device) {
      device = dmem->device;
    } else if (!gst_d3d12_device_is_equal (device, dmem->device)) {
      GST_ERROR ("memory %u belongs to different device", i);
      return FALSE;
    }

    auto resource = gst_d3d12_memory_get_resource_handle (dmem);
    D3D12_RESOURCE_DESC desc;
    desc = GetDesc (resource);

    if ((d3d12_flags & GST_D3D12_FRAME_MAP_FLAG_SRV) != 0) {
      if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
        GST_WARNING ("SRV map is requested but SRV is not allowed");
        return FALSE;
      }

      if (!gst_d3d12_memory_get_shader_resource_view_heap (dmem)) {
        GST_ERROR ("Couldn't get SRV descriptor heap");
        return FALSE;
      }
    }

    if ((d3d12_flags & GST_D3D12_FRAME_MAP_FLAG_UAV) != 0) {
      if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        GST_WARNING ("UAV map is requested but UAV is not allowed");
        return FALSE;
      }

      if (!gst_d3d12_memory_get_unordered_access_view_heap (dmem)) {
        GST_ERROR ("Couldn't get UAV descriptor heap");
        return FALSE;
      }
    }

    if ((d3d12_flags & GST_D3D12_FRAME_MAP_FLAG_RTV) != 0) {
      if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0) {
        GST_WARNING ("RTV map is requested but RTV is not allowed");
        return FALSE;
      }

      if (!gst_d3d12_memory_get_render_target_view_heap (dmem)) {
        GST_ERROR ("Couldn't get RTV descriptor heap");
        return FALSE;
      }
    }
  }

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  auto srv_inc_size =
      device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  auto rtv_inc_size =
      device_handle->GetDescriptorHandleIncrementSize
      (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  guint plane_idx = 0;
  for (guint i = 0; i < num_mem; i++) {
    if (plane_idx >= G_N_ELEMENTS (frame->data)) {
      GST_ERROR ("Too many planes");
      gst_d3d12_frame_unmap (frame);
      return FALSE;
    }

    auto mem = gst_buffer_peek_memory (buffer, i);
    GstMemory *new_mem = mem;
    if (need_map) {
      mem = gst_memory_ref (mem);

      new_mem = gst_memory_make_mapped (mem, &frame->map[i], map_flags);
      if (!new_mem) {
        GST_ERROR ("Couldn't map memory %u", i);
        gst_d3d12_frame_unmap (frame);
        return FALSE;
      }

      if (mem != new_mem && is_writable) {
        gst_buffer_replace_memory_range (buffer,
            i, 1, gst_memory_ref (new_mem));
      }
    }

    auto dmem = GST_D3D12_MEMORY_CAST (new_mem);
    auto num_planes = gst_d3d12_memory_get_plane_count (dmem);
    auto resource = gst_d3d12_memory_get_resource_handle (dmem);
    D3D12_RESOURCE_DESC desc;
    desc = GetDesc (resource);

    ID3D12DescriptorHeap *srv_heap = nullptr;
    ID3D12DescriptorHeap *uav_heap = nullptr;
    ID3D12DescriptorHeap *rtv_heap = nullptr;

    if ((d3d12_flags & GST_D3D12_FRAME_MAP_FLAG_SRV) != 0)
      srv_heap = gst_d3d12_memory_get_shader_resource_view_heap (dmem);

    if ((d3d12_flags & GST_D3D12_FRAME_MAP_FLAG_UAV) != 0)
      uav_heap = gst_d3d12_memory_get_unordered_access_view_heap (dmem);

    if ((d3d12_flags & GST_D3D12_FRAME_MAP_FLAG_RTV) != 0)
      rtv_heap = gst_d3d12_memory_get_render_target_view_heap (dmem);

    CD3DX12_CPU_DESCRIPTOR_HANDLE srv_handle = { };
    if (srv_heap) {
      srv_handle =
          CD3DX12_CPU_DESCRIPTOR_HANDLE
          (GetCPUDescriptorHandleForHeapStart (srv_heap));
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE uav_handle = { };
    if (uav_heap) {
      uav_handle =
          CD3DX12_CPU_DESCRIPTOR_HANDLE
          (GetCPUDescriptorHandleForHeapStart (uav_heap));
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle = { };
    if (rtv_heap) {
      rtv_handle =
          CD3DX12_CPU_DESCRIPTOR_HANDLE
          (GetCPUDescriptorHandleForHeapStart (rtv_heap));
    }

    for (guint j = 0; j < num_planes; j++) {
      if (plane_idx >= G_N_ELEMENTS (frame->data)) {
        GST_ERROR ("Too many planes");
        gst_d3d12_frame_unmap (frame);
        return FALSE;
      }

      frame->data[plane_idx] = resource;
      gst_d3d12_memory_get_subresource_index (dmem,
          j, &frame->subresource_index[plane_idx]);
      gst_d3d12_memory_get_plane_rectangle (dmem, j,
          &frame->plane_rect[plane_idx]);

      if (srv_heap) {
        frame->srv_desc_handle[plane_idx] = srv_handle;
        srv_handle.Offset (srv_inc_size);
      }

      if (rtv_heap) {
        frame->rtv_desc_handle[plane_idx] = rtv_handle;
        rtv_handle.Offset (rtv_inc_size);
      }

      if (uav_heap) {
        frame->uav_desc_handle[plane_idx] = uav_handle;
        uav_handle.Offset (srv_inc_size);
      }

      gst_d3d12_memory_get_fence (dmem, &frame->fence[plane_idx].fence,
          &frame->fence[plane_idx].fence_value);

      plane_idx++;
    }
  }

  guint frame_flags = 0;
  if (GST_VIDEO_INFO_IS_INTERLACED (info)) {
    if (GST_VIDEO_INFO_INTERLACE_MODE (info) == GST_VIDEO_INTERLACE_MODE_MIXED) {
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED)) {
        frame_flags |= GST_VIDEO_FRAME_FLAG_INTERLACED;
      }
    } else {
      frame_flags |= GST_VIDEO_FRAME_FLAG_INTERLACED;
    }

    if (GST_VIDEO_INFO_FIELD_ORDER (info) ==
        GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST) {
      frame_flags |= GST_VIDEO_FRAME_FLAG_TFF;
    } else {
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF))
        frame_flags |= GST_VIDEO_FRAME_FLAG_TFF;
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_RFF))
        frame_flags |= GST_VIDEO_FRAME_FLAG_RFF;
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_ONEFIELD))
        frame_flags |= GST_VIDEO_FRAME_FLAG_ONEFIELD;
    }
  }

  frame->device = device;
  frame->info = *info;
  frame->buffer = buffer;
  frame->frame_flags = (GstVideoFrameFlags) frame_flags;
  frame->d3d12_flags = d3d12_flags;

  return TRUE;
}

/**
 * gst_d3d12_frame_unmap:
 * @frame: a #GstD3D12Frame
 *
 * Unmap the memory previously mapped with gst_d3d12_frame_map
 *
 * Since: 1.26
 */
void
gst_d3d12_frame_unmap (GstD3D12Frame * frame)
{
  g_return_if_fail (frame);

  for (guint i = 0; i < G_N_ELEMENTS (frame->fence); i++) {
    if (frame->fence[i].fence)
      frame->fence[i].fence->Release ();
  }

  for (guint i = 0; i < G_N_ELEMENTS (frame->map); i++) {
    auto mem = frame->map[i].memory;
    if (!mem)
      return;

    gst_memory_unmap (mem, &frame->map[i]);
    gst_memory_unref (mem);
  }
}

static void
gst_d3d12_frame_build_copy_args (GstD3D12Frame * dest,
    const GstD3D12Frame * src, guint plane,
    GstD3D12CopyTextureRegionArgs * args, D3D12_BOX * src_box)
{
  src_box->left = 0;
  src_box->top = 0;
  src_box->right = MIN (dest->plane_rect[plane].right,
      src->plane_rect[plane].right);
  src_box->bottom = MIN (dest->plane_rect[plane].bottom,
      src->plane_rect[plane].bottom);
  src_box->front = 0;
  src_box->back = 1;

  args->dst = CD3DX12_TEXTURE_COPY_LOCATION (dest->data[plane],
      dest->subresource_index[plane]);
  args->src = CD3DX12_TEXTURE_COPY_LOCATION (src->data[plane],
      src->subresource_index[plane]);
}

/**
 * gst_d3d12_frame_copy:
 * @dest: a #GstD3D12Frame
 * @src: a #GstD3D12Frame
 * @fence_value: (out): a fence value for the copy operation
 *
 * Copy the contents from @src to @dest.
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_frame_copy (GstD3D12Frame * dest, const GstD3D12Frame * src,
    guint64 * fence_value)
{
  g_return_val_if_fail (dest, FALSE);
  g_return_val_if_fail (src, FALSE);
  g_return_val_if_fail (dest->device, FALSE);
  g_return_val_if_fail (src->device, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&dest->info) ==
      GST_VIDEO_INFO_FORMAT (&src->info), FALSE);

  if (!gst_d3d12_device_is_equal (dest->device, src->device)) {
    GST_ERROR ("Cross device copy is not supported");
    return FALSE;
  }

  GstD3D12CopyTextureRegionArgs args[GST_VIDEO_MAX_PLANES] = { };
  D3D12_BOX src_box[GST_VIDEO_MAX_PLANES] = { };

  for (guint i = 0; GST_VIDEO_INFO_N_PLANES (&dest->info); i++) {
    gst_d3d12_frame_build_copy_args (dest, src, i, &args[i], &src_box[i]);
    args[i].src_box = &src_box[i];
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_device_acquire_fence_data (dest->device, &fence_data);
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (src->buffer)));

  std::vector < ID3D12Fence * >fences_to_wait;
  std::vector < guint64 > fence_values_to_wait;

  for (guint i = 0; i < G_N_ELEMENTS (dest->fence); i++) {
    if (dest->fence[i].fence) {
      fences_to_wait.push_back (dest->fence[i].fence);
      fence_values_to_wait.push_back (dest->fence[i].fence_value);
    }

    if (src->fence[i].fence) {
      fences_to_wait.push_back (src->fence[i].fence);
      fence_values_to_wait.push_back (src->fence[i].fence_value);
    }
  }

  return gst_d3d12_device_copy_texture_region (dest->device,
      GST_VIDEO_INFO_N_PLANES (&dest->info), args, fence_data,
      (guint) fences_to_wait.size (), fences_to_wait.data (),
      fence_values_to_wait.data (), D3D12_COMMAND_LIST_TYPE_DIRECT,
      fence_value);
}

/**
 * gst_video_frame_copy_plane:
 * @dest: a #GstD3D12Frame
 * @src: a #GstD3D12Frame
 * @plane: a plane
 * @fence_value: (out): a fence value for the copy operation
 *
 * Copy the plane with index @plane from @src to @dest.
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_frame_copy_plane (GstD3D12Frame * dest, const GstD3D12Frame * src,
    guint plane, guint64 * fence_value)
{
  g_return_val_if_fail (dest, FALSE);
  g_return_val_if_fail (src, FALSE);
  g_return_val_if_fail (dest->device, FALSE);
  g_return_val_if_fail (src->device, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&dest->info) ==
      GST_VIDEO_INFO_FORMAT (&src->info), FALSE);
  g_return_val_if_fail (plane < GST_VIDEO_INFO_N_PLANES (&dest->info), FALSE);

  if (!gst_d3d12_device_is_equal (dest->device, src->device)) {
    GST_ERROR ("Cross device copy is not supported");
    return FALSE;
  }

  GstD3D12CopyTextureRegionArgs args = { };
  D3D12_BOX src_box = { };

  gst_d3d12_frame_build_copy_args (dest, src, plane, &args, &src_box);
  args.src_box = &src_box;

  GstD3D12FenceData *fence_data;
  gst_d3d12_device_acquire_fence_data (dest->device, &fence_data);
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (src->buffer)));

  auto cq = gst_d3d12_device_get_command_queue (src->device,
      D3D12_COMMAND_LIST_TYPE_DIRECT);
  auto cq_handle = gst_d3d12_command_queue_get_handle (cq);

  if (src->fence[plane].fence)
    cq_handle->Wait (src->fence[plane].fence, src->fence[plane].fence_value);

  if (dest->fence[plane].fence)
    cq_handle->Wait (dest->fence[plane].fence, dest->fence[plane].fence_value);

  return gst_d3d12_device_copy_texture_region (dest->device, 1, &args,
      fence_data, 0, nullptr, nullptr, D3D12_COMMAND_LIST_TYPE_DIRECT,
      fence_value);
}

/**
 * gst_d3d12_frame_fence_gpu_wait:
 * @frame: a #GstD3D12Frame
 * @queue: a GstD3D12CommandQueue
 *
 * Executes ID3D12CommandQueue::Wait() if @frame has different fence object
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_frame_fence_gpu_wait (const GstD3D12Frame * frame,
    GstD3D12CommandQueue * queue)
{
  g_return_val_if_fail (frame, FALSE);
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (frame->device), FALSE);
  g_return_val_if_fail (GST_IS_D3D12_COMMAND_QUEUE (queue), FALSE);

  ID3D12Fence *last_fence = nullptr;
  guint64 last_fence_val = 0;
  auto fence = gst_d3d12_command_queue_get_fence_handle (queue);

  for (guint i = 0; i < G_N_ELEMENTS (frame->fence); i++) {
    if (frame->fence[i].fence && frame->fence[i].fence != fence) {
      if (frame->fence[i].fence == last_fence &&
          frame->fence[i].fence_value <= last_fence_val) {
        continue;
      }
      last_fence = frame->fence[i].fence;
      last_fence_val = frame->fence[i].fence_value;

      auto hr = gst_d3d12_command_queue_execute_wait (queue,
          frame->fence[i].fence, frame->fence[i].fence_value);
      if (!gst_d3d12_result (hr, frame->device))
        return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_d3d12_frame_fence_cpu_wait:
 * @frame: a #GstD3D12Frame
 *
 * Waits for external fence objects
 *
 * Returns: %TRUE on success.
 *
 * Since: 1.26
 */
gboolean
gst_d3d12_frame_fence_cpu_wait (const GstD3D12Frame * frame)
{
  g_return_val_if_fail (frame, FALSE);
  g_return_val_if_fail (GST_IS_D3D12_DEVICE (frame->device), FALSE);

  ID3D12Fence *last_fence = nullptr;
  guint64 last_fence_val = 0;
  std::vector < ID3D12Fence * >fences;
  std::vector < UINT64 > fence_vals;

  for (guint i = 0; i < G_N_ELEMENTS (frame->fence); i++) {
    if (frame->fence[i].fence) {
      if (frame->fence[i].fence == last_fence &&
          frame->fence[i].fence_value <= last_fence_val) {
        continue;
      }
      last_fence = frame->fence[i].fence;
      last_fence_val = frame->fence[i].fence_value;

      fences.push_back (frame->fence[i].fence);
      fence_vals.push_back (frame->fence[i].fence_value);
    }
  }

  if (fences.empty ())
    return TRUE;

  ComPtr < ID3D12Device1 > device1;
  auto device = gst_d3d12_device_get_device_handle (frame->device);
  auto hr = device->QueryInterface (IID_PPV_ARGS (&device1));

  if (SUCCEEDED (hr)) {
    hr = device1->SetEventOnMultipleFenceCompletion (fences.data (),
        fence_vals.data (), fences.size (), D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL,
        nullptr);
  } else {
    for (size_t i = 0; i < fences.size (); i++) {
      hr = fences[i]->SetEventOnCompletion (fence_vals[i], nullptr);
      if (FAILED (hr))
        break;
    }
  }

  return gst_d3d12_result (hr, frame->device);
}

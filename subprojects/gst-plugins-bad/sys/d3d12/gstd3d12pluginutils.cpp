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

#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <vector>

#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>

/* *INDENT-OFF* */
using namespace DirectX;
/* *INDENT-ON* */

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12pluginutils", 0, "d3d12pluginutils");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

GType
gst_d3d12_sampling_method_get_type (void)
{
  static GType type = 0;
  static const GEnumValue methods[] = {
    {GST_D3D12_SAMPLING_METHOD_NEAREST,
        "Nearest Neighbour", "nearest-neighbour"},
    {GST_D3D12_SAMPLING_METHOD_BILINEAR,
        "Bilinear", "bilinear"},
    {GST_D3D12_SAMPLING_METHOD_LINEAR_MINIFICATION,
        "Linear minification, point magnification", "linear-minification"},
    {GST_D3D12_SAMPLING_METHOD_ANISOTROPIC, "Anisotropic", "anisotropic"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12SamplingMethod", methods);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

struct SamplingMethodMap
{
  GstD3D12SamplingMethod method;
  D3D12_FILTER filter;
};

static const SamplingMethodMap sampling_method_map[] = {
  {GST_D3D12_SAMPLING_METHOD_NEAREST, D3D12_FILTER_MIN_MAG_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_BILINEAR, D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_LINEAR_MINIFICATION,
      D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_ANISOTROPIC, D3D12_FILTER_ANISOTROPIC},
};

D3D12_FILTER
gst_d3d12_sampling_method_to_native (GstD3D12SamplingMethod method)
{
  for (guint i = 0; i < G_N_ELEMENTS (sampling_method_map); i++) {
    if (sampling_method_map[i].method == method)
      return sampling_method_map[i].filter;
  }

  return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

GType
gst_d3d12_msaa_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue msaa_mode[] = {
    {GST_D3D12_MSAA_DISABLED, "Disabled", "disabled"},
    {GST_D3D12_MSAA_2X, "2x MSAA", "2x"},
    {GST_D3D12_MSAA_4X, "4x MSAA", "4x"},
    {GST_D3D12_MSAA_8X, "8x MSAA", "8x"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12MSAAMode", msaa_mode);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

void
gst_d3d12_buffer_after_write (GstBuffer * buffer, guint64 fence_value)
{
  for (guint i = 0; i < gst_buffer_n_memory (buffer); i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    g_return_if_fail (gst_is_d3d12_memory (mem));

    auto dmem = GST_D3D12_MEMORY_CAST (mem);
    dmem->fence_value = fence_value;
    GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
    GST_MINI_OBJECT_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);
  }
}

gboolean
gst_d3d12_need_transform (gfloat rotation_x, gfloat rotation_y,
    gfloat rotation_z, gfloat scale_x, gfloat scale_y)
{
  const gfloat min_diff = 0.00001f;

  if (!XMScalarNearEqual (rotation_x, 0.0f, min_diff) ||
      !XMScalarNearEqual (rotation_y, 0.0f, min_diff) ||
      !XMScalarNearEqual (rotation_z, 0.0f, min_diff) ||
      !XMScalarNearEqual (scale_x, 1.0f, min_diff) ||
      !XMScalarNearEqual (scale_y, 1.0f, min_diff)) {
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_d3d12_buffer_copy_into_fallback (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  GstVideoFrame in_frame, out_frame;
  gboolean ret;

  if (!gst_video_frame_map (&in_frame, (GstVideoInfo *) info, src,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR ("Couldn't map src frame");
    return FALSE;
  }

  if (!gst_video_frame_map (&out_frame, (GstVideoInfo *) info, dst,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR ("Couldn't map dst frame");
    gst_video_frame_unmap (&in_frame);
    return FALSE;
  }

  ret = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&in_frame);
  gst_video_frame_unmap (&out_frame);

  return ret;
}

static gboolean
gst_is_d3d12_buffer (GstBuffer * buffer)
{
  auto size = gst_buffer_n_memory (buffer);
  if (size == 0)
    return FALSE;

  for (guint i = 0; i < size; i++) {
    auto mem = gst_buffer_peek_memory (buffer, i);
    if (!gst_is_d3d12_memory (mem))
      return FALSE;
  }

  return TRUE;
}

gboolean
gst_d3d12_buffer_copy_into (GstBuffer * dst, GstBuffer * src,
    const GstVideoInfo * info)
{
  g_return_val_if_fail (GST_IS_BUFFER (dst), FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (src), FALSE);
  g_return_val_if_fail (info, FALSE);

  auto num_mem = gst_buffer_n_memory (dst);

  if (gst_buffer_n_memory (src) != num_mem) {
    GST_LOG ("different memory layout, perform fallback copy");
    return gst_d3d12_buffer_copy_into_fallback (dst, src, info);
  }

  if (!gst_is_d3d12_buffer (dst) || !gst_is_d3d12_buffer (src)) {
    GST_LOG ("non-d3d12 memory, perform fallback copy");
    return gst_d3d12_buffer_copy_into_fallback (dst, src, info);
  }

  std::vector < GstD3D12CopyTextureRegionArgs > copy_args;
  D3D12_BOX src_box[4];
  guint resource_idx = 0;
  GstD3D12Device *device = nullptr;

  for (guint i = 0; i < num_mem; i++) {
    auto dst_mem = gst_buffer_peek_memory (dst, i);
    auto src_mem = gst_buffer_peek_memory (src, i);

    auto dst_dmem = GST_D3D12_MEMORY_CAST (dst_mem);
    auto src_dmem = GST_D3D12_MEMORY_CAST (src_mem);

    device = dst_dmem->device;
    if (device != src_dmem->device) {
      GST_LOG ("different device, perform fallback copy");
      return gst_d3d12_buffer_copy_into_fallback (dst, src, info);
    }

    /* Map memory to execute pending upload and wait for external fence */
    GstMapInfo map_info;
    if (!gst_memory_map (src_mem, &map_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_D3D12))) {
      GST_ERROR ("Cannot map src d3d12 memory");
      return FALSE;
    }
    gst_memory_unmap (src_mem, &map_info);

    if (!gst_memory_map (dst_mem, &map_info,
            (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D12))) {
      GST_ERROR ("Cannot map dst d3d12 memory");
      return FALSE;
    }
    gst_memory_unmap (dst_mem, &map_info);

    auto num_planes = gst_d3d12_memory_get_plane_count (src_dmem);

    for (guint j = 0; j < num_planes; j++) {
      GstD3D12CopyTextureRegionArgs args = { };
      D3D12_RECT src_rect;
      D3D12_RECT dst_rect;

      gst_d3d12_memory_get_plane_rectangle (src_dmem, j, &src_rect);
      gst_d3d12_memory_get_plane_rectangle (dst_dmem, j, &dst_rect);

      auto src_handle = gst_d3d12_memory_get_resource_handle (src_dmem);
      auto dst_handle = gst_d3d12_memory_get_resource_handle (dst_dmem);

      guint src_subresource;
      guint dst_subresource;
      gst_d3d12_memory_get_subresource_index (src_dmem, j, &src_subresource);
      gst_d3d12_memory_get_subresource_index (dst_dmem, j, &dst_subresource);

      args.src = CD3DX12_TEXTURE_COPY_LOCATION (src_handle, src_subresource);
      args.dst = CD3DX12_TEXTURE_COPY_LOCATION (dst_handle, dst_subresource);

      src_box[resource_idx].front = 0;
      src_box[resource_idx].back = 1;
      src_box[resource_idx].left = 0;
      src_box[resource_idx].top = 0;
      src_box[resource_idx].right = MIN (src_rect.right, dst_rect.right);
      src_box[resource_idx].bottom = MIN (src_rect.bottom, dst_rect.bottom);

      args.src_box = &src_box[resource_idx];
      resource_idx++;
      copy_args.push_back (args);
    }
  }

  g_assert (device);

  guint64 fence_val;
  if (!gst_d3d12_device_copy_texture_region (device, copy_args.size (),
          copy_args.data (), D3D12_COMMAND_LIST_TYPE_DIRECT, &fence_val)) {
    GST_ERROR ("Couldn't copy texture");
    return FALSE;
  }

  gst_d3d12_buffer_after_write (dst, fence_val);

  return TRUE;
}

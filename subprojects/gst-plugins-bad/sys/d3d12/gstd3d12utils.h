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

#pragma once

#include <gst/gst.h>
#include "gstd3d12_fwd.h"
#include "gstd3d12device.h"

G_BEGIN_DECLS

gboolean  gst_d3d12_handle_set_context (GstElement * element,
                                        GstContext * context,
                                        gint adapter_index,
                                        GstD3D12Device ** device);

gboolean  gst_d3d12_handle_set_context_for_adapter_luid (GstElement * element,
                                                         GstContext * context,
                                                         gint64 adapter_luid,
                                                         GstD3D12Device ** device);

gboolean  gst_d3d12_handle_context_query (GstElement * element,
                                          GstQuery * query,
                                          GstD3D12Device * device);

gboolean  gst_d3d12_ensure_element_data  (GstElement * element,
                                          gint adapter_index,
                                          GstD3D12Device ** device);

gboolean  gst_d3d12_ensure_element_data_for_adapter_luid (GstElement * element,
                                                          gint64 adapter_luid,
                                                          GstD3D12Device ** device);

gint64    gst_d3d12_luid_to_int64 (const LUID * luid);

GstContext * gst_d3d12_context_new (GstD3D12Device * device);

gboolean _gst_d3d12_result (HRESULT hr,
                            GstD3D12Device * device,
                            GstDebugCategory * cat,
                            const gchar * file,
                            const gchar * function,
                            gint line,
                            GstDebugLevel level);

/**
 * gst_d3d12_result:
 * @result: HRESULT D3D12 API return code
 * @device: (nullable): Associated #GstD3D12Device
 *
 * Returns: %TRUE if D3D12 API call result is SUCCESS
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_d3d12_result(result,device) \
    _gst_d3d12_result (result, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#else
#define gst_d3d12_result(result,device) \
    _gst_d3d12_result (result, device, NULL, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#endif

guint   gst_d3d12_calculate_subresource (guint mip_slice,
                                         guint array_slice,
                                         guint plane_slice,
                                         guint mip_level,
                                         guint array_size);

#define GST_D3D12_CLEAR_COM(obj) G_STMT_START { \
    if (obj) { \
      (obj)->Release (); \
      (obj) = NULL; \
    } \
  } G_STMT_END

G_END_DECLS

struct CD3D12_HEAP_PROPERTIES : public D3D12_HEAP_PROPERTIES
{
  CD3D12_HEAP_PROPERTIES (
      D3D12_HEAP_TYPE type,
      D3D12_CPU_PAGE_PROPERTY cpu_page_property = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      D3D12_MEMORY_POOL memory_pool_preference = D3D12_MEMORY_POOL_UNKNOWN,
      UINT creation_node_mask = 1,
      UINT visible_node_mask = 1)
  {
    Type = type;
    CPUPageProperty = cpu_page_property;
    MemoryPoolPreference = memory_pool_preference;
    CreationNodeMask = creation_node_mask;
    VisibleNodeMask = visible_node_mask;
  }
};

struct CD3D12_RESOURCE_DESC : public D3D12_RESOURCE_DESC
{
  CD3D12_RESOURCE_DESC(
      D3D12_RESOURCE_DIMENSION dimension,
      UINT64 alignment,
      UINT64 width,
      UINT height,
      UINT16 depth_or_array_size,
      UINT16 mip_levels,
      DXGI_FORMAT format,
      UINT sample_count,
      UINT sample_quality,
      D3D12_TEXTURE_LAYOUT layout,
      D3D12_RESOURCE_FLAGS flags)
  {
    Dimension = dimension;
    Alignment = alignment;
    Width = width;
    Height = height;
    DepthOrArraySize = depth_or_array_size;
    MipLevels = mip_levels;
    Format = format;
    SampleDesc.Count = sample_count;
    SampleDesc.Quality = sample_quality;
    Layout = layout;
    Flags = flags;
  }

  static inline CD3D12_RESOURCE_DESC Buffer (
      UINT64 width,
      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
      UINT64 alignment = 0)
  {
    return CD3D12_RESOURCE_DESC (D3D12_RESOURCE_DIMENSION_BUFFER, alignment,
        width, 1, 1, 1, DXGI_FORMAT_UNKNOWN, 1, 0,
        D3D12_TEXTURE_LAYOUT_ROW_MAJOR, flags);
  }

  static inline CD3D12_RESOURCE_DESC Tex2D (
      DXGI_FORMAT format,
      UINT64 width,
      UINT height,
      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
      UINT16 array_size = 1,
      UINT16 mip_levels = 1,
      UINT sample_count = 1,
      UINT sample_quality = 0,
      D3D12_TEXTURE_LAYOUT layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
      UINT64 alignment = 0)
  {
    return CD3D12_RESOURCE_DESC(D3D12_RESOURCE_DIMENSION_TEXTURE2D, alignment,
        width, height, array_size, mip_levels, format, sample_count,
        sample_quality, layout, flags);
  }
};

struct CD3D12_RESOURCE_BARRIER : public D3D12_RESOURCE_BARRIER
{
  static inline CD3D12_RESOURCE_BARRIER Transition (
      ID3D12Resource * resource,
      D3D12_RESOURCE_STATES before,
      D3D12_RESOURCE_STATES after,
      UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
      D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
  {
    CD3D12_RESOURCE_BARRIER result;
    D3D12_RESOURCE_BARRIER &barrier = result;
    result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    result.Flags = flags;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = subresource;

    return result;
  }
};

struct CD3D12_TEXTURE_COPY_LOCATION : public D3D12_TEXTURE_COPY_LOCATION
{
  CD3D12_TEXTURE_COPY_LOCATION (ID3D12Resource * resource)
  {
    pResource = resource;
    Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    PlacedFootprint = {};
  }

  CD3D12_TEXTURE_COPY_LOCATION (
      ID3D12Resource * resource,
      D3D12_PLACED_SUBRESOURCE_FOOTPRINT const & foot_print)
  {
    pResource = resource;
    Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    PlacedFootprint = foot_print;
  }

  CD3D12_TEXTURE_COPY_LOCATION (
      ID3D12Resource * resource,
      UINT subresource)
  {
    pResource = resource;
    Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    PlacedFootprint = {};
    SubresourceIndex = subresource;
  }
};

#include <mutex>

class GstD3D12CSLockGuard
{
public:
  explicit GstD3D12CSLockGuard(CRITICAL_SECTION * cs) : cs_ (cs)
  {
    EnterCriticalSection (cs_);
  }

  ~GstD3D12CSLockGuard()
  {
    LeaveCriticalSection (cs_);
  }

  GstD3D12CSLockGuard(const GstD3D12CSLockGuard&) = delete;
  GstD3D12CSLockGuard& operator=(const GstD3D12CSLockGuard&) = delete;

private:
  CRITICAL_SECTION *cs_;
};

#define GST_D3D12_CALL_ONCE_BEGIN \
    static std::once_flag __once_flag; \
    std::call_once (__once_flag, [&]()

#define GST_D3D12_CALL_ONCE_END )

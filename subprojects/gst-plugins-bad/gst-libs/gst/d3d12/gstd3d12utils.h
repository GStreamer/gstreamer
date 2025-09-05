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
#include <gst/d3d12/gstd3d12_fwd.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

GST_D3D12_API
gboolean  gst_d3d12_handle_set_context (GstElement * element,
                                        GstContext * context,
                                        gint adapter_index,
                                        GstD3D12Device ** device);

GST_D3D12_API
gboolean  gst_d3d12_handle_set_context_for_adapter_luid (GstElement * element,
                                                         GstContext * context,
                                                         gint64 adapter_luid,
                                                         GstD3D12Device ** device);

GST_D3D12_API
gboolean  gst_d3d12_handle_context_query (GstElement * element,
                                          GstQuery * query,
                                          GstD3D12Device * device);

GST_D3D12_API
gboolean  gst_d3d12_ensure_element_data  (GstElement * element,
                                          gint adapter_index,
                                          GstD3D12Device ** device);

GST_D3D12_API
gboolean  gst_d3d12_ensure_element_data_for_adapter_luid (GstElement * element,
                                                          gint64 adapter_luid,
                                                          GstD3D12Device ** device);

GST_D3D12_API
gint64    gst_d3d12_luid_to_int64 (const LUID * luid);

GST_D3D12_API
GstContext * gst_d3d12_context_new (GstD3D12Device * device);

GST_D3D12_API
gint64    gst_d3d12_create_user_token (void);

GST_D3D12_API
gboolean  gst_d3d12_buffer_copy_into (GstBuffer * dest,
                                      GstBuffer * src,
                                      const GstVideoInfo * info);

GST_D3D12_API
gboolean  gst_d3d12_buffer_copy_into_full (GstBuffer * dest,
                                           GstBuffer * src,
                                           const GstVideoInfo * info,
                                           D3D12_COMMAND_LIST_TYPE queue_type);

GST_D3D12_API
void      gst_d3d12_buffer_set_fence (GstBuffer * buffer,
                                      ID3D12Fence * fence,
                                      guint64 fence_value,
                                      gboolean wait);

GST_D3D12_API
gboolean  gst_d3d12_get_copyable_footprints (GstD3D12Device * device,
                                             const GstVideoInfo * info,
                                             D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES],
                                             guint64 * size);


GST_D3D12_API
void      gst_d3d12_flush_all_devices (void);

GST_D3D12_API
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
 *
 * Since: 1.26
 */
#ifndef GST_DISABLE_GST_DEBUG
#define gst_d3d12_result(result,device) \
    _gst_d3d12_result (result, device, GST_CAT_DEFAULT, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#else
#define gst_d3d12_result(result,device) \
    _gst_d3d12_result (result, device, NULL, __FILE__, GST_FUNCTION, __LINE__, GST_LEVEL_ERROR)
#endif

G_END_DECLS


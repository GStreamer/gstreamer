/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_UTILS_H__
#define __GST_D3D11_UTILS_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

GstVideoFormat  gst_d3d11_dxgi_format_to_gst        (DXGI_FORMAT format);

DXGI_FORMAT     gst_d3d11_dxgi_format_from_gst      (GstVideoFormat format);

GstCaps *       gst_d3d11_device_get_supported_caps (GstD3D11Device * device,
                                                     D3D11_FORMAT_SUPPORT flags);

gboolean        gst_d3d11_calculate_buffer_size     (GstVideoInfo * info,
                                                     guint pitch,
                                                     gsize offset[GST_VIDEO_MAX_PLANES],
                                                     gint stride[GST_VIDEO_MAX_PLANES],
                                                     gsize *size);

gboolean        gst_d3d11_handle_set_context        (GstElement * element,
                                                     GstContext * context,
                                                     GstD3D11Device ** device);

gboolean        gst_d3d11_handle_context_query      (GstElement * element,
                                                     GstQuery * query,
                                                     GstD3D11Device * device);

gboolean        gst_d3d11_ensure_element_data       (GstElement * element,
                                                     GstD3D11Device ** device,
                                                     gint preferred_adapter);

G_END_DECLS

#endif /* __GST_D3D11_UTILS_H__ */

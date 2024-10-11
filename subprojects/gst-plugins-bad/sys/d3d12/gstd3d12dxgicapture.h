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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstd3d12screencapture.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D12_DXGI_CAPTURE (gst_d3d12_dxgi_capture_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12DxgiCapture, gst_d3d12_dxgi_capture,
    GST, D3D12_DXGI_CAPTURE, GstD3D12ScreenCapture);

GstD3D12ScreenCapture * gst_d3d12_dxgi_capture_new (GstD3D12Device * device,
                                                    HMONITOR monitor_handle);

GstFlowReturn gst_d3d12_dxgi_capture_do_capture (GstD3D12DxgiCapture * capture,
                                                 GstBuffer * buffer,
                                                 const D3D12_BOX * crop_box,
                                                 gboolean draw_mouse);

G_END_DECLS


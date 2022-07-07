/*
 * GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d11/gstd3d11.h>

G_BEGIN_DECLS

#define GST_D3D11_SCREEN_CAPTURE_FLOW_EXPECTED_ERROR GST_FLOW_CUSTOM_SUCCESS
#define GST_D3D11_SCREEN_CAPTURE_FLOW_SIZE_CHANGED GST_FLOW_CUSTOM_SUCCESS_1
#define GST_D3D11_SCREEN_CAPTURE_FLOW_UNSUPPORTED GST_FLOW_CUSTOM_ERROR

#define GST_TYPE_D3D11_SCREEN_CAPTURE (gst_d3d11_screen_capture_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11ScreenCapture, gst_d3d11_screen_capture,
    GST, D3D11_SCREEN_CAPTURE, GstObject);

GstD3D11ScreenCapture * gst_d3d11_screen_capture_new (GstD3D11Device * device,
                                                      HMONITOR monitor_handle);

GstFlowReturn   gst_d3d11_screen_capture_prepare (GstD3D11ScreenCapture * capture);

gboolean        gst_d3d11_screen_capture_get_size (GstD3D11ScreenCapture * capture,
                                                   guint * width,
                                                   guint * height);

gboolean        gst_d3d11_screen_capture_get_colorimetry (GstD3D11ScreenCapture * capture,
                                                          GstVideoColorimetry * colorimetry);

GstFlowReturn   gst_d3d11_screen_capture_do_capture (GstD3D11ScreenCapture * capture,
                                                     GstD3D11Device * device,
                                                     ID3D11Texture2D * texture,
                                                     ID3D11RenderTargetView * rtv,
                                                     ID3D11VertexShader * vs,
                                                     ID3D11PixelShader * ps,
                                                     ID3D11InputLayout * layout,
                                                     ID3D11SamplerState * sampler,
                                                     ID3D11BlendState * blend,
                                                     D3D11_BOX * crop_box,
                                                     gboolean draw_mouse);

HRESULT         gst_d3d11_screen_capture_find_output_for_monitor (HMONITOR monitor,
                                                                  IDXGIAdapter1 ** adapter,
                                                                  IDXGIOutput ** output);

HRESULT         gst_d3d11_screen_capture_find_primary_monitor (HMONITOR * monitor,
                                                               IDXGIAdapter1 ** adapter,
                                                               IDXGIOutput ** output);

HRESULT         gst_d3d11_screen_capture_find_nth_monitor (guint index,
                                                           HMONITOR * monitor,
                                                           IDXGIAdapter1 ** adapter,
                                                           IDXGIOutput ** output);

G_END_DECLS


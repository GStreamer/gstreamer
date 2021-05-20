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

#ifndef __GST_D3D11_DESKTOP_DUP_H__
#define __GST_D3D11_DESKTOP_DUP_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11.h>

G_BEGIN_DECLS

#define GST_D3D11_DESKTOP_DUP_FLOW_EXPECTED_ERROR GST_FLOW_CUSTOM_SUCCESS
#define GST_D3D11_DESKTOP_DUP_FLOW_SIZE_CHANGED GST_FLOW_CUSTOM_SUCCESS_1
#define GST_D3D11_DESKTOP_DUP_FLOW_UNSUPPORTED GST_FLOW_CUSTOM_ERROR

#define GST_TYPE_D3D11_DESKTOP_DUP (gst_d3d11_desktop_dup_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11DesktopDup, gst_d3d11_desktop_dup,
    GST, D3D11_DESKTOP_DUP, GstObject);

GstD3D11DesktopDup *  gst_d3d11_desktop_dup_new (GstD3D11Device * device,
                                                 gint output_index);

GstFlowReturn   gst_d3d11_desktop_dup_prepare (GstD3D11DesktopDup * desktop);

gboolean        gst_d3d11_desktop_dup_get_size (GstD3D11DesktopDup * desktop,
                                                guint * width,
                                                guint * height);

GstFlowReturn   gst_d3d11_desktop_dup_capture (GstD3D11DesktopDup * desktop,
                                               ID3D11Texture2D * texture,
                                               ID3D11RenderTargetView *rtv,
                                               gboolean draw_mouse);

G_END_DECLS

#endif /* __GST_D3D11_DESKTOP_DUP_H__ */

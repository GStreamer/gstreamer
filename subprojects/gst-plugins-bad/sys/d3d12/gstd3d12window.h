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
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>
#include "gstd3d12pluginutils.h"
#include "gstd3d12window-swapchain-resource.h"
#include <string>

G_BEGIN_DECLS

#define GST_D3D12_WINDOW_FLOW_CLOSED GST_FLOW_CUSTOM_ERROR

#define GST_TYPE_D3D12_WINDOW (gst_d3d12_window_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12Window, gst_d3d12_window,
    GST, D3D12_WINDOW, GstObject)

enum GstD3D12WindowOverlayMode
{
  GST_D3D12_WINDOW_OVERLAY_NONE = 0,
  GST_D3D12_WINDOW_OVERLAY_D3D12 = 0x1,
  GST_D3D12_WINDOW_OVERLAY_D3D11 = 0x3,
  GST_D3D12_WINDOW_OVERLAY_D2D = 0x7,
};

DEFINE_ENUM_FLAG_OPERATORS (GstD3D12WindowOverlayMode);

#define GST_TYPE_D3D12_WINDOW_OVERLAY_MODE (gst_d3d12_window_overlay_mode_get_type())
GType gst_d3d12_window_overlay_mode_get_type (void);

GstD3D12Window * gst_d3d12_window_new (void);

void             gst_d3d12_window_invalidate (GstD3D12Window * window);

guintptr         gst_d3d12_window_get_window_handle (GstD3D12Window * window);

gboolean         gst_d3d12_window_is_closed (GstD3D12Window * window);

GstFlowReturn    gst_d3d12_window_open (GstD3D12Window * window,
                                        GstD3D12Device * device,
                                        guint display_width,
                                        guint display_height,
                                        HWND parent_hwnd,
                                        gboolean direct_swapchain);

GstFlowReturn    gst_d3d12_window_prepare (GstD3D12Window * window,
                                           GstD3D12Device * device,
                                           guint display_width,
                                           guint display_height,
                                           GstCaps * caps,
                                           GstStructure * config,
                                           DXGI_FORMAT display_format);

void             gst_d3d12_window_unprepare (GstD3D12Window * window);

void             gst_d3d12_window_unlock    (GstD3D12Window * window);

void             gst_d3d12_window_unlock_stop (GstD3D12Window * window);

void             gst_d3d12_window_expose      (GstD3D12Window * window);

GstFlowReturn    gst_d3d12_window_set_buffer (GstD3D12Window * window,
                                              GstBuffer * buffer);

GstFlowReturn    gst_d3d12_window_render     (GstD3D12Window * window,
                                              SwapChainResource * resource,
                                              GstBuffer * buffer,
                                              bool is_first,
                                              RECT & output_rect);

GstFlowReturn    gst_d3d12_window_present (GstD3D12Window * window);

void             gst_d3d12_window_set_render_rect (GstD3D12Window * window,
                                                   const GstVideoRectangle * rect);

void             gst_d3d12_window_get_render_rect (GstD3D12Window * window,
                                                   GstVideoRectangle * rect);

void             gst_d3d12_window_set_force_aspect_ratio (GstD3D12Window * window,
                                                          gboolean force_aspect_ratio);

void             gst_d3d12_window_set_enable_navigation_events (GstD3D12Window * window,
                                                                gboolean enable);

gboolean         gst_d3d12_window_get_navigation_events_enabled (GstD3D12Window * window);

void             gst_d3d12_window_set_orientation (GstD3D12Window * window,
                                                   gboolean immediate,
                                                   GstVideoOrientationMethod orientation,
                                                   gfloat fov,
                                                   gboolean ortho,
                                                   gfloat rotation_x,
                                                   gfloat rotation_y,
                                                   gfloat rotation_z,
                                                   gfloat scale_x,
                                                   gfloat scale_y);

void             gst_d3d12_window_set_title (GstD3D12Window * window,
                                             const gchar * title);

void             gst_d3d12_window_enable_fullscreen_on_alt_enter (GstD3D12Window * window,
                                                                  gboolean enable);

void             gst_d3d12_window_set_fullscreen (GstD3D12Window * window,
                                                  gboolean enable);

void             gst_d3d12_window_set_msaa (GstD3D12Window * window,
                                            GstD3D12MSAAMode msaa);

void             gst_d3d12_window_get_msaa (GstD3D12Window * window,
                                            GstD3D12MSAAMode & msaa);

void             gst_d3d12_window_set_overlay_mode (GstD3D12Window * window,
                                                    GstD3D12WindowOverlayMode mode);

void             gst_d3d12_window_on_key_event (GstD3D12Window * window,
                                                const gchar * event,
                                                const gchar * name);

void             gst_d3d12_window_on_mouse_event (GstD3D12Window * window,
                                                  const gchar * event,
                                                  gint button,
                                                  double xpos,
                                                  double ypos,
                                                  guint modifier);

void             gst_d3d12_window_get_create_params (GstD3D12Window * window,
                                                     std::wstring & title,
                                                     GstVideoRectangle * rect,
                                                     int & display_width,
                                                     int & display_height,
                                                     GstVideoOrientationMethod & orientation);

void             gst_d3d12_window_get_mouse_pos_info (GstD3D12Window * window,
                                                      GstVideoRectangle * out_rect,
                                                      int & input_width,
                                                      int & input_height,
                                                      GstVideoOrientationMethod & orientation);

G_END_DECLS


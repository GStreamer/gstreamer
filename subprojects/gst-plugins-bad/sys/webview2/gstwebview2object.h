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
#include <gst/d3d11/gstd3d11.h>
#include <d3d11_4.h>
#include <string>

G_BEGIN_DECLS

#define GST_TYPE_WEBVIEW2_OBJECT (gst_webview2_object_get_type())
G_DECLARE_FINAL_TYPE (GstWebView2Object, gst_webview2_object,
    GST, WEBVIEW2_OBJECT, GstObject);

GstWebView2Object * gst_webview2_object_new          (GstD3D11Device * device,
                                                      const std::string & user_data_folder);

gboolean            gst_webview2_object_set_location (GstWebView2Object * client,
                                                      const std::string & location,
                                                      const std::string & script);

gboolean            gst_webview_object_update_size   (GstWebView2Object * client,
                                                      guint width,
                                                      guint height);

void                gst_webview2_object_send_event   (GstWebView2Object * client,
                                                      GstEvent * event);

GstFlowReturn       gst_webview2_object_do_capture   (GstWebView2Object * client,
                                                      ID3D11Texture2D * texture,
                                                      ID3D11DeviceContext4 * context4,
                                                      ID3D11Fence * fence,
                                                      guint64 * fence_val,
                                                      gboolean need_signal);

void                gst_webview2_object_set_flushing (GstWebView2Object * client,
                                                      gboolean flushing);

G_END_DECLS


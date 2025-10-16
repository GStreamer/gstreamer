/* GStreamer
 *
 * GStreamer Vulkan plugins utilities
 * Copyright (C) 2025 Igalia, S.L.
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

#include <gst/vulkan/vulkan.h>

G_BEGIN_DECLS

#ifndef GST_DISABLE_GST_DEBUG
#define gst_vulkan_buffer_peek_plane_memory(buffer, vinfo, plane)       \
  _gst_vulkan_buffer_peek_plane_memory(buffer, vinfo, plane, GST_CAT_DEFAULT)
#else
#define gst_vulkan_buffer_peek_plane_memory(buffer, vinfo, plane)       \
  _gst_vulkan_buffer_peek_plane_memory(buffer, vinfo, plane, NULL)
#endif

GstMemory *             _gst_vulkan_buffer_peek_plane_memory    (GstBuffer * buffer,
                                                                 const GstVideoInfo * vinfo,
                                                                 gint plane,
                                                                 GstDebugCategory * cat);
G_END_DECLS

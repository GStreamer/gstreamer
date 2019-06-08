/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifndef __GST_VULKAN_API_H__
#define __GST_VULKAN_API_H__

#define VK_PROTOTYPES

#include <gst/vulkan/gstvkconfig.h>
#include <gst/vulkan/vulkan-prelude.h>
#include <gst/vulkan/vulkan_fwd.h>
#include <gst/vulkan/vulkan-enumtypes.h>

/* Need these defined to have access to winsys functions before including vulkan.h */
#if GST_VULKAN_HAVE_WINDOW_XCB
#ifndef VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XCB_KHR
#endif
#endif

#if GST_VULKAN_HAVE_WINDOW_WAYLAND
#ifndef VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif

#if GST_VULKAN_HAVE_WINDOW_COCOA
#ifndef VK_USE_PLATFORM_MACOS_MVK
#define VK_USE_PLATFORM_MACOS_MVK
#endif
#endif

#if GST_VULKAN_HAVE_WINDOW_IOS
#ifndef VK_USE_PLATFORM_IOS_MVK
#define VK_USE_PLATFORM_IOS_MVK
#endif
#endif

#if GST_VULKAN_HAVE_WINDOW_WIN32
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif

#include <vulkan/vulkan.h>

#endif /* __GST_VULKAN_API_H__ */

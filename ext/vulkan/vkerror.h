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

#ifndef _VK_ERROR_H_
#define _VK_ERROR_H_

#include <gst/gst.h>
#include <vulkan/vulkan.h>

G_BEGIN_DECLS

#define GST_VULKAN_ERROR (gst_vulkan_error_quark ())
GQuark gst_vulkan_error_quark (void);

/* only fills error iff error != NULL and result < 0 */
VkResult gst_vulkan_error_to_g_error (VkResult result, GError ** error, const char * format, ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS

#endif /* _VK_INSTANCE_H_ */

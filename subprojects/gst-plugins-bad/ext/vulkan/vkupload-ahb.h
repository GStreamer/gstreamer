/*
 * GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#include "vkupload.h"

#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#endif

#if defined(__ANDROID__) && defined(VK_ANDROID_external_memory_android_hardware_buffer)
#define GST_VULKAN_UPLOAD_HAVE_AHB 1
#else
#define GST_VULKAN_UPLOAD_HAVE_AHB 0
#endif

G_BEGIN_DECLS

#if GST_VULKAN_UPLOAD_HAVE_AHB
extern const struct UploadMethod gst_vulkan_upload_ahb_method;
void gst_vulkan_upload_ahb_request_device_extensions (GstVulkanUpload * upload);
#else
static inline void
gst_vulkan_upload_ahb_request_device_extensions (GstVulkanUpload * upload)
{
}
#endif

G_END_DECLS

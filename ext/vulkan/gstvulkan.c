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

/**
 * SECTION:plugin-vulkan
 * @title: vulkan
 *
 * Cross-platform Vulkan plugin.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vksink.h"
#include "vkupload.h"
#include "vkimageidentity.h"
#include "vkcolorconvert.h"
#include "vkdownload.h"
#include "vkviewconvert.h"
#include "vkdeviceprovider.h"

#define GST_CAT_DEFAULT gst_vulkan_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_vulkan_debug, "vulkan", 0, "vulkan");

  if (!gst_element_register (plugin, "vulkansink",
          GST_RANK_NONE, GST_TYPE_VULKAN_SINK)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "vulkanupload",
          GST_RANK_NONE, GST_TYPE_VULKAN_UPLOAD)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "vulkandownload",
          GST_RANK_NONE, GST_TYPE_VULKAN_DOWNLOAD)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "vulkancolorconvert",
          GST_RANK_NONE, GST_TYPE_VULKAN_COLOR_CONVERT)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "vulkanimageidentity",
          GST_RANK_NONE, GST_TYPE_VULKAN_IMAGE_IDENTITY)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "vulkanviewconvert",
          GST_RANK_NONE, GST_TYPE_VULKAN_VIEW_CONVERT)) {
    return FALSE;
  }

  if (!gst_device_provider_register (plugin, "vulkandeviceprovider",
          GST_RANK_MARGINAL, GST_TYPE_VULKAN_DEVICE_PROVIDER))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vulkan,
    "Vulkan plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

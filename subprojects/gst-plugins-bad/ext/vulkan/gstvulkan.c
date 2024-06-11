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
#include "vkdownload.h"
#include "vkdeviceprovider.h"
#include "gstvulkanelements.h"

#if defined(HAVE_GLSLC)
#include "vkimageidentity.h"
#include "vkcolorconvert.h"
#include "vkshaderspv.h"
#include "vkviewconvert.h"
#include "vkoverlaycompositor.h"
#endif

#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
#include "vkh264dec.h"
#include "vkh265dec.h"
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;
  GstVulkanInstance *instance = gst_vulkan_instance_new ();
  gboolean have_instance = gst_vulkan_instance_open (instance, NULL);
  const gchar *env_vars[] =
      { "VK_ICD_FILENAMES", "VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES", NULL };
#ifndef G_OS_WIN32
  const gchar *kernel_paths[] = { "/dev/dri", NULL };
  const gchar *kernel_names[] = { "renderD", NULL };

  /* features get updated upon changes in /dev/dri/renderD* */
  gst_plugin_add_dependency (plugin, NULL, kernel_paths, kernel_names,
      GST_PLUGIN_DEPENDENCY_FLAG_FILE_NAME_IS_PREFIX);

  /* features get updated upon changes on VK_ICD_FILENAMES envvar */
#endif
  gst_plugin_add_dependency (plugin, env_vars, NULL, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  ret |= GST_DEVICE_PROVIDER_REGISTER (vulkandeviceprovider, plugin);

  ret |= GST_ELEMENT_REGISTER (vulkanupload, plugin);

  ret |= GST_ELEMENT_REGISTER (vulkandownload, plugin);
#if defined(HAVE_GLSLC)
  ret |= GST_ELEMENT_REGISTER (vulkancolorconvert, plugin);

  ret |= GST_ELEMENT_REGISTER (vulkanimageidentity, plugin);

  ret |= GST_ELEMENT_REGISTER (vulkanshaderspv, plugin);

  ret |= GST_ELEMENT_REGISTER (vulkanviewconvert, plugin);

  ret |= GST_ELEMENT_REGISTER (vulkanoverlaycompositor, plugin);
#endif
  if (have_instance && instance->n_physical_devices) {
#if GST_VULKAN_HAVE_VIDEO_EXTENSIONS
    for (gint i = 0; i < instance->n_physical_devices; i++) {
      GstVulkanDevice *device = gst_vulkan_device_new_with_index (instance, i);
      if (gst_vulkan_device_is_extension_enabled (device,
              VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME)) {
        ret |= gst_vulkan_h264_decoder_register (plugin, device, GST_RANK_NONE);
      }
      if (gst_vulkan_device_is_extension_enabled (device,
              VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME)) {
        ret |= gst_vulkan_h265_decoder_register (plugin, device, GST_RANK_NONE);
      }
      ret |= gst_vulkan_sink_register (plugin, device, GST_RANK_NONE);
      gst_object_unref (device);
    }
#endif
  }
  gst_object_unref (instance);
  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vulkan,
    "Vulkan plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

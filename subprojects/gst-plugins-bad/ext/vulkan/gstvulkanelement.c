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

#include "gstvulkanelements.h"

#include <gst/vulkan/vulkan.h>

#define GST_CAT_DEFAULT gst_vulkan_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

void
vulkan_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;

  if (g_once_init_enter (&res)) {
    GST_DEBUG_CATEGORY_INIT (gst_vulkan_debug, "vulkan", 0, "vulkan");
    g_once_init_leave (&res, TRUE);
  }
}

void
gst_vulkan_create_feature_name (GstVulkanDevice * device,
    const gchar * type_name_default, const gchar * type_name_templ,
    gchar ** type_name, const gchar * feature_name_default,
    const gchar * feature_name_templ, gchar ** feature_name,
    gchar ** desc, guint * rank)
{
  /* The first element to be registered should use a constant name,
   * like vkh264enc, for any additional elements, we create unique
   * names, using inserting the render device name. */
  if (device->physical_device->device_index == 0) {
    *type_name = g_strdup (type_name_default);
    *feature_name = g_strdup (feature_name_default);
    *desc = g_strdup (device->physical_device->properties.deviceName);
    return;
  }

  *type_name =
      g_strdup_printf (type_name_templ, device->physical_device->device_index);
  *feature_name =
      g_strdup_printf (feature_name_templ,
      device->physical_device->device_index);

  *desc = g_strdup (device->physical_device->properties.deviceName);

  if (*rank > 0)
    *rank -= 1;
}

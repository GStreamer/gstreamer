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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gprintf.h>

#include "vkerror.h"

/* *INDENT-OFF* */
static const struct 
{
  VkResult result;
  const char *str;
} vk_result_string_map[] = {
  {VK_ERROR_OUT_OF_HOST_MEMORY, "Out Of Host Memory"},
  {VK_ERROR_OUT_OF_DEVICE_MEMORY, "Out of Device Memory"},
  {VK_ERROR_INITIALIZATION_FAILED, "Initialization Failed"},
  {VK_ERROR_DEVICE_LOST, "Device Lost"},
  {VK_ERROR_MEMORY_MAP_FAILED, "Map Failed"},
  {VK_ERROR_LAYER_NOT_PRESENT, "Layer Not Present"},
  {VK_ERROR_EXTENSION_NOT_PRESENT, "Extension Not Present"},
  {VK_ERROR_INCOMPATIBLE_DRIVER, "Incompatible Driver"},
};

static const struct
{
  VkResult result;
  GstVulkanError gst_enum;
} vk_result_gst_error_map[] = {
  {VK_ERROR_OUT_OF_HOST_MEMORY, GST_VULKAN_ERROR_OUT_OF_HOST_MEMORY},
  {VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY},
  {VK_ERROR_INITIALIZATION_FAILED, GST_VULKAN_ERROR_INITIALIZATION_FAILED},
  {VK_ERROR_DEVICE_LOST, GST_VULKAN_ERROR_DEVICE_LOST},
  {VK_ERROR_MEMORY_MAP_FAILED, GST_VULKAN_ERROR_MEMORY_MAP_FAILED},
  {VK_ERROR_LAYER_NOT_PRESENT, GST_VULKAN_ERROR_LAYER_NOT_PRESENT},
  {VK_ERROR_EXTENSION_NOT_PRESENT, GST_VULKAN_ERROR_EXTENSION_NOT_PRESENT},
  {VK_ERROR_INCOMPATIBLE_DRIVER, GST_VULKAN_ERROR_INCOMPATIBLE_DRIVER},
};
/* *INDENT-ON* */

GQuark
gst_vulkan_error_quark (void)
{
  return g_quark_from_static_string ("gst-vulkan-error");
}

static const char *
_vk_result_to_string (VkResult result)
{
  int i;

  if (result >= 0)
    return NULL;
  if (result < VK_RESULT_BEGIN_RANGE)
    return "Unknown Error";

  for (i = 0; i < G_N_ELEMENTS (vk_result_string_map); i++) {
    if (result == vk_result_string_map[i].result)
      return vk_result_string_map[i].str;
  }

  return "Unknown Error";
}

static GstVulkanError
_vk_result_to_g_error_enum (VkResult result)
{
  int i;

  if (result >= 0)
    return 0;
  if (result < VK_RESULT_BEGIN_RANGE)
    return 0;

  for (i = 0; i < G_N_ELEMENTS (vk_result_gst_error_map); i++) {
    if (result == vk_result_gst_error_map[i].result)
      return vk_result_gst_error_map[i].gst_enum;
  }

  return GST_VULKAN_ERROR_FAILED;
}

VkResult
gst_vulkan_error_to_g_error (VkResult result, GError ** error,
    const char *format, ...)
{
  GstVulkanError gst_enum;
  const char *result_str;
  gchar *string;
  va_list args;

  if (error == NULL)
    /* we don't have an error to set */
    return result;

  result_str = _vk_result_to_string (result);
  if (result_str == NULL)
    return result;

  gst_enum = _vk_result_to_g_error_enum (result);

  va_start (args, format);
  g_vasprintf (&string, format, args);
  va_end (args);

  g_set_error (error, GST_VULKAN_ERROR, gst_enum, "%s: %s", result_str, string);

  return result;
}

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

#include "vkdevice.h"
#include "vkutils_private.h"

#include <string.h>

static const char *device_validation_layers[] = {
  "Threading",
  "MemTracker",
  "ObjectTracker",
  "DrawState",
  "ParamChecker",
  "ShaderChecker",
  "Swapchain",
  "DeviceLimits",
  "Image",
};

#define GST_CAT_DEFAULT gst_vulkan_device_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

G_DEFINE_TYPE_WITH_CODE (GstVulkanDevice, gst_vulkan_device, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkandevice", 0,
        "Vulkan Device"));

static void gst_vulkan_device_finalize (GObject * object);

struct _GstVulkanDevicePrivate
{
  gboolean opened;
};

GstVulkanDevice *
gst_vulkan_device_new (GstVulkanInstance * instance)
{
  GstVulkanDevice *device = g_object_new (GST_TYPE_VULKAN_DEVICE, NULL);

  device->instance = gst_object_ref (instance);
  /* FIXME: select this externally */
  device->device_index = 0;

  return device;
}

static void
gst_vulkan_device_init (GstVulkanDevice * device)
{
  device->priv = G_TYPE_INSTANCE_GET_PRIVATE ((device),
      GST_TYPE_VULKAN_DEVICE, GstVulkanDevicePrivate);
}

static void
gst_vulkan_device_class_init (GstVulkanDeviceClass * device_class)
{
  GObjectClass *gobject_class = (GObjectClass *) device_class;

  g_type_class_add_private (device_class, sizeof (GstVulkanDevicePrivate));

  gobject_class->finalize = gst_vulkan_device_finalize;
}

static void
gst_vulkan_device_finalize (GObject * object)
{
  GstVulkanDevice *device = GST_VULKAN_DEVICE (object);

  g_free (device->queue_family_props);
  device->queue_family_props = NULL;

  if (device->cmd_pool.handle)
    vkDestroyCommandPool (device->device, device->cmd_pool);
  device->cmd_pool.handle = 0;

  if (device->device)
    vkDestroyDevice (device->device);
  device->device = NULL;

  if (device->instance)
    gst_object_unref (device->instance);
  device->instance = NULL;
}

static const gchar *
_device_type_to_string (VkPhysicalDeviceType type)
{
  switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_OTHER:
      return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
      return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
      return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
      return "CPU";
    default:
      return "unknown";
  }
}

static gboolean
_physical_device_info (GstVulkanDevice * device, GError ** error)
{
  VkPhysicalDeviceProperties props;
  VkPhysicalDevice gpu;
  VkResult err;

  gpu = gst_vulkan_device_get_physical_device (device);
  if (!gpu) {
    g_set_error (error, GST_VULKAN_ERROR,
        GST_VULKAN_ERROR_INITIALIZATION_FAILED,
        "Failed to retrieve physical device");
    return FALSE;
  }

  err = vkGetPhysicalDeviceProperties (gpu, &props);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkGetPhysicalDeviceProperties") < 0)
    return FALSE;

  GST_INFO_OBJECT (device, "device name %s type %s api version %u, "
      "driver version %u vendor ID 0x%x, device ID 0x%x", props.deviceName,
      _device_type_to_string (props.deviceType), props.apiVersion,
      props.driverVersion, props.vendorId, props.deviceId);

  return TRUE;
}

gboolean
gst_vulkan_device_open (GstVulkanDevice * device, GError ** error)
{
  const char *extension_names[64];
  uint32_t enabled_extension_count = 0;
  uint32_t device_extension_count = 0;
  VkExtensionProperties *device_extensions = NULL;
  uint32_t enabled_layer_count = 0;
  uint32_t device_layer_count = 0;
  VkLayerProperties *device_layers;
  gboolean have_swapchain_ext;
  gboolean validation_found;
  VkPhysicalDevice gpu;
  VkResult err;
  guint i;

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), FALSE);

  GST_OBJECT_LOCK (device);

  if (device->priv->opened) {
    GST_OBJECT_UNLOCK (device);
    return TRUE;
  }

  if (!_physical_device_info (device, error))
    goto error;

  gpu = gst_vulkan_device_get_physical_device (device);

  /* Look for validation layers */
  err = vkEnumerateDeviceLayerProperties (gpu, &device_layer_count, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceLayerProperties") < 0)
    goto error;

  device_layers = g_new0 (VkLayerProperties, device_layer_count);
  err =
      vkEnumerateDeviceLayerProperties (gpu, &device_layer_count,
      device_layers);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceLayerProperties") < 0) {
    g_free (device_layers);
    goto error;
  }

  validation_found =
      _check_for_all_layers (G_N_ELEMENTS (device_validation_layers),
      device_validation_layers, device_layer_count, device_layers);
  g_free (device_layers);
  device_layers = NULL;
  if (!validation_found) {
    g_error ("vkEnumerateDeviceLayerProperties failed to find"
        "a required validation layer.\n\n"
        "Please look at the Getting Started guide for additional "
        "information.\nvkCreateDevice Failure");
    goto error;
  }
  enabled_layer_count = G_N_ELEMENTS (device_validation_layers);

  err =
      vkEnumerateDeviceExtensionProperties (gpu, NULL,
      &device_extension_count, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceExtensionProperties") < 0)
    goto error;

  have_swapchain_ext = 0;
  enabled_extension_count = 0;
  memset (extension_names, 0, sizeof (extension_names));
  device_extensions = g_new0 (VkExtensionProperties, device_extension_count);
  err = vkEnumerateDeviceExtensionProperties (gpu, NULL,
      &device_extension_count, device_extensions);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceExtensionProperties") < 0) {
    g_free (device_extensions);
    goto error;
  }

  for (uint32_t i = 0; i < device_extension_count; i++) {
    if (!strcmp ("VK_EXT_KHR_device_swapchain", device_extensions[i].extName)) {
      have_swapchain_ext = TRUE;
      extension_names[enabled_extension_count++] =
          "VK_EXT_KHR_device_swapchain";
    }
    g_assert (enabled_extension_count < 64);
  }
  if (!have_swapchain_ext) {
    g_error ("vkEnumerateDeviceExtensionProperties failed to find the "
        "\"VK_EXT_KHR_device_swapchain\" extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\nvkCreateInstance Failure");
  }
  g_free (device_extensions);

  err = vkGetPhysicalDeviceProperties (gpu, &device->gpu_props);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkGetPhysicalDeviceProperties") < 0)
    goto error;

  err = vkGetPhysicalDeviceMemoryProperties (gpu, &device->memory_properties);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkGetPhysicalDeviceProperties") < 0)
    goto error;

  err = vkGetPhysicalDeviceFeatures (gpu, &device->gpu_features);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkGetPhysicalDeviceFeatures") < 0)
    goto error;

  /* Call with NULL data to get count */
  err =
      vkGetPhysicalDeviceQueueFamilyProperties (gpu, &device->n_queue_families,
      NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkGetPhysicalDeviceQueueFamilyProperties") < 0)
    goto error;
  g_assert (device->n_queue_families >= 1);

  device->queue_family_props =
      g_new0 (VkQueueFamilyProperties, device->n_queue_families);
  err =
      vkGetPhysicalDeviceQueueFamilyProperties (gpu, &device->n_queue_families,
      device->queue_family_props);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkGetPhysicalDeviceQueueFamilyProperties") < 0)
    goto error;

  /* FIXME: allow overriding/selecting */
  for (i = 0; i < device->n_queue_families; i++) {
    if (device->queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      break;
  }
  if (i >= device->n_queue_families) {
    g_set_error (error, GST_VULKAN_ERROR,
        GST_VULKAN_ERROR_INITIALIZATION_FAILED,
        "Failed to find a compatible queue family");
    goto error;
  }
  device->queue_family_id = i;
  device->n_queues = 1;

  {
    VkDeviceQueueCreateInfo queue_info = { 0, };
    VkDeviceCreateInfo device_info = { 0, };

    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.pNext = NULL;
    queue_info.queueFamilyIndex = device->queue_family_id;
    queue_info.queueCount = device->n_queues;

    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = NULL;
    device_info.queueRecordCount = 1;
    device_info.pRequestedQueues = &queue_info;
    device_info.layerCount = enabled_layer_count;
    device_info.ppEnabledLayerNames =
        (const char *const *) device_validation_layers;
    device_info.extensionCount = enabled_extension_count;
    device_info.ppEnabledExtensionNames = (const char *const *) extension_names;
    device_info.pEnabledFeatures = NULL;

    err = vkCreateDevice (gpu, &device_info, &device->device);
    if (gst_vulkan_error_to_g_error (err, error, "vkCreateDevice") < 0)
      goto error;
  }
  {
    VkCmdPoolCreateInfo cmd_pool_info = { 0, };

    cmd_pool_info.sType = VK_STRUCTURE_TYPE_CMD_POOL_CREATE_INFO;
    cmd_pool_info.pNext = NULL;
    cmd_pool_info.queueFamilyIndex = device->queue_family_id;
    cmd_pool_info.flags = 0;

    err =
        vkCreateCommandPool (device->device, &cmd_pool_info, &device->cmd_pool);
    if (gst_vulkan_error_to_g_error (err, error, "vkCreateCommandPool") < 0)
      goto error;
  }

  GST_OBJECT_UNLOCK (device);
  return TRUE;

error:
  {
    GST_OBJECT_UNLOCK (device);
    return FALSE;
  }
}

GstVulkanQueue *
gst_vulkan_device_get_queue (GstVulkanDevice * device, guint32 queue_family,
    guint32 queue_i, GError ** error)
{
  GstVulkanQueue *ret;
  VkResult err;

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);
  g_return_val_if_fail (device->device != NULL, NULL);
  g_return_val_if_fail (queue_family < device->n_queues, NULL);
  g_return_val_if_fail (queue_i <
      device->queue_family_props[queue_family].queueCount, NULL);

  ret = g_object_new (GST_TYPE_VULKAN_QUEUE, NULL);
  ret->device = gst_object_ref (device);
  ret->family = queue_family;
  ret->index = queue_i;

  err = vkGetDeviceQueue (device->device, queue_family, queue_i, &ret->queue);
  if (gst_vulkan_error_to_g_error (err, error, "vkGetDeviceQueue") < 0) {
    gst_object_unref (ret);
    return NULL;
  }

  return ret;
}

gpointer
gst_vulkan_device_get_proc_address (GstVulkanDevice * device,
    const gchar * name)
{
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);
  g_return_val_if_fail (device->device != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_TRACE_OBJECT (device, "%s", name);

  return vkGetDeviceProcAddr (device->device, name);
}

GstVulkanInstance *
gst_vulkan_device_get_instance (GstVulkanDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);

  return device->instance ? gst_object_ref (device->instance) : NULL;
}

VkPhysicalDevice
gst_vulkan_device_get_physical_device (GstVulkanDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);

  if (device->instance->physical_devices == NULL)
    return NULL;
  if (device->device_index >= device->instance->n_physical_devices)
    return NULL;

  return device->instance->physical_devices[device->device_index];
}

gboolean
gst_vulkan_device_create_cmd_buffer (GstVulkanDevice * device,
    VkCmdBuffer * cmd, GError ** error)
{
  VkResult err;
  VkCmdBufferCreateInfo cmd_info = { 0, };

  cmd_info.sType = VK_STRUCTURE_TYPE_CMD_BUFFER_CREATE_INFO;
  cmd_info.pNext = NULL;
  cmd_info.cmdPool = device->cmd_pool;
  cmd_info.level = VK_CMD_BUFFER_LEVEL_PRIMARY;
  cmd_info.flags = 0;

  err = vkCreateCommandBuffer (device->device, &cmd_info, cmd);
  if (gst_vulkan_error_to_g_error (err, error, "vkCreateCommandBuffer") < 0)
    return FALSE;

  GST_LOG_OBJECT (device, "created cmd buffer %p", cmd);

  return TRUE;
}

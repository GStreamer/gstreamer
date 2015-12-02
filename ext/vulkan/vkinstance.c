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

#include "vkinstance.h"
#include "vkutils.h"

#include <string.h>

#define APP_SHORT_NAME "GStreamer"

static const char *instance_validation_layers[] = {
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

#define GST_CAT_DEFAULT gst_vulkan_instance_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY (GST_VULKAN_DEBUG_CAT);

#define gst_vulkan_instance_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanInstance, gst_vulkan_instance,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "vulkaninstance", 0, "Vulkan Instance");
    GST_DEBUG_CATEGORY_INIT (GST_VULKAN_DEBUG_CAT,
        "vulkandebug", 0, "Vulkan Debug"));

static void gst_vulkan_instance_finalize (GObject * object);

GstVulkanInstance *
gst_vulkan_instance_new (void)
{
  return g_object_new (GST_TYPE_VULKAN_INSTANCE, NULL);
}

static void
gst_vulkan_instance_init (GstVulkanInstance * instance)
{
}

static void
gst_vulkan_instance_class_init (GstVulkanInstanceClass * instance_class)
{
  gst_vulkan_memory_init_once ();
  gst_vulkan_image_memory_init_once ();

  G_OBJECT_CLASS (instance_class)->finalize = gst_vulkan_instance_finalize;
}

static void
gst_vulkan_instance_finalize (GObject * object)
{
  GstVulkanInstance *instance = GST_VULKAN_INSTANCE (object);

  if (instance->instance)
    vkDestroyInstance (instance->instance);
  instance->instance = NULL;
}

static VkBool32
_gst_vk_debug_callback (VkFlags msgFlags, VkDbgObjectType objType,
    uint64_t srcObject, size_t location, int32_t msgCode,
    const char *pLayerPrefix, const char *pMsg, void *pUserData)
{
  if (msgFlags & VK_DBG_REPORT_ERROR_BIT) {
    GST_CAT_ERROR (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DBG_REPORT_WARN_BIT) {
    GST_CAT_WARNING (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DBG_REPORT_INFO_BIT) {
    GST_CAT_LOG (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DBG_REPORT_PERF_WARN_BIT) {
    GST_CAT_FIXME (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DBG_REPORT_DEBUG_BIT) {
    GST_CAT_TRACE (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else {
    return FALSE;
  }

  /*
   * false indicates that layer should not bail-out of an
   * API call that had validation failures. This may mean that the
   * app dies inside the driver due to invalid parameter(s).
   * That's what would happen without validation layers, so we'll
   * keep that behavior here.
   */
  return FALSE;
}

gboolean
gst_vulkan_instance_open (GstVulkanInstance * instance, GError ** error)
{
  VkExtensionProperties *instance_extensions;
  char *extension_names[64];    /* FIXME: make dynamic */
  VkLayerProperties *instance_layers;
  uint32_t instance_extension_count = 0;
  uint32_t enabled_extension_count = 0;
  uint32_t instance_layer_count = 0;
  uint32_t enabled_layer_count = 0;
  gboolean validation_found;
  gboolean have_swapchain_ext = FALSE;
  VkResult err;

  /* Look for validation layers */
  err = vkEnumerateInstanceLayerProperties (&instance_layer_count, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vKEnumerateInstanceLayerProperties") < 0)
    return FALSE;

  instance_layers = g_new0 (VkLayerProperties, instance_layer_count);
  err =
      vkEnumerateInstanceLayerProperties (&instance_layer_count,
      instance_layers);
  if (gst_vulkan_error_to_g_error (err, error,
          "vKEnumerateInstanceLayerProperties") < 0) {
    g_free (instance_layers);
    return FALSE;
  }

  /* TODO: allow outside selection */
  validation_found =
      _check_for_all_layers (G_N_ELEMENTS (instance_validation_layers),
      instance_validation_layers, instance_layer_count, instance_layers);
  if (!validation_found) {
    g_error ("vkEnumerateInstanceLayerProperties failed to find"
        "required validation layer.\n\n"
        "Please look at the Getting Started guide for additional "
        "information.\nvkCreateInstance Failure");
  }
  enabled_layer_count = G_N_ELEMENTS (instance_validation_layers);

  err =
      vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count,
      NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateInstanceExtensionProperties") < 0) {
    g_free (instance_layers);
    return FALSE;
  }

  memset (extension_names, 0, sizeof (extension_names));
  instance_extensions =
      g_new0 (VkExtensionProperties, instance_extension_count);
  err =
      vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count,
      instance_extensions);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateInstanceExtensionProperties") < 0) {
    g_free (instance_layers);
    g_free (instance_extensions);
    return FALSE;
  }

  /* TODO: allow outside selection */
  for (uint32_t i = 0; i < instance_extension_count; i++) {
    if (!g_strcmp0 ("VK_EXT_KHR_swapchain", instance_extensions[i].extName)) {
      have_swapchain_ext = TRUE;
      extension_names[enabled_extension_count++] =
          (gchar *) "VK_EXT_KHR_swapchain";
    }
    if (!g_strcmp0 (VK_DEBUG_REPORT_EXTENSION_NAME,
            instance_extensions[i].extName)) {
      extension_names[enabled_extension_count++] =
          (gchar *) VK_DEBUG_REPORT_EXTENSION_NAME;
    }
    g_assert (enabled_extension_count < 64);
  }
  if (!have_swapchain_ext) {
    g_error ("vkEnumerateInstanceExtensionProperties failed to find the "
        "\"VK_EXT_KHR_swapchain\" extension.\n\nDo you have a compatible "
        "Vulkan installable client driver (ICD) installed?\nPlease "
        "look at the Getting Started guide for additional "
        "information.\nvkCreateInstance Failure");
  }

  {
    VkApplicationInfo app = { 0, };
    VkInstanceCreateInfo inst_info = { 0, };

    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pNext = NULL;
    app.pAppName = APP_SHORT_NAME;
    app.appVersion = 0;
    app.pEngineName = APP_SHORT_NAME;
    app.engineVersion = 0;
    app.apiVersion = VK_API_VERSION;

    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pNext = NULL;
    inst_info.pAppInfo = &app;
    inst_info.pAllocCb = NULL;
    inst_info.layerCount = enabled_layer_count;
    inst_info.ppEnabledLayerNames =
        (const char *const *) instance_validation_layers;
    inst_info.extensionCount = enabled_extension_count;
    inst_info.ppEnabledExtensionNames = (const char *const *) extension_names;

    err = vkCreateInstance (&inst_info, &instance->instance);
    if (gst_vulkan_error_to_g_error (err, error, "vkCreateInstance") < 0) {
      g_free (instance_layers);
      g_free (instance_extensions);
      return FALSE;
    }
  }

  g_free (instance_layers);
  g_free (instance_extensions);

  err =
      vkEnumeratePhysicalDevices (instance->instance,
      &instance->n_physical_devices, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumeratePhysicalDevices") < 0)
    return FALSE;
  g_assert (instance->n_physical_devices > 0);
  instance->physical_devices =
      g_new0 (VkPhysicalDevice, instance->n_physical_devices);
  err =
      vkEnumeratePhysicalDevices (instance->instance,
      &instance->n_physical_devices, instance->physical_devices);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumeratePhysicalDevices") < 0)
    return FALSE;

  instance->dbgCreateMsgCallback = (PFN_vkDbgCreateMsgCallback)
      gst_vulkan_instance_get_proc_address (instance, "vkDbgCreateMsgCallback");
  if (!instance->dbgCreateMsgCallback) {
    g_set_error (error, GST_VULKAN_ERROR, GST_VULKAN_ERROR_FAILED,
        "Failed to retreive vkDbgCreateMsgCallback");
    return FALSE;
  }
  instance->dbgDestroyMsgCallback = (PFN_vkDbgDestroyMsgCallback)
      gst_vulkan_instance_get_proc_address (instance,
      "vkDbgDestroyMsgCallback");
  if (!instance->dbgDestroyMsgCallback) {
    g_set_error (error, GST_VULKAN_ERROR, GST_VULKAN_ERROR_FAILED,
        "Failed to retreive vkDbgDestroyMsgCallback");
    return FALSE;
  }
  instance->dbgBreakCallback =
      (PFN_vkDbgMsgCallback) gst_vulkan_instance_get_proc_address (instance,
      "vkDbgBreakCallback");
  if (!instance->dbgBreakCallback) {
    g_set_error (error, GST_VULKAN_ERROR, GST_VULKAN_ERROR_FAILED,
        "Failed to retreive vkDbgBreakCallback");
    return FALSE;
  }

  err = instance->dbgCreateMsgCallback (instance->instance,
      VK_DBG_REPORT_ERROR_BIT | VK_DBG_REPORT_WARN_BIT | VK_DBG_REPORT_INFO_BIT
      | VK_DBG_REPORT_DEBUG_BIT | VK_DBG_REPORT_PERF_WARN_BIT,
      _gst_vk_debug_callback, NULL, &instance->msg_callback);
  if (gst_vulkan_error_to_g_error (err, error, "vkDbgCreateMsgCallback") < 0)
    return FALSE;

  return TRUE;
}

gpointer
gst_vulkan_instance_get_proc_address (GstVulkanInstance * instance,
    const gchar * name)
{
  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), NULL);
  g_return_val_if_fail (instance->instance != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_TRACE_OBJECT (instance, "%s", name);

  return vkGetInstanceProcAddr (instance->instance, name);
}

void
gst_vulkan_instance_close (GstVulkanInstance * instance)
{
  g_return_if_fail (GST_IS_VULKAN_INSTANCE (instance));
  g_return_if_fail (instance->instance != NULL);

  if (instance->dbgDestroyMsgCallback)
    instance->dbgDestroyMsgCallback (instance->instance,
        instance->msg_callback);

  g_free (instance->physical_devices);
}

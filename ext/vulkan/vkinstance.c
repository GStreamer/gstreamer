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
#include "vkutils_private.h"

#include <string.h>

#define APP_SHORT_NAME "GStreamer"

static const char *instance_validation_layers[] = {
  "VK_LAYER_GOOGLE_threading",
  "VK_LAYER_LUNARG_mem_tracker",
  "VK_LAYER_LUNARG_object_tracker",
  "VK_LAYER_LUNARG_draw_state",
  "VK_LAYER_LUNARG_param_checker",
  "VK_LAYER_LUNARG_swapchain",
  "VK_LAYER_LUNARG_device_limits",
  "VK_LAYER_LUNARG_image",
};

#define GST_CAT_DEFAULT gst_vulkan_instance_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY (GST_VULKAN_DEBUG_CAT);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

enum
{
  SIGNAL_0,
  SIGNAL_CREATE_DEVICE,
  LAST_SIGNAL
};

static guint gst_vulkan_instance_signals[LAST_SIGNAL] = { 0 };

#define gst_vulkan_instance_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanInstance, gst_vulkan_instance,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "vulkaninstance", 0, "Vulkan Instance");
    GST_DEBUG_CATEGORY_INIT (GST_VULKAN_DEBUG_CAT,
        "vulkandebug", 0, "Vulkan Debug");
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT"));

static void gst_vulkan_instance_finalize (GObject * object);

struct _GstVulkanInstancePrivate
{
  gboolean opened;
};

GstVulkanInstance *
gst_vulkan_instance_new (void)
{
  GstVulkanInstance *instance;

  instance = g_object_new (GST_TYPE_VULKAN_INSTANCE, NULL);
  gst_object_ref_sink (instance);

  return instance;
}

static void
gst_vulkan_instance_init (GstVulkanInstance * instance)
{
  instance->priv = G_TYPE_INSTANCE_GET_PRIVATE ((instance),
      GST_TYPE_VULKAN_INSTANCE, GstVulkanInstancePrivate);
}

static void
gst_vulkan_instance_class_init (GstVulkanInstanceClass * klass)
{
  gst_vulkan_memory_init_once ();
  gst_vulkan_image_memory_init_once ();
  gst_vulkan_buffer_memory_init_once ();

  g_type_class_add_private (klass, sizeof (GstVulkanInstancePrivate));

  /**
   * GstVulkanInstance::create-device:
   * @object: the #GstVulkanDisplay
   *
   * Overrides the #GstVulkanDevice creation mechanism.
   * It can be called from any thread.
   *
   * Returns: the newly created #GstVulkanDevice.
   */
  gst_vulkan_instance_signals[SIGNAL_CREATE_DEVICE] =
      g_signal_new ("create-device", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      GST_TYPE_VULKAN_DEVICE, 0);

  G_OBJECT_CLASS (klass)->finalize = gst_vulkan_instance_finalize;
}

static void
gst_vulkan_instance_finalize (GObject * object)
{
  GstVulkanInstance *instance = GST_VULKAN_INSTANCE (object);

  if (instance->priv->opened) {
    if (instance->dbgDestroyDebugReportCallback)
      instance->dbgDestroyDebugReportCallback (instance->instance,
          instance->msg_callback, NULL);

    g_free (instance->physical_devices);
  }
  instance->priv->opened = FALSE;

  if (instance->instance)
    vkDestroyInstance (instance->instance, NULL);
  instance->instance = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static VkBool32
_gst_vk_debug_callback (VkDebugReportFlagsEXT msgFlags,
    VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location,
    int32_t msgCode, const char *pLayerPrefix, const char *pMsg,
    void *pUserData)
{
  if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
    GST_CAT_ERROR (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
    GST_CAT_WARNING (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
    GST_CAT_LOG (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
    GST_CAT_FIXME (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
  } else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
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
  gchar **enabled_layers;
  VkResult err;

  GST_OBJECT_LOCK (instance);
  if (instance->priv->opened) {
    GST_OBJECT_UNLOCK (instance);
    return TRUE;
  }

  /* Look for validation layers */
  err = vkEnumerateInstanceLayerProperties (&instance_layer_count, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vKEnumerateInstanceLayerProperties") < 0)
    goto error;

  instance_layers = g_new0 (VkLayerProperties, instance_layer_count);
  err =
      vkEnumerateInstanceLayerProperties (&instance_layer_count,
      instance_layers);
  if (gst_vulkan_error_to_g_error (err, error,
          "vKEnumerateInstanceLayerProperties") < 0) {
    g_free (instance_layers);
    goto error;
  }

  /* TODO: allow outside selection */
  _check_for_all_layers (G_N_ELEMENTS (instance_validation_layers),
      instance_validation_layers, instance_layer_count, instance_layers,
      &enabled_layer_count, &enabled_layers);

  g_free (instance_layers);

  err =
      vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count,
      NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateInstanceExtensionProperties") < 0) {
    g_strfreev (enabled_layers);
    goto error;
  }
  GST_DEBUG_OBJECT (instance, "Found %u extensions", instance_extension_count);

  memset (extension_names, 0, sizeof (extension_names));
  instance_extensions =
      g_new0 (VkExtensionProperties, instance_extension_count);
  err =
      vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count,
      instance_extensions);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateInstanceExtensionProperties") < 0) {
    g_strfreev (enabled_layers);
    g_free (instance_extensions);
    goto error;
  }

  {
    GstVulkanDisplayType display_type;
    gboolean swapchain_ext_found = FALSE;
    gboolean winsys_ext_found = FALSE;
    const gchar *winsys_ext_name;
    uint32_t i;

    display_type = gst_vulkan_display_choose_type (instance);

    winsys_ext_name =
        gst_vulkan_display_type_to_extension_string (display_type);
    if (!winsys_ext_name) {
      GST_WARNING_OBJECT (instance, "No window system extension enabled");
      winsys_ext_found = TRUE;  /* Don't error out completely */
    }

    /* TODO: allow outside selection */
    for (i = 0; i < instance_extension_count; i++) {
      GST_TRACE_OBJECT (instance, "checking instance extension %s",
          instance_extensions[i].extensionName);

      if (!g_strcmp0 (VK_KHR_SURFACE_EXTENSION_NAME,
              instance_extensions[i].extensionName)) {
        swapchain_ext_found = TRUE;
        extension_names[enabled_extension_count++] =
            (gchar *) VK_KHR_SURFACE_EXTENSION_NAME;
      }
      if (!g_strcmp0 (VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
              instance_extensions[i].extensionName)) {
        extension_names[enabled_extension_count++] =
            (gchar *) VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
      }
      if (!g_strcmp0 (winsys_ext_name, instance_extensions[i].extensionName)) {
        winsys_ext_found = TRUE;
        extension_names[enabled_extension_count++] = (gchar *) winsys_ext_name;
      }
      g_assert (enabled_extension_count < 64);
    }
    if (!swapchain_ext_found) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "vkEnumerateInstanceExtensionProperties failed to find the required "
          "\"" VK_KHR_SURFACE_EXTENSION_NAME "\" extension");
      g_strfreev (enabled_layers);
      g_free (instance_extensions);
      goto error;
    }
    if (!winsys_ext_found) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "vkEnumerateInstanceExtensionProperties failed to find the required "
          "\"%s\" window system extension", winsys_ext_name);
      g_strfreev (enabled_layers);
      g_free (instance_extensions);
      goto error;
    }
  }

  {
    VkApplicationInfo app = { 0, };
    VkInstanceCreateInfo inst_info = { 0, };

    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pNext = NULL;
    app.pApplicationName = APP_SHORT_NAME;
    app.applicationVersion = 0;
    app.pEngineName = APP_SHORT_NAME;
    app.engineVersion = 0;
    app.apiVersion = VK_API_VERSION_1_0;

    inst_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_info.pNext = NULL;
    inst_info.pApplicationInfo = &app;
#if 0
    inst_info.enabledLayerCount = enabled_layer_count;
    inst_info.ppEnabledLayerNames = (const char *const *) enabled_layers;
#else
    inst_info.enabledLayerCount = 0;
    inst_info.ppEnabledLayerNames = NULL;
#endif
    inst_info.enabledExtensionCount = enabled_extension_count;
    inst_info.ppEnabledExtensionNames = (const char *const *) extension_names;

    err = vkCreateInstance (&inst_info, NULL, &instance->instance);
    if (gst_vulkan_error_to_g_error (err, error, "vkCreateInstance") < 0) {
      g_strfreev (enabled_layers);
      g_free (instance_extensions);
      goto error;
    }
  }

  g_free (instance_extensions);
  g_strfreev (enabled_layers);

  err =
      vkEnumeratePhysicalDevices (instance->instance,
      &instance->n_physical_devices, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumeratePhysicalDevices") < 0)
    goto error;
  g_assert (instance->n_physical_devices > 0);
  instance->physical_devices =
      g_new0 (VkPhysicalDevice, instance->n_physical_devices);
  err =
      vkEnumeratePhysicalDevices (instance->instance,
      &instance->n_physical_devices, instance->physical_devices);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumeratePhysicalDevices") < 0)
    goto error;

  instance->dbgCreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)
      gst_vulkan_instance_get_proc_address (instance,
      "vkCreateDebugReportCallbackEXT");
  if (!instance->dbgCreateDebugReportCallback) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Failed to retreive vkCreateDebugReportCallback");
    goto error;
  }
  instance->dbgDestroyDebugReportCallback =
      (PFN_vkDestroyDebugReportCallbackEXT)
      gst_vulkan_instance_get_proc_address (instance,
      "vkDestroyDebugReportCallbackEXT");
  if (!instance->dbgDestroyDebugReportCallback) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Failed to retreive vkDestroyDebugReportCallback");
    goto error;
  }
  instance->dbgReportMessage = (PFN_vkDebugReportMessageEXT)
      gst_vulkan_instance_get_proc_address (instance,
      "vkDebugReportMessageEXT");
  if (!instance->dbgReportMessage) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Failed to retreive vkDebugReportMessage");
    goto error;
  }

  {
    VkDebugReportCallbackCreateInfoEXT info = { 0, };

    info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    info.pNext = NULL;
    info.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
        VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT |
        VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    info.pfnCallback = (PFN_vkDebugReportCallbackEXT) _gst_vk_debug_callback;
    info.pUserData = NULL;

    err =
        instance->dbgCreateDebugReportCallback (instance->instance, &info, NULL,
        &instance->msg_callback);
    if (gst_vulkan_error_to_g_error (err, error,
            "vkCreateDebugReportCallback") < 0)
      goto error;
  }

  instance->priv->opened = TRUE;
  GST_OBJECT_UNLOCK (instance);

  return TRUE;

error:
  {
    GST_OBJECT_UNLOCK (instance);
    return FALSE;
  }
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

GstVulkanDevice *
gst_vulkan_instance_create_device (GstVulkanInstance * instance,
    GError ** error)
{
  GstVulkanDevice *device;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), NULL);

  g_signal_emit (instance, gst_vulkan_instance_signals[SIGNAL_CREATE_DEVICE], 0,
      &device);

  if (!device)
    device = gst_vulkan_device_new (instance);

  if (!gst_vulkan_device_open (device, error)) {
    gst_object_unref (device);
    device = NULL;
  }

  return device;
}

/**
 * gst_context_set_vulkan_instance:
 * @context: a #GstContext
 * @instance: a #GstVulkanInstance
 *
 * Sets @instance on @context
 *
 * Since: 1.10
 */
void
gst_context_set_vulkan_instance (GstContext * context,
    GstVulkanInstance * instance)
{
  GstStructure *s;

  g_return_if_fail (context != NULL);
  g_return_if_fail (gst_context_is_writable (context));

  if (instance)
    GST_CAT_LOG (GST_CAT_CONTEXT,
        "setting GstVulkanInstance(%" GST_PTR_FORMAT ") on context(%"
        GST_PTR_FORMAT ")", instance, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR,
      GST_TYPE_VULKAN_INSTANCE, instance, NULL);
}

/**
 * gst_context_get_vulkan_instance:
 * @context: a #GstContext
 * @instance: resulting #GstVulkanInstance
 *
 * Returns: Whether @instance was in @context
 *
 * Since: 1.10
 */
gboolean
gst_context_get_vulkan_instance (GstContext * context,
    GstVulkanInstance ** instance)
{
  const GstStructure *s;
  gboolean ret;

  g_return_val_if_fail (instance != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  s = gst_context_get_structure (context);
  ret = gst_structure_get (s, GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR,
      GST_TYPE_VULKAN_INSTANCE, instance, NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT, "got GstVulkanInstance(%" GST_PTR_FORMAT
      ") from context(%" GST_PTR_FORMAT ")", *instance, context);

  return ret;
}

gboolean
gst_vulkan_instance_handle_context_query (GstElement * element,
    GstQuery * query, GstVulkanInstance ** instance)
{
  gboolean res = FALSE;
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT, FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);

  gst_query_parse_context_type (query, &context_type);

  if (g_strcmp0 (context_type, GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR, TRUE);

    gst_context_set_vulkan_instance (context, *instance);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *instance != NULL;
  }

  return res;
}

gboolean
gst_vulkan_instance_run_context_query (GstElement * element,
    GstVulkanInstance ** instance)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);

  if (*instance && GST_IS_VULKAN_INSTANCE (*instance))
    return TRUE;

  gst_vulkan_global_context_query (element,
      GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR);

  GST_DEBUG_OBJECT (element, "found instance %p", *instance);

  if (*instance)
    return TRUE;

  return FALSE;
}

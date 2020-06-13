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

#include "gstvkinstance.h"

#include <string.h>

/**
 * SECTION:vkinstance
 * @title: GstVulkanInstance
 * @short_description: GStreamer Vulkan instance
 * @see_also: #GstVulkanPhysicalDevice, #GstVulkanDevice
 *
 * #GstVulkanInstance encapsulates the necessary information for the toplevel
 * Vulkan instance object.
 *
 * If GStreamer is built with debugging support, the default Vulkan API chosen
 * can be selected with the environment variable
 * `GST_VULKAN_INSTANCE_API_VERSION=1.0`.  Any subsequent setting of the
 * requested Vulkan API version through the available properties will override
 * the environment variable.
 */

#define APP_SHORT_NAME "GStreamer"

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

enum
{
  PROP_0,
  PROP_REQUESTED_API_MAJOR_VERSION,
  PROP_REQUESTED_API_MINOR_VERSION,
};

#define DEFAULT_REQUESTED_API_VERSION_MAJOR 0
#define DEFAULT_REQUESTED_API_VERSION_MINOR 0

static guint gst_vulkan_instance_signals[LAST_SIGNAL] = { 0 };

static void gst_vulkan_instance_finalize (GObject * object);

struct _GstVulkanInstancePrivate
{
  gboolean opened;
  guint requested_api_major;
  guint requested_api_minor;
  uint32_t supported_instance_api;
};

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkaninstance", 0,
        "Vulkan Instance");
    GST_DEBUG_CATEGORY_INIT (GST_VULKAN_DEBUG_CAT, "vulkandebug", 0,
        "Vulkan Debug");
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&_init, 1);
  }
}

#define GET_PRIV(instance) gst_vulkan_instance_get_instance_private (instance)

#define gst_vulkan_instance_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanInstance, gst_vulkan_instance,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstVulkanInstance)
    _init_debug ());

GstVulkanInstance *
gst_vulkan_instance_new (void)
{
  GstVulkanInstance *instance;

  instance = g_object_new (GST_TYPE_VULKAN_INSTANCE, NULL);
  gst_object_ref_sink (instance);

  return instance;
}

static void
gst_vulkan_instance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanInstance *instance = GST_VULKAN_INSTANCE (object);
  GstVulkanInstancePrivate *priv = GET_PRIV (instance);

  GST_OBJECT_LOCK (instance);
  switch (prop_id) {
    case PROP_REQUESTED_API_MAJOR_VERSION:
      if (priv->opened)
        g_warning ("Attempt to set the requested API version after the "
            "instance has been opened");
      priv->requested_api_major = g_value_get_uint (value);
      break;
    case PROP_REQUESTED_API_MINOR_VERSION:
      if (priv->opened)
        g_warning ("Attempt to set the requested API version after the "
            "instance has been opened");
      priv->requested_api_minor = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (instance);
}

static void
gst_vulkan_instance_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVulkanInstance *instance = GST_VULKAN_INSTANCE (object);
  GstVulkanInstancePrivate *priv = GET_PRIV (instance);

  GST_OBJECT_LOCK (instance);
  switch (prop_id) {
    case PROP_REQUESTED_API_MAJOR_VERSION:
      g_value_set_uint (value, priv->requested_api_major);
      break;
    case PROP_REQUESTED_API_MINOR_VERSION:
      g_value_set_uint (value, priv->requested_api_minor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (instance);
}

static void
gst_vulkan_instance_init (GstVulkanInstance * instance)
{
  GstVulkanInstancePrivate *priv = GET_PRIV (instance);

  priv->requested_api_major = DEFAULT_REQUESTED_API_VERSION_MAJOR;
  priv->requested_api_minor = DEFAULT_REQUESTED_API_VERSION_MINOR;

#if !defined (GST_DISABLE_DEBUG)
  {
    const gchar *api_override = g_getenv ("GST_VULKAN_INSTANCE_API_VERSION");
    if (api_override) {
      gchar *end;
      gint64 major, minor;

      major = g_ascii_strtoll (api_override, &end, 10);
      if (end && end[0] == '.') {
        minor = g_ascii_strtoll (&end[1], NULL, 10);
        if (major > 0 && major < G_MAXINT64 && minor >= 0 && minor < G_MAXINT64) {
          priv->requested_api_major = major;
          priv->requested_api_minor = minor;
        }
      }
    }
  }
#endif
}

static void
gst_vulkan_instance_class_init (GstVulkanInstanceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gst_vulkan_memory_init_once ();
  gst_vulkan_image_memory_init_once ();
  gst_vulkan_buffer_memory_init_once ();

  gobject_class->get_property = gst_vulkan_instance_get_property;
  gobject_class->set_property = gst_vulkan_instance_set_property;
  gobject_class->finalize = gst_vulkan_instance_finalize;

  g_object_class_install_property (gobject_class,
      PROP_REQUESTED_API_MAJOR_VERSION,
      g_param_spec_uint ("requested-api-major", "Requested API Major",
          "Major version of the requested Vulkan API (0 = maximum supported)",
          0, G_MAXUINT, DEFAULT_REQUESTED_API_VERSION_MAJOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_REQUESTED_API_MINOR_VERSION,
      g_param_spec_uint ("requested-api-minor", "Requested API Minor",
          "Minor version of the requested Vulkan API",
          0, G_MAXUINT, DEFAULT_REQUESTED_API_VERSION_MINOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVulkanInstance::create-device:
   * @object: the #GstVulkanDisplay
   *
   * Overrides the #GstVulkanDevice creation mechanism.
   * It can be called from any thread.
   *
   * Returns: (transfer full): the newly created #GstVulkanDevice.
   *
   * Since: 1.18
   */
  gst_vulkan_instance_signals[SIGNAL_CREATE_DEVICE] =
      g_signal_new ("create-device", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, GST_TYPE_VULKAN_DEVICE, 0);
}

static void
gst_vulkan_instance_finalize (GObject * object)
{
  GstVulkanInstance *instance = GST_VULKAN_INSTANCE (object);
  GstVulkanInstancePrivate *priv = GET_PRIV (instance);

  if (priv->opened) {
    if (instance->dbgDestroyDebugReportCallback)
      instance->dbgDestroyDebugReportCallback (instance->instance,
          instance->msg_callback, NULL);

    g_free (instance->physical_devices);
  }
  priv->opened = FALSE;

  if (instance->instance)
    vkDestroyInstance (instance->instance, NULL);
  instance->instance = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

VKAPI_ATTR static VkBool32
_gst_vk_debug_callback (VkDebugReportFlagsEXT msgFlags,
    VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location,
    int32_t msgCode, const char *pLayerPrefix, const char *pMsg,
    void *pUserData)
{
  if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
    GST_CAT_ERROR (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
    g_critical ("[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
  } else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
    GST_CAT_WARNING (GST_VULKAN_DEBUG_CAT, "[%s] Code %d : %s", pLayerPrefix,
        msgCode, pMsg);
    g_warning ("[%s] Code %d : %s", pLayerPrefix, msgCode, pMsg);
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

static void
gst_vulkan_get_supported_api_version_unlocked (GstVulkanInstance * instance)
{
  GstVulkanInstancePrivate *priv = GET_PRIV (instance);
  PFN_vkEnumerateInstanceVersion gst_vkEnumerateInstanceVersion;

  if (priv->supported_instance_api)
    return;

  gst_vkEnumerateInstanceVersion =
      (PFN_vkEnumerateInstanceVersion) vkGetInstanceProcAddr (NULL,
      "vkEnumerateInstanceVersion");

  if (!gst_vkEnumerateInstanceVersion
      || VK_SUCCESS !=
      gst_vkEnumerateInstanceVersion (&priv->supported_instance_api)) {
    priv->supported_instance_api = VK_MAKE_VERSION (1, 0, 0);
  }
}

/**
 * gst_vulkan_instance_open:
 * @instance: a #GstVulkanInstance
 * @error: #GError
 *
 * Returns: whether the instance could be created
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_instance_open (GstVulkanInstance * instance, GError ** error)
{
  GstVulkanInstancePrivate *priv;
  VkExtensionProperties *instance_extensions;
  char *extension_names[64];    /* FIXME: make dynamic */
  VkLayerProperties *instance_layers;
  uint32_t instance_extension_count = 0;
  uint32_t enabled_extension_count = 0;
  uint32_t instance_layer_count = 0;
  uint32_t requested_instance_api;
  gboolean have_debug_extension = FALSE;
  VkResult err;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), FALSE);

  priv = GET_PRIV (instance);

  GST_OBJECT_LOCK (instance);
  if (priv->opened) {
    GST_OBJECT_UNLOCK (instance);
    return TRUE;
  }

  gst_vulkan_get_supported_api_version_unlocked (instance);
  if (priv->requested_api_major) {
    requested_instance_api =
        VK_MAKE_VERSION (priv->requested_api_major, priv->requested_api_minor,
        0);
    GST_INFO_OBJECT (instance, "requesting Vulkan API %u.%u, max supported "
        "%u.%u", priv->requested_api_major, priv->requested_api_minor,
        VK_VERSION_MAJOR (priv->supported_instance_api),
        VK_VERSION_MINOR (priv->supported_instance_api));
  } else {
    requested_instance_api = priv->supported_instance_api;
    GST_INFO_OBJECT (instance, "requesting maximum supported API %u.%u",
        VK_VERSION_MAJOR (priv->supported_instance_api),
        VK_VERSION_MINOR (priv->supported_instance_api));
  }

  if (requested_instance_api > priv->supported_instance_api) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Requested API version (%u.%u) is larger than the maximum supported "
        "version (%u.%u)", VK_VERSION_MAJOR (requested_instance_api),
        VK_VERSION_MINOR (requested_instance_api),
        VK_VERSION_MAJOR (priv->supported_instance_api),
        VK_VERSION_MINOR (priv->supported_instance_api));
    goto error;
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

  g_free (instance_layers);

  err =
      vkEnumerateInstanceExtensionProperties (NULL, &instance_extension_count,
      NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateInstanceExtensionProperties") < 0) {
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
        have_debug_extension = TRUE;
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
      g_free (instance_extensions);
      goto error;
    }
    if (!winsys_ext_found) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "vkEnumerateInstanceExtensionProperties failed to find the required "
          "\"%s\" window system extension", winsys_ext_name);
      g_free (instance_extensions);
      goto error;
    }
  }

  {
    VkApplicationInfo app = { 0, };
    VkInstanceCreateInfo inst_info = { 0, };

    /* *INDENT-OFF* */
    app = (VkApplicationInfo) {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = APP_SHORT_NAME,
        .applicationVersion = 0,
        .pEngineName = APP_SHORT_NAME,
        .engineVersion = 0,
        .apiVersion = requested_instance_api,
    };

    inst_info = (VkInstanceCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .pApplicationInfo = &app,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = enabled_extension_count,
        .ppEnabledExtensionNames = (const char *const *) extension_names
    };
    /* *INDENT-ON* */

    err = vkCreateInstance (&inst_info, NULL, &instance->instance);
    if (gst_vulkan_error_to_g_error (err, error, "vkCreateInstance") < 0) {
      g_free (instance_extensions);
      goto error;
    }
  }

  g_free (instance_extensions);

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

  if (have_debug_extension) {
    VkDebugReportCallbackCreateInfoEXT info = { 0, };

    instance->dbgCreateDebugReportCallback =
        (PFN_vkCreateDebugReportCallbackEXT)
        gst_vulkan_instance_get_proc_address (instance,
        "vkCreateDebugReportCallbackEXT");
    if (!instance->dbgCreateDebugReportCallback) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "Failed to retrieve vkCreateDebugReportCallback");
      goto error;
    }
    instance->dbgDestroyDebugReportCallback =
        (PFN_vkDestroyDebugReportCallbackEXT)
        gst_vulkan_instance_get_proc_address (instance,
        "vkDestroyDebugReportCallbackEXT");
    if (!instance->dbgDestroyDebugReportCallback) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "Failed to retrieve vkDestroyDebugReportCallback");
      goto error;
    }
    instance->dbgReportMessage = (PFN_vkDebugReportMessageEXT)
        gst_vulkan_instance_get_proc_address (instance,
        "vkDebugReportMessageEXT");
    if (!instance->dbgReportMessage) {
      g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
          "Failed to retrieve vkDebugReportMessage");
      goto error;
    }

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

  priv->opened = TRUE;
  GST_OBJECT_UNLOCK (instance);

  return TRUE;

error:
  {
    GST_OBJECT_UNLOCK (instance);
    return FALSE;
  }
}

/**
 * gst_vulkan_instance_get_proc_address:
 * @instance: a #GstVulkanInstance
 * @name: name of the function to retrieve
 *
 * Performs vkGetInstanceProcAddr() with @instance and @name
 *
 * Returns: the function pointer for @name or %NULL
 *
 * Since: 1.18
 */
gpointer
gst_vulkan_instance_get_proc_address (GstVulkanInstance * instance,
    const gchar * name)
{
  gpointer ret;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), NULL);
  g_return_val_if_fail (instance->instance != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  ret = vkGetInstanceProcAddr (instance->instance, name);

  GST_TRACE_OBJECT (instance, "%s = %p", name, ret);

  return ret;
}

/**
 * gst_vulkan_instance_create_device:
 * @instance: a #GstVulkanIncstance
 *
 * Returns: (transfer full): a new #GstVulkanDevice
 *
 * Since: 1.18
 */
GstVulkanDevice *
gst_vulkan_instance_create_device (GstVulkanInstance * instance,
    GError ** error)
{
  GstVulkanDevice *device;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), NULL);

  g_signal_emit (instance, gst_vulkan_instance_signals[SIGNAL_CREATE_DEVICE], 0,
      &device);

  if (!device) {
    device = gst_vulkan_device_new_with_index (instance, 0);
  }

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
 * Since: 1.18
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
 * Since: 1.18
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

/**
 * gst_vulkan_instance_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type #GST_QUERY_CONTEXT
 * @instance: (nullable): the #GstVulkanInstance
 *
 * If a #GstVulkanInstance is requested in @query, sets @instance as the reply.
 *
 * Intended for use with element query handlers to respond to #GST_QUERY_CONTEXT
 * for a #GstVulkanInstance.
 *
 * Returns: whether @query was responded to with @instance
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_instance_handle_context_query (GstElement * element,
    GstQuery * query, GstVulkanInstance * instance)
{
  gboolean res = FALSE;
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT, FALSE);

  if (!instance)
    return FALSE;

  gst_query_parse_context_type (query, &context_type);

  if (g_strcmp0 (context_type, GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR, TRUE);

    gst_context_set_vulkan_instance (context, instance);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = instance != NULL;
  }

  return res;
}

/**
 * gst_vulkan_instance_run_context_query:
 * @element: a #GstElement
 * @instance: (inout): a #GstVulkanInstance
 *
 * Attempt to retrieve a #GstVulkanInstance using #GST_QUERY_CONTEXT from the
 * surrounding elements of @element.
 *
 * Returns: whether @instance contains a valid #GstVulkanInstance
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_instance_run_context_query (GstElement * element,
    GstVulkanInstance ** instance)
{
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (instance != NULL, FALSE);

  _init_debug ();

  if (*instance && GST_IS_VULKAN_INSTANCE (*instance))
    return TRUE;

  gst_vulkan_global_context_query (element,
      GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR);

  GST_DEBUG_OBJECT (element, "found instance %p", *instance);

  if (*instance)
    return TRUE;

  return FALSE;
}

/**
 * gst_vulkan_instance_check_version:
 * @instance: a #GstVulkanInstance
 * @major: major version
 * @minor: minor version
 * @patch: patch version
 *
 * Check if the configured vulkan instance supports the specified version.
 * Will not work prior to opening the instance with gst_vulkan_instance_open().
 * If a specific version is requested, the @patch level is ignored.
 *
 * Returns: whether @instance is at least the requested version.
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_instance_check_version (GstVulkanInstance * instance,
    guint major, guint minor, guint patch)
{
  GstVulkanInstancePrivate *priv;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), FALSE);

  priv = GET_PRIV (instance);

  return (priv->requested_api_major == 0
      && VK_MAKE_VERSION (major, minor, patch) <= priv->supported_instance_api)
      || (priv->requested_api_major >= 0 && (major < priv->requested_api_major
          || (major == priv->requested_api_major
              && minor <= priv->requested_api_minor)));
}

/**
 * gst_vulkan_instance_get_version:
 * @instance: a #GstVulkanInstance
 * @major: major version
 * @minor: minor version
 * @patch: patch version
 *
 * Retrieve the vulkan instance configured version.  Only returns the supported
 * API version by the instance without taking into account the requested API
 * version.  This means gst_vulkan_instance_check_version() will return
 * different values if a specific version has been requested (which is the
 * default) than a version check that is performed manually by retrieving the
 * version with this function.
 *
 * Since: 1.18
 */
void
gst_vulkan_instance_get_version (GstVulkanInstance * instance,
    guint * major, guint * minor, guint * patch)
{
  GstVulkanInstancePrivate *priv;

  g_return_if_fail (GST_IS_VULKAN_INSTANCE (instance));

  priv = GET_PRIV (instance);

  GST_OBJECT_LOCK (instance);
  if (!priv->supported_instance_api)
    gst_vulkan_get_supported_api_version_unlocked (instance);

  if (major)
    *major = VK_VERSION_MAJOR (priv->supported_instance_api);
  if (minor)
    *minor = VK_VERSION_MINOR (priv->supported_instance_api);
  if (patch)
    *patch = VK_VERSION_PATCH (priv->supported_instance_api);
  GST_OBJECT_UNLOCK (instance);
}

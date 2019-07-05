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

#include "gstvkdevice.h"

#include <string.h>

/**
 * SECTION:vkdevice
 * @title: GstVulkanDevice
 * @short_description: Vulkan device
 * @see_also: #GstVulkanInstance
 *
 * A #GstVulkanDevice encapsulates a VkDevice
 */

#define GST_CAT_DEFAULT gst_vulkan_device_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_CONTEXT);

enum
{
  PROP_0,
  PROP_INSTANCE,
};

static void gst_vulkan_device_finalize (GObject * object);

struct _GstVulkanDevicePrivate
{
  gboolean opened;
};

static void
_init_debug (void)
{
  static volatile gsize init;

  if (g_once_init_enter (&init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkandevice", 0,
        "Vulkan Device");
    GST_DEBUG_CATEGORY_GET (GST_CAT_CONTEXT, "GST_CONTEXT");
    g_once_init_leave (&init, 1);
  }
}

#define gst_vulkan_device_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanDevice, gst_vulkan_device, GST_TYPE_OBJECT,
    G_ADD_PRIVATE (GstVulkanDevice);
    _init_debug ());

/**
 * gst_vulkan_device_new:
 * @instance: the parent #GstVulkanInstance
 *
 * Returns: (transfer full): a new #GstVulkanDevice
 *
 * Since: 1.18
 */
GstVulkanDevice *
gst_vulkan_device_new (GstVulkanInstance * instance)
{
  GstVulkanDevice *device;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), NULL);

  device = g_object_new (GST_TYPE_VULKAN_DEVICE, "instance", instance, NULL);
  gst_object_ref_sink (device);

  /* FIXME: select this externally */
  device->device_index = 0;

  return device;
}

static void
gst_vulkan_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanDevice *device = GST_VULKAN_DEVICE (object);

  switch (prop_id) {
    case PROP_INSTANCE:
      device->instance = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanDevice *device = GST_VULKAN_DEVICE (object);

  switch (prop_id) {
    case PROP_INSTANCE:
      g_value_set_object (value, device->instance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_device_init (GstVulkanDevice * device)
{
  device->priv = gst_vulkan_device_get_instance_private (device);
}

static void
gst_vulkan_device_class_init (GstVulkanDeviceClass * device_class)
{
  GObjectClass *gobject_class = (GObjectClass *) device_class;

  gobject_class->set_property = gst_vulkan_device_set_property;
  gobject_class->get_property = gst_vulkan_device_get_property;
  gobject_class->finalize = gst_vulkan_device_finalize;

  g_object_class_install_property (gobject_class, PROP_INSTANCE,
      g_param_spec_object ("instance", "Instance",
          "Associated Vulkan Instance",
          GST_TYPE_VULKAN_INSTANCE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gst_vulkan_device_finalize (GObject * object)
{
  GstVulkanDevice *device = GST_VULKAN_DEVICE (object);

  g_free (device->queue_family_props);
  device->queue_family_props = NULL;

  if (device->device) {
    vkDeviceWaitIdle (device->device);
    vkDestroyDevice (device->device, NULL);
  }
  device->device = VK_NULL_HANDLE;

  if (device->instance)
    gst_object_unref (device->instance);
  device->instance = VK_NULL_HANDLE;

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

  gpu = gst_vulkan_device_get_physical_device (device);
  if (!gpu) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Failed to retrieve physical device");
    return FALSE;
  }

  vkGetPhysicalDeviceProperties (gpu, &props);

  GST_INFO_OBJECT (device, "device name %s type %s api version %u.%u.%u, "
      "driver version %u.%u.%u vendor ID 0x%x, device ID 0x%x",
      props.deviceName, _device_type_to_string (props.deviceType),
      VK_VERSION_MAJOR (props.apiVersion), VK_VERSION_MINOR (props.apiVersion),
      VK_VERSION_PATCH (props.apiVersion),
      VK_VERSION_MAJOR (props.driverVersion),
      VK_VERSION_MINOR (props.driverVersion),
      VK_VERSION_PATCH (props.driverVersion), props.vendorID, props.deviceID);

  return TRUE;
}

/**
 * gst_vulkan_device_open:
 * @device: a #GstVulkanDevice
 * @error: a #GError
 *
 * Attempts to create the internal #VkDevice object.
 *
 * Returns: whether a vulkan device could be created
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_device_open (GstVulkanDevice * device, GError ** error)
{
  const char *extension_names[64];
  uint32_t enabled_extension_count = 0;
  uint32_t device_extension_count = 0;
  VkExtensionProperties *device_extensions = NULL;
  uint32_t device_layer_count = 0;
  VkLayerProperties *device_layers;
  gboolean have_swapchain_ext;
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

  g_free (device_layers);
  device_layers = NULL;

  err =
      vkEnumerateDeviceExtensionProperties (gpu, NULL,
      &device_extension_count, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceExtensionProperties") < 0) {
    goto error;
  }
  GST_DEBUG_OBJECT (device, "Found %u extensions", device_extension_count);

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

  for (i = 0; i < device_extension_count; i++) {
    GST_TRACE_OBJECT (device, "checking device extension %s",
        device_extensions[i].extensionName);
    if (!strcmp (VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            device_extensions[i].extensionName)) {
      have_swapchain_ext = TRUE;
      extension_names[enabled_extension_count++] =
          (gchar *) VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    }
    g_assert (enabled_extension_count < 64);
  }
  if (!have_swapchain_ext) {
    g_set_error_literal (error, GST_VULKAN_ERROR,
        VK_ERROR_EXTENSION_NOT_PRESENT,
        "Failed to find required extension, \"" VK_KHR_SWAPCHAIN_EXTENSION_NAME
        "\"");
    goto error;
  }
  g_free (device_extensions);

  vkGetPhysicalDeviceProperties (gpu, &device->gpu_props);
  vkGetPhysicalDeviceMemoryProperties (gpu, &device->memory_properties);
  vkGetPhysicalDeviceFeatures (gpu, &device->gpu_features);

  vkGetPhysicalDeviceQueueFamilyProperties (gpu, &device->n_queue_families,
      NULL);
  g_assert (device->n_queue_families >= 1);

  device->queue_family_props =
      g_new0 (VkQueueFamilyProperties, device->n_queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties (gpu, &device->n_queue_families,
      device->queue_family_props);

  /* FIXME: allow overriding/selecting */
  for (i = 0; i < device->n_queue_families; i++) {
    if (device->queue_family_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
      break;
  }
  if (i >= device->n_queue_families) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Failed to find a compatible queue family");
    goto error;
  }
  device->queue_family_id = i;
  device->n_queues = 1;

  {
    VkDeviceQueueCreateInfo queue_info = { 0, };
    VkDeviceCreateInfo device_info = { 0, };
    gfloat queue_priority = 0.5;

    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.pNext = NULL;
    queue_info.queueFamilyIndex = device->queue_family_id;
    queue_info.queueCount = device->n_queues;
    queue_info.pQueuePriorities = &queue_priority;

    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.pNext = NULL;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledLayerCount = 0;
    device_info.ppEnabledLayerNames = NULL;
    device_info.enabledExtensionCount = enabled_extension_count;
    device_info.ppEnabledExtensionNames = (const char *const *) extension_names;
    device_info.pEnabledFeatures = NULL;

    err = vkCreateDevice (gpu, &device_info, NULL, &device->device);
    if (gst_vulkan_error_to_g_error (err, error, "vkCreateDevice") < 0) {
      goto error;
    }
  }

  GST_OBJECT_UNLOCK (device);
  return TRUE;

error:
  {
    GST_OBJECT_UNLOCK (device);
    return FALSE;
  }
}

/**
 * gst_vulkan_device_get_queue:
 * @device: a #GstVulkanDevice
 * @queue_family: a queue family to retrieve
 * @queue_i: index of the family to retrieve
 *
 * Returns: (transfer full): a new #GstVulkanQueue
 *
 * Since: 1.18
 */
GstVulkanQueue *
gst_vulkan_device_get_queue (GstVulkanDevice * device, guint32 queue_family,
    guint32 queue_i)
{
  GstVulkanQueue *ret;

  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);
  g_return_val_if_fail (device->device != NULL, NULL);
  g_return_val_if_fail (queue_family < device->n_queues, NULL);
  g_return_val_if_fail (queue_i <
      device->queue_family_props[queue_family].queueCount, NULL);

  ret = g_object_new (GST_TYPE_VULKAN_QUEUE, NULL);
  gst_object_ref_sink (ret);
  ret->device = gst_object_ref (device);
  ret->family = queue_family;
  ret->index = queue_i;

  vkGetDeviceQueue (device->device, queue_family, queue_i, &ret->queue);

  return ret;
}

/**
 * gst_vulkan_device_foreach_queue:
 * @device: a #GstVulkanDevice
 * @func: (scope call): a #GstVulkanDeviceForEachQueueFunc to run for each #GstVulkanQueue
 * @user_data: (closure func): user data to pass to each call of @func
 *
 * Iterate over each queue family available on #GstVulkanDevice
 *
 * Since: 1.18
 */
void
gst_vulkan_device_foreach_queue (GstVulkanDevice * device,
    GstVulkanDeviceForEachQueueFunc func, gpointer user_data)
{
  gboolean done = FALSE;
  guint i;

  for (i = 0; i < device->n_queues; i++) {
    GstVulkanQueue *queue =
        gst_vulkan_device_get_queue (device, device->queue_family_id, i);

    if (!func (device, queue, user_data))
      done = TRUE;

    gst_object_unref (queue);

    if (done)
      break;
  }
}

/**
 * gst_vulkan_device_get_proc_address:
 * @device: a #GstVulkanDevice
 * @name: name of the function to retrieve
 *
 * Performs vkGetDeviceProcAddr() with @device and @name
 *
 * Returns: the function pointer for @name or %NULL
 *
 * Since: 1.18
 */
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

/**
 * gst_vulkan_device_get_instance:
 * @device: a #GstVulkanDevice
 *
 * Returns: (transfer full): the #GstVulkanInstance used to create this @device
 *
 * Since: 1.18
 */
GstVulkanInstance *
gst_vulkan_device_get_instance (GstVulkanDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_DEVICE (device), NULL);

  return device->instance ? gst_object_ref (device->instance) : NULL;
}

/**
 * gst_vulkan_device_get_physical_device: (skip)
 * @device: a #GstVulkanDevice
 *
 * Returns: The VkPhysicalDevice used to create @device
 *
 * Since: 1.18
 */
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

/**
 * gst_context_set_vulkan_device:
 * @context: a #GstContext
 * @device: a #GstVulkanDevice
 *
 * Sets @device on @context
 *
 * Since: 1.18
 */
void
gst_context_set_vulkan_device (GstContext * context, GstVulkanDevice * device)
{
  GstStructure *s;

  g_return_if_fail (context != NULL);
  g_return_if_fail (gst_context_is_writable (context));

  if (device)
    GST_CAT_LOG (GST_CAT_CONTEXT,
        "setting GstVulkanDevice(%" GST_PTR_FORMAT ") on context(%"
        GST_PTR_FORMAT ")", device, context);

  s = gst_context_writable_structure (context);
  gst_structure_set (s, GST_VULKAN_DEVICE_CONTEXT_TYPE_STR,
      GST_TYPE_VULKAN_DEVICE, device, NULL);
}

/**
 * gst_context_get_vulkan_device:
 * @context: a #GstContext
 * @device: resulting #GstVulkanDevice
 *
 * Returns: Whether @device was in @context
 *
 * Since: 1.18
 */
gboolean
gst_context_get_vulkan_device (GstContext * context, GstVulkanDevice ** device)
{
  const GstStructure *s;
  gboolean ret;

  g_return_val_if_fail (device != NULL, FALSE);
  g_return_val_if_fail (context != NULL, FALSE);

  s = gst_context_get_structure (context);
  ret = gst_structure_get (s, GST_VULKAN_DEVICE_CONTEXT_TYPE_STR,
      GST_TYPE_VULKAN_DEVICE, device, NULL);

  GST_CAT_LOG (GST_CAT_CONTEXT, "got GstVulkanDevice(%" GST_PTR_FORMAT
      ") from context(%" GST_PTR_FORMAT ")", *device, context);

  return ret;
}

/**
 * gst_vulkan_device_handle_context_query:
 * @element: a #GstElement
 * @query: a #GstQuery of type #GST_QUERY_CONTEXT
 * @device: the #GstVulkanDevice
 *
 * If a #GstVulkanDevice is requested in @query, sets @device as the reply.
 *
 * Intended for use with element query handlers to respond to #GST_QUERY_CONTEXT
 * for a #GstVulkanDevice.
 *
 * Returns: whether @query was responded to with @device
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_device_handle_context_query (GstElement * element, GstQuery * query,
    GstVulkanDevice ** device)
{
  gboolean res = FALSE;
  const gchar *context_type;
  GstContext *context, *old_context;

  g_return_val_if_fail (element != NULL, FALSE);
  g_return_val_if_fail (query != NULL, FALSE);
  g_return_val_if_fail (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT, FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  gst_query_parse_context_type (query, &context_type);

  if (g_strcmp0 (context_type, GST_VULKAN_DEVICE_CONTEXT_TYPE_STR) == 0) {
    gst_query_parse_context (query, &old_context);

    if (old_context)
      context = gst_context_copy (old_context);
    else
      context = gst_context_new (GST_VULKAN_DEVICE_CONTEXT_TYPE_STR, TRUE);

    gst_context_set_vulkan_device (context, *device);
    gst_query_set_context (query, context);
    gst_context_unref (context);

    res = *device != NULL;
  }

  return res;
}

/**
 * gst_vulkan_device_run_context_query:
 * @element: a #GstElement
 * @device: (inout): a #GstVulkanDevice
 *
 * Attempt to retrieve a #GstVulkanDevice using #GST_QUERY_CONTEXT from the
 * surrounding elements of @element.
 *
 * Returns: whether @device contains a valid #GstVulkanDevice
 *
 * Since: 1.18
 */
gboolean
gst_vulkan_device_run_context_query (GstElement * element,
    GstVulkanDevice ** device)
{
  GstQuery *query;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  _init_debug ();

  if (*device && GST_IS_VULKAN_DEVICE (*device))
    return TRUE;

  if ((query =
          gst_vulkan_local_context_query (element,
              GST_VULKAN_DEVICE_CONTEXT_TYPE_STR))) {
    GstContext *context;

    gst_query_parse_context (query, &context);
    if (context)
      gst_context_get_vulkan_device (context, device);

    gst_query_unref (query);
  }

  GST_DEBUG_OBJECT (element, "found device %p", *device);

  if (*device)
    return TRUE;

  return FALSE;
}

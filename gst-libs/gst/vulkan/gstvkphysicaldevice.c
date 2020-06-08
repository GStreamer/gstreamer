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

#include "gstvkphysicaldevice.h"

#include "gstvkdebug.h"

#include <string.h>

/**
 * SECTION:vkphysicaldevice
 * @title: GstVulkanPhysicalDevice
 * @short_description: Vulkan physical device
 * @see_also: #GstVulkanInstance, #GstVulkanDevice
 *
 * A #GstVulkanPhysicalDevice encapsulates a VkPhysicalDevice
 */

#define GST_CAT_DEFAULT gst_vulkan_physical_device_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_INSTANCE,
  PROP_DEVICE_ID,
  PROP_NAME,
};

static void gst_vulkan_physical_device_finalize (GObject * object);

struct _GstVulkanPhysicalDevicePrivate
{
  guint dummy;
};

static void
_init_debug (void)
{
  static volatile gsize init;

  if (g_once_init_enter (&init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkandevice", 0,
        "Vulkan Device");
    g_once_init_leave (&init, 1);
  }
}

#define GET_PRIV(device) gst_vulkan_physical_device_get_instance_private (device)

#define gst_vulkan_physical_device_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanPhysicalDevice, gst_vulkan_physical_device,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstVulkanPhysicalDevice);
    _init_debug ());

static gboolean gst_vulkan_physical_device_fill_info (GstVulkanPhysicalDevice *
    device, GError ** error);

/**
 * gst_vulkan_physical_device_new:
 * @instance: the parent #GstVulkanInstance
 *
 * Returns: (transfer full): a new #GstVulkanPhysicalDevice
 *
 * Since: 1.18
 */
GstVulkanPhysicalDevice *
gst_vulkan_physical_device_new (GstVulkanInstance * instance,
    guint device_index)
{
  GstVulkanPhysicalDevice *device;

  g_return_val_if_fail (GST_IS_VULKAN_INSTANCE (instance), NULL);

  device = g_object_new (GST_TYPE_VULKAN_PHYSICAL_DEVICE, "instance", instance,
      "device-index", device_index, NULL);
  gst_object_ref_sink (device);

  return device;
}

static void
gst_vulkan_physical_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVulkanPhysicalDevice *device = GST_VULKAN_PHYSICAL_DEVICE (object);

  switch (prop_id) {
    case PROP_INSTANCE:
      device->instance = g_value_dup_object (value);
      break;
    case PROP_DEVICE_ID:{
      guint device_id = g_value_get_uint (value);
      if (device->instance == VK_NULL_HANDLE
          || device_id >= device->instance->n_physical_devices) {
        g_critical ("%s: Cannot set device-index larger than the "
            "number of physical devices", GST_OBJECT_NAME (device));
      } else {
        device->device_index = device_id;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_physical_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVulkanPhysicalDevice *device = GST_VULKAN_PHYSICAL_DEVICE (object);

  switch (prop_id) {
    case PROP_INSTANCE:
      g_value_set_object (value, device->instance);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, device->device_index);
      break;
    case PROP_NAME:
      g_value_set_string (value, device->properties.deviceName);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vulkan_physical_device_init (GstVulkanPhysicalDevice * device)
{
}

static void
gst_vulkan_physical_device_constructed (GObject * object)
{
  GstVulkanPhysicalDevice *device = GST_VULKAN_PHYSICAL_DEVICE (object);
  GError *error = NULL;

  if (device->instance == VK_NULL_HANDLE) {
    GST_ERROR_OBJECT (object, "Constructed without any instance set");
    return;
  }

  device->device = device->instance->physical_devices[device->device_index];

  if (!gst_vulkan_physical_device_fill_info (device, &error)) {
    GST_ERROR_OBJECT (object, "%s", error->message);
    g_clear_error (&error);
  }
}

static void
gst_vulkan_physical_device_class_init (GstVulkanPhysicalDeviceClass *
    device_class)
{
  GObjectClass *gobject_class = (GObjectClass *) device_class;

  gobject_class->set_property = gst_vulkan_physical_device_set_property;
  gobject_class->get_property = gst_vulkan_physical_device_get_property;
  gobject_class->finalize = gst_vulkan_physical_device_finalize;
  gobject_class->constructed = gst_vulkan_physical_device_constructed;

  g_object_class_install_property (gobject_class, PROP_INSTANCE,
      g_param_spec_object ("instance", "Instance",
          "Associated Vulkan Instance",
          GST_TYPE_VULKAN_INSTANCE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-index", "Device Index", "Device Index", 0,
          G_MAXUINT32, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_NAME,
      g_param_spec_string ("name", "Name", "Device Name", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_vulkan_physical_device_finalize (GObject * object)
{
  GstVulkanPhysicalDevice *device = GST_VULKAN_PHYSICAL_DEVICE (object);

  g_free (device->device_layers);
  device->device_layers = NULL;

  g_free (device->device_extensions);
  device->device_extensions = NULL;

  g_free (device->queue_family_props);
  device->queue_family_props = NULL;

  if (device->instance)
    gst_object_unref (device->instance);
  device->instance = VK_NULL_HANDLE;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#define DEBUG_BOOL(prefix, name, value)                             \
  GST_DEBUG_OBJECT (device, prefix " " G_STRINGIFY(name) ": %s",    \
    value ? "YES" : "NO")

static gboolean
dump_features (GstVulkanPhysicalDevice * device, GError ** error)
{
#define DEBUG_BOOL_FEATURE(name) DEBUG_BOOL("support for", name, device->features.name)
  DEBUG_BOOL_FEATURE (robustBufferAccess);
  DEBUG_BOOL_FEATURE (fullDrawIndexUint32);
  DEBUG_BOOL_FEATURE (imageCubeArray);
  DEBUG_BOOL_FEATURE (independentBlend);
  DEBUG_BOOL_FEATURE (geometryShader);
  DEBUG_BOOL_FEATURE (tessellationShader);
  DEBUG_BOOL_FEATURE (sampleRateShading);
  DEBUG_BOOL_FEATURE (sampleRateShading);
  DEBUG_BOOL_FEATURE (dualSrcBlend);
  DEBUG_BOOL_FEATURE (logicOp);
  DEBUG_BOOL_FEATURE (multiDrawIndirect);
  DEBUG_BOOL_FEATURE (drawIndirectFirstInstance);
  DEBUG_BOOL_FEATURE (depthClamp);
  DEBUG_BOOL_FEATURE (depthBiasClamp);
  DEBUG_BOOL_FEATURE (fillModeNonSolid);
  DEBUG_BOOL_FEATURE (depthBounds);
  DEBUG_BOOL_FEATURE (wideLines);
  DEBUG_BOOL_FEATURE (largePoints);
  DEBUG_BOOL_FEATURE (alphaToOne);
  DEBUG_BOOL_FEATURE (multiViewport);
  DEBUG_BOOL_FEATURE (samplerAnisotropy);
  DEBUG_BOOL_FEATURE (textureCompressionETC2);
  DEBUG_BOOL_FEATURE (textureCompressionASTC_LDR);
  DEBUG_BOOL_FEATURE (textureCompressionBC);
  DEBUG_BOOL_FEATURE (occlusionQueryPrecise);
  DEBUG_BOOL_FEATURE (pipelineStatisticsQuery);
  DEBUG_BOOL_FEATURE (vertexPipelineStoresAndAtomics);
  DEBUG_BOOL_FEATURE (fragmentStoresAndAtomics);
  DEBUG_BOOL_FEATURE (shaderTessellationAndGeometryPointSize);
  DEBUG_BOOL_FEATURE (shaderImageGatherExtended);
  DEBUG_BOOL_FEATURE (shaderStorageImageExtendedFormats);
  DEBUG_BOOL_FEATURE (shaderStorageImageMultisample);
  DEBUG_BOOL_FEATURE (shaderStorageImageReadWithoutFormat);
  DEBUG_BOOL_FEATURE (shaderStorageImageWriteWithoutFormat);
  DEBUG_BOOL_FEATURE (shaderUniformBufferArrayDynamicIndexing);
  DEBUG_BOOL_FEATURE (shaderSampledImageArrayDynamicIndexing);
  DEBUG_BOOL_FEATURE (shaderStorageBufferArrayDynamicIndexing);
  DEBUG_BOOL_FEATURE (shaderStorageImageArrayDynamicIndexing);
  DEBUG_BOOL_FEATURE (shaderClipDistance);
  DEBUG_BOOL_FEATURE (shaderCullDistance);
  DEBUG_BOOL_FEATURE (shaderFloat64);
  DEBUG_BOOL_FEATURE (shaderInt64);
  DEBUG_BOOL_FEATURE (shaderInt16);
  DEBUG_BOOL_FEATURE (shaderResourceResidency);
  DEBUG_BOOL_FEATURE (shaderResourceMinLod);
  DEBUG_BOOL_FEATURE (sparseBinding);
  DEBUG_BOOL_FEATURE (sparseResidencyBuffer);
  DEBUG_BOOL_FEATURE (sparseResidencyImage2D);
  DEBUG_BOOL_FEATURE (sparseResidencyImage3D);
  DEBUG_BOOL_FEATURE (sparseResidency2Samples);
  DEBUG_BOOL_FEATURE (sparseResidency4Samples);
  DEBUG_BOOL_FEATURE (sparseResidency8Samples);
  DEBUG_BOOL_FEATURE (sparseResidency16Samples);
  DEBUG_BOOL_FEATURE (sparseResidencyAliased);
  DEBUG_BOOL_FEATURE (variableMultisampleRate);
  DEBUG_BOOL_FEATURE (inheritedQueries);

#undef DEBUG_BOOL_FEATURE

  return TRUE;
}

static gboolean
dump_memory_properties (GstVulkanPhysicalDevice * device, GError ** error)
{
  int i;

  GST_DEBUG_OBJECT (device, "found %" G_GUINT32_FORMAT " memory heaps",
      device->memory_properties.memoryHeapCount);
  for (i = 0; i < device->memory_properties.memoryHeapCount; i++) {
    gchar *prop_flags_str =
        gst_vulkan_memory_heap_flags_to_string (device->
        memory_properties.memoryHeaps[i].flags);
    GST_LOG_OBJECT (device,
        "memory heap at index %i has size %" G_GUINT64_FORMAT
        " and flags (0x%x) \'%s\'", i,
        (guint64) device->memory_properties.memoryHeaps[i].size,
        device->memory_properties.memoryHeaps[i].flags, prop_flags_str);
    g_free (prop_flags_str);
  }
  GST_DEBUG_OBJECT (device, "found %" G_GUINT32_FORMAT " memory types",
      device->memory_properties.memoryTypeCount);
  for (i = 0; i < device->memory_properties.memoryTypeCount; i++) {
    gchar *prop_flags_str =
        gst_vulkan_memory_property_flags_to_string (device->memory_properties.
        memoryTypes[i].propertyFlags);
    GST_LOG_OBJECT (device,
        "memory type at index %i is allocatable from "
        "heap %i with flags (0x%x) \'%s\'", i,
        device->memory_properties.memoryTypes[i].heapIndex,
        device->memory_properties.memoryTypes[i].propertyFlags, prop_flags_str);
    g_free (prop_flags_str);
  }

  return TRUE;
}

static gboolean
dump_queue_properties (GstVulkanPhysicalDevice * device, GError ** error)
{
  int i;

  GST_DEBUG_OBJECT (device, "found %" G_GUINT32_FORMAT " queue families",
      device->n_queue_families);
  for (i = 0; i < device->n_queue_families; i++) {
    gchar *queue_flags_str =
        gst_vulkan_queue_flags_to_string (device->
        queue_family_props[i].queueFlags);
    GST_LOG_OBJECT (device,
        "queue family at index %i supports %i queues "
        "with flags (0x%x) \'%s\', %" G_GUINT32_FORMAT " timestamp bits and "
        "a minimum image transfer granuality of %" GST_VULKAN_EXTENT3D_FORMAT,
        i, device->queue_family_props[i].queueCount,
        device->queue_family_props[i].queueFlags, queue_flags_str,
        device->queue_family_props[i].timestampValidBits,
        GST_VULKAN_EXTENT3D_ARGS (device->
            queue_family_props[i].minImageTransferGranularity));
    g_free (queue_flags_str);
  }

  return TRUE;
}

static gboolean
dump_limits (GstVulkanPhysicalDevice * device, GError ** error)
{
#define DEBUG_LIMIT(limit, format, type)                                \
  GST_DEBUG_OBJECT (device, "limit " G_STRINGIFY(limit) ": %" format,   \
    (type) device->properties.limits.limit)
#define DEBUG_LIMIT_2(limit, format, type)                                  \
  GST_DEBUG_OBJECT (device, "limit " G_STRINGIFY(limit)                     \
    ": %" format ", %" format,                                              \
    (type) device->properties.limits.limit[0],                                           \
    (type) device->properties.limits.limit[1])
#define DEBUG_LIMIT_3(limit, format, type)                                  \
  GST_DEBUG_OBJECT (device, "limit " G_STRINGIFY(limit)                     \
    ": %" format ", %" format ", %" format,                                 \
    (type) device->properties.limits.limit[0],                                           \
    (type) device->properties.limits.limit[1],                                           \
    (type) device->properties.limits.limit[2])
#define DEBUG_BOOL_LIMIT(limit) DEBUG_BOOL("limit", limit, device->properties.limits.limit)

#define DEBUG_UINT32_LIMIT(limit) DEBUG_LIMIT(limit, G_GUINT32_FORMAT, guint32)
#define DEBUG_UINT32_2_LIMIT(limit) DEBUG_LIMIT_2(limit, G_GUINT32_FORMAT, guint32)
#define DEBUG_UINT32_3_LIMIT(limit) DEBUG_LIMIT_3(limit, G_GUINT32_FORMAT, guint32)

#define DEBUG_INT32_LIMIT(limit) DEBUG_LIMIT(limit, G_GINT32_FORMAT, gint32)

#define DEBUG_UINT64_LIMIT(limit) DEBUG_LIMIT(limit, G_GUINT64_FORMAT, guint64)

#define DEBUG_SIZE_LIMIT(limit) DEBUG_LIMIT(limit, G_GSIZE_FORMAT, gsize)

#define DEBUG_FLOAT_LIMIT(limit) DEBUG_LIMIT(limit, "f", float)
#define DEBUG_FLOAT_2_LIMIT(limit) DEBUG_LIMIT_2(limit, "f", float)

#define DEBUG_FLAGS_LIMIT(limit, under_name_type)                           \
  G_STMT_START {                                                            \
    gchar *str = G_PASTE(G_PASTE(gst_vulkan_,under_name_type),_flags_to_string) (device->properties.limits.limit); \
    GST_DEBUG_OBJECT (device, "limit " G_STRINGIFY(limit) ": (0x%x) %s",    \
        device->properties.limits.limit, str);                              \
    g_free (str);                                                           \
  } G_STMT_END

  DEBUG_UINT32_LIMIT (maxImageDimension1D);
  DEBUG_UINT32_LIMIT (maxImageDimension2D);
  DEBUG_UINT32_LIMIT (maxImageDimension3D);
  DEBUG_UINT32_LIMIT (maxImageDimensionCube);
  DEBUG_UINT32_LIMIT (maxImageArrayLayers);
  DEBUG_UINT32_LIMIT (maxTexelBufferElements);
  DEBUG_UINT32_LIMIT (maxUniformBufferRange);
  DEBUG_UINT32_LIMIT (maxStorageBufferRange);
  DEBUG_UINT32_LIMIT (maxPushConstantsSize);
  DEBUG_UINT32_LIMIT (maxMemoryAllocationCount);
  DEBUG_UINT32_LIMIT (maxSamplerAllocationCount);
  DEBUG_UINT64_LIMIT (bufferImageGranularity);
  DEBUG_UINT64_LIMIT (sparseAddressSpaceSize);
  DEBUG_UINT32_LIMIT (maxBoundDescriptorSets);
  DEBUG_UINT32_LIMIT (maxPerStageDescriptorSamplers);
  DEBUG_UINT32_LIMIT (maxPerStageDescriptorUniformBuffers);
  DEBUG_UINT32_LIMIT (maxPerStageDescriptorStorageBuffers);
  DEBUG_UINT32_LIMIT (maxPerStageDescriptorSampledImages);
  DEBUG_UINT32_LIMIT (maxPerStageDescriptorStorageImages);
  DEBUG_UINT32_LIMIT (maxPerStageDescriptorInputAttachments);
  DEBUG_UINT32_LIMIT (maxPerStageResources);
  DEBUG_UINT32_LIMIT (maxDescriptorSetSamplers);
  DEBUG_UINT32_LIMIT (maxDescriptorSetUniformBuffers);
  DEBUG_UINT32_LIMIT (maxDescriptorSetUniformBuffersDynamic);
  DEBUG_UINT32_LIMIT (maxDescriptorSetStorageBuffers);
  DEBUG_UINT32_LIMIT (maxDescriptorSetStorageBuffersDynamic);
  DEBUG_UINT32_LIMIT (maxDescriptorSetSampledImages);
  DEBUG_UINT32_LIMIT (maxDescriptorSetStorageImages);
  DEBUG_UINT32_LIMIT (maxDescriptorSetInputAttachments);
  DEBUG_UINT32_LIMIT (maxVertexInputAttributes);
  DEBUG_UINT32_LIMIT (maxVertexInputBindings);
  DEBUG_UINT32_LIMIT (maxVertexInputBindings);
  DEBUG_UINT32_LIMIT (maxVertexInputAttributeOffset);
  DEBUG_UINT32_LIMIT (maxVertexInputBindingStride);
  DEBUG_UINT32_LIMIT (maxVertexOutputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationGenerationLevel);
  DEBUG_UINT32_LIMIT (maxTessellationPatchSize);
  DEBUG_UINT32_LIMIT (maxTessellationControlPerVertexInputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationControlPerVertexOutputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationControlPerPatchOutputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationControlTotalOutputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationControlTotalOutputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationEvaluationInputComponents);
  DEBUG_UINT32_LIMIT (maxTessellationEvaluationOutputComponents);
  DEBUG_UINT32_LIMIT (maxGeometryShaderInvocations);
  DEBUG_UINT32_LIMIT (maxGeometryInputComponents);
  DEBUG_UINT32_LIMIT (maxGeometryOutputComponents);
  DEBUG_UINT32_LIMIT (maxGeometryOutputVertices);
  DEBUG_UINT32_LIMIT (maxGeometryTotalOutputComponents);
  DEBUG_UINT32_LIMIT (maxFragmentInputComponents);
  DEBUG_UINT32_LIMIT (maxFragmentOutputAttachments);
  DEBUG_UINT32_LIMIT (maxFragmentDualSrcAttachments);
  DEBUG_UINT32_LIMIT (maxFragmentCombinedOutputResources);
  DEBUG_UINT32_LIMIT (maxComputeSharedMemorySize);
  DEBUG_UINT32_3_LIMIT (maxComputeWorkGroupCount);
  DEBUG_UINT32_LIMIT (maxComputeWorkGroupInvocations);
  DEBUG_UINT32_3_LIMIT (maxComputeWorkGroupSize);
  DEBUG_UINT32_LIMIT (subPixelPrecisionBits);
  DEBUG_UINT32_LIMIT (subTexelPrecisionBits);
  DEBUG_UINT32_LIMIT (mipmapPrecisionBits);
  DEBUG_UINT32_LIMIT (maxDrawIndexedIndexValue);
  DEBUG_UINT32_LIMIT (maxDrawIndirectCount);
  DEBUG_FLOAT_LIMIT (maxSamplerLodBias);
  DEBUG_FLOAT_LIMIT (maxSamplerAnisotropy);
  DEBUG_UINT32_LIMIT (maxViewports);
  DEBUG_UINT32_2_LIMIT (maxViewportDimensions);
  DEBUG_FLOAT_2_LIMIT (viewportBoundsRange);
  DEBUG_UINT32_LIMIT (viewportSubPixelBits);
  DEBUG_SIZE_LIMIT (minMemoryMapAlignment);
  DEBUG_UINT64_LIMIT (minTexelBufferOffsetAlignment);
  DEBUG_UINT64_LIMIT (minUniformBufferOffsetAlignment);
  DEBUG_UINT64_LIMIT (minStorageBufferOffsetAlignment);
  DEBUG_INT32_LIMIT (minTexelOffset);
  DEBUG_UINT32_LIMIT (maxTexelOffset);
  DEBUG_INT32_LIMIT (minTexelGatherOffset);
  DEBUG_UINT32_LIMIT (maxTexelGatherOffset);
  DEBUG_FLOAT_LIMIT (minInterpolationOffset);
  DEBUG_FLOAT_LIMIT (maxInterpolationOffset);
  DEBUG_UINT32_LIMIT (subPixelInterpolationOffsetBits);
  DEBUG_UINT32_LIMIT (maxFramebufferWidth);
  DEBUG_UINT32_LIMIT (maxFramebufferHeight);
  DEBUG_UINT32_LIMIT (maxFramebufferLayers);
  DEBUG_FLAGS_LIMIT (framebufferColorSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (framebufferDepthSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (framebufferStencilSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (framebufferNoAttachmentsSampleCounts, sample_count);
  DEBUG_UINT32_LIMIT (maxColorAttachments);
  DEBUG_FLAGS_LIMIT (sampledImageColorSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (sampledImageIntegerSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (sampledImageDepthSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (sampledImageStencilSampleCounts, sample_count);
  DEBUG_FLAGS_LIMIT (storageImageSampleCounts, sample_count);
  DEBUG_BOOL_LIMIT (timestampComputeAndGraphics);
  DEBUG_FLOAT_LIMIT (timestampPeriod);
  DEBUG_UINT32_LIMIT (maxClipDistances);
  DEBUG_UINT32_LIMIT (maxCullDistances);
  DEBUG_UINT32_LIMIT (maxCombinedClipAndCullDistances);
  DEBUG_UINT32_LIMIT (discreteQueuePriorities);
  DEBUG_FLOAT_2_LIMIT (pointSizeRange);
  DEBUG_FLOAT_2_LIMIT (lineWidthRange);
  DEBUG_FLOAT_LIMIT (pointSizeGranularity);
  DEBUG_FLOAT_LIMIT (lineWidthGranularity);
  DEBUG_BOOL_LIMIT (strictLines);
  DEBUG_BOOL_LIMIT (standardSampleLocations);
  DEBUG_UINT64_LIMIT (optimalBufferCopyOffsetAlignment);
  DEBUG_UINT64_LIMIT (optimalBufferCopyRowPitchAlignment);
  DEBUG_UINT64_LIMIT (nonCoherentAtomSize);

#undef DEBUG_LIMIT
#undef DEBUG_LIMIT_2
#undef DEBUG_LIMIT_3
#undef DEBUG_BOOL_LIMIT
#undef DEBUG_UINT32_LIMIT
#undef DEBUG_UINT32_2_LIMIT
#undef DEBUG_UINT32_3_LIMIT
#undef DEBUG_INT32_LIMIT
#undef DEBUG_UINT64_LIMIT
#undef DEBUG_SIZE_LIMIT
#undef DEBUG_FLOAT_LIMIT
#undef DEBUG_FLOAT_2_LIMIT
#undef DEBUG_FLAGS_LIMIT

  return TRUE;
}

static gboolean
dump_sparse_properties (GstVulkanPhysicalDevice * device, GError ** error)
{
#define DEBUG_BOOL_SPARSE_PROPERTY(name) DEBUG_BOOL("sparse property", name, device->properties.sparseProperties.name)
  DEBUG_BOOL_SPARSE_PROPERTY (residencyStandard2DBlockShape);
  DEBUG_BOOL_SPARSE_PROPERTY (residencyStandard2DMultisampleBlockShape);
  DEBUG_BOOL_SPARSE_PROPERTY (residencyStandard3DBlockShape);
  DEBUG_BOOL_SPARSE_PROPERTY (residencyAlignedMipSize);
  DEBUG_BOOL_SPARSE_PROPERTY (residencyNonResidentStrict);
#undef DEBUG_BOOL_SPARSE_PROPERTY

  return TRUE;
}

static gboolean
physical_device_info (GstVulkanPhysicalDevice * device, GError ** error)
{
  GST_INFO_OBJECT (device, "physical device %i name \'%s\' type \'%s\' "
      "api version %u.%u.%u, driver version %u.%u.%u vendor ID 0x%x, "
      "device ID 0x%x", device->device_index, device->properties.deviceName,
      gst_vulkan_physical_device_type_to_string (device->properties.deviceType),
      VK_VERSION_MAJOR (device->properties.apiVersion),
      VK_VERSION_MINOR (device->properties.apiVersion),
      VK_VERSION_PATCH (device->properties.apiVersion),
      VK_VERSION_MAJOR (device->properties.driverVersion),
      VK_VERSION_MINOR (device->properties.driverVersion),
      VK_VERSION_PATCH (device->properties.driverVersion),
      device->properties.vendorID, device->properties.deviceID);

  if (!dump_queue_properties (device, error))
    return FALSE;
  if (!dump_memory_properties (device, error))
    return FALSE;
  if (!dump_features (device, error))
    return FALSE;
  if (!dump_limits (device, error))
    return FALSE;
  if (!dump_sparse_properties (device, error))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vulkan_physical_device_fill_info (GstVulkanPhysicalDevice * device,
    GError ** error)
{
  VkResult err;

  device->device = gst_vulkan_physical_device_get_handle (device);
  if (!device->device) {
    g_set_error (error, GST_VULKAN_ERROR, VK_ERROR_INITIALIZATION_FAILED,
        "Failed to retrieve physical device");
    goto error;
  }

  err =
      vkEnumerateDeviceLayerProperties (device->device,
      &device->n_device_layers, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceLayerProperties") < 0)
    goto error;

  device->device_layers = g_new0 (VkLayerProperties, device->n_device_layers);
  err =
      vkEnumerateDeviceLayerProperties (device->device,
      &device->n_device_layers, device->device_layers);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceLayerProperties") < 0) {
    goto error;
  }

  err =
      vkEnumerateDeviceExtensionProperties (device->device, NULL,
      &device->n_device_extensions, NULL);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceExtensionProperties") < 0) {
    goto error;
  }
  GST_DEBUG_OBJECT (device, "Found %u extensions", device->n_device_extensions);

  device->device_extensions =
      g_new0 (VkExtensionProperties, device->n_device_extensions);
  err =
      vkEnumerateDeviceExtensionProperties (device->device, NULL,
      &device->n_device_extensions, device->device_extensions);
  if (gst_vulkan_error_to_g_error (err, error,
          "vkEnumerateDeviceExtensionProperties") < 0) {
    goto error;
  }

  vkGetPhysicalDeviceProperties (device->device, &device->properties);
  vkGetPhysicalDeviceMemoryProperties (device->device,
      &device->memory_properties);
  vkGetPhysicalDeviceFeatures (device->device, &device->features);
  vkGetPhysicalDeviceQueueFamilyProperties (device->device,
      &device->n_queue_families, NULL);
  if (device->n_queue_families > 0) {
    device->queue_family_props =
        g_new0 (VkQueueFamilyProperties, device->n_queue_families);
    vkGetPhysicalDeviceQueueFamilyProperties (device->device,
        &device->n_queue_families, device->queue_family_props);
  }

  if (!physical_device_info (device, error))
    goto error;

  return TRUE;

error:
  return FALSE;
}

/**
 * gst_vulkan_physical_device_get_handle: (skip)
 * @device: a #GstVulkanPhysicalDevice
 *
 * Returns: The associated `VkPhysicalDevice` handle
 *
 * Since: 1.18
 */
VkPhysicalDevice
gst_vulkan_physical_device_get_handle (GstVulkanPhysicalDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_PHYSICAL_DEVICE (device), NULL);

  return device->device;
}

/**
 * gst_vulkan_physical_device_get_instance:
 * @device: a #GstVulkanPhysicalDevice
 *
 * Returns: (transfer full): The #GstVulkanInstance associated with this physical device
 *
 * Since: 1.18
 */
GstVulkanInstance *
gst_vulkan_physical_device_get_instance (GstVulkanPhysicalDevice * device)
{
  g_return_val_if_fail (GST_IS_VULKAN_PHYSICAL_DEVICE (device), NULL);

  return device->instance ? gst_object_ref (device->instance) : NULL;
}

/*
 * GStreamer
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

#ifndef __GST_VK_AMF_EXTENSIONS_H__
#define __GST_VK_AMF_EXTENSIONS_H__

#include <vulkan/vulkan.h>

/* X-macros listing the Vulkan instance/device extension names AMF asks the
 * pipeline's shared #GstVulkanInstance / #GstVulkanDevice to enable. The
 * consumer macro signature is X(name_string).
 *
 * Each entry is guarded by the corresponding Vulkan SDK feature define so
 * that names that aren't known to the build's Vulkan headers are silently
 * dropped instead of producing a compile error.
 */

#ifdef VK_KHR_get_physical_device_properties2
#define AMF_VK_INSTANCE_EXT_KHR_get_physical_device_properties2(X) \
  X (VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
#else
#define AMF_VK_INSTANCE_EXT_KHR_get_physical_device_properties2(X)
#endif

#ifdef VK_KHR_surface
#define AMF_VK_INSTANCE_EXT_KHR_surface(X) \
  X (VK_KHR_SURFACE_EXTENSION_NAME)
#else
#define AMF_VK_INSTANCE_EXT_KHR_surface(X)
#endif

#ifdef VK_KHR_external_semaphore_capabilities
#define AMF_VK_INSTANCE_EXT_KHR_external_semaphore_capabilities(X) \
  X (VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME)
#else
#define AMF_VK_INSTANCE_EXT_KHR_external_semaphore_capabilities(X)
#endif

#ifdef VK_KHR_external_memory_capabilities
#define AMF_VK_INSTANCE_EXT_KHR_external_memory_capabilities(X) \
  X (VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)
#else
#define AMF_VK_INSTANCE_EXT_KHR_external_memory_capabilities(X)
#endif

#ifdef VK_KHR_win32_surface
#define AMF_VK_INSTANCE_EXT_KHR_win32_surface(X) \
  X (VK_KHR_WIN32_SURFACE_EXTENSION_NAME)
#else
#define AMF_VK_INSTANCE_EXT_KHR_win32_surface(X)
#endif

#ifdef VK_KHR_xlib_surface
#define AMF_VK_INSTANCE_EXT_KHR_xlib_surface(X) \
  X (VK_KHR_XLIB_SURFACE_EXTENSION_NAME)
#else
#define AMF_VK_INSTANCE_EXT_KHR_xlib_surface(X)
#endif

/* Vulkan instance extensions AMF wants enabled on the shared instance. */
#define AMF_VK_INSTANCE_EXTENSION_NAMES(X)                       \
  AMF_VK_INSTANCE_EXT_KHR_get_physical_device_properties2 (X)    \
  AMF_VK_INSTANCE_EXT_KHR_surface (X)                            \
  AMF_VK_INSTANCE_EXT_KHR_external_semaphore_capabilities (X)    \
  AMF_VK_INSTANCE_EXT_KHR_external_memory_capabilities (X)       \
  AMF_VK_INSTANCE_EXT_KHR_win32_surface (X)                      \
  AMF_VK_INSTANCE_EXT_KHR_xlib_surface (X)

#ifdef VK_KHR_swapchain
#define AMF_VK_DEVICE_EXT_KHR_swapchain(X) \
  X (VK_KHR_SWAPCHAIN_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_swapchain(X)
#endif

#ifdef VK_KHR_synchronization2
#define AMF_VK_DEVICE_EXT_KHR_synchronization2(X) \
  X (VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_synchronization2(X)
#endif

#ifdef VK_KHR_external_memory
#define AMF_VK_DEVICE_EXT_KHR_external_memory(X) \
  X (VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_external_memory(X)
#endif

#ifdef VK_KHR_external_memory_win32
#define AMF_VK_DEVICE_EXT_KHR_external_memory_win32(X) \
  X (VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_external_memory_win32(X)
#endif

#ifdef VK_KHR_external_semaphore
#define AMF_VK_DEVICE_EXT_KHR_external_semaphore(X) \
  X (VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_external_semaphore(X)
#endif

#ifdef VK_KHR_external_semaphore_win32
#define AMF_VK_DEVICE_EXT_KHR_external_semaphore_win32(X) \
  X (VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_external_semaphore_win32(X)
#endif

#ifdef VK_AMD_device_coherent_memory
#define AMF_VK_DEVICE_EXT_AMD_device_coherent_memory(X) \
  X (VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_AMD_device_coherent_memory(X)
#endif

#ifdef VK_KHR_external_memory_fd
#define AMF_VK_DEVICE_EXT_KHR_external_memory_fd(X) \
  X (VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_external_memory_fd(X)
#endif

#ifdef VK_KHR_external_semaphore_fd
#define AMF_VK_DEVICE_EXT_KHR_external_semaphore_fd(X) \
  X (VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_external_semaphore_fd(X)
#endif

#ifdef VK_KHR_timeline_semaphore
#define AMF_VK_DEVICE_EXT_KHR_timeline_semaphore(X) \
  X (VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_KHR_timeline_semaphore(X)
#endif

#ifdef VK_EXT_external_memory_dma_buf
#define AMF_VK_DEVICE_EXT_EXT_external_memory_dma_buf(X) \
  X (VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_EXT_external_memory_dma_buf(X)
#endif

#ifdef VK_EXT_image_drm_format_modifier
#define AMF_VK_DEVICE_EXT_EXT_image_drm_format_modifier(X) \
  X (VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_EXT_image_drm_format_modifier(X)
#endif

#ifdef VK_EXT_pci_bus_info
#define AMF_VK_DEVICE_EXT_EXT_pci_bus_info(X) \
  X (VK_EXT_PCI_BUS_INFO_EXTENSION_NAME)
#else
#define AMF_VK_DEVICE_EXT_EXT_pci_bus_info(X)
#endif

/* Vulkan device extensions AMF wants enabled on the shared device. */
#define AMF_VK_DEVICE_EXTENSION_NAMES(X)                  \
  AMF_VK_DEVICE_EXT_KHR_swapchain (X)                     \
  AMF_VK_DEVICE_EXT_KHR_synchronization2 (X)              \
  AMF_VK_DEVICE_EXT_KHR_external_memory (X)               \
  AMF_VK_DEVICE_EXT_KHR_external_memory_win32 (X)         \
  AMF_VK_DEVICE_EXT_KHR_external_semaphore (X)            \
  AMF_VK_DEVICE_EXT_KHR_external_semaphore_win32 (X)      \
  AMF_VK_DEVICE_EXT_AMD_device_coherent_memory (X)        \
  AMF_VK_DEVICE_EXT_KHR_external_memory_fd (X)            \
  AMF_VK_DEVICE_EXT_KHR_external_semaphore_fd (X)         \
  AMF_VK_DEVICE_EXT_KHR_timeline_semaphore (X)            \
  AMF_VK_DEVICE_EXT_EXT_external_memory_dma_buf (X)       \
  AMF_VK_DEVICE_EXT_EXT_image_drm_format_modifier (X)     \
  AMF_VK_DEVICE_EXT_EXT_pci_bus_info (X)

#endif /* __GST_VK_AMF_EXTENSIONS_H__ */

/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsthip-config.h"
#include "gsthip.h"

#include <mutex>
#include <condition_variable>

#ifdef HAVE_GST_GL
#include "gsthip-gl.h"
#endif

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hip-interop", 0, "hip-interop");
      });

  return cat;
}
#endif

#ifdef HAVE_GST_GL
static void
unregister_resource_on_gl_thread (GstGLContext * gl_context,
    GstHipGraphicsResource * resource);
#endif

/* *INDENT-OFF* */
struct _GstHipGraphicsResource : public GstMiniObject
{
  _GstHipGraphicsResource ()
  {
  }

  ~_GstHipGraphicsResource ()
  {
#ifdef HAVE_GST_GL
    if (gl_context) {
      gst_gl_context_thread_add (gl_context,
          (GstGLContextThreadFunc) unregister_resource_on_gl_thread,
          this);

      gst_object_unref (gl_context);
    } else
#else
    if (gst_hip_device_set_current (device))
      HipGraphicsUnregisterResource (vendor, handle);
#endif

    gst_object_unref (device);
  }

  GstHipDevice *device = nullptr;
  GstHipVendor vendor = GST_HIP_VENDOR_UNKNOWN;
  hipGraphicsResource_t handle = nullptr;
  std::mutex lock;
  std::condition_variable cond;
  guint64 map_count = 0;
  void *mapped_dev_ptr = nullptr;
  size_t mapped_size = 0;
  hipStream_t mapped_stream = nullptr;
#ifdef HAVE_GST_GL
  GstGLContext *gl_context = nullptr;
#endif
};
/* *INDENT-ON* */

#ifdef HAVE_GST_GL
static void
unregister_resource_on_gl_thread (GstGLContext * gl_context,
    GstHipGraphicsResource * resource)
{
  if (gst_hip_device_set_current (resource->device))
    HipGraphicsUnregisterResource (resource->vendor, resource->handle);
}
#endif

GST_DEFINE_MINI_OBJECT_TYPE (GstHipGraphicsResource, gst_hip_graphics_resource);

/**
 * gst_hip_graphics_resource_map:
 * @resource: a #GstHipGraphicsResource
 * @stream: (type gpointer) (nullable): a hipStream_t handle
 *
 * Map registered @resource for I/O operation
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_graphics_resource_map (GstHipGraphicsResource * resource,
    hipStream_t stream)
{
  g_return_val_if_fail (resource, hipErrorInvalidValue);

  std::unique_lock < std::mutex > lk (resource->lock);

  if (resource->map_count > 0) {
    if (stream == resource->mapped_stream) {
      resource->map_count++;
      return hipSuccess;
    }

    while (resource->map_count > 0)
      resource->cond.wait (lk);
  }

  auto ret = HipGraphicsMapResources (resource->vendor, 1, &resource->handle,
      stream);
  if (!gst_hip_result (ret, resource->vendor))
    return ret;

  resource->map_count++;
  resource->mapped_stream = stream;
  return hipSuccess;
}

/**
 * gst_hip_graphics_resource_unmap:
 * @resource: a #GstHipGraphicsResource
 * @stream: (type gpointer) (nullable): a hipStream_t handle
 *
 * Unmap mapped @resource via gst_hip_graphics_resource_map()
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_graphics_resource_unmap (GstHipGraphicsResource * resource,
    hipStream_t stream)
{
  g_return_val_if_fail (resource, hipErrorInvalidValue);

  std::lock_guard < std::mutex > lk (resource->lock);

  if (resource->map_count == 0) {
    GST_WARNING ("resource %p is not mapped", resource);
    return hipErrorNotMapped;
  }

  resource->map_count--;

  if (resource->map_count > 0)
    return hipSuccess;

  auto ret = HipGraphicsUnmapResources (resource->vendor, 1, &resource->handle,
      stream);

  resource->mapped_stream = nullptr;
  resource->mapped_dev_ptr = nullptr;
  resource->mapped_size = 0;

  resource->cond.notify_all ();

  return ret;
}

/**
 * gst_hip_graphics_resource_get_mapped_pointer:
 * @resource: a #GstHipGraphicsResource
 * @dev_ptr: (out) (optional): a pointer to mapped device memory
 * @size: (out) (optional): the size of mapped device memory
 *
 * Get mapped device pointer from @resource.
 * Caller must map @resource via gst_hip_graphics_resource_map()
 * before getting mapped device memory
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_graphics_resource_get_mapped_pointer (GstHipGraphicsResource * resource,
    void **dev_ptr, size_t *size)
{
  g_return_val_if_fail (resource, hipErrorInvalidValue);

  std::lock_guard < std::mutex > lk (resource->lock);

  if (resource->map_count == 0) {
    GST_WARNING ("resource %p is not mapped", resource);
    return hipErrorNotMapped;
  }

  if (!resource->mapped_dev_ptr) {
    auto ret = HipGraphicsResourceGetMappedPointer (resource->vendor,
        &resource->mapped_dev_ptr, &resource->mapped_size, resource->handle);
    if (!gst_hip_result (ret, resource->vendor))
      return ret;
  }

  if (dev_ptr)
    *dev_ptr = resource->mapped_dev_ptr;

  if (size)
    *size = resource->mapped_size;

  return hipSuccess;
}

/**
 * gst_hip_graphics_resource_ref:
 * @resource: a #GstHipGraphicsResource
 *
 * Increments the reference count on @resource
 *
 * Returns: (transfer full): a pointer to @resource
 *
 * Since: 1.28
 */
GstHipGraphicsResource *
gst_hip_graphics_resource_ref (GstHipGraphicsResource * resource)
{
  return (GstHipGraphicsResource *) gst_mini_object_ref (resource);
}

/**
 * gst_hip_graphics_resource_unref:
 * @resource: a #GstHipGraphicsResource
 *
 * Decrements the reference count on @resource
 *
 * Since: 1.28
 */
void
gst_hip_graphics_resource_unref (GstHipGraphicsResource * resource)
{
  gst_mini_object_unref (resource);
}

/**
 * gst_clear_hip_graphics_resource: (skip)
 * @resource: a pointer to a #GstHipGraphicsResource
 *
 * Clears a reference to the @resource
 *
 * Since: 1.28
 */
void
gst_clear_hip_graphics_resource (GstHipGraphicsResource ** resource)
{
  gst_clear_mini_object (resource);
}

#ifdef HAVE_GST_GL
static void
gst_hip_graphics_resource_free (GstHipGraphicsResource * resource)
{
  delete resource;
}

struct GetResourceData
{
  GstHipGraphicsResource *resource = nullptr;
  hipError_t ret = hipSuccess;
  GstMemory *gl_mem;
  GstHipDevice *device;
};

static void
get_resource_on_gl_thread (GstGLContext * gl_context, GetResourceData * data)
{
  static GQuark gl_quark = 0;
  static std::once_flag once;

  std::call_once (once,[&] {
        gl_quark = g_quark_from_static_string ("GstHipGraphicsResourceGL");
      });

  auto resource = (GstHipGraphicsResource *)
      gst_mini_object_get_qdata ((GstMiniObject *) data->gl_mem, gl_quark);

  if (resource) {
    data->resource = gst_hip_graphics_resource_ref (resource);
    data->ret = hipSuccess;
    return;
  }

  auto vendor = gst_hip_device_get_vendor (data->device);
  auto ret = HipSetDevice (vendor, gst_hip_device_get_device_id (data->device));
  if (!gst_hip_result (ret, vendor)) {
    data->ret = ret;
    return;
  }

  auto pbo = (GstGLMemoryPBO *) data->gl_mem;
  hipGraphicsResource *handle;
  ret = HipGraphicsGLRegisterBuffer (vendor,
      &handle, pbo->pbo->id, hipGraphicsRegisterFlagsNone);
  if (!gst_hip_result (ret, vendor)) {
    data->ret = ret;
    return;
  }

  auto new_resource = new GstHipGraphicsResource ();
  new_resource->device = (GstHipDevice *) gst_object_ref (data->device);
  new_resource->gl_context = (GstGLContext *) gst_object_ref (gl_context);
  new_resource->vendor = vendor;
  new_resource->handle = handle;

  gst_mini_object_init (new_resource, 0, gst_hip_graphics_resource_get_type (),
      nullptr, nullptr,
      (GstMiniObjectFreeFunction) gst_hip_graphics_resource_free);

  gst_mini_object_set_qdata ((GstMiniObject *) data->gl_mem, gl_quark,
      gst_hip_graphics_resource_ref (new_resource),
      (GDestroyNotify) gst_mini_object_unref);

  data->resource = new_resource;
  data->ret = hipSuccess;
}

/**
 * gst_hip_gl_get_graphics_resource_from_memory:
 * @device: a #GstHipDevice
 * @mem: a #GstMemory
 * @resource: (out) (transfer full) (nullable): a location to store created #GstHipGraphicsResource
 *
 * Creates a new #GstHipGraphicsResource from gl memory.
 * @mem must be a valid #GstGLMemoryPBO
 *
 * Returns: (type gint): hipError_t error code
 *
 * Since: 1.28
 */
hipError_t
gst_hip_gl_get_graphics_resource_from_memory (GstHipDevice * device,
    GstMemory * mem, GstHipGraphicsResource ** resource)
{
  g_return_val_if_fail (GST_IS_HIP_DEVICE (device), hipErrorInvalidValue);
  g_return_val_if_fail (gst_is_gl_memory_pbo (mem), hipErrorInvalidValue);
  g_return_val_if_fail (resource, hipErrorInvalidValue);

  GetResourceData data;
  data.device = device;
  data.gl_mem = mem;

  gst_gl_context_thread_add (GST_GL_BASE_MEMORY_CAST (mem)->context,
      (GstGLContextThreadFunc) get_resource_on_gl_thread, &data);

  if (data.ret != hipSuccess)
    return data.ret;

  *resource = data.resource;
  return hipSuccess;
}
#endif

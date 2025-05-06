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

#include "gsthip.h"
#include "gsthiploader.h"
#include <gmodule.h>
#include <mutex>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;
  static std::once_flag once;

  std::call_once (once,[&] {
        cat = _gst_debug_category_new ("hiploader", 0, "hiploader");
      });

  return cat;
}
#endif

#define LOAD_SYMBOL(name) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &table->name)) { \
    GST_ERROR ("Failed to load '%s', %s", G_STRINGIFY (name), g_module_error()); \
    g_module_close (module); \
    return; \
  } \
} G_STMT_END;

/* *INDENT-OFF* */
struct GstHipFuncTableAmd
{
  gboolean loaded = FALSE;

  const char *(*hipGetErrorName) (hipError_t hip_error);
  const char *(*hipGetErrorString) (hipError_t hipError);
  hipError_t (*hipInit) (unsigned int flags);
  hipError_t (*hipGetDeviceCount) (int *count);
  hipError_t (*hipGetDeviceProperties) (hipDeviceProp_t * prop, int deviceId);
  hipError_t (*hipDeviceGetAttribute) (int *pi, hipDeviceAttribute_t attr,
    int deviceId);
  hipError_t (*hipSetDevice) (int deviceId);
  hipError_t (*hipMalloc) (void **ptr, size_t size);
  hipError_t (*hipFree) (void *ptr);
  hipError_t (*hipHostMalloc) (void **ptr, size_t size, unsigned int flags);
  hipError_t (*hipHostFree) (void *ptr);
  hipError_t (*hipStreamSynchronize) (hipStream_t stream);
  hipError_t (*hipModuleLoadData) (hipModule_t * module, const void *image);
  hipError_t (*hipModuleUnload) (hipModule_t module);
  hipError_t (*hipModuleGetFunction) (hipFunction_t * function,
    hipModule_t module, const char *kname);
  hipError_t (*hipModuleLaunchKernel) (hipFunction_t f, unsigned int gridDimX,
    unsigned int gridDimY,
    unsigned int gridDimZ, unsigned int blockDimX,
    unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, hipStream_t stream,
    void **kernelParams, void **extra);
  hipError_t (*hipMemcpyParam2DAsync) (const hip_Memcpy2D * pCopy,
    hipStream_t stream);
  hipError_t (*hipTexObjectCreate) (hipTextureObject_t * pTexObject,
    const HIP_RESOURCE_DESC * pResDesc, const HIP_TEXTURE_DESC * pTexDesc,
    const HIP_RESOURCE_VIEW_DESC * pResViewDesc);
  hipError_t (*hipTexObjectDestroy) (hipTextureObject_t texObject);
};
/* *INDENT-ON* */

static GstHipFuncTableAmd amd_ftable = { };

static void
load_amd_func_table (void)
{
  GModule *module = nullptr;
#ifndef G_OS_WIN32
  module = g_module_open ("libamdhip64.so", G_MODULE_BIND_LAZY);
  if (!module)
    module = g_module_open ("/opt/rocm/lib/libamdhip64.so", G_MODULE_BIND_LAZY);
#else
  /* Prefer hip dll in SDK */
  auto hip_root = g_getenv ("HIP_PATH");
  if (hip_root) {
    auto path = g_build_path (G_DIR_SEPARATOR_S, hip_root, "bin", nullptr);
    auto dir = g_dir_open (path, 0, nullptr);
    if (dir) {
      const gchar *name;
      while ((name = g_dir_read_name (dir))) {
        if (g_str_has_prefix (name, "amdhip64_") && g_str_has_suffix (name,
                ".dll")) {
          auto lib_path = g_build_filename (path, name, nullptr);
          module = g_module_open (lib_path, G_MODULE_BIND_LAZY);
          break;
        }
      }

      g_dir_close (dir);
    }
    g_free (path);
  }

  /* Try dll in System32 */
  if (!module)
    module = g_module_open ("amdhip64.dll", G_MODULE_BIND_LAZY);
#endif

  if (!module) {
    GST_INFO ("Couldn't open HIP library");
    return;
  }

  auto table = &amd_ftable;
  LOAD_SYMBOL (hipGetErrorName);
  LOAD_SYMBOL (hipGetErrorString);
  LOAD_SYMBOL (hipInit);
  LOAD_SYMBOL (hipGetDeviceCount);
  LOAD_SYMBOL (hipGetDeviceProperties);
  LOAD_SYMBOL (hipDeviceGetAttribute);
  LOAD_SYMBOL (hipSetDevice);
  LOAD_SYMBOL (hipMalloc);
  LOAD_SYMBOL (hipFree);
  LOAD_SYMBOL (hipHostMalloc);
  LOAD_SYMBOL (hipHostFree);
  LOAD_SYMBOL (hipStreamSynchronize);
  LOAD_SYMBOL (hipModuleLoadData);
  LOAD_SYMBOL (hipModuleUnload);
  LOAD_SYMBOL (hipModuleGetFunction);
  LOAD_SYMBOL (hipModuleLaunchKernel);
  LOAD_SYMBOL (hipMemcpyParam2DAsync);
  LOAD_SYMBOL (hipTexObjectCreate);
  LOAD_SYMBOL (hipTexObjectDestroy);

  table->loaded = TRUE;
}

gboolean
gst_hip_load_library (void)
{
  static std::once_flag once;
  std::call_once (once,[]() {
        load_amd_func_table ();
      });

  return amd_ftable.loaded;
}

const char *
HipGetErrorName (hipError_t hip_error)
{
  g_return_val_if_fail (gst_hip_load_library (), nullptr);

  return amd_ftable.hipGetErrorName (hip_error);
}

const char *
HipGetErrorString (hipError_t hipError)
{
  g_return_val_if_fail (gst_hip_load_library (), nullptr);

  return amd_ftable.hipGetErrorString (hipError);
}

hipError_t
HipInit (unsigned int flags)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipInit (flags);
}

hipError_t
HipGetDeviceCount (int *count)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipGetDeviceCount (count);
}

hipError_t
HipGetDeviceProperties (hipDeviceProp_t * prop, int deviceId)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipGetDeviceProperties (prop, deviceId);
}

hipError_t
HipDeviceGetAttribute (int *pi, hipDeviceAttribute_t attr, int deviceId)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipDeviceGetAttribute (pi, attr, deviceId);
}

hipError_t
HipSetDevice (int deviceId)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipSetDevice (deviceId);
}

hipError_t
HipMalloc (void **ptr, size_t size)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipMalloc (ptr, size);
}

hipError_t
HipFree (void *ptr)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipFree (ptr);
}

hipError_t
HipHostMalloc (void **ptr, size_t size, unsigned int flags)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipHostMalloc (ptr, size, flags);
}

hipError_t
HipHostFree (void *ptr)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipHostFree (ptr);
}

hipError_t
HipStreamSynchronize (hipStream_t stream)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipStreamSynchronize (stream);
}

hipError_t
HipModuleLoadData (hipModule_t * module, const void *image)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipModuleLoadData (module, image);
}

hipError_t
HipModuleUnload (hipModule_t module)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipModuleUnload (module);
}

hipError_t
HipModuleGetFunction (hipFunction_t * function, hipModule_t module,
    const char *kname)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipModuleGetFunction (function, module, kname);
}

hipError_t
HipModuleLaunchKernel (hipFunction_t f, unsigned int gridDimX,
    unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX,
    unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes,
    hipStream_t stream, void **kernelParams, void **extra)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipModuleLaunchKernel (f, gridDimX, gridDimY, gridDimZ,
      blockDimX, blockDimY, blockDimZ, sharedMemBytes, stream,
      kernelParams, extra);
}

hipError_t
HipMemcpyParam2DAsync (const hip_Memcpy2D * pCopy, hipStream_t stream)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipMemcpyParam2DAsync (pCopy, stream);
}

hipError_t
HipTexObjectCreate (hipTextureObject_t * pTexObject,
    const HIP_RESOURCE_DESC * pResDesc,
    const HIP_TEXTURE_DESC * pTexDesc,
    const HIP_RESOURCE_VIEW_DESC * pResViewDesc)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipTexObjectCreate (pTexObject, pResDesc, pTexDesc,
      pResViewDesc);
}

hipError_t
HipTexObjectDestroy (hipTextureObject_t texObject)
{
  g_return_val_if_fail (gst_hip_load_library (), hipErrorNotInitialized);

  return amd_ftable.hipTexObjectDestroy (texObject);
}

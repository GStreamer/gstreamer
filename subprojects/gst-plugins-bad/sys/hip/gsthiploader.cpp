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
#include "gsthiploader.h"
#include <gmodule.h>
#include <mutex>
#include <hip/nvidia_hip_runtime_api.h>
#include <string.h>

#ifdef HAVE_GST_GL
#include "gsthiploader-gl.h"
#include <cudaGL.h>
#endif

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

/* *INDENT-OFF* */
struct GstHipFuncTableAmd
{
  gboolean loaded = FALSE;

  hipError_t (*hipInit) (unsigned int flags);
  hipError_t (*hipDriverGetVersion) (int *driverVersion);
  hipError_t (*hipRuntimeGetVersion) (int *runtimeVersion);
  const char *(*hipGetErrorName) (hipError_t hip_error);
  const char *(*hipGetErrorString) (hipError_t hipError);
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
  hipError_t (*hipGraphicsMapResources) (int count,
    hipGraphicsResource_t* resources, hipStream_t stream);
  hipError_t (*hipGraphicsResourceGetMappedPointer) (void** devPtr,
    size_t* size, hipGraphicsResource_t resource);
  hipError_t (*hipGraphicsUnmapResources) (int count,
    hipGraphicsResource_t* resources, hipStream_t stream);
  hipError_t (*hipGraphicsUnregisterResource) (hipGraphicsResource_t resource);
#ifdef HAVE_GST_GL
  hipError_t (*hipGLGetDevices) (unsigned int* pHipDeviceCount,
      int* pHipDevices, unsigned int hipDeviceCount,
      hipGLDeviceList deviceList);
  hipError_t (*hipGraphicsGLRegisterBuffer) (hipGraphicsResource** resource,
      unsigned int buffer, unsigned int flags);
#endif
};

struct GstHipFuncTableCuda
{
  gboolean loaded = FALSE;

  CUresult (CUDAAPI *cuInit) (unsigned int flags);
  CUresult (CUDAAPI *cuDriverGetVersion) (int *driverVersion);
  CUresult (CUDAAPI *cuDeviceGetAttribute) (int *pi,
    CUdevice_attribute attrib, CUdevice dev);
  CUresult (CUDAAPI *cuModuleLoadData) (CUmodule * module, const void *image);
  CUresult (CUDAAPI *cuModuleUnload) (CUmodule module);
  CUresult (CUDAAPI *cuModuleGetFunction) (CUfunction * function,
    CUmodule module, const char *kname);
  CUresult (CUDAAPI *cuLaunchKernel) (CUfunction f, unsigned int gridDimX,
    unsigned int gridDimY,
    unsigned int gridDimZ, unsigned int blockDimX,
    unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, CUstream stream,
    void **kernelParams, void **extra);
  CUresult (CUDAAPI *cuMemcpy2DAsync) (const CUDA_MEMCPY2D * pCopy,
    CUstream stream);
  CUresult (CUDAAPI *cuTexObjectCreate) (CUtexObject * pTexObject,
    const CUDA_RESOURCE_DESC * pResDesc, const CUDA_TEXTURE_DESC * pTexDesc,
    const CUDA_RESOURCE_VIEW_DESC * pResViewDesc);
  CUresult (CUDAAPI *cuTexObjectDestroy) (CUtexObject texObject);
};

struct GstHipFuncTableCudaRt
{
  gboolean loaded = FALSE;

  cudaError_t (CUDAAPI *cudaRuntimeGetVersion) (int *runtimeVersion);
  const char * (CUDAAPI *cudaGetErrorName) (cudaError_t error);
  const char * (CUDAAPI *cudaGetErrorString) (cudaError_t error);
  cudaError_t (CUDAAPI *cudaGetDeviceCount) (int *count);
  cudaError_t (CUDAAPI *cudaGetDeviceProperties) (struct cudaDeviceProp * prop,
    int device);
  cudaError_t (CUDAAPI *cudaDeviceGetAttribute) (int *value, enum cudaDeviceAttr attr,
    int device);
  cudaError_t (CUDAAPI *cudaSetDevice) (int device);
  cudaError_t (CUDAAPI *cudaMalloc) (void **ptr, size_t size);
  cudaError_t (CUDAAPI *cudaFree) (void *ptr);
  cudaError_t (CUDAAPI *cudaMallocHost) (void **ptr, size_t size, unsigned int flags);
  cudaError_t (CUDAAPI *cudaFreeHost) (void *ptr);
  cudaError_t (CUDAAPI *cudaStreamSynchronize) (cudaStream_t stream);
  cudaError_t (CUDAAPI *cudaGraphicsMapResources) (int count,
    cudaGraphicsResource_t *resources, cudaStream_t stream);
  cudaError_t (CUDAAPI *cudaGraphicsResourceGetMappedPointer) (void **devPtr,
    size_t *size, cudaGraphicsResource_t resource);
  cudaError_t (CUDAAPI *cudaGraphicsUnmapResources) (int count,
    cudaGraphicsResource_t *resources, cudaStream_t stream);
  cudaError_t (CUDAAPI *cudaGraphicsUnregisterResource) (cudaGraphicsResource_t resource);
#ifdef HAVE_GST_GL
  cudaError_t (CUDAAPI *cudaGLGetDevices) (unsigned int *pCudaDeviceCount,
    int *pCudaDevices, unsigned int cudaDeviceCount,
    enum cudaGLDeviceList deviceList);
  cudaError_t (CUDAAPI *cudaGraphicsGLRegisterBuffer) (struct cudaGraphicsResource **resource,
    unsigned int buffer, unsigned int flags);
#endif
};
/* *INDENT-ON* */

static GstHipFuncTableAmd amd_ftable = { };
static GstHipFuncTableCuda cuda_ftable = { };
static GstHipFuncTableCudaRt cudart_ftable = { };

#define LOAD_SYMBOL(name) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &table->name)) { \
    GST_ERROR ("Failed to load '%s', %s", G_STRINGIFY (name), g_module_error()); \
    g_module_close (module); \
    return; \
  } \
} G_STMT_END;

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
    module = g_module_open ("amdhip64_6.dll", G_MODULE_BIND_LAZY);
#endif

  if (!module) {
    GST_INFO ("Couldn't open HIP library");
    return;
  }

  auto table = &amd_ftable;
  LOAD_SYMBOL (hipInit);
  LOAD_SYMBOL (hipDriverGetVersion);
  LOAD_SYMBOL (hipRuntimeGetVersion);
  LOAD_SYMBOL (hipGetErrorName);
  LOAD_SYMBOL (hipGetErrorString);
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
  LOAD_SYMBOL (hipGraphicsMapResources);
  LOAD_SYMBOL (hipGraphicsResourceGetMappedPointer);
  LOAD_SYMBOL (hipGraphicsUnmapResources);
  LOAD_SYMBOL (hipGraphicsUnregisterResource);
#ifdef HAVE_GST_GL
  LOAD_SYMBOL (hipGLGetDevices);
  LOAD_SYMBOL (hipGraphicsGLRegisterBuffer);
#endif

  table->loaded = TRUE;
}

static void
load_cuda_func_table (void)
{
  GModule *module = nullptr;
#ifndef G_OS_WIN32
  module = g_module_open ("libcuda.so", G_MODULE_BIND_LAZY);
#else
  module = g_module_open ("nvcuda.dll", G_MODULE_BIND_LAZY);
#endif

  if (!module) {
    GST_INFO ("Couldn't open CUDA library");
    return;
  }

  auto table = &cuda_ftable;
  LOAD_SYMBOL (cuInit);
  LOAD_SYMBOL (cuDriverGetVersion);
  LOAD_SYMBOL (cuModuleLoadData);
  LOAD_SYMBOL (cuModuleUnload);
  LOAD_SYMBOL (cuModuleGetFunction);
  LOAD_SYMBOL (cuLaunchKernel);
  LOAD_SYMBOL (cuMemcpy2DAsync);
  LOAD_SYMBOL (cuTexObjectCreate);
  LOAD_SYMBOL (cuTexObjectDestroy);

  table->loaded = TRUE;
}

static void
load_cudart_func_table (guint major_ver, guint minor_ver)
{
  GModule *module = nullptr;
  auto module_name = g_getenv ("GST_HIP_CUDART_LIBNAME");
  if (module_name)
    module = g_module_open (module_name, G_MODULE_BIND_LAZY);

  if (!module) {
#ifndef G_OS_WIN32
    module = g_module_open ("libcudart.so", G_MODULE_BIND_LAZY);
#else
    auto lib_name = g_strdup_printf ("cudart64_%d.dll", major_ver);
    module = g_module_open (lib_name, G_MODULE_BIND_LAZY);
    g_free (lib_name);

    if (!module) {
      lib_name = g_strdup_printf ("cudart64_%d%d.dll", major_ver, minor_ver);
      module = g_module_open (lib_name, G_MODULE_BIND_LAZY);
      g_free (lib_name);
    }

    if (!module) {
      auto cuda_root = g_getenv ("CUDA_PATH");
      if (cuda_root) {
        auto path = g_build_path (G_DIR_SEPARATOR_S, cuda_root, "bin", nullptr);
        auto dir = g_dir_open (path, 0, nullptr);
        if (dir) {
          const gchar *name;
          while ((name = g_dir_read_name (dir))) {
            if (g_str_has_prefix (name, "cudart64_") &&
                g_str_has_suffix (name, ".dll")) {
              auto lib_path = g_build_filename (path, name, nullptr);
              module = g_module_open (lib_path, G_MODULE_BIND_LAZY);
              g_free (lib_path);
              break;
            }
          }

          g_dir_close (dir);
        }
        g_free (path);
      }
    }
#endif
  }

  if (!module) {
    GST_INFO ("Couldn't open CUDA runtime library");
    return;
  }

  auto table = &cudart_ftable;
  LOAD_SYMBOL (cudaRuntimeGetVersion);
  LOAD_SYMBOL (cudaGetErrorName);
  LOAD_SYMBOL (cudaGetErrorString);
  LOAD_SYMBOL (cudaGetDeviceCount);
  LOAD_SYMBOL (cudaGetDeviceProperties);
  LOAD_SYMBOL (cudaDeviceGetAttribute);
  LOAD_SYMBOL (cudaSetDevice);
  LOAD_SYMBOL (cudaMalloc);
  LOAD_SYMBOL (cudaFree);
  LOAD_SYMBOL (cudaMallocHost);
  LOAD_SYMBOL (cudaFreeHost);
  LOAD_SYMBOL (cudaStreamSynchronize);
  LOAD_SYMBOL (cudaGraphicsMapResources);
  LOAD_SYMBOL (cudaGraphicsResourceGetMappedPointer);
  LOAD_SYMBOL (cudaGraphicsUnmapResources);
  LOAD_SYMBOL (cudaGraphicsUnregisterResource);
#ifdef HAVE_GST_GL
  LOAD_SYMBOL (cudaGLGetDevices);
  LOAD_SYMBOL (cudaGraphicsGLRegisterBuffer);
#endif

  table->loaded = TRUE;
}

/* *INDENT-OFF* */
static gboolean
gst_hip_load_library_amd (void)
{
  static std::once_flag once;
  std::call_once (once,[]() {
    load_amd_func_table ();
    if (amd_ftable.loaded) {
      auto ret = amd_ftable.hipInit (0);
      if (ret != hipSuccess)
        amd_ftable.loaded = FALSE;
    }
  });

  return amd_ftable.loaded;
}

static gboolean
gst_hip_load_library_nvidia (void)
{
  static std::once_flag once;
  std::call_once (once,[]() {
    load_cuda_func_table ();
    if (cuda_ftable.loaded) {
      auto ret = cuda_ftable.cuInit (0);
      if (ret != CUDA_SUCCESS) {
        cuda_ftable.loaded = FALSE;
        return;
      }

      int cuda_ver = 0;
      ret = cuda_ftable.cuDriverGetVersion (&cuda_ver);
      if (ret != CUDA_SUCCESS)
        return;

      int major_ver = cuda_ver / 1000;
      int minor_ver = (cuda_ver % 1000) / 10;
      load_cudart_func_table (major_ver, minor_ver);
    }
  });

  if (!cuda_ftable.loaded || !cudart_ftable.loaded)
    return FALSE;

  return TRUE;
}
/* *INDENT-ON* */

gboolean
gst_hip_load_library (GstHipVendor vendor)
{
  switch (vendor) {
    case GST_HIP_VENDOR_AMD:
      return gst_hip_load_library_amd ();
    case GST_HIP_VENDOR_NVIDIA:
      return gst_hip_load_library_nvidia ();
    case GST_HIP_VENDOR_UNKNOWN:
      if (gst_hip_load_library_amd () || gst_hip_load_library_nvidia ())
        return TRUE;
      break;
  }

  return FALSE;
}

#define CHECK_VENDOR(v) \
  g_return_val_if_fail (vendor != GST_HIP_VENDOR_UNKNOWN, \
      hipErrorNotInitialized); \
  g_return_val_if_fail (gst_hip_load_library (vendor), hipErrorNotInitialized);


hipError_t
HipInit (GstHipVendor vendor, unsigned int flags)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipInit (flags);

  auto cuda_ret = cuda_ftable.cuInit (flags);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipDriverGetVersion (GstHipVendor vendor, int *driverVersion)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipDriverGetVersion (driverVersion);

  auto cuda_ret = cuda_ftable.cuDriverGetVersion (driverVersion);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipRuntimeGetVersion (GstHipVendor vendor, int *runtimeVersion)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipRuntimeGetVersion (runtimeVersion);

  auto cuda_ret = cudart_ftable.cudaRuntimeGetVersion (runtimeVersion);
  return hipCUDAErrorTohipError (cuda_ret);
}

const char *
HipGetErrorName (GstHipVendor vendor, hipError_t hip_error)
{
  g_return_val_if_fail (vendor != GST_HIP_VENDOR_UNKNOWN, nullptr);
  g_return_val_if_fail (gst_hip_load_library (vendor), nullptr);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGetErrorName (hip_error);

  auto cuda_ret = hipErrorToCudaError (hip_error);
  return cudart_ftable.cudaGetErrorName (cuda_ret);
}

const char *
HipGetErrorString (GstHipVendor vendor, hipError_t hipError)
{
  g_return_val_if_fail (vendor != GST_HIP_VENDOR_UNKNOWN, nullptr);
  g_return_val_if_fail (gst_hip_load_library (vendor), nullptr);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGetErrorString (hipError);

  auto cuda_ret = hipErrorToCudaError (hipError);
  return cudart_ftable.cudaGetErrorString (cuda_ret);
}

hipError_t
HipGetDeviceCount (GstHipVendor vendor, int *count)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGetDeviceCount (count);

  auto cuda_ret = cudart_ftable.cudaGetDeviceCount (count);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipGetDeviceProperties (GstHipVendor vendor, hipDeviceProp_t * prop,
    int deviceId)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGetDeviceProperties (prop, deviceId);

  if (!prop)
    return hipErrorInvalidValue;

  struct cudaDeviceProp cdprop;
  auto cuda_ret = cudart_ftable.cudaGetDeviceProperties (&cdprop, deviceId);
  if (cuda_ret != cudaSuccess)
    return hipCUDAErrorTohipError (cuda_ret);

  strncpy (prop->name, cdprop.name, 256);
  strncpy (prop->uuid.bytes, cdprop.uuid.bytes, 16);
  strncpy (prop->luid, cdprop.luid, 8);
  prop->luidDeviceNodeMask = cdprop.luidDeviceNodeMask;
  prop->totalGlobalMem = cdprop.totalGlobalMem;
  prop->sharedMemPerBlock = cdprop.sharedMemPerBlock;
  prop->regsPerBlock = cdprop.regsPerBlock;
  prop->memPitch = cdprop.memPitch;
  prop->maxThreadsPerBlock = cdprop.maxThreadsPerBlock;
  prop->maxThreadsDim[0] = cdprop.maxThreadsDim[0];
  prop->maxThreadsDim[1] = cdprop.maxThreadsDim[1];
  prop->maxThreadsDim[2] = cdprop.maxThreadsDim[2];
  prop->maxGridSize[0] = cdprop.maxGridSize[0];
  prop->maxGridSize[1] = cdprop.maxGridSize[1];
  prop->maxGridSize[2] = cdprop.maxGridSize[2];
  prop->clockRate = cdprop.clockRate;
  prop->totalConstMem = cdprop.totalConstMem;
  prop->major = cdprop.major;
  prop->minor = cdprop.minor;
  prop->textureAlignment = cdprop.textureAlignment;
  prop->texturePitchAlignment = cdprop.texturePitchAlignment;
  prop->deviceOverlap = cdprop.deviceOverlap;
  prop->multiProcessorCount = cdprop.multiProcessorCount;
  prop->kernelExecTimeoutEnabled = cdprop.kernelExecTimeoutEnabled;
  prop->integrated = cdprop.integrated;
  prop->canMapHostMemory = cdprop.canMapHostMemory;
  prop->computeMode = cdprop.computeMode;
  prop->maxTexture1D = cdprop.maxTexture1D;
  prop->maxTexture1DMipmap = cdprop.maxTexture1DMipmap;
  prop->maxTexture1DLinear = cdprop.maxTexture1DLinear;
  prop->maxTexture2D[0] = cdprop.maxTexture2D[0];
  prop->maxTexture2D[1] = cdprop.maxTexture2D[1];
  prop->maxTexture2DMipmap[0] = cdprop.maxTexture2DMipmap[0];
  prop->maxTexture2DMipmap[1] = cdprop.maxTexture2DMipmap[1];
  prop->maxTexture2DLinear[0] = cdprop.maxTexture2DLinear[0];
  prop->maxTexture2DLinear[1] = cdprop.maxTexture2DLinear[1];
  prop->maxTexture2DLinear[2] = cdprop.maxTexture2DLinear[2];
  prop->maxTexture2DGather[0] = cdprop.maxTexture2DGather[0];
  prop->maxTexture2DGather[1] = cdprop.maxTexture2DGather[1];
  prop->maxTexture3D[0] = cdprop.maxTexture3D[0];
  prop->maxTexture3D[1] = cdprop.maxTexture3D[1];
  prop->maxTexture3D[2] = cdprop.maxTexture3D[2];
  prop->maxTexture3DAlt[0] = cdprop.maxTexture3DAlt[0];
  prop->maxTexture3DAlt[1] = cdprop.maxTexture3DAlt[1];
  prop->maxTexture3DAlt[2] = cdprop.maxTexture3DAlt[2];
  prop->maxTextureCubemap = cdprop.maxTextureCubemap;
  prop->maxTexture1DLayered[0] = cdprop.maxTexture1DLayered[0];
  prop->maxTexture1DLayered[1] = cdprop.maxTexture1DLayered[1];
  prop->maxTexture2DLayered[0] = cdprop.maxTexture2DLayered[0];
  prop->maxTexture2DLayered[1] = cdprop.maxTexture2DLayered[1];
  prop->maxTexture2DLayered[2] = cdprop.maxTexture2DLayered[2];
  prop->maxTextureCubemapLayered[0] = cdprop.maxTextureCubemapLayered[0];
  prop->maxTextureCubemapLayered[1] = cdprop.maxTextureCubemapLayered[1];
  prop->maxSurface1D = cdprop.maxSurface1D;
  prop->maxSurface2D[0] = cdprop.maxSurface2D[0];
  prop->maxSurface2D[1] = cdprop.maxSurface2D[1];
  prop->maxSurface3D[0] = cdprop.maxSurface3D[0];
  prop->maxSurface3D[1] = cdprop.maxSurface3D[1];
  prop->maxSurface3D[2] = cdprop.maxSurface3D[2];
  prop->maxSurface1DLayered[0] = cdprop.maxSurface1DLayered[0];
  prop->maxSurface1DLayered[1] = cdprop.maxSurface1DLayered[1];
  prop->maxSurface2DLayered[0] = cdprop.maxSurface2DLayered[0];
  prop->maxSurface2DLayered[1] = cdprop.maxSurface2DLayered[1];
  prop->maxSurface2DLayered[2] = cdprop.maxSurface2DLayered[2];
  prop->maxSurfaceCubemap = cdprop.maxSurfaceCubemap;
  prop->maxSurfaceCubemapLayered[0] = cdprop.maxSurfaceCubemapLayered[0];
  prop->maxSurfaceCubemapLayered[1] = cdprop.maxSurfaceCubemapLayered[1];
  prop->surfaceAlignment = cdprop.surfaceAlignment;
  prop->concurrentKernels = cdprop.concurrentKernels;
  prop->ECCEnabled = cdprop.ECCEnabled;
  prop->pciBusID = cdprop.pciBusID;
  prop->pciDeviceID = cdprop.pciDeviceID;
  prop->pciDomainID = cdprop.pciDomainID;
  prop->tccDriver = cdprop.tccDriver;
  prop->asyncEngineCount = cdprop.asyncEngineCount;
  prop->unifiedAddressing = cdprop.unifiedAddressing;
  prop->memoryClockRate = cdprop.memoryClockRate;
  prop->memoryBusWidth = cdprop.memoryBusWidth;
  prop->l2CacheSize = cdprop.l2CacheSize;
  prop->maxThreadsPerMultiProcessor = cdprop.maxThreadsPerMultiProcessor;
  prop->streamPrioritiesSupported = cdprop.streamPrioritiesSupported;
  prop->globalL1CacheSupported = cdprop.globalL1CacheSupported;
  prop->localL1CacheSupported = cdprop.localL1CacheSupported;
  prop->sharedMemPerMultiprocessor = cdprop.sharedMemPerMultiprocessor;
  prop->regsPerMultiprocessor = cdprop.regsPerMultiprocessor;
  prop->managedMemory = cdprop.managedMemory;
  prop->isMultiGpuBoard = cdprop.isMultiGpuBoard;
  prop->multiGpuBoardGroupID = cdprop.multiGpuBoardGroupID;
  prop->hostNativeAtomicSupported = cdprop.hostNativeAtomicSupported;
  prop->singleToDoublePrecisionPerfRatio =
      cdprop.singleToDoublePrecisionPerfRatio;
  prop->pageableMemoryAccess = cdprop.pageableMemoryAccess;
  prop->concurrentManagedAccess = cdprop.concurrentManagedAccess;
  prop->computePreemptionSupported = cdprop.computePreemptionSupported;
  prop->canUseHostPointerForRegisteredMem =
      cdprop.canUseHostPointerForRegisteredMem;
  prop->cooperativeLaunch = cdprop.cooperativeLaunch;
  prop->cooperativeMultiDeviceLaunch = cdprop.cooperativeMultiDeviceLaunch;
  prop->sharedMemPerBlockOptin = cdprop.sharedMemPerBlockOptin;
  prop->pageableMemoryAccessUsesHostPageTables =
      cdprop.pageableMemoryAccessUsesHostPageTables;
  prop->directManagedMemAccessFromHost = cdprop.directManagedMemAccessFromHost;
  prop->accessPolicyMaxWindowSize = cdprop.accessPolicyMaxWindowSize;
  prop->maxBlocksPerMultiProcessor = cdprop.maxBlocksPerMultiProcessor;
  prop->persistingL2CacheMaxSize = cdprop.persistingL2CacheMaxSize;
  prop->reservedSharedMemPerBlock = cdprop.reservedSharedMemPerBlock;
  prop->warpSize = cdprop.warpSize;
  prop->clusterLaunch = cdprop.clusterLaunch;
  prop->deferredMappingHipArraySupported =
      cdprop.deferredMappingCudaArraySupported;
  prop->gpuDirectRDMAFlushWritesOptions =
      cdprop.gpuDirectRDMAFlushWritesOptions;
  prop->gpuDirectRDMASupported = cdprop.gpuDirectRDMASupported;
  prop->gpuDirectRDMAWritesOrdering = cdprop.gpuDirectRDMAWritesOrdering;
  prop->hostRegisterReadOnlySupported = cdprop.hostRegisterReadOnlySupported;
  prop->hostRegisterSupported = cdprop.hostRegisterSupported;
  prop->ipcEventSupported = cdprop.ipcEventSupported;
  prop->memoryPoolSupportedHandleTypes = cdprop.memoryPoolSupportedHandleTypes;
  prop->memoryPoolsSupported = cdprop.memoryPoolsSupported;
  prop->sparseHipArraySupported = cdprop.sparseCudaArraySupported;
  prop->timelineSemaphoreInteropSupported =
      cdprop.timelineSemaphoreInteropSupported;
  prop->unifiedFunctionPointers = cdprop.unifiedFunctionPointers;

  return hipSuccess;
}

hipError_t
HipDeviceGetAttribute (GstHipVendor vendor, int *pi, hipDeviceAttribute_t attr,
    int deviceId)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipDeviceGetAttribute (pi, attr, deviceId);

  enum cudaDeviceAttr cdattr;
  switch (attr) {
    case hipDeviceAttributeMaxThreadsPerBlock:
      cdattr = cudaDevAttrMaxThreadsPerBlock;
      break;
    case hipDeviceAttributeMaxBlockDimX:
      cdattr = cudaDevAttrMaxBlockDimX;
      break;
    case hipDeviceAttributeMaxBlockDimY:
      cdattr = cudaDevAttrMaxBlockDimY;
      break;
    case hipDeviceAttributeMaxBlockDimZ:
      cdattr = cudaDevAttrMaxBlockDimZ;
      break;
    case hipDeviceAttributeMaxGridDimX:
      cdattr = cudaDevAttrMaxGridDimX;
      break;
    case hipDeviceAttributeMaxGridDimY:
      cdattr = cudaDevAttrMaxGridDimY;
      break;
    case hipDeviceAttributeMaxGridDimZ:
      cdattr = cudaDevAttrMaxGridDimZ;
      break;
    case hipDeviceAttributeMaxSharedMemoryPerBlock:
      cdattr = cudaDevAttrMaxSharedMemoryPerBlock;
      break;
    case hipDeviceAttributeTotalConstantMemory:
      cdattr = cudaDevAttrTotalConstantMemory;
      break;
    case hipDeviceAttributeWarpSize:
      cdattr = cudaDevAttrWarpSize;
      break;
    case hipDeviceAttributeMaxRegistersPerBlock:
      cdattr = cudaDevAttrMaxRegistersPerBlock;
      break;
    case hipDeviceAttributeClockRate:
      cdattr = cudaDevAttrClockRate;
      break;
    case hipDeviceAttributeMemoryClockRate:
      cdattr = cudaDevAttrMemoryClockRate;
      break;
    case hipDeviceAttributeMemoryBusWidth:
      cdattr = cudaDevAttrGlobalMemoryBusWidth;
      break;
    case hipDeviceAttributeMultiprocessorCount:
      cdattr = cudaDevAttrMultiProcessorCount;
      break;
    case hipDeviceAttributeComputeMode:
      cdattr = cudaDevAttrComputeMode;
      break;
    case hipDeviceAttributeL2CacheSize:
      cdattr = cudaDevAttrL2CacheSize;
      break;
    case hipDeviceAttributeMaxThreadsPerMultiProcessor:
      cdattr = cudaDevAttrMaxThreadsPerMultiProcessor;
      break;
    case hipDeviceAttributeComputeCapabilityMajor:
      cdattr = cudaDevAttrComputeCapabilityMajor;
      break;
    case hipDeviceAttributeComputeCapabilityMinor:
      cdattr = cudaDevAttrComputeCapabilityMinor;
      break;
    case hipDeviceAttributeConcurrentKernels:
      cdattr = cudaDevAttrConcurrentKernels;
      break;
    case hipDeviceAttributePciBusId:
      cdattr = cudaDevAttrPciBusId;
      break;
    case hipDeviceAttributePciDeviceId:
      cdattr = cudaDevAttrPciDeviceId;
      break;
    case hipDeviceAttributeMaxSharedMemoryPerMultiprocessor:
      cdattr = cudaDevAttrMaxSharedMemoryPerMultiprocessor;
      break;
    case hipDeviceAttributeIsMultiGpuBoard:
      cdattr = cudaDevAttrIsMultiGpuBoard;
      break;
    case hipDeviceAttributeIntegrated:
      cdattr = cudaDevAttrIntegrated;
      break;
    case hipDeviceAttributeMaxTexture1DWidth:
      cdattr = cudaDevAttrMaxTexture1DWidth;
      break;
    case hipDeviceAttributeMaxTexture2DWidth:
      cdattr = cudaDevAttrMaxTexture2DWidth;
      break;
    case hipDeviceAttributeMaxTexture2DHeight:
      cdattr = cudaDevAttrMaxTexture2DHeight;
      break;
    case hipDeviceAttributeMaxTexture3DWidth:
      cdattr = cudaDevAttrMaxTexture3DWidth;
      break;
    case hipDeviceAttributeMaxTexture3DHeight:
      cdattr = cudaDevAttrMaxTexture3DHeight;
      break;
    case hipDeviceAttributeMaxTexture3DDepth:
      cdattr = cudaDevAttrMaxTexture3DDepth;
      break;
    case hipDeviceAttributeMaxPitch:
      cdattr = cudaDevAttrMaxPitch;
      break;
    case hipDeviceAttributeTextureAlignment:
      cdattr = cudaDevAttrTextureAlignment;
      break;
    case hipDeviceAttributeTexturePitchAlignment:
      cdattr = cudaDevAttrTexturePitchAlignment;
      break;
    case hipDeviceAttributeKernelExecTimeout:
      cdattr = cudaDevAttrKernelExecTimeout;
      break;
    case hipDeviceAttributeCanMapHostMemory:
      cdattr = cudaDevAttrCanMapHostMemory;
      break;
    case hipDeviceAttributeEccEnabled:
      cdattr = cudaDevAttrEccEnabled;
      break;
    case hipDeviceAttributeCooperativeLaunch:
      cdattr = cudaDevAttrCooperativeLaunch;
      break;
    case hipDeviceAttributeCooperativeMultiDeviceLaunch:
      cdattr = cudaDevAttrCooperativeMultiDeviceLaunch;
      break;
    case hipDeviceAttributeHostRegisterSupported:
      cdattr = cudaDevAttrHostRegisterSupported;
      break;
    case hipDeviceAttributeConcurrentManagedAccess:
      cdattr = cudaDevAttrConcurrentManagedAccess;
      break;
    case hipDeviceAttributeManagedMemory:
      cdattr = cudaDevAttrManagedMemory;
      break;
    case hipDeviceAttributePageableMemoryAccessUsesHostPageTables:
      cdattr = cudaDevAttrPageableMemoryAccessUsesHostPageTables;
      break;
    case hipDeviceAttributePageableMemoryAccess:
      cdattr = cudaDevAttrPageableMemoryAccess;
      break;
    case hipDeviceAttributeDirectManagedMemAccessFromHost:
      cdattr = cudaDevAttrDirectManagedMemAccessFromHost;
      break;
    case hipDeviceAttributeGlobalL1CacheSupported:
      cdattr = cudaDevAttrGlobalL1CacheSupported;
      break;
    case hipDeviceAttributeMaxBlocksPerMultiProcessor:
      cdattr = cudaDevAttrMaxBlocksPerMultiprocessor;
      break;
    case hipDeviceAttributeMultiGpuBoardGroupID:
      cdattr = cudaDevAttrMultiGpuBoardGroupID;
      break;
    case hipDeviceAttributeReservedSharedMemPerBlock:
      cdattr = cudaDevAttrReservedSharedMemoryPerBlock;
      break;
    case hipDeviceAttributeSingleToDoublePrecisionPerfRatio:
      cdattr = cudaDevAttrSingleToDoublePrecisionPerfRatio;
      break;
    case hipDeviceAttributeStreamPrioritiesSupported:
      cdattr = cudaDevAttrStreamPrioritiesSupported;
      break;
    case hipDeviceAttributeSurfaceAlignment:
      cdattr = cudaDevAttrSurfaceAlignment;
      break;
    case hipDeviceAttributeTccDriver:
      cdattr = cudaDevAttrTccDriver;
      break;
    case hipDeviceAttributeUnifiedAddressing:
      cdattr = cudaDevAttrUnifiedAddressing;
      break;
    case hipDeviceAttributeMemoryPoolsSupported:
      cdattr = cudaDevAttrMemoryPoolsSupported;
      break;
    case hipDeviceAttributeVirtualMemoryManagementSupported:
    {
      auto cuda_ret = cuda_ftable.cuDeviceGetAttribute (pi,
          CU_DEVICE_ATTRIBUTE_VIRTUAL_MEMORY_MANAGEMENT_SUPPORTED,
          deviceId);
      return hipCUResultTohipError (cuda_ret);
    }
    case hipDeviceAttributeAccessPolicyMaxWindowSize:
      cdattr = cudaDevAttrMaxAccessPolicyWindowSize;
      break;
    case hipDeviceAttributeAsyncEngineCount:
      cdattr = cudaDevAttrAsyncEngineCount;
      break;
    case hipDeviceAttributeCanUseHostPointerForRegisteredMem:
      cdattr = cudaDevAttrCanUseHostPointerForRegisteredMem;
      break;
    case hipDeviceAttributeComputePreemptionSupported:
      cdattr = cudaDevAttrComputePreemptionSupported;
      break;
    case hipDeviceAttributeHostNativeAtomicSupported:
      cdattr = cudaDevAttrHostNativeAtomicSupported;
      break;
    default:
      return hipCUDAErrorTohipError (cudaErrorInvalidValue);
  }

  auto cuda_ret = cudart_ftable.cudaDeviceGetAttribute (pi, cdattr, deviceId);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipSetDevice (GstHipVendor vendor, int deviceId)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipSetDevice (deviceId);

  auto cuda_ret = cudart_ftable.cudaSetDevice (deviceId);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipMalloc (GstHipVendor vendor, void **ptr, size_t size)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipMalloc (ptr, size);

  auto cuda_ret = cudart_ftable.cudaMalloc (ptr, size);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipFree (GstHipVendor vendor, void *ptr)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipFree (ptr);

  auto cuda_ret = cudart_ftable.cudaFree (ptr);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipHostMalloc (GstHipVendor vendor, void **ptr, size_t size, unsigned int flags)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipHostMalloc (ptr, size, flags);

  auto cuda_ret = cudart_ftable.cudaMallocHost (ptr, size, flags);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipHostFree (GstHipVendor vendor, void *ptr)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipHostFree (ptr);

  auto cuda_ret = cudart_ftable.cudaFreeHost (ptr);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipStreamSynchronize (GstHipVendor vendor, hipStream_t stream)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipStreamSynchronize (stream);

  auto cuda_ret = cudart_ftable.cudaStreamSynchronize (stream);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipModuleLoadData (GstHipVendor vendor, hipModule_t * module, const void *image)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipModuleLoadData (module, image);

  auto cuda_ret = cuda_ftable.cuModuleLoadData ((CUmodule *) module, image);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipModuleUnload (GstHipVendor vendor, hipModule_t module)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipModuleUnload (module);

  auto cuda_ret = cuda_ftable.cuModuleUnload ((CUmodule) module);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipModuleGetFunction (GstHipVendor vendor, hipFunction_t * function,
    hipModule_t module, const char *kname)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipModuleGetFunction (function, module, kname);


  auto cuda_ret = cuda_ftable.cuModuleGetFunction ((CUfunction *) function,
      (CUmodule) module, kname);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipModuleLaunchKernel (GstHipVendor vendor, hipFunction_t f,
    unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ,
    unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, hipStream_t stream, void **kernelParams,
    void **extra)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipModuleLaunchKernel (f, gridDimX, gridDimY, gridDimZ,
        blockDimX, blockDimY, blockDimZ, sharedMemBytes, stream,
        kernelParams, extra);

  auto cuda_ret = cuda_ftable.cuLaunchKernel ((CUfunction) f, gridDimX,
      gridDimY, gridDimZ,
      blockDimX, blockDimY, blockDimZ, sharedMemBytes, (CUstream) stream,
      kernelParams, extra);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipMemcpyParam2DAsync (GstHipVendor vendor, const hip_Memcpy2D * pCopy,
    hipStream_t stream)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipMemcpyParam2DAsync (pCopy, stream);

  CUresult cuda_ret;
  if (!pCopy) {
    cuda_ret = cuda_ftable.cuMemcpy2DAsync (nullptr, (CUstream) stream);
  } else {
    CUDA_MEMCPY2D cudaCopy = { };
    hipMemcpy2DTocudaMemcpy2D (cudaCopy, pCopy);
    cuda_ret = cuda_ftable.cuMemcpy2DAsync (&cudaCopy, (CUstream) stream);
  }

  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipTexObjectCreate (GstHipVendor vendor, hipTextureObject_t * pTexObject,
    const HIP_RESOURCE_DESC * pResDesc,
    const HIP_TEXTURE_DESC * pTexDesc,
    const HIP_RESOURCE_VIEW_DESC * pResViewDesc)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipTexObjectCreate (pTexObject, pResDesc, pTexDesc,
        pResViewDesc);

  auto cuda_ret = cuda_ftable.cuTexObjectCreate ((CUtexObject *) pTexObject,
      (const CUDA_RESOURCE_DESC *) pResDesc,
      (const CUDA_TEXTURE_DESC *) pTexDesc,
      (const CUDA_RESOURCE_VIEW_DESC *) pResViewDesc);

  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipTexObjectDestroy (GstHipVendor vendor, hipTextureObject_t texObject)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipTexObjectDestroy (texObject);

  auto cuda_ret = cuda_ftable.cuTexObjectDestroy ((CUtexObject) texObject);
  return hipCUResultTohipError (cuda_ret);
}

hipError_t
HipGraphicsMapResources (GstHipVendor vendor, int count,
    hipGraphicsResource_t * resources, hipStream_t stream)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGraphicsMapResources (count, resources, stream);

  auto cuda_ret = cudart_ftable.cudaGraphicsMapResources (count,
      (cudaGraphicsResource_t *) resources, stream);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipGraphicsResourceGetMappedPointer (GstHipVendor vendor, void **devPtr,
    size_t *size, hipGraphicsResource_t resource)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD) {
    return amd_ftable.hipGraphicsResourceGetMappedPointer (devPtr,
        size, resource);
  }

  auto cuda_ret = cudart_ftable.cudaGraphicsResourceGetMappedPointer (devPtr,
      size, (cudaGraphicsResource_t) resource);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipGraphicsUnmapResources (GstHipVendor vendor, int count,
    hipGraphicsResource_t * resources, hipStream_t stream)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGraphicsUnmapResources (count, resources, stream);

  auto cuda_ret = cudart_ftable.cudaGraphicsUnmapResources (count,
      (cudaGraphicsResource_t *) resources, stream);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipGraphicsUnregisterResource (GstHipVendor vendor,
    hipGraphicsResource_t resource)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGraphicsUnregisterResource (resource);

  auto cuda_ret =
      cudart_ftable.cudaGraphicsUnregisterResource ((cudaGraphicsResource_t)
      resource);
  return hipCUDAErrorTohipError (cuda_ret);
}

#ifdef HAVE_GST_GL
hipError_t
HipGLGetDevices (GstHipVendor vendor, unsigned int *pHipDeviceCount,
    int *pHipDevices, unsigned int hipDeviceCount, hipGLDeviceList deviceList)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD) {
    return amd_ftable.hipGLGetDevices (pHipDeviceCount, pHipDevices,
        hipDeviceCount, deviceList);
  }

  auto cuda_ret = cudart_ftable.cudaGLGetDevices (pHipDeviceCount, pHipDevices,
      hipDeviceCount, (enum cudaGLDeviceList) deviceList);
  return hipCUDAErrorTohipError (cuda_ret);
}

hipError_t
HipGraphicsGLRegisterBuffer (GstHipVendor vendor,
    hipGraphicsResource ** resource, unsigned int buffer, unsigned int flags)
{
  CHECK_VENDOR (vendor);

  if (vendor == GST_HIP_VENDOR_AMD)
    return amd_ftable.hipGraphicsGLRegisterBuffer (resource, buffer, flags);

  auto cuda_ret =
      cudart_ftable.cudaGraphicsGLRegisterBuffer ((struct cudaGraphicsResource
          **) resource,
      buffer, flags);
  return hipCUDAErrorTohipError (cuda_ret);
}
#endif

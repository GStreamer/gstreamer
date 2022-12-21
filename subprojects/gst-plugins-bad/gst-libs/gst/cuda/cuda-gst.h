#pragma once

#include <gst/cuda/cuda-prelude.h>
#include <cuda.h>
#include <cudaGL.h>

#ifdef G_OS_WIN32
#ifndef INITGUID
#include <initguid.h>
#endif /* INITGUID */

#include <d3d11.h>
#include <dxgi.h>
#include <cudaD3D11.h>
#endif /* G_OS_WIN32 */

G_BEGIN_DECLS

/* cuda.h */
GST_CUDA_API
CUresult CUDAAPI CuInit             (unsigned int Flags);

GST_CUDA_API
CUresult CUDAAPI CuGetErrorName     (CUresult error,
                                     const char **pStr);

GST_CUDA_API
CUresult CUDAAPI CuGetErrorString   (CUresult error,
                                     const char **pStr);

GST_CUDA_API
CUresult CUDAAPI CuCtxCreate        (CUcontext * pctx,
                                     unsigned int flags,
                                     CUdevice dev);

GST_CUDA_API
CUresult CUDAAPI CuCtxDestroy       (CUcontext ctx);

GST_CUDA_API
CUresult CUDAAPI CuCtxPopCurrent    (CUcontext * pctx);

GST_CUDA_API
CUresult CUDAAPI CuCtxPushCurrent   (CUcontext ctx);

GST_CUDA_API
CUresult CUDAAPI CuCtxEnablePeerAccess (CUcontext peerContext,
                                             unsigned int Flags);

GST_CUDA_API
CUresult CUDAAPI CuCtxDisablePeerAccess (CUcontext peerContext);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsMapResources     (unsigned int count,
                                             CUgraphicsResource * resources,
                                             CUstream hStream);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsUnmapResources   (unsigned int count,
                                             CUgraphicsResource * resources,
                                             CUstream hStream);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsSubResourceGetMappedArray    (CUarray * pArray,
                                                         CUgraphicsResource resource,
                                                         unsigned int arrayIndex,
                                                         unsigned int mipLevel);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsResourceGetMappedPointer     (CUdeviceptr * pDevPtr,
                                                         size_t * pSize,
                                                         CUgraphicsResource resource);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsUnregisterResource           (CUgraphicsResource resource);

GST_CUDA_API
CUresult CUDAAPI CuMemAlloc         (CUdeviceptr * dptr,
                                     unsigned int bytesize);

GST_CUDA_API
CUresult CUDAAPI CuMemAllocPitch    (CUdeviceptr * dptr,
                                     size_t * pPitch,
                                     size_t WidthInBytes,
                                     size_t Height,
                                     unsigned int ElementSizeBytes);

GST_CUDA_API
CUresult CUDAAPI CuMemAllocHost     (void **pp,
                                     unsigned int bytesize);

GST_CUDA_API
CUresult CUDAAPI CuMemcpy2D         (const CUDA_MEMCPY2D * pCopy);

GST_CUDA_API
CUresult CUDAAPI CuMemcpy2DAsync    (const CUDA_MEMCPY2D *pCopy, CUstream hStream);

GST_CUDA_API
CUresult CUDAAPI CuMemFree          (CUdeviceptr dptr);

GST_CUDA_API
CUresult CUDAAPI CuMemFreeHost      (void *p);

GST_CUDA_API
CUresult CUDAAPI CuStreamCreate     (CUstream *phStream,
                                     unsigned int Flags);

GST_CUDA_API
CUresult CUDAAPI CuStreamDestroy    (CUstream hStream);

GST_CUDA_API
CUresult CUDAAPI CuStreamSynchronize (CUstream hStream);

GST_CUDA_API
CUresult CUDAAPI CuDeviceGet        (CUdevice * device,
                                     int ordinal);

GST_CUDA_API
CUresult CUDAAPI CuDeviceGetCount   (int *count);

GST_CUDA_API
CUresult CUDAAPI CuDeviceGetName    (char *name,
                                     int len,
                                     CUdevice dev);

GST_CUDA_API
CUresult CUDAAPI CuDeviceGetAttribute (int *pi,
                                       CUdevice_attribute attrib,
                                       CUdevice dev);

GST_CUDA_API
CUresult CUDAAPI CuDeviceCanAccessPeer (int *canAccessPeer,
                                        CUdevice dev,
                                        CUdevice peerDev);

GST_CUDA_API
CUresult CUDAAPI CuDriverGetVersion   (int * driverVersion);

GST_CUDA_API
CUresult CUDAAPI CuModuleLoadData     (CUmodule* module,
                                       const void *image);

GST_CUDA_API
CUresult CUDAAPI CuModuleUnload      (CUmodule module);

GST_CUDA_API
CUresult CUDAAPI CuModuleGetFunction  (CUfunction* hfunc,
                                       CUmodule hmod,
                                       const char* name);

GST_CUDA_API
CUresult CUDAAPI CuTexObjectCreate    (CUtexObject *pTexObject,
                                       const CUDA_RESOURCE_DESC *pResDesc,
                                       const CUDA_TEXTURE_DESC *pTexDesc,
                                       const CUDA_RESOURCE_VIEW_DESC *pResViewDesc);

GST_CUDA_API
CUresult CUDAAPI CuTexObjectDestroy   (CUtexObject texObject);

GST_CUDA_API
CUresult CUDAAPI CuLaunchKernel       (CUfunction f,
                                       unsigned int gridDimX,
                                       unsigned int gridDimY,
                                       unsigned int gridDimZ,
                                       unsigned int blockDimX,
                                       unsigned int blockDimY,
                                       unsigned int blockDimZ,
                                       unsigned int sharedMemBytes,
                                       CUstream hStream,
                                       void **kernelParams,
                                       void **extra);

/* cudaGL.h */
GST_CUDA_API
CUresult CUDAAPI CuGraphicsGLRegisterImage  (CUgraphicsResource * pCudaResource,
                                             unsigned int image,
                                             unsigned int target,
                                             unsigned int Flags);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsGLRegisterBuffer (CUgraphicsResource * pCudaResource,
                                             unsigned int buffer,
                                             unsigned int Flags);

GST_CUDA_API
CUresult CUDAAPI CuGraphicsResourceSetMapFlags (CUgraphicsResource resource,
                                                unsigned int flags);

GST_CUDA_API
CUresult CUDAAPI CuGLGetDevices (unsigned int * pCudaDeviceCount,
                                 CUdevice * pCudaDevices,
                                 unsigned int cudaDeviceCount,
                                 CUGLDeviceList deviceList);


#ifdef G_OS_WIN32
/* cudaD3D11.h */
GST_CUDA_API
CUresult CUDAAPI CuGraphicsD3D11RegisterResource(CUgraphicsResource * pCudaResource,
                                                 ID3D11Resource * pD3DResource,
                                                 unsigned int Flags);

GST_CUDA_API
CUresult CUDAAPI CuD3D11GetDevice(CUdevice * device,
                                  IDXGIAdapter * pAdapter);

GST_CUDA_API
CUresult CUDAAPI CuD3D11GetDevices(unsigned int * pCudaDeviceCount,
                                   CUdevice* pCudaDevices,
                                   unsigned int cudaDeviceCount,
                                   ID3D11Device * pD3D11Device,
                                   CUd3d11DeviceList deviceList);
#endif

G_END_DECLS
